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
#include "pierre.h"
#include "box.h"
#include "effect2.h"
#include "sphere.h"
#include "gameflow.h"
#include "objects.h"
#include "control.h"
#include "lot.h"
#include "items.h"
#include "people.h"
#include "../specific/function_stubs.h"
#include "lara.h"
#include "../specific/file.h"

static BITE_INFO larson_gun = { -55, 200, 5, 14 };
static BITE_INFO pierre_gun1 = { 60, 200, 0, 11 };
static BITE_INFO pierre_gun2 = { -57, 200, 0, 14 };

// Kann Larson/Pierre von hier aus fliehen? Die Flucht ab 40 LP laeuft ueber
// ai_bits = AMBUSH zu einem AIAmbush-Marker mit passender OCB in derselben
// Zone (FindAITargetObject, box.cpp:1631) - dort despawnt er ueber
// reached_goal. Core garantiert den Marker in den Vanilla-Leveln; als
// Pool-Gegner steht er auch in Leveln OHNE passenden Marker. Dann fand
// FindAITargetObject nie ein Ziel, er rannte endlos "fliehend" hinter Lara
// her, und die 40-LP-Klammer machte ihn unsterblich (im Spiel gesehen:
// joby2 Item 36). Ohne Fluchtziel greift die Klammer jetzt nicht - er
// kaempft und stirbt wie ein normaler Gegner (seine Todesanimation bringt
// ihr KillItem-Kommando selbst mit). Die Marker-Pruefung ist dieselbe wie
// in FindAITargetObject: Typ, OCB, Zone.
static long PierreKannFliehen(ITEM_INFO* item, CREATURE_INFO* pierre)
{
	AIOBJECT* aiObj;
	ROOM_INFO* r;
	short* zone;

	zone = ground_zone[pierre->LOT.zone][flip_status];

	for (int i = 0; i < nAIObjects; i++)
	{
		aiObj = &AIObjects[i];

		if (aiObj->object_number != AI_AMBUSH || aiObj->trigger_flags != item->item_flags[3] || aiObj->room_number == NO_ROOM)
			continue;

		r = &room[item->room_number];
		item->box_number = r->floor[((item->pos.z_pos - r->z) >> 10) + r->x_size * ((item->pos.x_pos - r->x) >> 10)].box;

		r = &room[aiObj->room_number];
		aiObj->box_number = r->floor[((aiObj->z - r->z) >> 10) + r->x_size * ((aiObj->x - r->x) >> 10)].box;

		if (zone[item->box_number] == zone[aiObj->box_number])
			return 1;
	}

	return 0;
}

void InitialisePierre(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	item->anim_number = objects[item->object_number].anim_index;
	item->frame_number = anims[item->anim_number].frame_base;
	item->current_anim_state = 1;
	item->goal_anim_state = 1;

	if (item->trigger_flags)
	{
		item->item_flags[3] = item->trigger_flags;

		if (item->pos.y_rot > 0x1000 && item->pos.y_rot < 0x7000)
			item->pos.x_pos += 384;
		else if (item->pos.y_rot < -0x1000 && item->pos.y_rot > -0x7000)
			item->pos.x_pos -= 384;

		if (item->pos.y_rot > -0x2000 && item->pos.y_rot < 0x2000)
			item->pos.z_pos += 384;
		else if (item->pos.y_rot < -0x5000 || item->pos.y_rot > 0x5000)
			item->pos.z_pos -= 384;
	}
}

void PierreControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* pierre;
	FLOOR_INFO* floor;
	ROOM_INFO* r;
	AI_INFO info;
	PHD_VECTOR pos;
	long x, y, z;
	short angle, tilt, head, torso_x, torso_y, room_number;

	if (!CreatureActive(item_number))
		return;

	torso_y = 0;
	tilt = 0;
	angle = 0;
	head = 0;
	torso_x = 0;
	item = &items[item_number];
	pierre = (CREATURE_INFO*)item->data;

	if (item->hit_points <= 40 && !(item->flags & IFL_INVISIBLE) && PierreKannFliehen(item, pierre))
	{
		item->hit_points = 40;
		pierre->flags++;
	}

	if (item->fired_weapon)
	{
		pos.x = larson_gun.x;
		pos.y = larson_gun.y;
		pos.z = larson_gun.z;
		GetJointAbsPosition(item, &pos, larson_gun.mesh_num);
		TriggerDynamic(pos.x, pos.y, pos.z, 2 * item->fired_weapon + 10, 192, 128, 32);
		item->fired_weapon--;
	}

	if (item->trigger_flags)
	{
		if (gfLevelId == LVL5_TRAJAN_MARKETS)
		{
			item->item_flags[3] = 1;
			item->ai_bits = AMBUSH;
		}
		else
			item->ai_bits = GUARD;

		item->trigger_flags = 0;
	}

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state != 5)
		{
			if (item->object_number == PIERRE)
				item->anim_number = objects[PIERRE].anim_index + 12;
			else
				item->anim_number = objects[LARSON].anim_index + 15;

			item->frame_number = anims[item->anim_number].frame_base;
			item->current_anim_state = 5;
		}
		else if (item->object_number == LARSON && item->frame_number == anims[item->anim_number].frame_end)
		{
			room_number = item->item_flags[2] & 0xFF;
			r = &room[room_number];
			x = r->x + ((item->draw_room & 0xFFFFFF00) << 2) + 512;
			y = r->minfloor + (item->item_flags[2] & 0xFFFFFF00);
			z = ((item->draw_room & 0xFF) << 10) + r->z + 512;
			floor = GetFloor(x, y, z, &room_number);
			GetHeight(floor, x, y, z);
			TestTriggers(trigger_index, 1, 0);

			// Larson ausblenden lassen wie jeden anderen Gegner.
			//
			// Seine Sterbeanimation traegt keinen Anim-Befehl 4 (KillItem) -
			// nachgesehen, sie hat sieben PlaySound-Befehle und sonst nichts.
			// Beim Hund sitzt der Befehl in der Sterbeanimation und setzt ueber
			// control.cpp:3220 after_death = 1; von da an blendet draw.cpp:742
			// die Leiche aus und raeumt sie bei 128 weg.
			//
			// Bei Larson fehlt das, weil Core seine Leiche nur an einer einzigen
			// Stelle wegraeumt: in Trajan Markets uebernimmt am letzten Frame
			// dieser Animation die Cutscene, und andrea3b_init (deltapak.cpp)
			// setzt ihn auf ITEM_INVISIBLE. In jedem anderen Level - wo er in
			// Vanilla nie stirbt, weil er vorher flieht - bliebe die Leiche
			// stehen. Seine Sterbeanimation endet naemlich im Stand, sie ist
			// ein 207 Frames langes Stueck fuer die Kamera und kein Umfallen.
			//
			// Fuer den Gegner-Pool muss er ueberall sterben koennen, deshalb
			// hier der Anstoss, den seine Animation nicht mitbringt. Andrea2
			// bleibt unberuehrt: dort blendet die Cutscene ihn im selben
			// Augenblick aus, das Verblassen wird nie sichtbar.
			//
			// Mit dem Fade muss auch die Fracht heraus: getragene Items gibt
			// sonst NUR CreatureDie frei, und dorthin kommt Larson nie -
			// seine Sterbeanimation traegt keinen Befehl 4, der ueber
			// ITEM_DEACTIVATED (control.cpp:3246) dorthin fuehren wuerde.
			if (!item->after_death)
			{
				DropBaddyPickups(item);
				item->after_death = 1;
			}
		}
		else if (item->object_number == PIERRE && item->frame_number >= anims[item->anim_number].frame_end - 1)
		{
			// Pierres Todesanimation (+12) springt an ihrem Ende in eine
			// LEBEND-Animation (+8) zurueck - Core hat seinen generischen Tod
			// nie benutzt, vanilla stirbt er per Cutscene (Colosseum). Ohne
			// Riegel setzte die Steuerung den Tod im naechsten Tick neu auf:
			// eine Imp-artige Endlosschleife (im Spiel gesehen). Die Anim
			// kurz vor dem Ende festhalten - kein Sprung, Pose bleibt liegen,
			// after_death blendet aus und raeumt bei 128 weg.
			item->frame_number = anims[item->anim_number].frame_end - 1;

			// Fracht freigeben: der Riegel eine Bildnummer vor dem Ende ist
			// genau der Grund, warum Pierres eigener Befehl 4 nie mehr
			// ausgeloest wird - und damit auch CreatureDie nicht, das die
			// getragenen Items sonst ablegen wuerde.
			if (!item->after_death)
			{
				DropBaddyPickups(item);
				item->after_death = 1;
			}
		}
	}
	else
	{
		if (item->ai_bits)
			GetAITarget(pierre);
		else
			pierre->enemy = lara_item;

		CreatureAIInfo(item, &info);

		if (info.ahead)
			head = info.angle;

		if (pierre->flags)
		{
			item->hit_points = 60;
			item->ai_bits = AMBUSH;
			pierre->flags = 0;
		}

		GetCreatureMood(item, &info, 1);
		CreatureMood(item, &info, 1);

		if (info.distance < 0x400000 && lara_item->speed > 20 || item->hit_status || TargetVisible(item, &info))
		{
			item->ai_bits &= ~GUARD;
			pierre->alerted = 1;
		}

		angle = CreatureTurn(item, pierre->maximum_turn);

		switch (item->current_anim_state)
		{
		case 1:
			torso_y = info.angle >> 1;
			head = info.angle >> 1;

			if (info.ahead)
				torso_x = info.x_angle;

			if (item->required_anim_state)
				item->goal_anim_state = item->required_anim_state;
			else if (item->ai_bits == AMBUSH)
				item->goal_anim_state = 3;
			else if (Targetable(item, &info))
				item->goal_anim_state = 4;
			else if (item->ai_bits & GUARD || gfLevelId == LVL5_TRAJAN_MARKETS || item->item_flags[3])
			{
				pierre->maximum_turn = 0;
				item->goal_anim_state = 1;

				if (abs(info.angle) < 364)
					item->pos.y_rot += info.angle;
				else if (info.angle >= 0)
					item->pos.y_rot += 364;
				else
					item->pos.y_rot -= 364;
			}
			else if (pierre->mood == BORED_MOOD)
				item->goal_anim_state = GetRandomControl() >= 96 ? 2 : 6;
			else if (pierre->mood == ESCAPE_MOOD)
				item->goal_anim_state = 3;
			else
				item->goal_anim_state = 2;

			break;

		case 2:

			if (info.ahead)
				head = info.angle;

			pierre->maximum_turn = 1274;

			if (pierre->mood == BORED_MOOD && GetRandomControl() < 96)
			{
				item->required_anim_state = 6;
				item->goal_anim_state = 1;
			}
			else if (pierre->mood == ESCAPE_MOOD || item->ai_bits == AMBUSH)
			{
				item->required_anim_state = 3;
				item->goal_anim_state = 1;
			}
			else if (Targetable(item, &info))
			{
				item->required_anim_state = 4;
				item->goal_anim_state = 1;
			}
			else if (!info.ahead || info.distance > 0x900000)
			{
				item->required_anim_state = 3;
				item->goal_anim_state = 1;
			}

			break;

		case 3:

			if (info.ahead)
				head = info.angle;

			pierre->maximum_turn = 2002;
			tilt = angle >> 1;

			if (pierre->reached_goal)
				item->goal_anim_state = 1;
			else if (item->ai_bits == AMBUSH)
				item->goal_anim_state = 3;
			else if (pierre->mood == BORED_MOOD && GetRandomControl() < 96)
			{
				item->required_anim_state = 6;
				item->goal_anim_state = 1;
			}
			else if (Targetable(item, &info))
			{
				item->required_anim_state = 4;
				item->goal_anim_state = 1;
			}
			else if (info.ahead && info.distance < 0x900000)
			{
				item->required_anim_state = 2;
				item->goal_anim_state = 1;
			}

			break;

		case 4:
			torso_y = info.angle >> 1;
			head = info.angle >> 1;

			if (info.ahead)
				torso_x = info.x_angle;

			pierre->maximum_turn = 0;

			if (abs(info.angle) < 364)
				item->pos.y_rot += info.angle;
			else if (info.angle >= 0)
				item->pos.y_rot += 364;
			else
				item->pos.y_rot -= 364;

			if (Targetable(item, &info))
				item->goal_anim_state = 7;
			else
				item->goal_anim_state = 1;

			break;

		case 6:
			torso_y = info.angle >> 1;
			head = info.angle >> 1;

			if (info.ahead)
				torso_x = info.x_angle;

			if (pierre->mood != BORED_MOOD)
				item->goal_anim_state = 1;
			else if (GetRandomControl() < 96)
			{
				item->required_anim_state = 2;
				item->goal_anim_state = 1;
			}

			break;

		case 7:
			torso_y = info.angle >> 1;
			head = info.angle >> 1;

			if (info.ahead)
				torso_x = info.x_angle;

			pierre->maximum_turn = 0;

			if (abs(info.angle) < 364)
				item->pos.y_rot += info.angle;
			else if (info.angle >= 0)
				item->pos.y_rot += 364;
			else
				item->pos.y_rot -= 364;

			if (item->frame_number == anims[item->anim_number].frame_base)
			{
				if (item->object_number == PIERRE)
				{
					ShotLara(item, &info, &pierre_gun1, torso_y, balance.dmgPierreShot);
					ShotLara(item, &info, &pierre_gun2, torso_y, balance.dmgPierreShot);
				}
				else
					ShotLara(item, &info, &larson_gun, torso_y, balance.dmgLarsonShot);

				item->fired_weapon = 2;
			}

			if (pierre->mood == ESCAPE_MOOD && GetRandomControl() > 0x2000)
				item->required_anim_state = 1;

			break;
		}
	}

	CreatureTilt(item, tilt);
	CreatureJoint(item, 0, torso_y);
	CreatureJoint(item, 1, torso_x);
	CreatureJoint(item, 2, head);
	CreatureAnimation(item_number, angle, 0);

	if (pierre->reached_goal)
	{
		if (gfLevelId == LVL5_TRAJAN_MARKETS)
		{
			item->goal_anim_state = 1;
			item->required_anim_state = 1;
			item->trigger_flags = 0;
			item->ai_bits = 0;
			pierre->reached_goal = 0;
		}
		else
		{
			// Getragenes zuerst ablegen, dann verschwinden.
			//
			// GetCarriedItems (setup.cpp:1962) haengt beim Levelstart JEDES
			// Pickup im Umkreis von 512 Einheiten an den naechsten Gegner:
			// RemoveDrawnItem, room_number = NO_ROOM - das Item existiert
			// danach nur noch als seine Fracht. Beim normalen Tod gibt
			// CreatureDie sie ueber DropBaddyPickups wieder frei; dieser
			// Fluchtabgang ruft aber direkt KillItem, und damit waere die
			// Fracht fuer immer fort.
			//
			// In Vanilla faellt das nicht auf, weil Core keinem fliehenden
			// Larson etwas mitgibt. Mit zufaelliger Itemverteilung kann dort
			// aber ein Schluesselitem liegen - und dann waere das Level nach
			// seiner Flucht nicht mehr loesbar.
			DropBaddyPickups(item);
			item->hit_points = -16384;
			DisableBaddieAI(item_number);
			KillItem(item_number);
		}
	}
}

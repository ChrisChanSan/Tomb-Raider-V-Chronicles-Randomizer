#include "../tomb5/pch.h"
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

	// GEIER-VERSATZ, nicht Kraehe (#91).
	//
	// In Tomb Raider 3 steuert EINE Funktion beide Voegel und unterscheidet
	// per object_number (tomb3 bird.cpp:19-32): die Kraehe startet mit
	// anim_index + 14 im Fresszustand, der Geier mit + 5 im Stehzustand.
	// TR5 hat nur den Kraehen-Zweig behalten - dort gibt es keinen Geier.
	//
	// Der Slot bleibt trotzdem gefahrlos umzustellen: CROW hat in KEINEM der
	// 16 TR5-Level ein Modell (an allen Dateien nachgemessen). Diese Funktion
	// laeuft also ausschliesslich fuer den importierten TR3-Geier, der nur
	// 12 Animationen hat - mit + 14 liefe sie ins Leere.
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
			// Sturzanimation: Kraehe + 1, Geier + 8 (tomb3 bird.cpp:65-68).
			// Siehe die Begruendung in InitialiseCrow.
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
				lara_item->hit_points -= 20;
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

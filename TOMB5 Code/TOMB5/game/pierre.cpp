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

// Whether the level itself marked this Larson or Pierre: a non-zero OCB,
// which InitialisePierre copies into item_flags[3] (and Trajan Markets'
// first tick turns into 1). The vanilla specimens are the three Larsons of
// Andrea1 and Trajan Markets; a pool Larson or Pierre has OCB 0, the
// randomizer clears it when it swaps one in. GetAIPickups (setup.cpp) also
// writes item_flags[3]: the OCB of an AI object he stands on at level
// start, a vanilla enemy's marker when he took that enemy's place. It
// keeps that pickup in TOSSPAD bits 8-12 (the ai_bits; InitialiseCreature
// leaves them 0, bits 13-15 hold the angle) and SetupGame rebuilds it on
// every load. No marked specimen stands on an AI object, so a mark next to
// a pickup is the marker's, not the level's.
static long PierreScripted(ITEM_INFO* item)
{
	return item->item_flags[3] && !(item->TOSSPAD & 0x1F00);
}

// Whether Larson/Pierre can flee from here. The flight from 40 HP runs via
// ai_bits = AMBUSH to an AIAmbush marker with a matching OCB in the same
// zone (FindAITargetObject, box.cpp) - there he despawns via reached_goal.
// Core guarantees the marker in the vanilla levels; as a pool enemy he also
// stands in levels without a matching marker. There FindAITargetObject
// never finds a target, so he would run endlessly "fleeing" behind Lara
// while the 40-HP clamp keeps him immortal (as in joby2 item 36). Without a
// flight target the clamp no longer engages: he fights and dies like a
// normal enemy (his death animation brings its own KillItem command). The
// marker check mirrors FindAITargetObject's own: type, OCB, zone.
//
// Only a Larson or Pierre the level marked (PierreScripted) may flee. An
// AIAmbush with OCB 0 matches every unmarked one: rich1 carries two, and
// a pool Larson or Pierre in their zone would run off with his cargo. No
// vanilla Larson or Pierre is unmarked, so vanilla loses nothing.
static long PierreCanFlee(ITEM_INFO* item, CREATURE_INFO* pierre)
{
	AIOBJECT* aiObj;
	ROOM_INFO* r;
	short* zone;

	if (!PierreScripted(item))
		return 0;

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

// The frame of Larson's death animation (+15, 208 frames at rate 2) he lies
// on. The animation is a fall AND a rise: the root drops from 398 to 62
// above the floor by frame 18 (key 9); frame 28 (key 14) is the lowest pose
// of the whole piece, 187 high with the arms settled; from frame 60 he props
// himself up, from frame 148 he gets up, from frame 180 he stands, and
// frame 207 is a man standing taller than at the start - the animation then
// holds that last frame (it continues with itself there). Outside Trajan
// Markets that rise is all a player sees of a death, so the animation stops
// on frame 28.
#define LARSON_DEATH_DOWN	28

// Lay a dead Larson or Pierre down as a corpse on the given frame: what
// AnimateItem's command 4 and CreatureAnimation do for any other enemy -
// after_death 1 if it is not running yet, ITEM_DEACTIVATED, CreatureDie (no
// collision, off the active chain, AI slot freed, IFL_INVISIBLE |
// IFL_CLEARBODY, the cargo dropped). From here on the corpse is a guard's:
// the Corpses switch fades it or keeps it, the savegame keeps it as a body
// (SaveBodyLies, savegame.cpp). The control never runs for it again, so the
// cargo comes out once; the caller returns at once, as item->data is gone.
static void PierreLayDown(short item_number, long frame)
{
	ITEM_INFO* item;

	item = &items[item_number];
	item->frame_number = (short)frame;

	if (!item->after_death)
		item->after_death = 1;

	item->status = ITEM_DEACTIVATED;
	CreatureDie(item_number, 0);

	if (gLogEnabled)
		Log("ITEMLOG %d: corpse laid down slot %d frame %d after_death %d"
			" room %d", item_number, item->object_number,
			item->frame_number - anims[item->anim_number].frame_base,
			item->after_death, item->room_number);
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

	if (item->hit_points <= 40 && !(item->flags & IFL_INVISIBLE) && PierreCanFlee(item, pierre))
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

	// Only an item with an OCB comes in here: a pool Larson's is 0, so
	// Trajan Markets' ambush start belongs to its scripted Larson alone.
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
		else if (item->object_number == LARSON &&
			(gfLevelId != LVL5_TRAJAN_MARKETS || !PierreScripted(item)))
		{
			// Outside Trajan Markets he lies down and stays down: the
			// animation is held on its lowest frame and he becomes a corpse
			// like a guard. No heavy trigger test here - only Trajan
			// Markets' scripted Larson stands on one (the cutscene's), and a
			// randomized level can put a pool Larson's start on any heavy
			// trigger. A pool Larson in Trajan Markets is no exception: the
			// level's way is its marked Larson's alone (PierreScripted).
			// The >= takes a Larson a save left standing at the end.
			if (item->frame_number >= anims[item->anim_number].frame_base + LARSON_DEATH_DOWN)
			{
				PierreLayDown(item_number, anims[item->anim_number].frame_base + LARSON_DEATH_DOWN);
				return;
			}
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

			// Trajan Markets' marked Larson only (the branch above takes
			// every other level and every other Larson). The heavy test
			// reads the sector he started on (InitialiseCreature keeps it
			// in item_flags[2] and draw_room):
			// there the level's HEAVY trigger starts the cutscene that ends
			// the fight, andrea3b_init (deltapak.cpp) sets him ITEM_INVISIBLE.
			// His death animation carries no anim command 4 (KillItem) -
			// seven PlaySound commands and nothing else - and ends with him
			// standing again: a 208-frame piece for the camera, a fall and a
			// rise. The animation holds its last frame, so this runs on
			// every tick from here on.
			//
			// after_death = 1 starts the fade a command 4 would have
			// started, and the cargo comes out with it: carried items are
			// otherwise released only by CreatureDie. The cutscene hides him
			// in the same moment; the fade never becomes visible.
			if (!item->after_death)
			{
				DropBaddyPickups(item);
				item->after_death = 1;
			}
		}
		else if (item->object_number == PIERRE && item->frame_number >= anims[item->anim_number].frame_end - 1)
		{
			// Pierre's death animation (+12, 95 frames at rate 3) kneels,
			// falls on frame 78 to 84 and lies from there; he never used
			// it in vanilla, where he dies by cutscene (Colosseum). The
			// latch one frame before the end was set against a jump back
			// into a living animation and an endless loop; the files
			// measured for #340 (vanilla Andrea3, a pool copy) show the
			// animation continuing with itself on its last frame and a
			// command 4 at its end. The latch stays: the frame before the
			// end lies as low as the last (176 high).
			//
			// The latch kept command 4 from ever firing, and with it
			// CreatureDie: he had kept his collision, his AI slot and his
			// place on the active chain, and the savegame did not know him
			// as a body. He becomes a corpse here as Larson does above.
			PierreLayDown(item_number, anims[item->anim_number].frame_end - 1);
			return;
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
			else if (item->ai_bits & GUARD || item->item_flags[3])
			{
				// Stand and turn toward Lara. Trajan Markets once sent every
				// Larson here; its scripted one carries item_flags[3] 1 from
				// his first tick, a pool one walks as in every other level.
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
		// The flight exit below is a marked specimen's alone. An unmarked
		// one reaches a goal only through a marker he picked up at level
		// start (GetAIPickups); he stops there as in Trajan Markets and
		// fights instead of vanishing.
		if (gfLevelId == LVL5_TRAJAN_MARKETS || !PierreScripted(item))
		{
			item->goal_anim_state = 1;
			item->required_anim_state = 1;
			item->trigger_flags = 0;
			item->ai_bits = 0;
			pierre->reached_goal = 0;
		}
		else
		{
			// Drop what he carries first, then vanish.
			//
			// GetCarriedItems (setup.cpp) attaches, at level start, each pickup
			// but a wall grab in an enemy's room within 512 units of him to the
			// first enemy in item order that reaches it: RemoveDrawnItem,
			// room_number = NO_ROOM - the item then exists only as his
			// cargo. On a normal death CreatureDie releases it via
			// DropBaddyPickups; this flight exit however calls KillItem
			// directly, and the cargo would be gone forever.
			//
			// In vanilla this goes unnoticed because a fleeing Larson
			// carries nothing. With random item distribution a key item can
			// lie there - and then the level would be unsolvable after his
			// flight.
			DropBaddyPickups(item);
			item->hit_points = -16384;
			DisableBaddieAI(item_number);
			KillItem(item_number);
		}
	}
}

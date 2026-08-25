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
 * Compsognathus from Tomb Raider 3, ported to TOMB5 in 2026 (#99).
 *
 * Taken from tomb3 (game/compy.cpp, GPLv3) - the same license as TOMB5 and
 * this project. The code is deliberately taken over directly instead of
 * rewritten: rewriting would obscure its origin.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 *
 * ----------------------------------------------------------------------------
 * In TR5 the Compy sits on slot SAILOR (53). Until now the slot hung on
 * InitialiseSas/SasControl and fetched its anims from SWAT - nobody ever
 * used it: none of the 16 levels carries a model there (measured with
 * MissMod against all of them, cross-checked via the dog). Fourth dead slot
 * after SAS 31 (T-Rex), CROW 43 (vulture) and REAPER 75 (Raptor).
 *
 * FOUR DEVIATIONS from tomb3, all justified here:
 *
 * 1. THE CARCASS SEARCH IS DROPPED. tomb3 searches the room for an object
 *    of type ANIMATING6 - in TR3 that is the dead raptor the pack gathers
 *    around and gnaws on. In TR5, ANIMATING6 is a perfectly normal scenery
 *    slot that REALLY is occupied in levels. Taken over, the compys would
 *    swarm around decoration.
 *
 *    What hangs on it, checked rather than dropped blindly (lesson from
 *    the T-Rex):
 *      - In BORED_MOOD the carcass was their gathering point. Without it
 *        they roam instead of gathering.
 *      - In state COMPY_STOP it led to jump/bite out of boredom. The
 *        branch is dropped; the fallback to COMPY_RUN still carries.
 *      - In COMPY_JUMP/COMPY_ATTACK it triggered the blood splatter while
 *        gnawing.
 *    The attack on Lara hangs on NONE of these spots and stays complete.
 *
 * 2. non_lot STAYS OFF. tomb3 sets the bit on the Compy so it bypasses the
 *    box pathfinding and swarms freely. TR5 carries the bit in OBJECT_INFO
 *    but reads it NOWHERE - the checks vanished from box.cpp in TR4/TR5.
 *    Rebuilding them would be an intervention into the shared code of ALL
 *    enemies; the user decided the compys may move like normal enemies.
 *    box.cpp thus stays untouched.
 *
 * 3. compy_scared_timer is reset ALONG on initialization. tomb3 only
 *    resets compys_attack_lara. Both are globals that survive a level
 *    change - this engine reloads a lot (level select), and a leftover
 *    scare counter would make the first pack in the next level flee for
 *    no reason.
 *
 * 4. TIMIDITY HALVED (user decision 2026-08-17). All three cowardice
 *    delays cut in half:
 *      - flight duration    compy_scared_timer  280 -> 140  (~9.3 -> ~4.7 s)
 *      - pre-attack lockout item->timer > 180   -> 90       (~6   -> ~3   s)
 *      - the same under fire    > 200           -> 100      (~6.7 -> ~3.3 s)
 *    Reason: TR3 gives the pack plenty of time because whole swarms live
 *    in wide jungle sections there and Lara accompanies them for long.
 *    With us, ONE pack of four animals stands somewhere in the level -
 *    nine seconds of flight there means Lara is long gone before anything
 *    happens. Cowardice as behavior stays, it just lasts half as long.
 *
 * Sounds come via the model's anim commands, no call is needed here.
 */
#include "../tomb5/pch.h"
#include "balance.h"
#include "compy.h"
#include "box.h"
#include "objects.h"
#include "effects.h"
#include "control.h"
#include "lara.h"
#include "lara_states.h"
#include "items.h"
#include "../specific/function_stubs.h"
#include "../specific/3dmath.h"

static BITE_INFO compy_hit = { 0, 0, 0, 2 };

long compy_scared_timer;
long compys_attack_lara;

void InitialiseCompy(short item_number)
{
	InitialiseCreature(item_number);
	compys_attack_lara = 0;
	compy_scared_timer = 0;      // siehe Abweichung 3 im Dateikopf
}

void CompyControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* compy;
	FLOOR_INFO* floor;
	AI_INFO info;
	short angle, torso, head, bits, room_number;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	compy = (CREATURE_INFO*)item->data;
	angle = 0;
	head = 0;
	torso = 0;

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state != COMPY_DEATH)
		{
			item->anim_number = objects[item->object_number].anim_index + 6;
			item->current_anim_state = COMPY_DEATH;
			item->frame_number = anims[item->anim_number].frame_base;
		}
	}
	else
	{
		CreatureAIInfo(item, &info);

		// The redirection to the carcass stood here - see deviation 1.

		bits = 512 * (item_number & 7) - 1792;

		if (!compy_scared_timer && !compys_attack_lara && (info.enemy_facing < 0x3000 && info.enemy_facing > -0x3000 && lara_item->speed > 15 || lara_item->current_anim_state == AS_ROLL || item->hit_status))
		{
			item->item_flags[0] = (bits + 1792) >> 7;
			// DEVIATION 4 (user decision 2026-08-17): all three timidity
			// delays halved. TR3 gave the pack plenty of time because
			// whole swarms live in wide jungle sections there; with us,
			// ONE pack of four animals stands in a TR5 level, and nine
			// seconds of flight are simply boring there. Original: 280
			// (~9.3 s at 30 frames/s).
			compy_scared_timer = 140;
		}
		else if (compy_scared_timer)
		{
			if (item->item_flags[0] > 0)
				item->item_flags[0]--;
			else
			{
				compy->mood = ESCAPE_MOOD;
				compy_scared_timer--;
			}

			// Halbiert, siehe Abweichung 4. Original: 180 (~6 s).
			if (GetRandomControl() < 0x1F && item->timer > 90)
				compys_attack_lara = 1;
		}
		else
			compy->mood = info.zone_number == info.enemy_zone ? ATTACK_MOOD : BORED_MOOD;

		switch (compy->mood)
		{
		case BORED_MOOD:
			// Replacement for the carcass gathering point (see deviation 1).
			//
			// Without a carcass this branch set NO target at all, and
			// compy->target kept whatever value it was born with - the
			// compys ran toward a spot nobody had set. Seen in game: after
			// waking, straight into the nearest wall first. In TR3 it never
			// shows because a carcass lies wherever compys occur.
			//
			// Lara is the obvious replacement: they roam near her without
			// attacking. Cowardice itself hangs on compys_attack_lara and
			// is untouched by this replacement; its DURATION has since been
			// halved (deviation 4 above).
			if (compy->enemy)
			{
				compy->target.x = compy->enemy->pos.x_pos;
				compy->target.z = compy->enemy->pos.z_pos;
			}

			break;

		case ATTACK_MOOD:
			compy->target.x = compy->enemy->pos.x_pos + (1024 * phd_sin(bits + item->pos.y_rot) >> W2V_SHIFT);
			compy->target.y = compy->enemy->pos.y_pos;
			compy->target.z = compy->enemy->pos.z_pos + (1024 * phd_cos(bits + item->pos.y_rot) >> W2V_SHIFT);
			break;

		case ESCAPE_MOOD:
		case STALK_MOOD:
			compy->target.x = compy->enemy->pos.x_pos + (1024 * phd_sin(bits + info.angle + 0x8000) >> W2V_SHIFT);
			compy->target.z = compy->enemy->pos.z_pos + (1024 * phd_cos(bits + info.angle + 0x8000) >> W2V_SHIFT);
			room_number = item->room_number;
			floor = GetFloor(compy->target.x, item->pos.y_pos, compy->target.z, &room_number);

			if (abs(boxes[floor->box].height - item->pos.y_pos) > 256)
			{
				compy->mood = BORED_MOOD;
				item->item_flags[0] = (short)compy_scared_timer;
			}

			break;
		}

		angle = CreatureTurn(item, compy->maximum_turn);

		if (info.ahead)
			torso = info.angle;

		head = -(info.angle / 4);
		item->timer++;

		// Halbiert, siehe Abweichung 4. Original: 200 (~6,7 s).
		if (item->hit_status && item->timer > 100 && GetRandomControl() < 0xC1C)
			compys_attack_lara = 1;

		switch (item->current_anim_state)
		{
		case COMPY_STOP:
			compy->flags &= ~1;
			compy->maximum_turn = 546;

			if (compy->mood == ATTACK_MOOD)
			{
				if (info.angle && info.distance < 0x718E4)
				{
					if (compys_attack_lara)
					{
						if (GetRandomControl() < 0x4000)
							item->goal_anim_state = COMPY_ATTACK;
						else
							item->goal_anim_state = COMPY_JUMP;
					}
					else
						item->goal_anim_state = COMPY_STOP;
				}
				else if (info.distance > 0xFF801 - 0x718E4 * compys_attack_lara)
					item->goal_anim_state = COMPY_RUN;
			}
			else if (compy->mood == BORED_MOOD)
			{
				// The carcass branch is dropped, the fallback to RUN carries.
				if (info.distance > 0x552AB)
					item->goal_anim_state = COMPY_RUN;
			}
			else if (GetRandomControl() < 0x1000)
				item->goal_anim_state = COMPY_JUMP;
			else
				item->goal_anim_state = COMPY_RUN;

			break;

		case COMPY_RUN:
			compy->flags &= ~1;
			compy->maximum_turn = 1820;

			if (info.angle < 0x3000 && info.angle > -0x3000 && info.distance < 0xFF801 - 0x718E4 * compys_attack_lara)
				item->goal_anim_state = COMPY_STOP;

			break;

		case COMPY_JUMP:
		case COMPY_ATTACK:
			compy->maximum_turn = 1820;

			// The second branch (gnawing at the carcass) drops with deviation 1.
			if (!(compy->flags & 1))
			{
				if (item->touch_bits & 4 && compys_attack_lara)
				{
					compy->flags |= 1;
					lara_item->hit_points -= EnemyDamage(balance.dmgCompyBite);
					lara_item->hit_status = 1;
					CreatureEffect(item, &compy_hit, DoBloodSplat);
				}
			}

			break;
		}
	}

	CreatureTilt(item, angle >> 1);
	CreatureJoint(item, 0, torso);
	CreatureJoint(item, 1, head);
	CreatureAnimation(item_number, angle, angle >> 1);
}

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
#include "sphere.h"
#include "delstuff.h"
#include "draw.h"
#include "../specific/function_stubs.h"
#include "chef_rule.h"

static BITE_INFO knife = { 0, 200, 0, 13 };

// The brawler strikes with his empty fist: mesh 13, the hand the chef
// holds the knife in, on him a box of eight vertices 85 units long.
// The blood comes from the middle of its knuckle face (vertices 0, 1, 4
// and 5 of joby2's slot 439); the knife's point above, 200 units down
// the hand, would lie 115 units past his knuckles.
static BITE_INFO fist = { 4, 72, 15, 13 };

// The chef's swing distance, vanilla's: 682 squared, the AI's distance
// (CreatureAIInfo) below which he strikes and his walk ends. The brawler
// has his own (chef_rule.h, BrawlerSwingSquared).
#define CHEF_SWING	0x718E4

// The seam: one line when the brawler's swing starts, one when it lands or
// ends without landing (-log).
static void BrawlLog(short item_number, const char* state, long dist,
	bool ahead, long frame, bool touch, bool reach, bool hit)
{
	if (!LogFam_BRAWLLOG || !gLogEnabled)
		return;

	Log("BRAWLLOG item=%d tick=%d state=%s dist=%ld ahead=%d frame=%ld "
		"touch=%d reach=%d hit=%d", item_number, GlobalCounter, state, dist,
		ahead ? 1 : 0, frame, touch ? 1 : 0, reach ? 1 : 0, hit ? 1 : 0);
}

/* Whether the brawler's fist reaches Lara this frame (chef_rule.h,
 * BrawlerFistReaches): her pivot against his, her direction off his arm -
 * his facing turned by the torso's twist, joint 0, which the control aims
 * at half her angle - and her frame's box against the fist's height, on a
 * living Lara (the swing begins only on one, info.bite). The fist reaches
 * 445, and a convex wall corner can stand between two pivots from 286 on
 * (his 102 and her 100 off the walls), so a line of sight from his torso
 * to hers guards it, the drone's precedent (drone.cpp); the touch bits
 * stay as they are. `dist` and `ahead` come back for the seam. */
static bool BrawlerReaches(ITEM_INFO* item, CREATURE_INFO* chef, long* dist,
	bool* ahead)
{
	GAME_VECTOR from, to;
	PHD_VECTOR pos;
	long long d2;
	short* bounds;
	long dx, dy, dz;
	short angle;

	dx = lara_item->pos.x_pos - item->pos.x_pos;
	dy = lara_item->pos.y_pos - item->pos.y_pos;
	dz = lara_item->pos.z_pos - item->pos.z_pos;
	d2 = (long long)dx * dx + (long long)dz * dz;
	*dist = (long)phd_sqrt(d2 > 0x7FFFFFFF ? 0x7FFFFFFF : (ulong)d2);
	angle = short(phd_atan(dz, dx) - item->pos.y_rot);
	*ahead = angle > -0x4000 && angle < 0x4000;
	angle = short(angle - chef->joint_rotation[0]);
	bounds = GetBestFrame(lara_item);

	if (lara_item->hit_points <= 0 ||
		item->anim_number != objects[item->object_number].anim_index + BRAWLER_STRIKE_ANIM ||
		!BrawlerFistReaches(item->frame_number - anims[item->anim_number].frame_base, d2, angle, dy, bounds[2]))
		return false;

	pos.x = 0;
	pos.y = 0;
	pos.z = 0;
	GetJointAbsPosition(item, &pos, BRAWLER_TORSO_MESH);
	from.x = pos.x;
	from.y = pos.y;
	from.z = pos.z;
	from.room_number = item->room_number;
	GetFloor(from.x, from.y, from.z, &from.room_number);
	pos.x = 0;
	pos.y = 0;
	pos.z = 0;
	GetLaraJointPos(&pos, LMX_TORSO);
	to.x = pos.x;
	to.y = pos.y;
	to.z = pos.z;
	to.room_number = lara_item->room_number;
	return LOS(&from, &to) != 0;
}

void InitialiseChef(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	// The item's own slot: the chef's set lies on CHEF, the brawler's copy
	// of it on BRAWLER (chef_rule.h, BrawlerAnimBase).
	item->anim_number = objects[item->object_number].anim_index;
	item->frame_number = anims[item->anim_number].frame_base;
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
	long dx, dz, swing, dist;
	short angle, head, torso_y, torso_x;
	bool brawler, touch, reach, ahead;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	chef = (CREATURE_INFO*)item->data;
	brawler = item->object_number == BRAWLER;
	swing = brawler ? BrawlerSwingSquared() : CHEF_SWING;
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
	// The brawler dies the same way: his skeleton is the chef's, and the
	// randomizer brings the guard along with him too.
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
				item->anim_number = objects[item->object_number].anim_index + 11;

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
		//
		// The brawler hunts whatever his OCB says - he has no kitchen
		// (ChefHunts, chef_rule.h).
		if (ChefHunts(item->object_number, item->trigger_flags) ||
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

		// The brawler strikes with the same animation and the same hand,
		// mesh 13 (touch bit 0x2000), with a fist of his own: his own line
		// in Balance.txt and his own point for the blood.
		//
		// The touch bits alone often let his fist miss, its sphere being
		// small, so his hit also lands where the fist reaches by measure:
		// frames 15 and 16 of the strike, Lara within his reach, cone and
		// band (chef_rule.h, BrawlerFistReaches). Once a swing, whichever
		// comes first; the chef's hit is the touch bits as ever (reach
		// stays false for him).
		//
		// Refused while a scene holds Lara (scene_hold.h).
		touch = item->touch_bits & 0x2000 && item->frame_number > anims[item->anim_number].frame_base + 10;
		reach = false;
		dist = 0;
		ahead = false;

		if (brawler && !chef->flags)
			reach = BrawlerReaches(item, chef, &dist, &ahead);

		if (brawler && item->frame_number == anims[item->anim_number].frame_base)
			BrawlLog(item_number, "start", dist, ahead, 0, touch, reach, false);

		if (!chef->flags && (touch || reach) &&
			!SceneHoldRefuses(EnemyDamage(brawler ? balance.dmgBrawlerFist : balance.dmgChefKnife),
				item->object_number, brawler ? "brawler" : "chef"))
		{
			lara_item->hit_points -= EnemyDamage(brawler ? balance.dmgBrawlerFist : balance.dmgChefKnife);
			lara_item->hit_status = 1;
			CreatureEffectT(item, brawler ? &fist : &knife, 20, item->pos.y_rot, DoBloodSplat);
			SoundEffect(SFX_LARA_THUD, &item->pos, SFX_DEFAULT);
			chef->flags = 1;

			if (brawler)
				BrawlLog(item_number, "hit", dist, ahead, item->frame_number - anims[item->anim_number].frame_base,
					touch, reach, true);
		}
		else if (brawler && !chef->flags && item->frame_number == anims[item->anim_number].frame_end)
			BrawlLog(item_number, "miss", dist, ahead, item->frame_number - anims[item->anim_number].frame_base,
				touch, reach, false);

		break;

	case 4:
		chef->flags = 0;
		chef->maximum_turn = 364;

		// He swings within his distance - the chef's 682 or the brawler's
		// own, which his fist reaches (chef_rule.h).
		if (info.distance < swing)
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

		// The walk ends at the same distance: the brawler walks on to where
		// he strikes instead of standing short of it.
		if (info.distance < swing || info.angle > 0x5000 || info.angle < -0x5000 || chef->mood != ATTACK_MOOD)
			item->goal_anim_state = 4;

		break;
	}

	CreatureTilt(item, 0);
	CreatureJoint(item, 0, torso_y);
	CreatureJoint(item, 1, torso_x);
	CreatureJoint(item, 2, head);
	CreatureAnimation(item_number, angle, 0);
}

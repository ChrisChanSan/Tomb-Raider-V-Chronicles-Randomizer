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
#include "drone.h"
#include "drone_rule.h"
#include "balance.h"
#include "box.h"
#include "objects.h"
#include "control.h"
#include "effects.h"
#include "effect2.h"
#include "sound.h"
#include "sphere.h"
#include "delstuff.h"
#include "debris.h"
#include "items.h"
#include "draw.h"
#include "lara.h"
#include "switch.h"
#include "gameflow.h"
#include "tomb4fx.h"
#include "../specific/3dmath.h"
#include "../specific/function_stubs.h"

/* The drone: RICH3's gunship at the size of the pool's vulture, flying
 * after Lara on the flyer zone and firing the gunship's gun.
 *
 * Every decision is drone_rule.h's, which says the whole of it as a table;
 * this file only measures what the rules ask for and carries out what they
 * answer. Its memory lies in item_flags, which the savegame keeps:
 *   [0] the state (DroneState)
 *   [1] its own tick, counted from its first control call
 *   [2] how many strafe legs the room turned round
 * item_flags[3] stays the AI target code's (GetAITarget).
 *
 * The animation is the gunship's one and only: ten frames that turn both
 * rotors and move nothing, so the drone's flight is this file's own step
 * (DroneStep) on the zone the creature code paths it through, and its
 * height the creature code's flyer climb toward the target y set here.
 *
 * The nose of the model looks down its own -z (Core hung the gunship that
 * way round), so facing Lara is a heading half a turn from hers. */

#define DRONE_NOSE	0x8000

/* One horizontal step, taken only where the flyer zone lets the creature
 * code take it: a box, the zone the drone is in, and its origin inside the
 * room at its height. What CreatureAnimation would undo is never done. */
static bool DroneStep(ITEM_INFO* item, long dx, long dz)
{
	CREATURE_INFO* drone;
	FLOOR_INFO* floor;
	short* zone;
	long x, y, z, h, c;
	short room_number;

	if (!dx && !dz)
		return true;

	drone = (CREATURE_INFO*)item->data;
	zone = ground_zone[drone->LOT.zone][flip_status];
	x = item->pos.x_pos + dx;
	y = item->pos.y_pos;
	z = item->pos.z_pos + dz;
	room_number = item->room_number;
	floor = GetFloor(x, y, z, &room_number);

	if (floor->box == 2047 || zone[floor->box] != zone[item->box_number])
		return false;

	h = GetHeight(floor, x, y, z);
	c = GetCeiling(floor, x, y, z);

	if (h == NO_HEIGHT || y > h || y < c)
		return false;

	item->pos.x_pos = x;
	item->pos.z_pos = z;
	return true;
}

// One line of the seam: a state change, a shot (`shot` set) or the death.
static void DroneLog(short item_number, unsigned tick, int state, long dist,
	bool los, bool fire, const char* shot, long dmg)
{
	if (!gLogEnabled)
		return;

	if (shot)
		Log("DRONELOG item=%d tick=%u state=%s dist=%ld los=%d fire=%d shot=%s dmg=%ld",
			item_number, tick, DroneStateName(state), dist, los ? 1 : 0,
			fire ? 1 : 0, shot, dmg);
	else
		Log("DRONELOG item=%d tick=%u state=%s dist=%ld los=%d fire=%d",
			item_number, tick, DroneStateName(state), dist, los ? 1 : 0,
			fire ? 1 : 0);
}

/* The gunship's gun (ControlGunship, game\tower.cpp), from the muzzle flash
 * and with the drone's own damage. It sounds and flashes every tick of a
 * burst and looks for what it hits every other tick, as the gunship does;
 * the ray runs on past the aim point twice as far again, is clipped at the
 * first wall, and the first thing on it takes the shot - Lara, a smashable
 * object, a glass static or the wall. The aim's scatter round her torso is
 * a hash of the drone's own tick, not a random number (drone_rule.h). */
static void DroneShoot(short item_number, ITEM_INFO* item, unsigned tick,
	int state, long dist)
{
	MESH_INFO* mesh;
	SPARKS* sptr;
	PHD_VECTOR m, aim, hit;
	GAME_VECTOR from, to, end;
	const char* what;
	long target, blocked, dmg;
	unsigned h;
	short status;

	SoundEffect(SFX_HK_FIRE, &item->pos, 0xC00000 | SFX_SETPITCH);
	item->mesh_bits |= DRONE_FLASH_BIT;

	if (!(GlobalCounter & 1))
		return;

	h = DroneHash(item_number, tick);

	m.x = 0;
	m.y = 0;
	m.z = 0;
	GetJointAbsPosition(item, &m, DRONE_MUZZLE_NODE);
	from.x = m.x;
	from.y = m.y;
	from.z = m.z;
	from.room_number = item->room_number;
	GetFloor(from.x, from.y, from.z, &from.room_number);

	aim.x = DroneSpread(item_number, tick, 0);
	aim.y = DroneSpread(item_number, tick, 1);
	aim.z = DroneSpread(item_number, tick, 2);
	GetLaraJointPos(&aim, LMX_TORSO);

	to.x = from.x + 3 * (aim.x - from.x);
	to.y = from.y + 3 * (aim.y - from.y);
	to.z = from.z + 3 * (aim.z - from.z);
	to.room_number = from.room_number;
	end = to;
	blocked = !LOS(&from, &end);

	/* The drone's own box is collidable and the muzzle sits on its rim: out
	 * of the search for the length of the one call. */
	status = item->status;
	item->status = ITEM_INVISIBLE;
	mesh = 0;
	GetLaraOnLOS = 1;
	target = ObjectOnLOS2(&from, &end, &hit, &mesh);
	GetLaraOnLOS = 0;
	item->status = status;

	TriggerDynamic(from.x, from.y, from.z, 16, 96 + (h & 0x3F), 64 + ((h >> 6) & 0x1F), 0);
	what = "none";
	dmg = 0;

	if (target != 999 && target >= 0)
	{
		if (items[target].object_number == LARA)
		{
			what = "lara";
			dmg = EnemyDamage(balance.dmgDrone);

			// Refused while a scene holds Lara (scene_hold.h).
			if (SceneHoldRefuses(dmg, item->object_number, "drone"))
				dmg = 0;
			else
			{
				DoBloodSplat(hit.x, hit.y, hit.z, short(((h >> 11) & 1) + 2),
					short(h >> 16), lara_item->room_number);
				lara_item->hit_points -= (short)dmg;
			}
		}
		else if (items[target].object_number >= SMASH_OBJECT1 &&
			items[target].object_number <= SMASH_OBJECT8)
		{
			what = "smash";
			ExplodeItemNode(&items[target], 0, 0, 0x80);
			SmashObject((short)target);
			KillItem((short)target);
		}
		else
			what = "item";
	}
	else
	{
		if (blocked)
		{
			what = "wall";
			TriggerRicochetSpark(&end, (h >> 12) & 0xFFFE, 3, 0);
			TriggerRicochetSpark(&end, (h >> 4) & 0xFFFE, 3, 0);
		}

		if (target < 0 && mesh && (h & 0x40000))
		{
			what = "static";

			if (mesh->static_number >= 50 && mesh->static_number < 59)
			{
				ShatterObject(0, mesh, 64, end.room_number, 0);
				mesh->Flags &= ~0x1;
				TestTriggersAtXYZ(mesh->x, mesh->y, mesh->z, end.room_number, 1, 0);
				SoundEffect(ShatterSounds[gfLevelId][mesh->static_number - 50],
					(PHD_3DPOS*)mesh, 0);
			}

			TriggerRicochetSpark((GAME_VECTOR*)&hit, (h >> 8) & 0xFFFE, 3, 0);
		}
	}

	// The tracer, as the gunship draws it.
	sptr = &spark[GetFreeSpark()];
	sptr->On = 1;
	sptr->sR = (uchar)(128 + (h & 0x7F));
	sptr->sG = (uchar)(((h >> 7) & 0x7F) + (sptr->sR >> 1));

	if (sptr->sG > sptr->sR)
		sptr->sG = sptr->sR;

	sptr->sB = 0;
	sptr->dR = 0;
	sptr->dG = 0;
	sptr->dB = 0;
	sptr->ColFadeSpeed = 12;
	sptr->TransType = 2;
	sptr->FadeToBlack = 0;
	sptr->Life = 12;
	sptr->sLife = 12;
	sptr->x = from.x;
	sptr->y = from.y;
	sptr->z = from.z;

	/* The gunship's speed, four times the clipped ray a frame - which at
	 * the drone's range would run past what a short holds, so the three
	 * axes are cut down together and the tracer keeps its direction. */
	{
		long vx = 4 * (end.x - from.x), vy = 4 * (end.y - from.y);
		long vz = 4 * (end.z - from.z), most = abs(vx);

		if (abs(vy) > most)
			most = abs(vy);

		if (abs(vz) > most)
			most = abs(vz);

		if (most > 32000)
		{
			vx = (long)((long long)vx * 32000 / most);
			vy = (long)((long long)vy * 32000 / most);
			vz = (long)((long long)vz * 32000 / most);
		}

		sptr->Xvel = (short)vx;
		sptr->Yvel = (short)vy;
		sptr->Zvel = (short)vz;
	}
	sptr->Friction = 0;
	sptr->MaxYvel = 0;
	sptr->Gravity = 0;
	sptr->Flags = SF_NONE;

	DroneLog(item_number, tick, state, dist, true, true, what, dmg);
}

/* Its death, on the pattern of the sentry gun's motion sensors
 * (ControlMotionSensors, game\autogun.cpp): the bang and two bursts of
 * explosion sparks in the middle of its box, then CreatureDie with its
 * explode flag - the body flies apart in pieces, the item ends, the AI
 * slot is freed and anything it carried drops. The kill itself was counted
 * by the shot that took its last hit point (HitTarget, game\larafire.cpp),
 * as for every enemy. */
static void DroneDie(short item_number, ITEM_INFO* item, unsigned tick)
{
	short* bounds;
	long y;

	bounds = GetBoundsAccurate(item);
	y = item->pos.y_pos + ((bounds[2] + bounds[3]) >> 1);
	item->mesh_bits &= ~DRONE_FLASH_BIT;
	SoundEffect(SFX_EXPLOSION2, &item->pos, 0);
	TriggerExplosionSparks(item->pos.x_pos, y, item->pos.z_pos, 2, -2, 0, item->room_number);
	TriggerExplosionSparks(item->pos.x_pos, y, item->pos.z_pos, 2, 0, 0, item->room_number);
	item->item_flags[0] = DRONE_DEAD;
	DroneLog(item_number, tick, DRONE_DEAD, 0, false, false, 0, 0);
	CreatureDie(item_number, 1);
}

void InitialiseDrone(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	item->anim_number = objects[DRONE].anim_index;
	item->frame_number = anims[item->anim_number].frame_base;
	item->current_anim_state = 0;
	item->goal_anim_state = 0;
	item->mesh_bits &= ~DRONE_FLASH_BIT;
	item->item_flags[0] = DRONE_CLOSE;
	item->item_flags[1] = 0;
	item->item_flags[2] = 0;
}

void DroneControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* drone;
	FLOOR_INFO* floor;
	AI_INFO info;
	GAME_VECTOR from, to;
	PHD_VECTOR torso;
	short* bounds;
	short* lara_bounds;
	long lx, lz, px, pz, dx, dz, dist, h, c;
	unsigned tick, leg;
	int last, state, sign;
	short heading, nose, room_number;
	bool los, fire;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	drone = (CREATURE_INFO*)item->data;
	last = item->item_flags[0];
	tick = (unsigned short)item->item_flags[1];
	item->item_flags[1] = (short)(tick + 1);

	// #124's lesson: a control that never reads its hit points never dies.
	if (item->hit_points <= 0)
	{
		DroneDie(item_number, item, tick);
		return;
	}

	SoundEffect(SFX_HELICOPTER_LOOP, &item->pos, 0);

	CreatureAIInfo(item, &info);
	GetCreatureMood(item, &info, 1);
	CreatureMood(item, &info, 1);

	// Lara from the drone, and the line from the middle of its box to her
	// torso.
	lx = lara_item->pos.x_pos - item->pos.x_pos;
	lz = lara_item->pos.z_pos - item->pos.z_pos;
	dist = (long)DroneSqrt((unsigned long long)((long long)lx * lx + (long long)lz * lz));
	bounds = GetBoundsAccurate(item);
	from.x = item->pos.x_pos;
	from.y = item->pos.y_pos + ((bounds[2] + bounds[3]) >> 1);
	from.z = item->pos.z_pos;
	from.room_number = item->room_number;
	GetFloor(from.x, from.y, from.z, &from.room_number);
	torso.x = 0;
	torso.y = 0;
	torso.z = 0;
	GetLaraJointPos(&torso, LMX_TORSO);
	to.x = torso.x;
	to.y = torso.y;
	to.z = torso.z;
	to.room_number = lara_item->room_number;
	los = LOS(&from, &to) != 0;

	state = DroneNextState(last, dist, los);

	// The nose round toward her.
	heading = (short)phd_atan(lz, lx);
	nose = (short)(heading - (short)(item->pos.y_rot + DRONE_NOSE));
	item->pos.y_rot += DroneTurnStep(nose);

	// The strafe leg's side, and the step this tick.
	leg = tick / DRONE_LEG + (unsigned short)item->item_flags[2];
	sign = DroneStrafeSign(item_number, leg);
	px = drone->target.x - item->pos.x_pos;
	pz = drone->target.z - item->pos.z_pos;
	DroneMove(state, lx, lz, px, pz, sign, &dx, &dz);

	if (!DroneStep(item, dx, dz))
	{
		// A strafe the room blocks turns round; a retreat it blocks
		// strafes instead.
		if (state == DRONE_HOLD)
			item->item_flags[2]++;
		else if (state == DRONE_BACK)
		{
			DroneMove(DRONE_HOLD, lx, lz, px, pz, sign, &dx, &dz);

			if (!DroneStep(item, dx, dz))
				item->item_flags[2]++;
		}
	}

	// The height: over Lara's head, clear of this room's floor and ceiling.
	// The creature code climbs toward it at the flyer's 16 units a tick.
	room_number = item->room_number;
	floor = GetFloor(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos, &room_number);
	h = GetHeight(floor, item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
	c = GetCeiling(floor, item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
	lara_bounds = GetBestFrame(lara_item);

	if (h != NO_HEIGHT)
		drone->target.y = DroneHoverY(lara_item->pos.y_pos + lara_bounds[2], h, c,
			bounds[2], bounds[3]);

	item->pos.z_rot = DroneBankStep(item->pos.z_rot, state, sign);

	fire = DroneFires(state, los, dist, nose, DroneBurstOn(item_number, tick));

	if (state != last)
	{
		item->item_flags[0] = (short)state;
		DroneLog(item_number, tick, state, dist, los, fire, 0, 0);
	}

	if (fire)
		DroneShoot(item_number, item, tick, state, dist);
	else
		item->mesh_bits &= ~DRONE_FLASH_BIT;

	CreatureAnimation(item_number, 0, 0);
}

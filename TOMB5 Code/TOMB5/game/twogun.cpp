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
#include "twogun.h"
#include "lara_states.h"
#include "items.h"
#include "../specific/audio.h"
#include "box.h"
#include "sphere.h"
#include "effect2.h"
#include "control.h"
#include "effects.h"
#include "../specific/3dmath.h"
#include "people.h"
#include "../specific/specificfx.h"
#include "../specific/function_stubs.h"
#include "tomb4fx.h"
#include "draw.h"
#include "traps.h"
#include "sound.h"
#include "spotcam.h"
#include "objects.h"
#include "lara.h"
#include "zip_call_rule.h"
#include "deltapak.h"
#include "gameflow.h"
#include "../specific/input.h"
#include "../specific/dxshell.h"
#include "../tomb5/tomb5.h"

static_assert(ZIP_AS_INTO == AS_INTO_ZIP && ZIP_AS_POSE == AS_ZIP &&
	ZIP_AS_OUT == AS_OUT_ZIP, "zip_call_rule.h states");

TWOGUN_INFO twogun[4];

static BITE_INFO Guns[2] =
{
	{ 0, 230, 40, 8 },
	{ 8, 230, 40, 5 }
};

static BITE_INFO twogun_head = { 0,-200, 0, 2 };

/* ZIPLOG, behind -log: one line at each step of a Zip call and at its end
 * (zip_call_rule.h, ZipCallWord) - the item, the stage it is in, its track
 * and the tick (GlobalCounter, as in FLYLOG and SKIPLATCH). */
static void ZipLog(short item_number, int step)
{
	ITEM_INFO* item;

	if (!LogFam_ZIPLOG || !gLogEnabled)
		return;

	item = &items[item_number];
	Log("ZIPLOG item=%d stage=%d track=%d tick=%d %s", item_number,
		item->item_flags[0], item->trigger_flags, GlobalCounter,
		ZipCallWord(step));
}

/* The one end of a call, whatever ends it (zip_call_rule.h): Lara out of
 * the pose as ZipPoseEnd says, the music back to the level's atmosphere
 * while the call's own track is still the music (a skip cut it), her
 * control back, the item killed. */
static void ZipCallFinish(short item_number, int why)
{
	ITEM_INFO* item;
	int pose, music;

	item = &items[item_number];
	pose = ZipPoseEnd(why, lara_item->current_anim_state);
	music = ZipEndMusic(XATrack == item->trigger_flags, CurrentAtmosphere);

	if (pose == ZIP_POSE_GOAL_OUT || pose == ZIP_POSE_OUT)
		lara_item->goal_anim_state = AS_OUT_ZIP;
	else if (pose == ZIP_POSE_STAND)
	{
		lara_item->goal_anim_state = AS_STOP;
		lara_item->current_anim_state = AS_STOP;
		lara_item->anim_number = ANIM_BREATH;
		lara_item->frame_number = anims[lara_item->anim_number].frame_base;
	}

	if (music == ZIP_MUSIC_ATMOSPHERE)
		S_CDPlay(CurrentAtmosphere, 1);
	else if (music == ZIP_MUSIC_STOP)
		S_CDStop();

	ZipLog(item_number, why);
	bDisableLaraControl = 0;
	KillItem(item_number);
}

void ControlZipController(short item_number)
{
	ITEM_INFO* item;
	int step;

	item = &items[item_number];
	step = ZipCallTick(item->item_flags[0], item->item_flags[1],
		lara_item->current_anim_state == AS_STOP &&
		lara_item->anim_number == ANIM_BREATH,
		item->item_flags[0] == ZIP_STAGE_LISTEN &&
		S_CDTrackPlaying(item->trigger_flags));

	switch (step)
	{
	case ZIP_STARTED:
		ZipLog(item_number, step);
		bDisableLaraControl = 1;
		item->item_flags[0]++;
		break;

	case ZIP_POSING:
		ZipLog(item_number, step);
		lara_item->goal_anim_state = AS_INTO_ZIP;
		lara_item->current_anim_state = AS_INTO_ZIP;
		lara_item->anim_number = ANIM_ZIP_IN;
		lara_item->frame_number = anims[lara_item->anim_number].frame_base;
		item->item_flags[0]++;
		IsAtmospherePlaying = 0;
		S_CDPlay(item->trigger_flags, 0);
		break;

	case ZIP_HEARD:
		ZipLog(item_number, step);
		item->item_flags[1] = 1;
		break;

	case ZIP_ENDED:
	case ZIP_ENDED_NO_TRACK:
		ZipCallFinish(item_number, step);
		break;
	}
}

/* A skipped scene ends every Zip call still running (zip_call_rule.h): the
 * end of a flyby a skip jumped (game\spotcam.cpp) and the cutscene skipper
 * (game\control.cpp) call this, and so does the skip of a call with no
 * scene below. A call is an active item whose control is the one above,
 * whatever its slot. */
void ZipCallsEndBySkip()
{
	short item_number, next;

	for (item_number = next_item_active; item_number != NO_ITEM;
		item_number = next)
	{
		next = items[item_number].next_active;

		if (objects[items[item_number].object_number].control ==
			ControlZipController)
			ZipCallFinish(item_number, ZIP_ENDED_BY_SKIP);
	}
}

/* The pad's confirm button as the skip without a scene last read it, on
 * every tick a call runs: the call's first tick, stage 0, which holds
 * nothing yet, primes it, so a button already down when the call begins
 * does not skip it. */
static char zip_confirm_held;

/* ZIPSKIP, behind -log: a call ended by a skip with no scene running - the
 * item, its stage, the tick (GlobalCounter) and what skipped it, escape or
 * pad. Its ZIPLOG line "ended by skip" follows. */
static void ZipSkipLog(short item_number, int how)
{
	if (!LogFam_ZIPSKIP || !gLogEnabled)
		return;

	Log("ZIPSKIP item=%d stage=%d tick=%d how=%s scene=0", item_number,
		items[item_number].item_flags[0], GlobalCounter, ZipSkipWord(how));
}

/* The skip of a call that holds Lara with no scene running (zip_call_rule.h,
 * ZipSkipStarts). game\control.cpp asks it once a tick, after her input is
 * masked and before the inventory, and makes the tick it fires on a held
 * one. The call ends as under a skipped scene (ZipCallsEndBySkip), and a
 * press of the pad's confirm button stays out of the input until it is let
 * go (game\skip_latch.h). Returns whether it fired. */
long ZipCallSkipWithoutScene()
{
	ITEM_INFO* item;
	short item_number;
	int fresh, step, how;

	for (item_number = next_item_active; item_number != NO_ITEM;
		item_number = items[item_number].next_active)
	{
		if (objects[items[item_number].object_number].control ==
			ControlZipController)
			break;
	}

	if (item_number == NO_ITEM)
		return 0;

	item = &items[item_number];
	fresh = SkipConfirmEdge((int)JoyConfirmDown(), &zip_confirm_held);

	// What the call's own tick does now, asked as the controller asks it.
	step = ZipCallTick(item->item_flags[0], item->item_flags[1],
		lara_item->current_anim_state == AS_STOP &&
		lara_item->anim_number == ANIM_BREATH,
		item->item_flags[0] == ZIP_STAGE_LISTEN &&
		S_CDTrackPlaying(item->trigger_flags));
	how = ZipSkipStarts(tomb5.cutseq_skipper && gfLevelId != LVL5_TITLE,
		item->item_flags[0], bDisableLaraControl != 0, step,
		bUseSpotCam || bTrackCamInit || cutseq_trig || GLOBAL_playing_cutseq,
		keymap[DIK_ESCAPE] != 0, fresh);

	if (how == ZIP_SKIP_NONE)
		return 0;

	ZipSkipLog(item_number, how);
	ZipCallsEndBySkip();

	if (ZipSkipLatchKind(fresh))
		InputSkipLatch(ZipSkipLatchKind(fresh));

	return 1;
}

void InitialiseTwogun(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	item->anim_number = objects[item->object_number].anim_index + 6;
	item->frame_number = anims[item->anim_number].frame_base;
	item->goal_anim_state = 1;
	item->current_anim_state = 1;
}

void TwogunControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* creature;
	BITE_INFO* bite;
	PHD_VECTOR pos;
	AI_INFO info;
	AI_INFO lara_info;
	long dx, dz;
	short angle, head, torso_x, torso_y, frame, base, room_number;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	creature = (CREATURE_INFO*)item->data;
	angle = 0;
	head = 0;
	torso_x = 0;
	torso_y = 0;

	if (item->item_flags[0] || item->item_flags[1])
	{
		bite = Guns;

		for (int i = 0; i < 2; i++)
		{
			if (item->item_flags[i])
			{
				pos.x = bite->x;
				pos.y = bite->y;
				pos.z = bite->z;
				GetJointAbsPosition(item, &pos, bite->mesh_num);
				FireTwogunWeapon(item, i, 1);

				if (item->item_flags[i] <= 0)
					item->item_flags[i]++;
				else
				{
					dx = item->item_flags[i] << 4;

					if (dx > 255)
						dx = 255;

					TriggerDynamic(pos.x, pos.y, pos.z, item->item_flags[i] + 8, 0, dx >> 2, dx);
					item->item_flags[i]--;
				}
			}
		}
	}

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state != 7 && item->trigger_flags != 1)
		{
			item->anim_number = objects[item->object_number].anim_index + 3;
			item->frame_number = anims[item->anim_number].frame_base;
			item->current_anim_state = 7;
		}
		else if (item->trigger_flags == 1)
		{
			switch (item->current_anim_state)
			{
			case 11:
				frame = item->frame_number;
				base = anims[item->anim_number].frame_base;

				if (frame == base + 48 || frame == base + 15)
				{
					room_number = item->room_number;
					GetHeight(GetFloor(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos, &room_number),
						item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
					TestTriggers(trigger_index, 1, 0);
				}

				break;

			case 12:
				item->gravity_status = 1;

				if (item->pos.y_pos >= item->floor)
				{
					item->pos.y_pos = item->floor;
					item->gravity_status = 0;
					item->fallspeed = 0;
					item->goal_anim_state = 13;
					CreatureEffectT(item, &twogun_head, 20, item->pos.y_rot, DoBloodSplat);
				}

				break;

			case 13:
				item->pos.y_pos = item->floor;
				break;

			default:
				item->anim_number = objects[item->object_number].anim_index + 14;
				item->frame_number = anims[item->anim_number].frame_base;
				item->current_anim_state = 11;
				creature->LOT.is_jumping = 1;
				item->pos.x_pos = room[item->room_number].x + ((item->draw_room & 0xFFFFFF00) << 2) + 512;
				item->pos.y_pos = room[item->room_number].minfloor + (item->item_flags[2] & 0xFFFFFF00);
				item->pos.z_pos = ((item->draw_room & 0xFF) << 10) + room[item->room_number].z + 512;
				item->pos.y_rot = item->TOSSPAD & 57344;
				break;
			}
		}
	}
	else
	{
		if (item->ai_bits)
			GetAITarget(creature);
		else
			creature->enemy = lara_item;

		CreatureAIInfo(item, &info);

		if (creature->enemy == lara_item)
		{
			lara_info.angle = info.angle;
			lara_info.distance = info.distance;
		}
		else
		{
			dx = lara_item->pos.x_pos - item->pos.x_pos;
			dz = lara_item->pos.z_pos - item->pos.z_pos;
			lara_info.angle = short(phd_atan(dz, dx) - item->pos.y_rot);
			lara_info.distance = SQUARE(dx) + SQUARE(dz);
		}

		GetCreatureMood(item, &info, creature->enemy != lara_item);
		CreatureMood(item, &info, creature->enemy != lara_item);
		angle = CreatureTurn(item, creature->maximum_turn);

		if (((lara_info.distance < 0x400000 && (lara_info.angle < 0x4000 && lara_info.angle > -16384 || lara_item->speed > 20)) ||
			item->hit_status || TargetVisible(item, &lara_info)) && abs(item->pos.y_pos - lara_item->pos.y_pos) < 1536)
		{
			creature->enemy = lara_item;
			AlertAllGuards(item_number);
		}

		switch (item->current_anim_state)
		{
		case 1:
			creature->flags = 0;
			creature->maximum_turn = 0;

			if (!(item->ai_bits & GUARD))
			{
				if (abs(info.angle) < 364)
					item->pos.y_rot += info.angle;
				else
				{
					if (info.angle >= 0)
						item->pos.y_rot += 364;
					else
						item->pos.y_rot -= 364;
				}

				head = lara_info.angle >> 1;
				torso_y = lara_info.angle >> 1;
				torso_x = info.x_angle >> 1;
			}

			if (item->ai_bits & GUARD)
				head = AIGuard(creature);
			else if (lara_info.angle > 20480 || lara_info.angle < -20480)
				item->goal_anim_state = 8;
			else if (!Targetable(item, &info))
			{
				if (item->trigger_flags != 1)
					item->goal_anim_state = 2;
			}
			else if (info.distance < 0x900000 || info.zone_number != info.enemy_zone)
				item->goal_anim_state = 5;
			else if (item->ai_bits != MODIFY)
			{
				if (item->trigger_flags != 1)
					item->goal_anim_state = 2;
			}

			break;

		case 2:
			creature->maximum_turn = 910;

			if (Targetable(item, &info) && lara_info.angle < 6144 && lara_info.angle > -6144)
			{
				if (item->frame_number >= anims[item->anim_number].frame_base + 29)
					item->goal_anim_state = 3;
				else
					item->goal_anim_state = 4;
			}
			else
			{
				if (lara_info.angle > 20480 || lara_info.angle < -20480)
					item->goal_anim_state = 1;
				else
					item->goal_anim_state = 2;
			}

			break;

		case 3:
		case 4:
			head = lara_info.angle;

			if (item->frame_number == anims[item->anim_number].frame_base)
			{
				if (item->current_anim_state == 4)
					FireTwogunWeapon(item, 0, 0);
				else
					FireTwogunWeapon(item, 1, 0);
			}

			break;

		case 5:
			creature->flags = 0;
			creature->maximum_turn = 0;
			head = lara_info.angle >> 1;
			torso_y = lara_info.angle >> 1;
			torso_x = info.x_angle >> 1;

			if (abs(info.angle) < 364)
				item->pos.y_rot += info.angle;
			else if (info.angle >= 0)
				item->pos.y_rot += 364;
			else
				item->pos.y_rot -= 364;

			if (item->trigger_flags == 2)
				item->goal_anim_state = 9;
			else if (item->trigger_flags == 3)
				item->goal_anim_state = 10;
			else if (Targetable(item, &info))
				item->goal_anim_state = 6;
			else
				item->goal_anim_state = 1;

			break;

		case 6:
			head = lara_info.angle >> 1;
			torso_y = lara_info.angle >> 1;
			torso_x = info.x_angle;

			if (abs(info.angle) < 364)
				item->pos.y_rot += info.angle;
			else if (info.angle >= 0)
				item->pos.y_rot += 364;
			else
				item->pos.y_rot -= 364;

			if (item->frame_number == anims[item->anim_number].frame_base + 17)
			{
				FireTwogunWeapon(item, 0, 0);
				FireTwogunWeapon(item, 1, 0);
			}

			break;

		case 8:
			creature->flags = 0;
			creature->maximum_turn = 0;

			if (info.angle < 0)
				item->pos.y_rot += 364;
			else
				item->pos.y_rot -= 364;

			if (item->frame_number == anims[item->anim_number].frame_end)
				item->pos.y_rot += 32768;

			break;

		case 9:
			torso_x = info.x_angle >> 1;
			head = lara_info.angle >> 1;
			torso_y = lara_info.angle >> 1;

			if (item->frame_number == anims[item->anim_number].frame_base + 18)
			{
				FireTwogunWeapon(item, 0, 0);
				item->item_flags[1] = -16;
				item->trigger_flags = 3;
			}

			break;

		case 10:
			item->trigger_flags = 0;
			break;
		}
	}

	CreatureJoint(item, 0, torso_y);
	CreatureJoint(item, 1, torso_x);
	CreatureJoint(item, 2, head);
	CreatureAnimation(item_number, angle, 0);
}

void UpdateTwogunLasers()
{
	TWOGUN_INFO* info;

	for (int i = 0; i < 4; i++)
	{
		info = &twogun[i];

		if (!info->life)
			continue;

		info->life--;

		if (info->life < 16)
		{
			info->size++;
			info->spinadd -= info->spinadd >> 3;
		}
		else if (info->life == 16)
		{
			info->spinadd = 8192;
			info->coil = 8192;
			info->length = info->dlength;
			info->size = 4;
		}
		else
		{
			info->coil += (8192 - info->coil) >> 3;

			if (info->dlength - info->length > info->dlength >> 2)
				info->length += (info->dlength - info->length) >> 2;
			else
			{
				info->spinadd += (8192 - info->spinadd) >> 3;
				info->length = info->dlength;
			}

			if (info->size < 4)
				info->size++;
		}

		if (info->fadein < 8)
			info->fadein++;

		info->spin -= info->spinadd;
	}
}

void DrawTwogunLasers()
{
	TWOGUN_INFO* info;

	for (int i = 0; i < 4; i++)
	{
		info = &twogun[i];

		if (info->life)
			DrawTwogunLaser(info);
	}
}

void TriggerTwogunPlasma(PHD_VECTOR* pos, short* angles, long life)
{
	SPARKS* sptr;
	long ang, size;

	sptr = &spark[GetFreeSpark()];
	sptr->On = 1;
	sptr->sB = uchar((life * ((GetRandomControl() & 0x7F) + 128)) >> 4);
	sptr->sR = sptr->sB - (sptr->sB >> 2);
	sptr->sG = sptr->sR;
	sptr->dR = 0;
	sptr->dB = uchar((life * ((GetRandomControl() & 0x7F) + 32)) >> 4);
	sptr->dG = sptr->dB >> 2;
	sptr->FadeToBlack = 8;
	sptr->ColFadeSpeed = (GetRandomControl() & 3) + 8;
	sptr->TransType = 2;
	sptr->Life = (GetRandomControl() & 3) + 24;
	sptr->sLife = sptr->Life;
	sptr->x = pos->x;
	sptr->y = pos->y;
	sptr->z = pos->z;
	
	ang = (GetRandomControl() & 0x7FFF) + angles[0] - 0x4000;
	size = ((life << 6) * phd_cos(angles[1])) >> W2V_SHIFT;
	sptr->Xvel = short((size * phd_sin(ang)) >> W2V_SHIFT);
	sptr->Yvel = short(((life << 4) * phd_sin(-angles[1])) >> W2V_SHIFT);
	sptr->Zvel = short((size * phd_cos(ang)) >> W2V_SHIFT);

	sptr->Friction = 0;
	sptr->Flags = SF_ROTATE | SF_DEF | SF_SCALE;
	sptr->RotAng = GetRandomControl() & 0xFFF;
	sptr->RotAdd = (GetRandomControl() & 0x7F) - 64;
	sptr->Gravity = (GetRandomControl() & 0x1F) + 32;
	sptr->MaxYvel = 0;
	sptr->Scalar = 1;
	sptr->Size = (GetRandomControl() & 0x3F) + 16;
	sptr->sSize = sptr->Size;
	sptr->dSize = 1;
}

void FireTwogunWeapon(ITEM_INFO* item, long lr, long plasma)
{
	TWOGUN_INFO* info;
	BITE_INFO* bite;
	PHD_VECTOR s;
	PHD_VECTOR d;
	short* bounds;
	long x, y, z, dx, dy, dz, xStep, yStep, zStep, dist, hit;
	short angles[2];

	bite = &Guns[lr];

	s.x = bite->x;
	s.y = bite->y;
	s.z = bite->z;
	GetJointAbsPosition(item, &s, bite->mesh_num);

	d.x = bite->x;
	d.y = bite->y + 4096;
	d.z = bite->z;
	GetJointAbsPosition(item, &d, bite->mesh_num);

	phd_GetVectorAngles(d.x - s.x, d.y - s.y, d.z - s.z, angles);

	if (plasma)
	{
		TriggerTwogunPlasma(&s, angles, abs(item->item_flags[lr]));
		return;
	}

	for (int i = 0; i < 4; i++)
	{
		info = &twogun[i];

		if (!info->life || i == 3)
			break;
	}

	info->pos.x_pos = s.x;
	info->pos.y_pos = s.y;
	info->pos.z_pos = s.z;
	info->pos.x_rot = angles[1];
	info->pos.y_rot = angles[0];
	info->pos.z_rot = 0;
	info->life = 17;
	info->spin = short(GetRandomControl() << 11);
	info->dlength = 4096;
	info->r = 0;
	info->g = 96;
	info->b = -1;	//255
	info->fadein = 8;
	TriggerLightningGlow(info->pos.x_pos, info->pos.y_pos, info->pos.z_pos,
		RGBA(0, uchar(info->g >> 1), uchar(info->b >> 1), (GetRandomControl() & 3) + 64));
	TriggerLightning(&s, &d, (GetRandomControl() & 7) + 8, RGBA(0, info->g, info->b, 22), 12, 80, 5);

	item->item_flags[lr] = 16;
	TriggerTwogunPlasma(&s, angles, 16);
	TriggerTwogunPlasma(&s, angles, 16);
	TriggerTwogunPlasma(&s, angles, 16);

	if (lara.burn)
		return;

	hit = 0;
	bounds = GetBoundsAccurate(lara_item);
	x = lara_item->pos.x_pos + ((bounds[0] + bounds[1]) >> 1);
	y = lara_item->pos.y_pos + ((bounds[2] + bounds[3]) >> 1);
	z = lara_item->pos.z_pos + ((bounds[4] + bounds[5]) >> 1);
	dist = phd_sqrt(SQUARE(x - s.x) + SQUARE(y - s.y) + SQUARE(z - s.z));

	if (dist < 4096)
	{
		dx = s.x;
		dy = s.y;
		dz = s.z;
		xStep = (d.x - dx) >> 4;
		yStep = (d.y - dy) >> 4;
		zStep = (d.z - dz) >> 4;

		for (int i = 0; i < dist; i += 256)
		{
			if (abs(x - dx) < 320 && abs(y - dy) < 320 && abs(z - dz) < 320)
			{
				hit = 1;
				break;
			}

			dx += xStep;
			dy += yStep;
			dz += zStep;
		}
	}

	// Both refused while a scene holds Lara (scene_hold.h).
	if (hit)
	{
		if (lara_item->hit_points >= 501)
		{
			if (!SceneHoldRefuses(EnemyDamage(balance.dmgTwoGunPlasma),
				item->object_number, "plasma"))
				lara_item->hit_points -= EnemyDamage(balance.dmgTwoGunPlasma);
		}
		else if (!SceneHoldRefuses(SCENE_HOLD_WHOLE_LIFE, item->object_number,
			"plasma"))
		{
			LaraBurn();
			lara.BurnBlue = 1;
			lara.BurnCount = 48;
			lara_item->hit_points = 0;
		}
	}
}

void ControlGunTestStation(ITEM_INFO* item)
{
	TWOGUN_INFO* p;
	PHD_VECTOR pos;
	PHD_VECTOR pos2;
	short f, lp;
	short ang[2];

	f = item->frame_number - anims[item->anim_number].frame_base;

	if (f < 340)
	{
		item->mesh_bits = 1;
		return;
	}

	if (f > 372)
		return;

	if (f == 340)
	{
		SoundEffect(SFX_SMASH_ROCK, &item->pos, SFX_DEFAULT);
		SoundEffect(SFX_2GUNTEX_LASER_FIRE, &item->pos, SFX_DEFAULT);
		ExplodeItemNode(item, 0, 0, 64);
		item->mesh_bits = 2;

		for (lp = 0; lp < 4; lp++)
		{
			if (!twogun[lp].life || lp == 3)
				break;
		}

		p = &twogun[lp];
		p->pos.x_pos = item->pos.x_pos + (512 * phd_sin(item->pos.y_rot) >> W2V_SHIFT);
		p->pos.y_pos = item->pos.y_pos - 628;
		p->pos.z_pos = item->pos.z_pos + (512 * phd_cos(item->pos.y_rot) >> W2V_SHIFT);
		p->pos.x_rot = -512;
		p->pos.y_rot = item->pos.y_rot;
		p->pos.z_rot = 0;
		p->life = 32;
		p->coil = 0;
		p->spin = 0;
		p->spinadd = 0;
		p->length = 0;
		p->dlength = 3072;
		p->size = 1;
		p->r = 0;
		p->g = 96;
		p->b = -1;	//255
		p->fadein = 0;
		item->item_flags[0] = lp;
	}

	p = &twogun[item->item_flags[0]];
	pos.x = p->pos.x_pos;
	pos.y = p->pos.y_pos;
	pos.z = p->pos.z_pos;

	ang[0] = item->pos.y_rot;
	ang[1] = 0;
	TriggerTwogunPlasma(&pos, ang, (373 - f) >> 1);
	TriggerTwogunPlasma(&pos, ang, (373 - f) >> 1);

	if (f == 340 || GlobalCounter & 1 && f < 356)
	{
		p = &twogun[item->item_flags[0]];
		pos2.x = p->pos.x_pos + (p->dlength * phd_sin(item->pos.y_rot) >> W2V_SHIFT);
		pos2.y = p->pos.y_pos + 128;
		pos2.z = p->pos.z_pos + (p->dlength * phd_cos(item->pos.y_rot) >> W2V_SHIFT);

		if (f == 340)
			TriggerLightning(&pos, &pos2, (GetRandomControl() & 7) + 8, RGBA(uchar(p->r >> 1), uchar(p->g >> 1), uchar(p->b >> 1), 50), 12, 64, 5);

		TriggerLightningGlow(pos.x, pos.y, pos.z, RGBA(p->r >> 1, p->g >> 1, p->b >> 1, (GetRandomControl() & 3) + 64));
		TriggerDynamic(pos.x, pos.y, pos.z, (GetRandomControl() & 3) + 20, p->r, p->g, p->b);
	}
}

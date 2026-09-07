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
#include "health.h"
#include "gameflow.h"
#include "newinv2.h"
#include "objects.h"
#include "../specific/output.h"
#include "text.h"
#include "deltapak.h"
#include "camera.h"
#include "spotcam.h"
#include "control.h"
#include "../specific/3dmath.h"
#include "lara.h"
#include "savegame.h"
#include "larafire.h"
#include "../specific/input.h"
#include "../tomb5/tomb5.h"
#include "../specific/drawbars.h"
#include "interpolate.h"		// the in-between frame's counters (#148, order 3)

DISPLAYPU pickups[8];
long PickupX;
short CurrentPickup;

long health_bar_timer = 0;

static long FullPickupX;
static short PickupVel;

long FlashIt()
{
	static long flash_state;
	static long flash_count;

	/* R4 (#148, order 3): the health and air bars blink on this, five frames
	 * on and five off, counted per DRAWN frame from DrawGameInfo. At sixty
	 * it would blink twice as fast. The in-between frame is handed the state
	 * the last tick left, so the bar keeps showing whatever it was showing
	 * and the blink keeps its speed. */
	if (InterpIsTweenFrame())
		return flash_state;

	if (flash_count)
		flash_count--;
	else
	{
		flash_state ^= 1;
		flash_count = 5;
	}

	return flash_state;
}

void DrawGameInfo(long timed)
{
	long flash_state, seconds, btm;
	char buf[80];
	short ammo;

	if (!GLOBAL_playing_cutseq && !bDisableLaraControl && gfGameMode != 1)
	{
		flash_state = FlashIt();
		DrawHealthBar(flash_state);
		DrawAirBar(flash_state);
		DrawPickups(timed);

		if (DashTimer < 120)
			S_DrawDashBar(100 * DashTimer / 120);

		if (lara.target)
		{
			if (tomb5.enemy_bars && lara.target->hit_points > 0)
				S_DrawEnemyBar(100 * lara.target->hit_points / objects[lara.target->object_number].hit_points);
		}

		if (gfLevelFlags & GF_TIMER && savegame.Level.Timer && savegame.Level.Timer < 0x1A5E0)
		{
			seconds = savegame.Level.Timer / 30;
			sprintf(buf, "%.2d", seconds / 60);
			PrintString(40, 24, 0, buf, 0);
			PrintString(60, 24, 0, ":", 0);
			sprintf(buf, "%.2d", seconds % 60);
			PrintString(66, 24, 0, buf, 0);
			PrintString(86, 24, 0, ":", 0);
			sprintf(buf, "%.2d", (334 * (savegame.Level.Timer % 30)) / 100);
			PrintString(92, 24, 0, buf, 0);
		}

		if (tomb5.ammo_counter && lara.gun_status == LG_READY)
		{
			ammo = *get_current_ammo_pointer(lara.gun_type);

			if (ammo != -1)
			{

				if (lara.gun_type == WEAPON_SHOTGUN)
					ammo /= 6;

				sprintf(&buf[0], "%i", ammo);
				GetStringLength(buf, 0, &btm);
				PrintString(LaserSight ? phd_centerx + 30 : phd_winxmax - GetStringLength(buf, 0, 0) - 80, phd_winymax - btm - 70, 0, buf, 0);
			}
		}

		if (ammo_change_timer)
		{
			// #148, order 3 (sweep): the "ammo changed" caption stands for
			// ammo_change_timer DRAWN frames. Same class as R4 and R5, and
			// it was not in the plan's list.
			if (!InterpIsTweenFrame())
				ammo_change_timer--;

			PrintString(phd_winwidth >> 1, font_height, 5, ammo_change_buf, FF_CENTER);

			if (ammo_change_timer <= 0)
				ammo_change_timer = 0;
		}
	}
}

void DrawHealthBar(long flash_state)
{
	static long old_hitpoints;
	long hitpoints;

	hitpoints = lara_item->hit_points;

	if (hitpoints < 0)
		hitpoints = 0;
	else if (hitpoints > 1000)
		hitpoints = 1000;

	if (old_hitpoints != hitpoints)
	{
		old_hitpoints = hitpoints;
		health_bar_timer = 40;
	}

	if (health_bar_timer < 0)
		health_bar_timer = 0;

	if (hitpoints <= 250)
	{
		if (flash_state)
			S_DrawHealthBar(hitpoints / 10);
		else
			S_DrawHealthBar(0);
	}
	else if (health_bar_timer > 0 || lara.gun_status == LG_READY && lara.gun_type != WEAPON_TORCH || lara.poisoned >= 256)
			S_DrawHealthBar(hitpoints / 10);

	// #148, order 3 (sweep): PoisonFlag is set to 16 by the poison damage in
	// ControlPhase and counted down here, once per DRAWN frame. Nothing in
	// the build ever READS it - it is dead state - and this is guarded only
	// so that the number stays what it is at thirty for whoever reads it
	// next.
	if (PoisonFlag && !InterpIsTweenFrame())
		PoisonFlag--;
}

void DrawAirBar(long flash_state)
{
	long air;

	if (lara.air == 1800 || lara_item->hit_points <= 0)
		return;

	air = lara.air;

	if (air < 0)
		air = 0;
	else if (air > 1800)
		air = 1800;

	if (air > 450 || flash_state)
		S_DrawAirBar(100 * air / 1800);
	else
		S_DrawAirBar(0);

	if (lara.Gassed)
	{
		if (lara.dpoisoned < 2048)
			lara.dpoisoned += 2;

		lara.Gassed = 0;
	}
}

void InitialisePickUpDisplay()
{
	for (int i = 0; i < 8; i++)
		pickups[i].life = -1;

	PickupX = GetFixedScale(128);
	FullPickupX = PickupX;
	PickupVel = 0;
	CurrentPickup = 0;
}

void DrawPickups(long timed)
{
	DISPLAYPU* pu;
	long lp;

	/* R5 (#148, order 3): DESPITE THE NAME, THIS DRAWS NOTHING.
	 *
	 * It is the pickup display's whole animation - how long the icon stays
	 * (pu->life), how far it has slid in from the right (PickupX), how fast
	 * it is sliding (PickupVel) and which of the eight queued pickups is up.
	 * The drawing is S_DrawPickup in S_OutputPolyList, out of PickupX. So
	 * the in-between frame leaves this alone entirely and the icon slides in
	 * over the same fraction of a second at either rate. */
	if (InterpIsTweenFrame())
		return;

	pu = &pickups[CurrentPickup];

	if (pu->life > 0)
	{
		if (PickupX > 0)
			PickupX += -PickupX >> 3;
		else
			pu->life--;
	}
	else if (!pu->life)
	{
		if (PickupX < FullPickupX)
		{
			if (PickupVel < FullPickupX >> 3)
				PickupVel++;

			PickupX += PickupVel;
		}
		else
		{
			pu->life = -1;
			PickupVel = 0;
		}
	}
	else
	{
		for (lp = 0; lp < 8; lp++)
		{
			if (pickups[CurrentPickup].life > 0)
				break;

			CurrentPickup = (CurrentPickup + 1) & 7;
		}

		if (lp == 8)
			CurrentPickup = 0;
	}
}

void AddDisplayPickup(short object_number)
{
	DISPLAYPU* pu; 

	if (gfLevelId == LVL5_SUBMARINE && object_number == PUZZLE_ITEM1 ||
		gfLevelId == LVL5_OLD_MILL && object_number == PUZZLE_ITEM3)
		object_number = CROWBAR_ITEM;

	for (int i = 0; i < 8; i++)
	{
		pu = &pickups[i];

		if (pu->life < 0)
		{
			pu->life = 45;
			pu->object_number = object_number;
			break;
		}
	}

	DEL_picked_up_object(object_number);
}

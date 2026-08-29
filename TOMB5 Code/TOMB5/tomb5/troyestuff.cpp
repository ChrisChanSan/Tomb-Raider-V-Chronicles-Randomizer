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
#include "troyestuff.h"
#include "../game/text.h"
#include "../game/sound.h"
#include "../specific/3dmath.h"
#include "tomb5.h"
#include "../specific/input.h"

#define PAGE0_NUM	12
#define PAGE1_NUM	14
#define PAGE2_NUM	4

// How many pages are in the cycle. The third one belongs to the first person
// view and only joins when that view is switched ON - a page of settings for
// something that is off would turn up unasked while paging, which is exactly
// why the credits were taken out of this cycle again.
static long TroyePageCount()
{
	return tomb5.first_person ? 3 : 2;
}

static long TroyePageNum(long page)
{
	if (page == 0) return PAGE0_NUM;
	if (page == 1) return PAGE1_NUM;
	return PAGE2_NUM;
}

// The tomb5 options are two pages that page into each other. The credits are
// NOT one of them - they are their own screen, reached from the options menu
// (see CreditsScreen at the bottom of this file).
static long TroyePage = 0;

// Sets the start page and returns the matching start selection (for the
// pure display pages the paging row), so the caller can take it straight
// into its sel.
ulong TroyeMenuPage(long page)
{
	long num;

	TroyePage = page;
	num = TroyePageNum(page);
	return 1ul << num;
}

void TroyeMenu(long textY, long& menu, ulong& selection, ulong selection_bak)
{
	long num;
	bool changed;

	num = 0;
	changed = 0;

	if (dbinput & IN_DESELECT)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		menu = 0;
		dbinput &= ~IN_DESELECT;
		selection = selection_bak;
		TroyePage = 0;
		return;
	}

	// The third page can vanish under one's feet: switching the first person
	// view off on page 2 takes it out of the cycle, and standing on it then
	// would leave the menu drawing a page that is no longer there.
	if (TroyePage >= TroyePageCount())
	{
		TroyePage = 0;
		selection = 1;
	}

	switch (TroyePage)
	{
	case 0:
		changed = Page0(num, textY, selection);
		break;

	case 1:
		changed = Page1(num, textY, selection);
		break;

	case 2:
		changed = Page2(num, textY, selection);
		break;
	}
	
	PrintString(phd_centerx - (phd_centerx >> 3), textY + (num + 2) * font_height, selection & (1 << num) ? 1 : 6, "\x19", 0);
	PrintString(phd_centerx + (phd_centerx >> 3), textY + (num + 2) * font_height, selection & (1 << num) ? 1 : 6, "\x1B", 0);

	if (dbinput & IN_FORWARD)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		selection >>= 1;
	}

	if (dbinput & IN_BACK)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		selection <<= 1;
	}

	if (!selection)
		selection = 1;

	if (selection > ulong(1 << num))
		selection = 1 << num;

	if (selection & (1 << num))
	{
		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);

			// A circle: options -> options 2 -> (first person, only when it
			// is on) -> back to the start. The credits used to sit in this
			// cycle too and turned up unasked while paging through the
			// settings, which is why a page only joins when it has
			// something to say.
			TroyePage = (TroyePage + 1) % TroyePageCount();
			num = TroyePageNum(TroyePage);

			selection = 1 << num;
		}
	}

	if (changed)
		save_new_tomb5_settings();
}

bool Page0(long& num, long textY, ulong selection)
{
	long y, i;
	char buffer[80];
	bool changed;

	changed = 0;
	num = PAGE0_NUM;
	PrintString(phd_centerx, font_height, 6, "New tomb5 options", FF_CENTER);

	y = 2;
	i = 0;

	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Footprints", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Shadow mode", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Fix climb up delay", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Flexible crawling", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Cutscene skipper", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Cheats", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Bar mode", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Bar positions", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Enemy bars", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Ammo counter", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Gameover menu", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Fog", 0);

	y = 2;
	i = 0;

	strcpy(buffer, tomb5.footprints ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.shadow_mode == 1 ? "Original" : tomb5.shadow_mode == 2 ? "Circle" : tomb5.shadow_mode == 3 ? "Faded circle" : "PSX");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.fix_climb_up_delay ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.flexible_crawling ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.cutseq_skipper ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.enable_cheats ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.bar_mode == 1 ? "Original" : tomb5.bar_mode == 2 ? "TR4" : "PSX");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.bars_pos == 1 ? "Original" : tomb5.bars_pos == 2 ? "Improved" : "PSX");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.enemy_bars ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.ammo_counter ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.gameover ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.fog ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	switch (selection)
	{
	case 1 << 0:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.footprints = !tomb5.footprints;
			changed = 1;
		}

		break;

	case 1 << 1:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.shadow_mode++;

			if (tomb5.shadow_mode > 4)
				tomb5.shadow_mode = 1;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.shadow_mode--;

			if (tomb5.shadow_mode < 1)
				tomb5.shadow_mode = 4;

			changed = 1;
		}

		break;

	case 1 << 2:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.fix_climb_up_delay = !tomb5.fix_climb_up_delay;
			changed = 1;
		}

		break;

	case 1 << 3:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.flexible_crawling = !tomb5.flexible_crawling;
			changed = 1;
		}

		break;

	case 1 << 4:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.cutseq_skipper = !tomb5.cutseq_skipper;
			changed = 1;
		}

		break;

	case 1 << 5:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.enable_cheats = !tomb5.enable_cheats;
			changed = 1;
		}

		break;

	case 1 << 6:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.bar_mode++;

			if (tomb5.bar_mode > 3)
				tomb5.bar_mode = 1;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.bar_mode--;

			if (tomb5.bar_mode < 1)
				tomb5.bar_mode = 3;

			changed = 1;
		}

		break;

	case 1 << 7:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.bars_pos++;

			if (tomb5.bars_pos > 3)
				tomb5.bars_pos = 1;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.bars_pos--;

			if (tomb5.bars_pos < 1)
				tomb5.bars_pos = 3;

			changed = 1;
		}

		break;

	case 1 << 8:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.enemy_bars = !tomb5.enemy_bars;
			changed = 1;
		}

		break;

	case 1 << 9:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.ammo_counter = !tomb5.ammo_counter;
			changed = 1;
		}

		break;

	case 1 << 10:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.gameover = !tomb5.gameover;
			changed = 1;
		}

		break;

	case 1 << 11:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.fog = !tomb5.fog;
			changed = 1;
		}

		break;
	}

	return changed;
}

bool Page1(long& num, long textY, ulong selection)
{
	long y, i;
	char buffer[80];
	bool changed;

	changed = 0;
	num = PAGE1_NUM;
	PrintString(phd_centerx, font_height, 6, "New tomb5 options", FF_CENTER);

	y = 2;
	i = 0;

	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Crawl Tilting", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "PSX skies", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "TR4 loadscreens", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Loadbar style", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Mono screen style", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Loading text", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Shimmer", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Distance fog", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Ammotype hotkeys", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Look transparency", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Static lighting", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "UW Effect", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "First person view", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Level select", 0);

	y = 2;
	i = 0;

	strcpy(buffer, tomb5.crawltilt ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.PSX_skies ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.tr4_loadscreens ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.tr4_loadbar ? "TR4" : "TR5");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.inv_bg_mode == 1 ? "Original" : tomb5.inv_bg_mode == 2 ? "TR4" : "Clear");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.loadingtxt ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.shimmer ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	sprintf(buffer, "%i", tomb5.distance_fog);
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.ammotype_hotkeys ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.look_transparency ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.static_lighting ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.uw_dust == 1 ? "off" : tomb5.uw_dust == 2 ? "Original" : "TR4");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.first_person ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.level_select ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	switch (selection)
	{
	case 1 << 0:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.crawltilt = !tomb5.crawltilt;
			changed = 1;
		}

		break;

	case 1 << 1:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.PSX_skies = !tomb5.PSX_skies;
			changed = 1;
		}

		break;

	case 1 << 2:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.tr4_loadscreens = !tomb5.tr4_loadscreens;
			changed = 1;
		}

		break;

	case 1 << 3:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.tr4_loadbar = !tomb5.tr4_loadbar;
			changed = 1;
		}

		break;

	case 1 << 4:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.inv_bg_mode++;

			if (tomb5.inv_bg_mode > 3)
				tomb5.inv_bg_mode = 1;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.inv_bg_mode--;

			if (tomb5.inv_bg_mode < 1)
				tomb5.inv_bg_mode = 3;

			changed = 1;
		}

		break;

	case 1 << 5:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.loadingtxt = !tomb5.loadingtxt;
			changed = 1;
		}

		break;

	case 1 << 6:

		if (dbinput & IN_RIGHT || dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.shimmer = !tomb5.shimmer;
			changed = 1;
		}

		break;

	case 1 << 7:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.distance_fog++;

			if (tomb5.distance_fog > 30)
				tomb5.distance_fog = 30;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.distance_fog--;

			if (tomb5.distance_fog < 3)
				tomb5.distance_fog = 3;

			changed = 1;
		}

		break;

	case 1 << 8:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.ammotype_hotkeys = !tomb5.ammotype_hotkeys;
			changed = 1;
		}

		break;

	case 1 << 9:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.look_transparency = !tomb5.look_transparency;
			changed = 1;
		}

		break;

	case 1 << 10:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.static_lighting = !tomb5.static_lighting;
			changed = 1;
		}

		break;

	case 1 << 11:

		if (dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.uw_dust++;

			if (tomb5.uw_dust > 3)
				tomb5.uw_dust = 1;

			changed = 1;
		}

		if (dbinput & IN_LEFT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.uw_dust--;

			if (tomb5.uw_dust < 1)
				tomb5.uw_dust = 3;

			changed = 1;
		}

		break;

	case 1 << 12:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person = !tomb5.first_person;
			changed = 1;
		}

		break;

	case 1 << 13:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.level_select = !tomb5.level_select;
			changed = 1;
		}

		break;
	}

	return changed;
}

// Third page (#134): everything about the first person view. One row for
// now - the field of view - and it is here rather than squeezed onto page 2
// because that page is full at fourteen rows.
//
// Steps of ten from 60 to 100 rather than a free slider: the difference
// between neighbouring values is already plain to see, and a stepped choice
// says what it is doing. LEFT and RIGHT walk the range and stop at its ends,
// the way the sound quality row behaves.
bool Page2(long& num, long textY, ulong selection)
{
	long y, i;
	char buffer[80];
	bool changed;

	changed = 0;
	num = PAGE2_NUM;
	PrintString(phd_centerx, font_height, 6, "First person view", FF_CENTER);

	y = 2;
	i = 0;

	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Field of view", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Death camera", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Follow head bone", 0);
	PrintString(phd_centerx >> 2, textY + y++ * font_height, selection & (1 << i++) ? 1 : 2, "Camera damping", 0);

	y = 2;
	i = 0;

	sprintf(buffer, "%d", (int)tomb5.first_person_fov);
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.first_person_death ? "First person" : "Third person");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.first_person_head ? "on" : "off");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	strcpy(buffer, tomb5.first_person_smooth == 0 ? "off" :
	               tomb5.first_person_smooth == 1 ? "Light" :
	               tomb5.first_person_smooth == 2 ? "Medium" : "Strong");
	PrintString(phd_centerx + (phd_centerx >> 2), textY + y++ * font_height, selection & (1 << i++) ? 1 : 6, buffer, 0);

	switch (selection)
	{
	case 1 << 0:

		if (dbinput & IN_LEFT && tomb5.first_person_fov > 60)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_fov -= 10;
			changed = 1;
		}

		if (dbinput & IN_RIGHT && tomb5.first_person_fov < 100)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_fov += 10;
			changed = 1;
		}

		break;

	case 1 << 1:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_death = !tomb5.first_person_death;
			changed = 1;
		}

		break;

	case 1 << 2:

		if (dbinput & IN_LEFT || dbinput & IN_RIGHT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_head = !tomb5.first_person_head;
			changed = 1;
		}

		break;

	case 1 << 3:

		if (dbinput & IN_LEFT && tomb5.first_person_smooth > 0)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_smooth--;
			changed = 1;
		}

		if (dbinput & IN_RIGHT && tomb5.first_person_smooth < 3)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			tomb5.first_person_smooth++;
			changed = 1;
		}

		break;
	}

	return changed;
}

// Credits (#33). GPLv3 courtesy and user requirement: the names without
// which this project would not exist belong IN THE GAME, not only in the
// README. Text only - no URLs, the game font does not carry every glyph
// (PrintString guard); the links stand in the README.
//
// A SCREEN OF ITS OWN, not a page of the tomb5 options. It was one at
// first, and that was wrong twice over: paging left/right through the
// settings ran into the credits unasked, and TroyeMenu draws its paging
// arrows at a fixed row below the last option - on a page of pure text
// they landed ACROSS a line of names ("Dear ImGui by Omar Cornut" wore
// two orange arrows). Nothing here is selectable and nothing pages, so
// the only input this screen takes is "back".
void CreditsScreen(long textY, long& menu, ulong& selection, ulong selection_bak)
{
	long y;

	PrintString(phd_centerx, font_height, 6, "Credits", FF_CENTER);

	// GROUPS ARE SEPARATED BY COLOUR, NOT BY BLANK LINES - and that is a
	// space decision, not a taste one. The screen holds fifteen rows
	// (y = 2..16, font_height apart, the last one ending just above the
	// bottom edge). Fourteen are names; four blank separators would eat
	// the entire remaining budget and push the tool credits off screen.
	// Headers therefore use the same gold as the title (colour 6) and the
	// names stay white (colour 2).
	y = 2;

	PrintString(phd_centerx, textY + y++ * font_height, 6, "TOMB5 Engine", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Troye - ChocolateFan", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "thatguyfrombb - Raildex", FF_CENTER);

	// The TR3 enemies (T-Rex, raptor, compy, vulture) are GPLv3 code taken
	// from Troye's tomb3 decompilation, not our own work - it belongs in the
	// game's credits, not only in a source comment.
	PrintString(phd_centerx, textY + y++ * font_height, 6, "TR3 Enemies from tomb3", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Troye and contributors", FF_CENTER);

	PrintString(phd_centerx, textY + y++ * font_height, 6, "Randomizer and Windows 11 Port", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "ChrisChanSan", FF_CENTER);

	// Testing, findings and ideas - the people who played the seeds and
	// reported what broke.
	PrintString(phd_centerx, textY + y++ * font_height, 6, "Contributors", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "XeNoN3560 - Lucifer230 - Todesengel1931", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Crockett - Grylle - Claude AI", FF_CENTER);

	PrintString(phd_centerx, textY + y++ * font_height, 6, "Tools and Libraries", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "trview by chreden", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Dear ImGui by Omar Cornut", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "stb by Sean Barrett", FF_CENTER);

	// Back to the options menu, with the row the player came from restored.
	// Confirm counts as back as well - there is nothing here to confirm,
	// and a screen that only reacts to one of the two keys feels stuck.
	if (dbinput & (IN_DESELECT | IN_SELECT))
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		menu = 0;
		selection = selection_bak;
		dbinput &= ~(IN_DESELECT | IN_SELECT);
	}
}

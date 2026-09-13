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
#include "../../tomb5/pch.h"
#include "hd_outfitmenu.h"
#include "hd_wardrobe.h"
#include "hd_lara.h"
#include "hd_laradraw.h"
#include "hd_level.h"
#include "hd_feet.h"
#include "hd_mods.h"
#include "../LoadSave.h"
#include "../input.h"
#include "../output.h"
#include "../3dmath.h"			// phd_centerx - the menu's own column
#include "../registry.h"
#include "../time.h"
#include "../gamemain.h"
#include "../dxshell.h"
#include "../function_stubs.h"
#include "../../game/text.h"
#include "../../game/sound.h"
#include "../../game/control.h"
#include "../../game/newinv2.h"
#include "../../game/gameflow.h"
#include "../../tomb5/tomb5.h"

namespace
{
	// The scrolling window. Nine rows plus the title, the footsteps row and
	// the hint line is what fits between font_height*3 and the bottom of a
	// 4:3 screen at the default font - the same room the options page uses.
	const int kWindow = 9;

	// The registry: the outfit is one REG_SZ under the game's own settings
	// key, and the footsteps are one REG_DWORD per wardrobe key under a
	// subkey of it. A wardrobe key holds slashes and spaces and is therefore
	// a VALUE name and never a subkey name - a subkey may hold no backslash
	// and a value name may hold almost anything.
	const char* kKeyRoot = "tomb5";
	const char* kKeyOutfit = "hd_outfit";
	const char* kKeyFeet = "tomb5\\hd_footsteps";

	// What one of those REG_DWORDs means. The numbers are HD_WARDROBE_FEET_*
	// so that a value written by hand reads the same way the wardrobe does.
	const ulong kFeetBare = HD_WARDROBE_FEET_BARE;
	const ulong kFeetBoots = HD_WARDROBE_FEET_BOOTS;

	bool g_saidNothingToDo;		// the one line an unarmed F4 gets

	/* WHAT THE MENU SHOWS, one row per line: the first is "Level facts" and
	 * the rest are the wardrobe scan in its own order. Held as an index
	 * rather than as a key, so that nothing here keeps a pointer into a scan
	 * that could be redone under it. */
	int row_count()
	{
		return 1 + HDWardrobeCount();
	}

	const HDWardrobeEntry* entry_of(int row)
	{
		return row > 0 ? HDWardrobeAt(row - 1) : 0;
	}

	const char* key_of(int row)
	{
		const HDWardrobeEntry* e = entry_of(row);

		return e ? e->key : "";
	}

	// The source tag beside the name: which of the three the body came from.
	const char* source_of(const HDWardrobeEntry* e)
	{
		if (!e)
			return "";

		if (e->source == HD_WARDROBE_SOURCE_46)
			return "IV-VI";

		if (e->source == HD_WARDROBE_SOURCE_13)
			return "I-III";

		return HDModsName(e->mod);
	}

	// Which row the registry's key is on, or 0 - "Level facts" - when there
	// is none or this machine's scan has not got it.
	int row_of_key(const char* key)
	{
		if (!key || !*key)
			return 0;

		const int found = HDWardrobeFind(key);

		return found >= 0 ? found + 1 : 0;
	}

	void write_outfit(const char* key)
	{
		/* OpenRegistry CREATES the key, and that is safe HERE and nowhere
		 * earlier: init_tomb5_stuff asks REG_KeyWasCreated() once at
		 * startup (tomb5\tomb5.cpp) and has long since run by the time a
		 * level is on the screen. The READER may not do this, which is why
		 * it is REG_PeekString - the note is in specific\registry.h. */
		if (!OpenRegistry(kKeyRoot))
			return;

		REG_WriteString((char*)kKeyOutfit, key ? key : "");
		CloseRegistry();
	}

	void write_feet(const char* key, bool bare)
	{
		if (!key || !*key || !OpenRegistry(kKeyFeet))
			return;

		REG_WriteLong((char*)key, bare ? kFeetBare : kFeetBoots);
		CloseRegistry();
	}
}

int HDOutfitMenuFeetFor(const char* key)
{
	ulong value = 0;

	if (!key || !*key)
		return -1;

	if (!REG_PeekLong(kKeyFeet, key, value))
		return -1;

	if (value != kFeetBare && value != kFeetBoots)
		return -1;

	return (int)value;
}

bool HDOutfitMenuFeetWanted(const char* key, int wardrobeFeet, const char** why)
{
	const int said = HDOutfitMenuFeetFor(key);

	if (said >= 0)
	{
		if (why)
			*why = "the player's own choice for this outfit";

		return said == (int)kFeetBare;
	}

	if (wardrobeFeet == HD_WARDROBE_FEET_BARE)
	{
		if (why)
			*why = "the wardrobe says this body is barefoot";

		return true;
	}

	if (why)
		*why = wardrobeFeet == HD_WARDROBE_FEET_BOOTS
			? "the wardrobe says this body wears boots"
			: "nobody has said, so boots";

	return false;
}

void HDOutfitMenuFollowFeet()
{
	const int outfit = HDLaraDrawOutfit();
	const char* key = HDLaraOutfitKey(outfit);
	const int found = HDWardrobeFind(key);
	const HDWardrobeEntry* e = found >= 0 ? HDWardrobeAt(found) : 0;
	const char* why = "";
	const bool want = HDOutfitMenuFeetWanted(key,
		e ? e->feet : HD_WARDROBE_FEET_UNKNOWN, &why);

	if (!HDFeetAvailable())
	{
		Log("SFXLOG footsteps: %s would walk %s (%s), but this level has "
			"no second set - %s", *key ? key : "the level's own body",
			want ? "barefoot" : "in boots", why, HDFeetWhyNot());
		return;
	}

	const bool now = HDFeetBare(want);

	Log("SFXLOG footsteps: %s walks %s - %s (%d recording%s, %s)",
		*key ? key : "the level's own body", now ? "barefoot" : "in boots",
		why, HDFeetBareTakes(), HDFeetBareTakes() == 1 ? "" : "s",
		HDFeetBareSource());
}

bool HDOutfitMenuWanted()
{
	static bool held;

	/* ONE PRESS, ONE MENU. The key is read on its own edge here rather than
	 * through the input bits, because F4 has no gameplay meaning and no
	 * binding: giving it one would mean a new IN_ bit for the sake of a
	 * keyboard key the pad cannot reach anyway. The menu's own inputs are
	 * dbinput's, which is what makes the pad work INSIDE it. */
	if (!keymap[DIK_F4])
	{
		held = false;
		return false;
	}

	if (held)
		return false;

	held = true;

	/* WITH THE HD PATH OFF THERE IS NOTHING TO CHANGE, and the player is
	 * told once per session rather than left pressing a dead key. Everything
	 * the menu can do - the wardrobe, the re-dress, the barefoot set - lives
	 * inside that gate. */
	if (!HDEnabled())
	{
		if (!g_saidNothingToDo)
		{
			g_saidNothingToDo = true;
			Log("HDLOG f4 pressed and there is nothing to change: %s",
				tomb5.hd_enable ? "no Tomb Raider IV-VI installation was found"
					: "HD textures are off in the options");
		}

		return false;
	}

	return true;
}

void HDOutfitMenu()
{
	char chosen[HD_WARDROBE_KEY];
	char buffer[HD_WARDROBE_NAME + 32];
	int selection;
	int top = 0;
	bool bare;
	bool feetTouched = false;
	bool done = false;
	bool cancelled = false;

	HDWardrobeScan();

	chosen[0] = 0;
	REG_PeekString(kKeyRoot, kKeyOutfit, chosen, sizeof(chosen));

	const int was = row_of_key(chosen);

	selection = was;
	bare = HDOutfitMenuFeetWanted(key_of(selection),
		entry_of(selection) ? entry_of(selection)->feet : HD_WARDROBE_FEET_UNKNOWN, 0);

	Log("HDLOG f4 opened: %d rows, wearing %s, in the level %s",
		row_count(), *chosen ? chosen : "what the level says",
		HDLaraOutfitLabel(HDLaraDrawOutfit()));

	CreateMonoScreen();
	InventoryActive = 1;

	do
	{
		S_InitialisePolyList();
		SetDebounce = 1;
		S_UpdateInput();
		S_DisplayMonoScreen();

		/* THE SAME TWO COLUMNS AND THE SAME STEP the options page draws in
		 * (specific\LoadSave.cpp): the label at a quarter of the screen, the
		 * value three quarters across, one font_height per row. A literal
		 * with no string id goes through CustomText, exactly as the
		 * "Language" row does. */
		const int f = 3 * font_height;
		int y = f;

		PrintString(phd_centerx, font_height, 6,
			(char*)CustomText("Outfit"), FF_CENTER);

		if (selection < top)
			top = selection;

		if (selection >= top + kWindow)
			top = selection - kWindow + 1;

		if (top > row_count() - kWindow)
			top = row_count() - kWindow;

		if (top < 0)
			top = 0;

		for (int i = 0; i < kWindow && top + i < row_count(); i++)
		{
			const int row = top + i;
			const HDWardrobeEntry* e = entry_of(row);
			const uchar colour = row == selection ? 1 : 2;

			y = f + i * font_height;

			if (!e)
			{
				/* THE FIRST ROW IS NO OUTFIT AT ALL. It is what clears the
				 * registry value, and with it cleared every level dresses
				 * her out of its own facts and the randomizer's - which is
				 * what a player who has never opened this menu has. */
				PrintString(phd_centerx >> 2, y, colour,
					(char*)CustomText("Level facts"), 0);
				PrintString(phd_centerx + (phd_centerx >> 2), y,
					row == selection ? 1 : 6,
					(char*)CustomText("the level and the run decide"), 0);
			}
			else
			{
				snprintf(buffer, sizeof(buffer), "%s%s", e->name,
					row == was ? " *" : "");
				PrintString(phd_centerx >> 2, y, colour, buffer, 0);
				PrintString(phd_centerx + (phd_centerx >> 2), y,
					row == selection ? 1 : 6, (char*)source_of(e), 0);
			}
		}

		/* THE FOOTSTEPS ROW belongs to the HIGHLIGHTED outfit and not to the
		 * menu, so it stands under the list and says whose it is. Left and
		 * right change it, which is the same gesture every value row in this
		 * game answers to. */
		y = f + kWindow * font_height + (font_height >> 1);

		PrintString(phd_centerx >> 2, y, 6, (char*)CustomText("Footsteps"), 0);

		if (!HDFeetAvailable())
			PrintString(phd_centerx + (phd_centerx >> 2), y, 3,
				(char*)CustomText("unavailable in this level"), 0);
		else
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText(bare ? "Bare" : "Boots"), 0);

		/* AND WHAT PRESSING SELECT WILL DO, because the answer is not always
		 * "now": the page cache never gives a page back, so a level in which
		 * the player has already changed his mind a great many times says so
		 * instead of growing without end. */
		y += font_height;

		if (!HDLevelRedressAllowed())
			PrintString(phd_centerx, y, 3,
				(char*)CustomText("applies at the next level"), FF_CENTER);

		if (dbinput & IN_FORWARD)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);

			if (selection > 0)
				selection--;

			bare = HDOutfitMenuFeetWanted(key_of(selection),
				entry_of(selection) ? entry_of(selection)->feet
					: HD_WARDROBE_FEET_UNKNOWN, 0);
		}

		if (dbinput & IN_BACK)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);

			if (selection < row_count() - 1)
				selection++;

			bare = HDOutfitMenuFeetWanted(key_of(selection),
				entry_of(selection) ? entry_of(selection)->feet
					: HD_WARDROBE_FEET_UNKNOWN, 0);
		}

		if (dbinput & (IN_LEFT | IN_RIGHT))
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			bare = !bare;
			feetTouched = true;
		}

		if (dbinput & IN_SELECT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_DEFAULT);
			done = true;
		}

		if (dbinput & (IN_DESELECT | IN_PAUSE))
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			done = true;
			cancelled = true;
		}

		UpdatePulseColour();
		S_OutputPolyList();
		S_DumpScreen();

	} while (!MainThread.ended && !done);

	TIME_Init();
	FreeMonoScreen();
	InventoryActive = 0;

	if (cancelled)
	{
		Log("HDLOG f4 closed without applying");
		return;
	}

	/* THE CHOICE IS WRITTEN BEFORE IT IS WORN, so that a level build which
	 * happens for any reason afterwards reads the same answer this one did.
	 * "Level facts" writes an EMPTY value rather than deleting the name: the
	 * reader treats empty and absent alike (REG_PeekString returns false for
	 * both) and an empty value is one a player can see in regedit. */
	const char* key = key_of(selection);

	write_outfit(key);

	if (feetTouched && *key)
		write_feet(key, bare);

	const bool feetNow = HDFeetBare(bare);

	Log("HDLOG f4 outfit=%s footsteps=%s%s", *key ? key : "(level facts)",
		HDFeetAvailable() ? (feetNow ? "bare" : "boots") : "unavailable",
		feetTouched ? " (the player's own choice, remembered)" : "");

	/* AND WORN AT ONCE (#153, round M3). HDLevelRedress is the level's own
	 * model build run again on the main thread with the game stopped; it says
	 * in the log what it cost and whether it ran at all. */
	if (was != selection)
		HDLevelRedress("the F4 outfit changer");
}

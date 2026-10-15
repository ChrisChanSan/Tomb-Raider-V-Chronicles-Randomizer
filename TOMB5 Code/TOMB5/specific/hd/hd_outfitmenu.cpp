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
#include "hd_switch.h"			// HDDrawn - the menu only over the HD draw
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
	/* The scrolling window: how many outfits one page of the F4 list holds.
	 * The page also carries a title, footsteps, head, shine and skin glow
	 * rows and a hint line. font_height is GetFixedScale(28) against a
	 * 640x480 reference (game\text.cpp), a ratio held at every resolution
	 * 4:3 or wider, so a 480-tall screen holds 480 / 28 = 17.1 lines of it:
	 *
	 *     title        1.0 * font_height
	 *     the window   3.0 .. 3.0 + (kWindow - 1) = 10.0
	 *     footsteps   11.5   (3 + kWindow + a half line of air)
	 *     head        12.5
	 *     shine       13.5
	 *     skin glow   14.5
	 *     hint        15.5, so the page ends at 16.5
	 *
	 * which leaves two lines of room. A third value row would not fit; the
	 * next one added has to come out of kWindow instead: 7 rows, 12.5 lines
	 * of page. */
	const int kWindow = 8;

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

	/* What the menu shows, one row per line: the first is "Level facts" and
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

	/* One press, one menu: the key is read on its own edge here rather than
	 * through the input bits, because F4 has no gameplay meaning and no
	 * binding - giving it one would mean a new IN_ bit for the sake of a
	 * keyboard key the pad cannot reach anyway. The menu's own inputs are
	 * dbinput's, which is what makes the pad work inside it. */
	if (!keymap[DIK_F4])
	{
		held = false;
		return false;
	}

	if (held)
		return false;

	held = true;

	/* With the HD path off there is nothing to change, and the player is
	 * told once per session rather than left pressing a dead key. Everything
	 * the menu can do - the wardrobe, the re-dress, the barefoot set - lives
	 * inside that gate. With the world drawn classic (HDDrawn, the HD
	 * textures row) Lara is the classic Lara, so the menu does not open
	 * either; its data stay built for the HD draw. */
	if (!HDEnabled() || !HDDrawn())
	{
		if (!g_saidNothingToDo)
		{
			g_saidNothingToDo = true;
			Log("HDLOG f4 pressed and there is nothing to change: %s",
				!tomb5.hd_build ? "hd_build is 0" : !HDEnabled() ?
				"no Tomb Raider IV-VI installation was found" :
				"HD textures are off - the world is drawn classic");
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
	bool feetWritten = false;
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

	/* How much room there is for another change is worked out once, here:
	 * the row below the list asks HDLevelRedressAllowed on every drawn frame
	 * this window is up, and that answer stands on a walk of the address
	 * space, so it is measured as the window opens and the frames read what
	 * it left. */
	HDLevelRedressRefresh("the F4 window opened");

	CreateMonoScreen();
	InventoryActive = 1;

	do
	{
		S_InitialisePolyList();
		SetDebounce = 1;
		S_UpdateInput();
		S_DisplayMonoScreen();

		/* The same two columns and the same step the options page draws in
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
				/* The first row is no outfit at all - it is what clears the
				 * registry value, and with it cleared every level dresses
				 * her out of its own facts and the randomizer's, which is
				 * what a player who has never opened this menu has.
				 *
				 * It carries the mark like any other row: it is the row in
				 * force for everyone who has never opened this menu, which
				 * is the commonest state there is, so marking wardrobe rows
				 * alone would leave the commonest case with nothing marked
				 * at all. */
				snprintf(buffer, sizeof(buffer), "%s%s",
					CustomText("Level facts"), row == was ? " *" : "");
				PrintString(phd_centerx >> 2, y, colour, buffer, 0);
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

		/* The footsteps row belongs to the highlighted outfit and not to the
		 * menu, so it stands under the list and says whose it is. Left and
		 * right change it, the same gesture every value row in this game
		 * answers to.
		 *
		 * On the first row there is nothing to change: "Level facts" is the
		 * absence of a choice, so there is no key to hang one on. The row
		 * says what will decide instead. */
		y = f + kWindow * font_height + (font_height >> 1);

		PrintString(phd_centerx >> 2, y, 6, (char*)CustomText("Footsteps"), 0);

		if (!entry_of(selection))
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText("as the outfit says"), 0);
		else if (!HDFeetAvailable())
			PrintString(phd_centerx + (phd_centerx >> 2), y, 3,
				(char*)CustomText(bare ? "Bare - no second set in this level"
					: "Boots - no second set in this level"), 0);
		else
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText(bare ? "Bare" : "Boots"), 0);

		/* Whose head the highlighted body wears.
		 *
		 * The footsteps row's neighbour and its opposite: that one is a
		 * choice and left and right change it, this one is a fact and
		 * nothing in this menu can change it - it is decided by the bundle
		 * the body was drawn for, or by the mod's own outfit.txt. It stands
		 * here because the question it answers is asked of the picture on
		 * the screen: an outfit of Tomb Raider I-III carries headgear on its
		 * head node and no hair, so it wears TR5's head instead, and a
		 * player who does not know that sees a body change more than he
		 * chose. The clause after it says who decided, which is also how a
		 * mod without a mod.txt is told from one with an origin= line. */
		y += font_height;

		PrintString(phd_centerx >> 2, y, 6, (char*)CustomText("Head"), 0);

		if (!entry_of(selection))
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText("as the outfit says"), 0);
		else
		{
			const HDWardrobeEntry* e = entry_of(selection);

			snprintf(buffer, sizeof(buffer), "%s (%s)",
				HDWardrobeHeadPhrase(e->head), HDWardrobeFromWord(e->fromHead));
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6, buffer, 0);
		}

		/* What the body is made of: two more facts of the highlighted body
		 * and its neighbours' opposite in the same way the head row is -
		 * nothing in this menu changes them. They stand here because they
		 * are the two the player can change himself, in a list file, and a
		 * value he has written is worth nothing if the game will not say
		 * whether it arrived, so the clause after each says which tier
		 * answered, and "the list" is the one that means his own line was
		 * read. Shine is the clothes, glow is her skin.
		 *
		 * The phrase form, HDWardrobeFromWord, is used rather than the
		 * one-word tag, the same the head row above uses, so that one tier
		 * does not read as two different things across the three rows of
		 * one window. The tag belongs where a column or a log field is
		 * counted in characters. */
		y += font_height;

		PrintString(phd_centerx >> 2, y, 6, (char*)CustomText("Shine"), 0);

		if (!entry_of(selection))
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText("as the outfit says"), 0);
		else
		{
			const HDWardrobeEntry* e = entry_of(selection);

			snprintf(buffer, sizeof(buffer), "%.2f/%.0f (%s)", e->sheen,
				e->sheenPower, HDWardrobeFromWord(e->fromSheen));
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6, buffer, 0);
		}

		y += font_height;

		PrintString(phd_centerx >> 2, y, 6, (char*)CustomText("Skin glow"), 0);

		if (!entry_of(selection))
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6,
				(char*)CustomText("as the outfit says"), 0);
		else
		{
			const HDWardrobeEntry* e = entry_of(selection);

			snprintf(buffer, sizeof(buffer), "%.2f/%.0f (%s)", e->skinSheen,
				e->skinSheenPower, HDWardrobeFromWord(e->fromSkinSheen));
			PrintString(phd_centerx + (phd_centerx >> 2), y, 6, buffer, 0);
		}

		/* What pressing select will do is not always "now": the page cache
		 * never gives a page back, so a level in which the player has
		 * already changed his mind a great many times says so instead of
		 * growing without end. */
		y += font_height;

		if (!HDLevelRedressAllowed())
			PrintString(phd_centerx, y, 3,
				(char*)CustomText("applies at the next level"), FF_CENTER);

		if (dbinput & IN_FORWARD)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);

			/* A flip belongs to the row it was made on: it is written when
			 * that row is left, not when some other row is chosen. */
			if (feetTouched && *key_of(selection))
			{
				write_feet(key_of(selection), bare);
				feetWritten = true;
			}

			feetTouched = false;

			if (selection > 0)
				selection--;

			bare = HDOutfitMenuFeetWanted(key_of(selection),
				entry_of(selection) ? entry_of(selection)->feet
					: HD_WARDROBE_FEET_UNKNOWN, 0);
		}

		if (dbinput & IN_BACK)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);

			if (feetTouched && *key_of(selection))
			{
				write_feet(key_of(selection), bare);
				feetWritten = true;
			}

			feetTouched = false;

			if (selection < row_count() - 1)
				selection++;

			bare = HDOutfitMenuFeetWanted(key_of(selection),
				entry_of(selection) ? entry_of(selection)->feet
					: HD_WARDROBE_FEET_UNKNOWN, 0);
		}

		if ((dbinput & (IN_LEFT | IN_RIGHT)) && entry_of(selection))
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			bare = !bare;
			feetTouched = true;
		}

		/* Not on Alt+Enter, the fullscreen toggle - the pause menu's own
		 * guard (LoadSave.cpp) - so that toggling the display inside the
		 * menu does not also choose a row. */
		if ((dbinput & IN_SELECT) && !keymap[DIK_LALT])
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

	FreeMonoScreen();
	InventoryActive = 0;

	/* The window closing counts as cancel: the loop above ends on
	 * MainThread.ended too, and falling through from there would write the
	 * highlighted row as a choice and run a re-dress during shutdown. */
	if (cancelled || MainThread.ended)
	{
		TIME_Init();
		Log("HDLOG f4 closed without applying%s",
			MainThread.ended ? " - the window is closing" : "");
		return;
	}

	/* The choice is written before it is worn, so that a level build which
	 * happens for any reason afterwards reads the same answer this one did.
	 * "Level facts" writes an empty value rather than deleting the name: the
	 * reader treats empty and absent alike (REG_PeekString returns false for
	 * both), and an empty value is one a player can see in regedit. */
	const char* key = key_of(selection);
	const bool changed = was != selection;

	write_outfit(key);

	if (feetTouched && *key)
	{
		write_feet(key, bare);
		feetWritten = true;
	}

	/* And worn at once: HDLevelRedress is the level's own model build run
	 * again on the main thread with the game stopped; it says in the log
	 * what it cost and whether it ran at all, and it ends by asking
	 * HDOutfitMenuFollowFeet which of the two sets the new body walks on -
	 * so the footsteps are not applied here when a body change is.
	 *
	 * They wait with it when it defers: a deferred change leaves the old
	 * body on the screen; giving it the new one's footsteps would be one
	 * half of a change without the other, and the menu had already told the
	 * player the body waits. Both halves are written to the registry either
	 * way, so the next level wears both. */
	const bool worn = !changed || HDLevelRedress("the F4 outfit changer");

	/* A footstep change on its own needs no re-dress: the body it belongs to
	 * is already on her and this is two shorts in the level's sound map. */
	if (!changed && HDFeetAvailable() && bare != HDFeetIsBare())
		HDFeetBare(bare);

	Log("HDLOG f4 outfit=%s (%s) footsteps=%s%s", *key ? key : "(level facts)",
		!changed ? "unchanged"
			: worn ? "worn now" : "waiting for the next level",
		HDFeetAvailable() ? (HDFeetIsBare() ? "bare" : "boots") : "unavailable",
		feetWritten ? " (the player's own choice, remembered)" : "");

	/* The clock restarts after the re-dress and not before it: with
	 * TIME_Init above the re-dress, its cost would go onto the game clock
	 * and ControlPhase would run up to five catch-up ticks of game logic
	 * the moment the menu closed. */
	TIME_Init();
}

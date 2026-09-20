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

/* MenuTest - the arithmetic of the options menu, checked without a game
 * (#161, round 161b1 step 1).
 *
 * Build with bmenu.bat, run with no arguments.
 *
 * WHY IT EXISTS. The menu's cursor used to be a one-hot bit mask shifted by
 * hand at five call sites, and every row added above a list meant recounting a
 * shift somewhere else - round 161a2 and round 161a2b each had to move two of
 * them, and the comments in specific\LoadSave.cpp say what went wrong when one
 * was missed. The arithmetic is now one header, game\menu_nav.h, and this file
 * INCLUDES THAT HEADER rather than a copy of it - the way InterpTest.cpp
 * includes game\interpolate_math.h. A change to the tree is a change this test
 * sees.
 *
 * WHAT IT CHECKS. Every function of the header is swept over its whole small
 * range rather than sampled, because the interesting cases are the ends: a
 * disabled first row, a disabled last row, a page with exactly one enabled row,
 * runs of disabled rows, a list of one and a list of none. The paging cases are
 * named after the two lists the engine really has: the keyboard's roles and
 * the pad's, and the balanced split the user asked for - 8+8 and 8+7 while
 * both lists were sixteen and fifteen long, and the keyboard's own three-page
 * split since the hotkey roles joined it (#161, round 161b2d).
 */

#include <stdio.h>
#include <string.h>

#include "../../../TOMB5 Code/TOMB5/game/menu_nav.h"
#include "../../../TOMB5 Code/TOMB5/game/key_scheme.h"
#include "../../../TOMB5 Code/TOMB5/game/pad_scheme.h"

static int g_checks;
static int g_fail;

static void check(int got, int want, const char* what)
{
	g_checks++;

	if (got != want)
	{
		g_fail++;
		printf("FAIL %-44s got %d, want %d\n", what, got, want);
	}
}

/* A tiny helper so a case can be written as a picture of the page: '1' is an
 * enabled row, '.' a disabled one. */
static int mask(const char* pic, unsigned char* out)
{
	int n = (int)strlen(pic);

	for (int i = 0; i < n; i++)
		out[i] = (unsigned char)(pic[i] == '1' ? 1 : 0);

	return n;
}

/* ---------------------------------------------------------------- cursor -- */

static void cursor_all_enabled()
{
	/* No mask at all is the common page: the cursor walks and stops at the
	 * ends, exactly as base's clamp did. */
	for (int n = 1; n <= 20; n++)
	{
		check(MenuCursorStep(0, n, 0, -1), 0, "step up off the top stays");
		check(MenuCursorStep(0, n, n - 1, +1), n - 1, "step down off the bottom stays");

		for (int i = 0; i < n - 1; i++)
			check(MenuCursorStep(0, n, i, +1), i + 1, "step down");

		for (int i = 1; i < n; i++)
			check(MenuCursorStep(0, n, i, -1), i - 1, "step up");

		check(MenuCursorStep(0, n, 0, 0), 0, "no direction, no move");
		check(MenuFirstEnabled(0, n), 0, "first enabled with no mask");
	}
}

static void cursor_disabled()
{
	unsigned char m[32];
	int n;

	/* First row disabled - entering the page must not land on it. */
	n = mask(".111", m);
	check(MenuFirstEnabled(m, n), 1, "first row disabled");
	check(MenuCursorStep(m, n, 1, -1), 1, "up into the disabled first row stays");
	check(MenuClampCursor(m, n, 0), 1, "clamp off the disabled first row");

	/* Last row disabled. */
	n = mask("111.", m);
	check(MenuCursorStep(m, n, 2, +1), 2, "down into the disabled last row stays");
	check(MenuClampCursor(m, n, 3), 2, "clamp off the disabled last row");

	/* A run of disabled rows is jumped in one step, not walked into. */
	n = mask("1...1", m);
	check(MenuCursorStep(m, n, 0, +1), 4, "one step over three disabled rows");
	check(MenuCursorStep(m, n, 4, -1), 0, "and back again");

	/* Two consecutive, in the middle, both directions. */
	n = mask("11..11", m);
	check(MenuCursorStep(m, n, 1, +1), 4, "down over two disabled");
	check(MenuCursorStep(m, n, 4, -1), 1, "up over two disabled");

	/* All but one enabled - whichever one it is, the cursor cannot leave it. */
	for (int k = 0; k < 6; k++)
	{
		char pic[8];

		for (int i = 0; i < 6; i++)
			pic[i] = (i == k) ? '1' : '.';

		pic[6] = 0;
		n = mask(pic, m);

		check(MenuFirstEnabled(m, n), k, "the only enabled row is found");
		check(MenuCursorStep(m, n, k, +1), k, "the only enabled row keeps the cursor (down)");
		check(MenuCursorStep(m, n, k, -1), k, "the only enabled row keeps the cursor (up)");

		for (int i = 0; i < 6; i++)
			check(MenuClampCursor(m, n, i), k, "clamp finds the only enabled row");
	}

	/* No enabled row at all: -1 says so rather than a row number that is a
	 * lie. MenuClampCursor has nothing to move to and says so by not moving. */
	n = mask("....", m);
	check(MenuFirstEnabled(m, n), -1, "a page with nothing selectable");
	check(MenuClampCursor(m, n, 2), 2, "clamp with nothing selectable");

	/* A remembered cursor from a longer visit. */
	n = mask("1111", m);
	check(MenuClampCursor(m, n, 9), 3, "remembered cursor past the end");
	check(MenuClampCursor(m, n, -3), 0, "remembered cursor before the start");
	check(MenuClampCursor(0, 0, 4), 0, "clamp on an empty page");
	check(MenuCursorStep(0, 0, 0, +1), 0, "step on an empty page");
	check(MenuFirstEnabled(0, 0), -1, "first enabled on an empty page");

	/* WHICH WAY THE CLAMP MOVES when it really has a choice. The header says
	 * downward first, and every case above is satisfied by a clamp that
	 * searched upward instead: the disabled row is at an end there, so only
	 * one direction ever finds anything. The review round swapped the two
	 * loops and MenuTest did not notice - these cases are what notices. */
	n = mask("1.1", m);
	check(MenuClampCursor(m, n, 1), 2, "clamp prefers the row below");

	n = mask("1..", m);
	check(MenuClampCursor(m, n, 1), 0, "clamp goes up only when nothing is below");

	for (int len = 3; len <= 20; len++)
	{
		char pic[24];

		/* Every interior row switched off in turn, everything else on: the
		 * answer is always the row below, never the one above. */
		for (int k = 1; k < len - 1; k++)
		{
			for (int i = 0; i < len; i++)
				pic[i] = (i == k) ? '.' : '1';

			pic[len] = 0;
			n = mask(pic, m);
			check(MenuClampCursor(m, n, k), k + 1, "clamp prefers the row below, swept");
		}
	}

	/* The Controls page's own case, drawn as a picture: Camera speed (row 5)
	 * is off under Tank, so the cursor jumps Control layout -> Targeting. */
	n = mask("1111.1", m);
	check(MenuCursorStep(m, n, 3, +1), 5, "Tank: Control layout down to Targeting");
	check(MenuCursorStep(m, n, 5, -1), 3, "Tank: Targeting up to Control layout");
}

/* ----------------------------------------------------------------- pages -- */

static void paging_engine_cases()
{
	/* The two lists the engine really pages, at the maximum the brief names. */
	check(MenuPageCount(16, 13), 2, "16 keyboard roles at 13: two pages");
	check(MenuRowsPerPage(16, 13), 8, "16 at 13: 8 per page");
	check(MenuRowsOnPage(16, 13, 0), 8, "16 at 13: page 0 has 8");
	check(MenuRowsOnPage(16, 13, 1), 8, "16 at 13: page 1 has 8");
	check(MenuPageFirstRow(16, 13, 0), 0, "16 at 13: page 0 starts at 0");
	check(MenuPageFirstRow(16, 13, 1), 8, "16 at 13: page 1 starts at 8");

	check(MenuPageCount(15, 13), 2, "15 pad roles at 13: two pages");
	check(MenuRowsPerPage(15, 13), 8, "15 at 13: 8 per page");
	check(MenuRowsOnPage(15, 13, 0), 8, "15 at 13: page 0 has 8");
	check(MenuRowsOnPage(15, 13, 1), 7, "15 at 13: page 1 has 7");
	check(MenuPageFirstRow(15, 13, 1), 8, "15 at 13: page 1 starts at 8");

	/* The same two lists at the maximum the frame really uses once the
	 * centred Reset row has taken one of the twelve lines (menu_frame.h). */
	check(MenuPageCount(16, 11), 2, "16 at 11: two pages");
	check(MenuRowsPerPage(16, 11), 8, "16 at 11: still 8 per page");
	check(MenuPageCount(15, 11), 2, "15 at 11: two pages");
	check(MenuRowsPerPage(15, 11), 8, "15 at 11: still 8 per page");

	/* AND THE KEYBOARD LIST AS ROUND 161b2d LEAVES IT (#161, item 2): sixteen
	 * roles, the Secondary one only under "Separate", ten hotkeys, and the five
	 * cheat rows only while Cheats are on - four lengths, all at the frame's
	 * real maximum of 11, and all of them three pages. That is the "1/N" item 1
	 * draws between the arrows, so the number is worth holding. */
	check(MenuPageCount(26, 11), 3, "One key, cheats off: 26 rows, three pages");
	check(MenuRowsPerPage(26, 11), 9, "26 at 11: 9 per page");
	check(MenuRowsOnPage(26, 11, 2), 8, "26 at 11: the last page has 8");

	check(MenuPageCount(27, 11), 3, "Separate, cheats off: 27 rows, three pages");
	check(MenuRowsPerPage(27, 11), 9, "27 at 11: 9 per page");
	check(MenuRowsOnPage(27, 11, 2), 9, "27 at 11: the last page has 9");

	check(MenuPageCount(31, 11), 3, "One key, cheats on: 31 rows, three pages");
	check(MenuRowsPerPage(31, 11), 11, "31 at 11: 11 per page");
	check(MenuRowsOnPage(31, 11, 2), 9, "31 at 11: the last page has 9");

	check(MenuPageCount(32, 11), 3, "Separate, cheats on: 32 rows, three pages");
	check(MenuRowsPerPage(32, 11), 11, "32 at 11: 11 per page");
	check(MenuRowsOnPage(32, 11, 2), 10, "32 at 11: the last page has 10");

	/* AND THE PAD LIST AS ROUND 161b2d LEAVES IT (#161, item 3). It was fifteen
	 * roles and one page-and-a-bit; it is sixteen roles with the Secondary one
	 * only under "Separate", the Modifier, six weapons and five cheat rows only
	 * while Cheats are on - 22, 23, 27 or 28 BODY rows, the centred Reset row
	 * being fixed and on every page (menu_frame.cpp's `fixed`). Two pages at the
	 * shortest and three at the other three, so "1/3" stays the widest string
	 * the indicator of item 1 can draw anywhere in this tree. */
	check(MenuPageCount(22, 11), 2, "pad, One key, cheats off: 22 body rows, two pages");
	check(MenuRowsPerPage(22, 11), 11, "22 at 11: 11 per page");
	check(MenuRowsOnPage(22, 11, 1), 11, "22 at 11: the last page has 11");

	check(MenuPageCount(23, 11), 3, "pad, Separate, cheats off: 23 body rows, three pages");
	check(MenuRowsPerPage(23, 11), 8, "23 at 11: 8 per page");
	check(MenuRowsOnPage(23, 11, 2), 7, "23 at 11: the last page has 7");

	check(MenuPageCount(27, 11), 3, "pad, One key, cheats on: 27 body rows, three pages");
	check(MenuRowsPerPage(27, 11), 9, "27 at 11: 9 per page");
	check(MenuRowsOnPage(27, 11, 2), 9, "27 at 11: the last page has 9");

	check(MenuPageCount(28, 11), 3, "pad, Separate, cheats on: 28 body rows, three pages");
	check(MenuRowsPerPage(28, 11), 10, "28 at 11: 10 per page");
	check(MenuRowsOnPage(28, 11, 2), 8, "28 at 11: the last page has 8");

	/* AND THE TWO PAGES ROUND 161b2d ITEM 5 LEAVES BEHIND IT. The four mode
	 * rows moved off the Controls page onto a sub-page of their own ("Die vier
	 * Modi bitte auf eine Unterseite"), so Controls is ten rows where it was
	 * twelve - three left, the "Modes" tab arrived - and Modes is four. Neither
	 * has a fixed row, so both are asked at the full MENU_MAX_ROWS of 12, and
	 * both are ONE page: no arrows, and therefore no "1/N" indicator on either. */
	check(MenuPageCount(10, 12), 1, "Controls: 10 rows at 12, one page");
	check(MenuRowsOnPage(10, 12, 0), 10, "Controls: all ten on page 0");
	check(MenuPageFirstRow(10, 12, 0), 0, "Controls: page 0 starts at 0");
	check(MenuRowsOnPage(10, 12, 1), 0, "Controls: there is no second page");

	check(MenuPageCount(4, 12), 1, "Modes: 4 rows at 12, one page");
	check(MenuRowsOnPage(4, 12, 0), 4, "Modes: all four on page 0");
	check(MenuRowsOnPage(4, 12, 1), 0, "Modes: there is no second page");

	// And the row count the page LEFT behind is still a one-page count, so the
	// move cannot have been what makes Controls page: it never did.
	check(MenuPageCount(12, 12), 1, "the twelve rows Controls had were one page too");
}

static void paging_ends()
{
	check(MenuPageCount(1, 13), 1, "one row, one page");
	check(MenuRowsPerPage(1, 13), 1, "one row per page");
	check(MenuRowsOnPage(1, 13, 0), 1, "one row on page 0");
	check(MenuPageFirstRow(1, 13, 0), 0, "one row starts at 0");

	check(MenuPageCount(0, 13), 1, "no rows still make one page");
	check(MenuRowsPerPage(0, 13), 0, "no rows per page");
	check(MenuRowsOnPage(0, 13, 0), 0, "no rows on page 0");

	check(MenuPageCount(13, 13), 1, "exactly the maximum is one page");
	check(MenuRowsOnPage(13, 13, 0), 13, "exactly the maximum fits");
	check(MenuPageCount(14, 13), 2, "one over the maximum is two pages");
	check(MenuRowsPerPage(14, 13), 7, "14 at 13 splits 7 + 7");
	check(MenuRowsOnPage(14, 13, 1), 7, "14 at 13: page 1 has 7");

	/* A maximum of one is the degenerate page-per-row case, and a maximum of
	 * zero or less must not divide by it. */
	check(MenuPageCount(5, 1), 5, "max 1: one page per row");
	check(MenuRowsPerPage(5, 1), 1, "max 1: one row per page");
	check(MenuPageCount(5, 0), 1, "max 0 is refused, not divided by");
	check(MenuPageCount(5, -4), 1, "a negative max likewise");
	check(MenuRowsOnPage(16, 13, 2), 0, "a page past the end is empty");
	check(MenuRowsOnPage(16, 13, 9), 0, "and stays empty further out");
}

static void paging_sweep()
{
	/* The two properties that matter, over every list and every maximum the
	 * menu could ever hand it: the pages cover the list exactly once, and no
	 * page is longer than the maximum. */
	for (int max = 1; max <= 20; max++)
	{
		for (int n = 0; n <= 40; n++)
		{
			int pages = MenuPageCount(n, max);
			int seen = 0;

			check(pages >= 1, 1, "at least one page");

			for (int p = 0; p < pages; p++)
			{
				int first = MenuPageFirstRow(n, max, p);
				int rows = MenuRowsOnPage(n, max, p);

				check(first == seen, 1, "pages follow on without a gap");
				check(rows <= max, 1, "no page is longer than the maximum");
				check(rows >= 0, 1, "no page has a negative length");
				seen += rows;
			}

			check(seen, n, "the pages cover the list exactly");
		}
	}
}

static void page_step()
{
	/* Wraps in both directions - that is the one thing base's own paging did
	 * not do. */
	check(MenuPageStep(0, 3, +1), 1, "page forward");
	check(MenuPageStep(1, 3, +1), 2, "page forward again");
	check(MenuPageStep(2, 3, +1), 0, "page forward wraps");
	check(MenuPageStep(0, 3, -1), 2, "page back wraps");
	check(MenuPageStep(2, 3, -1), 1, "page back");
	check(MenuPageStep(0, 1, +1), 0, "a single page goes nowhere");
	check(MenuPageStep(0, 1, -1), 0, "a single page goes nowhere back");
	check(MenuPageStep(0, 0, +1), 0, "no pages at all");

	/* A full lap in each direction over every page count the menu can reach
	 * ends where it started, and visits each page once. */
	for (int pages = 1; pages <= 8; pages++)
	{
		int p = 0;
		int seen[9];

		memset(seen, 0, sizeof(seen));

		for (int k = 0; k < pages; k++)
		{
			seen[p]++;
			p = MenuPageStep(p, pages, +1);
		}

		check(p, 0, "a forward lap comes home");

		for (int k = 0; k < pages; k++)
			check(seen[k], 1, "a forward lap visits each page once");

		p = 0;
		memset(seen, 0, sizeof(seen));

		for (int k = 0; k < pages; k++)
		{
			seen[p]++;
			p = MenuPageStep(p, pages, -1);
		}

		check(p, 0, "a backward lap comes home");

		for (int k = 0; k < pages; k++)
			check(seen[k], 1, "a backward lap visits each page once");
	}
}

/* ------------------------------------------------------------ keyboard -- */
/* game\key_scheme.h - the two keyboard sets (#161, round 161b2a, item 1).
 * Same rule as above: the ENGINE'S header, not a copy, so the row checked here
 * is the row a player gets. */

static const short kModern[KEY_SCHEME_ROLES] = KEY_SCHEME_MODERN_ROW;

/* A row that shares no key with the Modern one, to play the part of "some
 * other binding" wherever a second row is needed. The numbers are scan codes
 * of keys neither default row uses - letters, the two spare digits, the four F
 * keys the engine keeps for itself, the numeric keypad and Insert - and
 * nothing else about them matters. It had to be rewritten for round 161b2d:
 * its old numbers were the function keys, and the hotkey roles took eight of
 * them. */
static const short kOther[KEY_SCHEME_ROLES] =
{
	0x14, 0x15, 0x16, 0x17, 0x18,	/* T Y U I O           */
	0x22, 0x23, 0x24, 0x25, 0x26,	/* G H J K L           */
	0x2F, 0x30, 0x31, 0x32,			/* V B N M             */
	0x08, 0x09,						/* 7 8                 */
	0x3D, 0x3E, 0x44, 0x58,			/* F3 F4 F10 F12       */
	0x45, 0x47, 0x48, 0x49, 0x4A,	/* NumLock Num7 Num8 Num9 Num- */
	0x4B, 0x4C, 0x4D, 0x4E,			/* Num4 Num5 Num6 Num+ */
	0x4F, 0x50, 0x51, 0x53,			/* Num1 Num2 Num3 Num. */
	0xD2							/* Insert              */
};

/* THE SLOT NUMBERS THEMSELVES (#161, rounds 161b2c and 161b2d, item 2). Every
 * slot is addressed by number in the registry, so a number that moves is a
 * player's bindings silently meaning something else. Slot 18 is the Secondary
 * action and must stay 18 now that the hotkeys sit BEHIND it rather than
 * before it - it is no longer "the last slot" and no longer derivable from the
 * count, which is exactly why it is checked here. */
static void key_secondary_slot()
{
	check(KEY_SCHEME_ROLES, 34, "thirty-four roles since the hotkeys");
	check(KEY_ROLE_SECOND, 18, "the Secondary role is still slot 18");
	check(kModern[KEY_ROLE_SECOND], 0x52, "the Modern row's Secondary key is numpad 0");

	// Disjoint is what kOther is for, and every new slot is one more chance to
	// break it by accident.
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
		for (int j = 0; j < KEY_SCHEME_ROLES; j++)
			check(kOther[i] == kModern[j], 0, "kOther shares no key with the Modern row");
}

/* THE FIFTEEN HOTKEY SLOTS (#161, round 161b2d, item 2). Two things are worth
 * a test rather than a comment: that the numbers are the fifteen consecutive
 * slots behind the Secondary one (a gap or an overlap would make one registry
 * name mean two roles), and that the Modern row's keys are the keys
 * specific\input.cpp used to read directly - which is this round's whole
 * byte-identity claim, since Key(role) reads that key for an untouched
 * registry. The engine's own static_asserts say the same thing against the
 * DIK_ names; this says it against the numbers, where a player's registry
 * lives. */
static void key_hotkey_slots()
{
	const short want[15] =
	{
		0x0B, 0x0A, 0x3F, 0x43,			/* 0 9 F5 F9        */
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07,	/* 1 2 3 4 5 6  */
		0x3C, 0x41, 0x42, 0x57, 0x3B	/* F2 F7 F8 F11 F1  */
	};
	int i;

	check(KEY_ROLE_MEDI_SMALL, KEY_ROLE_SECOND + 1, "the hotkeys start behind the Secondary slot");
	check(KEY_ROLE_MEDI_LARGE, KEY_ROLE_MEDI_SMALL + 1, "large medipack follows small");
	check(KEY_ROLE_SAVE, KEY_ROLE_MEDI_LARGE + 1, "quick save follows the medipacks");
	check(KEY_ROLE_LOAD, KEY_ROLE_SAVE + 1, "quick load follows quick save");
	check(KEY_ROLE_WEAPON_FIRST, KEY_ROLE_LOAD + 1, "the weapons follow load");
	check(KEY_ROLE_WEAPONS, 6, "six weapon roles, DoWeaponHotkey's own six");
	check(KEY_ROLE_CHEAT_FLY, KEY_ROLE_WEAPON_FIRST + KEY_ROLE_WEAPONS, "the cheats follow the weapons");
	check(KEY_ROLE_CHEAT_GUNS, KEY_ROLE_CHEAT_FLY + 1, "all weapons follows fly mode");
	check(KEY_ROLE_CHEAT_ITEMS, KEY_ROLE_CHEAT_GUNS + 1, "all items follows all weapons");
	check(KEY_ROLE_CHEAT_HEAL, KEY_ROLE_CHEAT_ITEMS + 1, "heal follows all items");
	check(KEY_ROLE_CHEAT_SKIP, KEY_ROLE_CHEAT_HEAL + 1, "skip level follows heal");
	check(KEY_ROLE_CHEAT_SKIP, KEY_SCHEME_ROLES - 1, "and skip level is the last slot");

	for (i = 0; i < 15; i++)
		check(kModern[KEY_ROLE_MEDI_SMALL + i], want[i], "the Modern row's hotkey keys are the old hard-wired ones");
}

static void key_modern_row_is_sound()
{
	check(KeySchemeRowSound(kModern), 1, "the Modern default row is sound");

	// The two halves of "sound", said again slot by slot so that a failure
	// names the role rather than only the row.
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		int seen = 0;

		check(kModern[i] > 0, 1, "every Modern slot is bound");

		for (int j = 0; j < KEY_SCHEME_ROLES; j++)
			if (kModern[i] == kModern[j])
				seen++;

		check(seen, 1, "no Modern key is bound twice");
	}
}

static void key_row_sound_rejects()
{
	short row[KEY_SCHEME_ROLES];

	memcpy(row, kModern, sizeof(row));
	row[7] = 0;
	check(KeySchemeRowSound(row), 0, "a hole in the row is not sound");

	memcpy(row, kModern, sizeof(row));
	row[7] = row[3];
	check(KeySchemeRowSound(row), 0, "a key on two roles is not sound");

	memcpy(row, kModern, sizeof(row));
	row[KEY_SCHEME_ROLES - 1] = row[0];
	check(KeySchemeRowSound(row), 0, "the last slot is checked too");

	check(KeySchemeRowSound(kOther), 1, "the disjoint row is sound as well");
}

static void key_conflicts()
{
	long got[KEY_SCHEME_ROLES];
	short active[KEY_SCHEME_ROLES];
	int i, r;

	// Nothing shared: every role keeps its default key as a second trigger.
	// The last argument is "the Secondary role is live" (tomb5.action_split);
	// these three sweeps pass 1, which is the whole table as round 161b2a
	// wrote it. The inert case is the block below.
	KeySchemeConflicts(kOther, kModern, got, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 0, "a default key nobody claimed is free");

	// A player who never rebinds: the active set IS the default row, so every
	// default key is "claimed" - by the role that owns it. The fallback is
	// dead and costs nothing, because the primary binding is the same key.
	KeySchemeConflicts(kModern, kModern, got, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 1, "an untouched set claims its own defaults");

	// One claim at a time, swept over all nineteen roles: only the role whose
	// DEFAULT key was taken may go silent, and no other.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "exactly the claimed default is flagged");
	}
}

/* AN INERT SECONDARY SLOT CLAIMS NOTHING (#161, round 161b2c, fix round - the
 * skeptical review's fifth finding). With "One key" the Secondary role cannot
 * produce a bit however it is bound, so its key must not silence another
 * role's default-key fallback; with "Separate" it is a binding like any other
 * and Core's rule applies to it unchanged. */
static void key_conflicts_inert()
{
	long got[KEY_SCHEME_ROLES];
	short active[KEY_SCHEME_ROLES];
	int i, r;

	// The Secondary slot alone holds another role's default key. Live, it
	// silences that role; inert, it silences nothing at all.
	for (r = 0; r < KEY_SCHEME_ROLES - 1; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[KEY_ROLE_SECOND] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "live, the Secondary slot claims like any other");

		KeySchemeConflicts(active, kModern, got, 0);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], 0, "inert, the Secondary slot claims nothing");
	}

	// Every OTHER slot is untouched by the flag: a claim made by a live role
	// stands whether the Secondary role is offered or not.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 0);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "the flag moves only the Secondary slot");
	}

	// The engine's own case: with the Secondary slot at its default key, the
	// answer is the same in both settings, because neither default row holds
	// that key. This is the byte-identity claim the fix round makes.
	memcpy(active, kModern, sizeof(active));
	KeySchemeConflicts(active, kModern, got, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		long other[KEY_SCHEME_ROLES];

		KeySchemeConflicts(active, kModern, other, 0);
		check((int)other[i], (int)(i == KEY_ROLE_SECOND ? 0 : got[i]), "at the defaults only the inert slot itself differs");
	}
}

static void key_seat()
{
	short active[KEY_SCHEME_ROLES];
	short parked[KEY_SCHEME_ROLES];
	int seated, i;

	// Asking for the scheme already seated moves nothing - the menu row and
	// the startup seat both call it and neither may double-swap.
	memcpy(active, kOther, sizeof(active));
	memcpy(parked, kModern, sizeof(parked));
	seated = KeySchemeSeat(active, parked, 0, 0);
	check(seated, 0, "seating the seated scheme keeps it");

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		check(active[i], kOther[i], "the active set stands");
		check(parked[i], kModern[i], "the parked set stands");
	}

	// The flip: the set that leaves is kept, not overwritten.
	seated = KeySchemeSeat(active, parked, seated, 1);
	check(seated, 1, "the flip seats the other scheme");

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		check(active[i], kModern[i], "the other set is active now");
		check(parked[i], kOther[i], "and the first one is parked");
	}

	// And back again, to exactly what the player had.
	seated = KeySchemeSeat(active, parked, seated, 0);
	check(seated, 0, "the flip back comes home");

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		check(active[i], kOther[i], "a round trip changes no key");
		check(parked[i], kModern[i], "on either side");
	}

	// `want` is a tomb5_options bool by the time it gets here, but the
	// normalisation is the header's own so that no caller has to think about it.
	seated = KeySchemeSeat(active, parked, 0, 5);
	check(seated, 1, "any nonzero want means Modern");

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check(active[i], kModern[i], "and it really swapped");
}

/* THE PAD'S TWO SETS (#161, round 161b2c, item 3). Two rows of sixteen that
 * are DELIBERATELY not copies of the engine's tables: the seat swaps whatever
 * it is given and has no business knowing what a number means, so a twin of
 * jDefPS5DInput here would only be a second place to keep in step. 100+ and
 * 200+ say "some binding" and nothing more; the -1 in each is the one value
 * with a meaning - an unbound role - and it has to travel like any other. */
static const long kPadOne[JOY_ROLES] =
{
	100, 101, 102, 103, 104, 105, 106, 107,
	108, 109, 110, 111, 112, 113, 114, -1
};

static const long kPadSep[JOY_ROLES] =
{
	200, 201, 202, 203, 204, 205, 206, 207,
	208, 209, 210, -1, 212, 213, 214, 215
};

static void pad_seat()
{
	long active[JOY_ROLES];
	long parked[JOY_ROLES];
	int seated, i;

	// The count and the Secondary slot moved to pad_role_slots() with round
	// 161b2d (item 3): the Secondary role is no longer the last of them, so
	// "the last slot" stopped being a way to say 15.

	// Asking for the setting already seated moves nothing - the Action row and
	// the startup seat both call it and neither may double-swap.
	memcpy(active, kPadOne, sizeof(active));
	memcpy(parked, kPadSep, sizeof(parked));
	seated = PadSetSeat(active, parked, 0, 0);
	check(seated, 0, "seating the seated Action setting keeps it");

	for (i = 0; i < JOY_ROLES; i++)
	{
		check((int)active[i], (int)kPadOne[i], "the active pad set stands");
		check((int)parked[i], (int)kPadSep[i], "the parked pad set stands");
	}

	// The flip: the One-key bindings are kept, not overwritten.
	seated = PadSetSeat(active, parked, seated, 1);
	check(seated, 1, "the flip seats Separate");

	for (i = 0; i < JOY_ROLES; i++)
	{
		check((int)active[i], (int)kPadSep[i], "Separate's set is active now");
		check((int)parked[i], (int)kPadOne[i], "and the One-key set is parked");
	}

	// And back again, to exactly what the player had - the whole point of the
	// second set.
	seated = PadSetSeat(active, parked, seated, 0);
	check(seated, 0, "the flip back comes home");

	for (i = 0; i < JOY_ROLES; i++)
	{
		check((int)active[i], (int)kPadOne[i], "a round trip changes no binding");
		check((int)parked[i], (int)kPadSep[i], "on either side");
	}

	// `want` is a tomb5_options bool by the time it gets here, but the
	// normalisation is the header's own so that no caller has to think about it.
	seated = PadSetSeat(active, parked, 0, 7);
	check(seated, 1, "any nonzero want means Separate");

	for (i = 0; i < JOY_ROLES; i++)
		check((int)active[i], (int)kPadSep[i], "and it really swapped");
}

/* THE PAD'S TWELVE NEW ROLES AND THE ONE RULE THAT READS THEM (#161, round
 * 161b2d, item 3). The numbers are checked here for the reason KEY_ROLE_SECOND
 * is: they are what the registry addresses a stored binding by, so a slot that
 * moved would be a player's binding that moved with it. */
static void pad_role_slots(void)
{
	check(JOY_ROLES, 28, "twenty-eight pad roles");
	check(JOY_ROLE_SECOND, 15, "the Secondary pad role is still slot 15");
	check(JOY_ROLE_MODIFIER, 16, "the Modifier is slot 16");
	check(JOY_ROLE_WEAPON_FIRST, 17, "the pad's weapon hotkeys start at 17");
	check(JOY_ROLE_WEAPONS, 6, "and there are six of them");
	check(JOY_ROLE_CHEAT_FLY, 23, "fly mode is slot 23");
	check(JOY_ROLE_CHEAT_GUNS, 24, "all weapons is slot 24");
	check(JOY_ROLE_CHEAT_ITEMS, 25, "all items is slot 25");
	check(JOY_ROLE_CHEAT_HEAL, 26, "heal is slot 26");
	check(JOY_ROLE_CHEAT_SKIP, 27, "skip level is slot 27");
	check(JOY_ROLE_CHEAT_SKIP, JOY_ROLES - 1, "and the cheats are the last of the roles");
	check(JOY_ROLE_WEAPON_FIRST + JOY_ROLE_WEAPONS, JOY_ROLE_CHEAT_FLY, "the weapons end where the cheats begin");
	check(JOY_BIT_MAX, 23, "23 is the highest bit either joystick path writes");
}

/* THE CHORD RULE, SWEPT RATHER THAN SAMPLED. The two ends are the interesting
 * cases here as everywhere else in this file: an unbound role, an unbound
 * Modifier, and the Modifier's own button. */
static void pad_chord_rule(void)
{
	long bit, other;

	// PadBitDown closes both ends of the shift. -1 is the unbound row and has
	// to answer no; so does anything above the highest bit the pad writes,
	// because a binding comes out of the registry and is a number, not a
	// promise.
	for (bit = -2; bit <= JOY_BIT_MAX + 2; bit++)
		check(PadBitDown(0xFFFFFFFFUL, bit), (bit >= 0 && bit <= JOY_BIT_MAX) ? 1 : 0,
			"PadBitDown answers only for a real button");

	/* WITH NO MODIFIER BOUND THE RULE IS THE EXPRESSION IT REPLACED. This is
	 * the round's byte-identity argument as a test: for every button, a plain
	 * role fires exactly when its own button is down and nothing else does,
	 * which is what Key(jLayout[n] + 256) answered. */
	for (bit = 0; bit <= JOY_BIT_MAX; bit++)
	{
		const unsigned long fire = 1UL << bit;

		for (other = 0; other <= JOY_BIT_MAX; other++)
			check(PadRoleFires(fire, other, 0, -1), other == bit ? 1 : 0,
				"no Modifier: a plain role is its own button");

		check(PadRoleFires(fire, bit, 1, -1), 0, "no Modifier: no chord can fire");
		check(PadRoleFires(fire, -1, 0, -1), 0, "no Modifier: an unbound role fires nothing");
		check(PadRoleFires(fire, -1, 1, -1), 0, "no Modifier: nor an unbound chord");
	}

	/* And with one bound - bit 13, the touchpad click, which is where the two
	 * DirectInput-numbered Separate rows put it. R3 is bit 11 there. */
	{
		const long m = 13;
		const unsigned long modonly = 1UL << m;
		const unsigned long plain = 1UL << 11;
		const unsigned long pair = modonly | plain;

		check(PadRoleFires(plain, 11, 0, m), 1, "plain R3 fires on its own");
		check(PadRoleFires(plain, 11, 1, m), 0, "Modifier+R3 needs the Modifier");
		check(PadRoleFires(pair, 11, 1, m), 1, "Modifier+R3 fires with it");
		check(PadRoleFires(pair, 11, 0, m), 0, "and plain R3 stands aside while it does");
		check(PadRoleFires(modonly, 11, 0, m), 0, "the Modifier alone fires no plain role");
		check(PadRoleFires(modonly, 11, 1, m), 0, "and no chord either");
		check(PadRoleFires(modonly, m, 0, m), 0, "the Modifier's own button is no role's trigger");
		check(PadRoleFires(modonly, m, 1, m), 0, "plain or chord");
		check(PadRoleFires(0, 11, 0, m), 0, "nothing down, nothing fires");
		check(PadRoleFires(0, 11, 1, m), 0, "either way");
	}
}

/* THE EIGHT DEFAULT TABLES, AGAINST THE ONE RULE THEY ALL HAVE TO KEEP (#161,
 * round 161b2d, item 3): no two roles on one PAIR, and no plain role on the
 * Modifier's own button.
 *
 * The four Separate rows are the engine's, through the macros of
 * game\pad_scheme.h - this file instantiates what specific\input.cpp
 * instantiates, so the check runs on the row a player really gets. */
static const long kSepPS5DInput[JOY_ROLES] = JOY_SEP_PS5_DINPUT;
static const long kSepPS5XInput[JOY_ROLES] = JOY_SEP_PS5_XINPUT;
static const long kSepXbox[JOY_ROLES] = JOY_SEP_XBOX;
static const long kSepGeneric[JOY_ROLES] = JOY_SEP_GENERIC;

static void pad_table_sound(const long* row, long chord, int modern, const char* what)
{
	long t[JOY_ROLES];
	long i, k;

	for (i = 0; i < JOY_ROLES; i++)
		t[i] = row[i];

	// JoyDefaultRecord's own Modern override, in one place there and in one
	// place here: the medipacks move to the d-pad's own bits (the user's
	// decision 8, round 161a2 step 3).
	if (modern)
	{
		t[13] = 20;
		t[14] = 21;
	}

	// A chord mask with no Modifier to chord against would be twelve bindings
	// that can never fire.
	check(chord != 0 && t[JOY_ROLE_MODIFIER] < 0 ? 1 : 0, 0, what);
	// The Modifier is always plain - it is the thing a chord is held with.
	check((int)((chord >> JOY_ROLE_MODIFIER) & 1), 0, what);

	for (i = 0; i < JOY_ROLES; i++)
	{
		if (t[i] < 0)
		{
			// An unbound role carries no flag either, or a Reset would hand
			// back a chord on nothing.
			check((int)((chord >> i) & 1), 0, what);
			continue;
		}

		check(t[i] <= JOY_BIT_MAX ? 1 : 0, 1, what);

		// Nothing plain sits on the Modifier's button: its own press would
		// hold the Modifier down, so the role could never fire.
		if (i != JOY_ROLE_MODIFIER)
			check(t[i] == t[JOY_ROLE_MODIFIER] ? 1 : 0, 0, what);

		// And no two roles share a pair. R3 and Modifier+R3 are two pairs and
		// are allowed to coexist - that is the second term.
		for (k = i + 1; k < JOY_ROLES; k++)
			check(t[k] == t[i] && ((chord >> k) & 1) == ((chord >> i) & 1) ? 1 : 0, 0, what);
	}
}

static void pad_default_tables(void)
{
	long one[JOY_ROLES];
	long i;

	pad_table_sound(kSepPS5DInput, JOY_SEP_CHORD_DINPUT, 0, "Separate PS5 DirectInput, Tank");
	pad_table_sound(kSepPS5DInput, JOY_SEP_CHORD_DINPUT, 1, "Separate PS5 DirectInput, Modern");
	pad_table_sound(kSepGeneric, JOY_SEP_CHORD_DINPUT, 0, "Separate generic, Tank");
	pad_table_sound(kSepGeneric, JOY_SEP_CHORD_DINPUT, 1, "Separate generic, Modern");
	pad_table_sound(kSepPS5XInput, JOY_SEP_CHORD_XINPUT, 0, "Separate PS5 XInput, Tank");
	pad_table_sound(kSepPS5XInput, JOY_SEP_CHORD_XINPUT, 1, "Separate PS5 XInput, Modern");
	pad_table_sound(kSepXbox, JOY_SEP_CHORD_XINPUT, 0, "Separate Xbox, Tank");
	pad_table_sound(kSepXbox, JOY_SEP_CHORD_XINPUT, 1, "Separate Xbox, Modern");

	/* AND THE FOUR ONE-KEY ROWS BY THE CONSTRUCTION THAT BUILDS THEM. They
	 * themselves are fifteen entries in specific\input.cpp and this round does
	 * not touch them - the user's decision of 2026-09-16 keeps them as our pad
	 * layout - so what there is to check is what JoyDefaultRecord puts BEHIND
	 * them at "One key": every role from the Secondary on unbound, and a chord
	 * mask of 0. With no Modifier bound the last two of the rules above are
	 * vacuous, which is the point: at One key no chord can exist at all. The
	 * fifteen here stand in for whichever row the pad is on. */
	for (i = 0; i < 15; i++)
		one[i] = i;

	for (i = 15; i < JOY_ROLES; i++)
		one[i] = -1;

	check((int)one[JOY_ROLE_MODIFIER], -1, "One key: no Modifier on any path");
	pad_table_sound(one, 0, 0, "One key, Tank");
	pad_table_sound(one, 0, 1, "One key, Modern");

	/* THE USER'S OWN SECOND BINDING PAGE, DECODED. The four Separate tables are
	 * bit indices; these are the seven pairs he wrote down, read back out of
	 * the DirectInput row by name (specific\input.cpp names the bits: 0 Square,
	 * 1 Cross, 2 Circle, 3 Triangle, 5 R1, 10 L3, 11 R3, 13 Touchpad). */
	check((int)kSepPS5DInput[JOY_ROLE_MODIFIER], 13, "Modifier = the touchpad click");
	check((int)kSepPS5DInput[11], 11, "quick save on R3");
	check((int)kSepPS5DInput[12], 10, "quick load on L3");
	check((int)kSepPS5DInput[6], 5, "flare on R1");
	check((int)kSepPS5DInput[JOY_ROLE_WEAPON_FIRST + 0], 1, "pistols on Cross");
	check((int)kSepPS5DInput[JOY_ROLE_WEAPON_FIRST + 1], 2, "shotgun on Circle");
	check((int)kSepPS5DInput[JOY_ROLE_WEAPON_FIRST + 2], 3, "uzi on Triangle");
	check((int)kSepPS5DInput[JOY_ROLE_WEAPON_FIRST + 3], 0, "revolver on Square");

	for (i = 0; i < JOY_ROLES; i++)
	{
		const int want = (i == 6 || i == 11 || i == 12 ||
			(i >= JOY_ROLE_WEAPON_FIRST && i < JOY_ROLE_WEAPON_FIRST + 4)) ? 1 : 0;

		check((int)((JOY_SEP_CHORD_DINPUT >> i) & 1), want, "and all seven of them are chords");
		check((int)((JOY_SEP_CHORD_XINPUT >> i) & 1), 0, "the XInput rows have no chord at all");
	}

	// The medipacks are a pair and both go: the small one sat on bit 13, which
	// is the Modifier's button now. 0 and 9 still work, and under Modern the
	// override above puts them back on the d-pad.
	check((int)kSepPS5DInput[13], -1, "Separate DirectInput: small medipack unbound");
	check((int)kSepPS5DInput[14], -1, "and the large one with it");
	// The two XInput rows keep the 161b2c defaults whole: no spare button, so
	// no Modifier and no chord.
	check((int)kSepXbox[JOY_ROLE_MODIFIER], -1, "Separate Xbox: no Modifier");
	check((int)kSepXbox[6], 5, "so the flare stays a plain R1");
	check((int)kSepXbox[13], 18, "and the medipacks stay where they were");
	check((int)kSepXbox[14], 19, "both of them");
}

int main()
{
	printf("MenuTest - game\\menu_nav.h, game\\key_scheme.h, game\\pad_scheme.h (#161, rounds 161b1, 161b2a, 161b2c and 161b2d)\n\n");

	cursor_all_enabled();
	cursor_disabled();
	paging_engine_cases();
	paging_ends();
	paging_sweep();
	page_step();
	key_secondary_slot();
	key_hotkey_slots();
	key_modern_row_is_sound();
	key_row_sound_rejects();
	key_conflicts();
	key_conflicts_inert();
	key_seat();
	pad_seat();
	pad_role_slots();
	pad_chord_rule();
	pad_default_tables();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

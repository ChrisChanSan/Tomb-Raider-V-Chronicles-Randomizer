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

/* MenuTest - the arithmetic of the options menu, checked without a game.
 *
 * Build with bmenu.bat, run with no arguments from this folder: the last two
 * families (pad_scheme_repair and controls_mouse_rows) read engine files as
 * text, by their paths relative to it, because the tables and rows they check
 * live in files that cannot be linked here.
 *
 * The menu's cursor used to be a one-hot bit mask shifted by hand at five
 * call sites, so every row added above a list meant recounting a shift
 * somewhere else, and the comments in specific\LoadSave.cpp say what went
 * wrong when one was missed. The arithmetic now lives in one header,
 * game\menu_nav.h, and this file includes that header rather than a copy of
 * it, the way InterpTest.cpp includes game\interpolate_math.h, so a change
 * to the tree is a change this test sees.
 *
 * Every function of the header is swept over its whole small range rather
 * than sampled, because the interesting cases are the ends: a disabled
 * first row, a disabled last row, a page with exactly one enabled row, runs
 * of disabled rows, a list of one and a list of none. The paging cases are
 * named after the two lists the engine really has: the keyboard's roles and
 * the pad's, with a balanced split (8+8 and 8+7 while both lists were
 * sixteen and fifteen long) and the keyboard's own three-page split once
 * the hotkey roles joined it.
 */

#include <stdio.h>
#include <stdlib.h>
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

	/* Which way the clamp moves when it really has a choice: the header says
	 * downward first, and every case above is satisfied by a clamp that
	 * searched upward instead, because the disabled row is at an end there,
	 * so only one direction ever finds anything. These cases force the
	 * choice. */
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
	/* The two lists the engine really pages, checked at a page maximum of 13. */
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

	/* The keyboard list: sixteen roles, the Secondary one only under
	 * "Separate", ten hotkeys, and the five cheat rows only while Cheats are
	 * on - four lengths, all at the frame's real maximum of 11, and all of
	 * them three pages. That count is what the "1/N" indicator draws between
	 * the arrows, so it is worth checking directly. */
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

	/* The pad list: sixteen roles with the Secondary one only under
	 * "Separate", the Modifier, six weapons and five cheat rows only while
	 * Cheats are on - 22, 23, 27 or 28 body rows, with the centred Reset row
	 * fixed and present on every page (menu_frame.cpp's `fixed`). Two pages
	 * at the shortest and three at the other three, so "1/3" is the widest
	 * string the page indicator ever has to draw. */
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

	/* The Controls and Modes pages: the five mode rows sit on their own
	 * sub-page, so Controls has twelve rows with the two mouse rows and Modes
	 * has five. Neither has a fixed row, so both are asked at the full
	 * MENU_MAX_ROWS of 12, and both are one page: no arrows, and therefore no
	 * "1/N" indicator on either. */
	check(MenuPageCount(12, 12), 1, "Controls: 12 rows at 12, one page");
	check(MenuRowsOnPage(12, 12, 0), 12, "Controls: all twelve on page 0");
	check(MenuPageFirstRow(12, 12, 0), 0, "Controls: page 0 starts at 0");
	check(MenuRowsOnPage(12, 12, 1), 0, "Controls: there is no second page");

	check(MenuPageCount(5, 12), 1, "Modes: 5 rows at 12, one page");
	check(MenuRowsOnPage(5, 12, 0), 5, "Modes: all five on page 0");
	check(MenuRowsOnPage(5, 12, 1), 0, "Modes: there is no second page");

	// Twelve is the most the page holds: a thirteenth row would split it.
	check(MenuPageCount(13, 12), 2, "a thirteenth Controls row would make two pages");
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
/* game\key_scheme.h - the two keyboard sets. Same rule as above: the
 * engine's header, not a copy, so the row checked here is the row a
 * player gets. */

static const short kModern[KEY_SCHEME_ROLES] = KEY_SCHEME_MODERN_ROW;

/* A row that shares no key with the Modern one, to play the part of "some
 * other binding" wherever a second row is needed. The numbers are scan
 * codes of keys neither default row uses - letters, the two spare digits,
 * the four F keys the engine keeps for itself, the numeric keypad and
 * Insert - and nothing else about them matters. */
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

/* Every slot is addressed by number in the registry, so a number that
 * moves is a player's bindings silently meaning something else. Slot 18 is
 * the Secondary action and must stay 18 now that the hotkeys sit behind it
 * rather than before it - it is no longer "the last slot" and no longer
 * derivable from the count, which is why it is checked directly. */
static void key_secondary_slot()
{
	check(KEY_SCHEME_ROLES, 34, "thirty-four roles since the hotkeys");
	check(KEY_ROLE_SECOND, 18, "the Secondary role is still slot 18");
	check(kModern[KEY_ROLE_SECOND], KEY_MOUSE_LEFT, "the Modern row's Secondary key is the left mouse button");

	// Disjoint is what kOther is for, and every new slot is one more chance to
	// break it by accident.
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
		for (int j = 0; j < KEY_SCHEME_ROLES; j++)
			check(kOther[i] == kModern[j], 0, "kOther shares no key with the Modern row");
}

/* Two things are worth a test rather than a comment: that the numbers are
 * the fifteen consecutive slots behind the Secondary one (a gap or an
 * overlap would make one registry name mean two roles), and that the
 * Modern row's keys are the keys specific\input.cpp used to read directly,
 * since Key(role) reads that key for an untouched registry. The engine's
 * own static_asserts say the same thing against the DIK_ names; this says
 * it against the numbers, where a player's registry lives. */
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
	// these three sweeps pass 1, covering the whole table. The inert case
	// is the block below.
	KeySchemeConflicts(kOther, kModern, got, 1, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 0, "a default key nobody claimed is free");

	// A player who never rebinds: the active set IS the default row, so every
	// default key is "claimed" - by the role that owns it. The fallback is
	// dead and costs nothing, because the primary binding is the same key.
	KeySchemeConflicts(kModern, kModern, got, 1, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 1, "an untouched set claims its own defaults");

	// One claim at a time, swept over all nineteen roles: only the role whose
	// DEFAULT key was taken may go silent, and no other.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 1, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "exactly the claimed default is flagged");
	}
}

/* An inert Secondary slot claims nothing. With "One key" the Secondary role
 * cannot produce a bit however it is bound, so its key must not silence
 * another role's default-key fallback; with "Separate" it is a binding like
 * any other and Core's rule applies to it unchanged. */
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

		KeySchemeConflicts(active, kModern, got, 1, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "live, the Secondary slot claims like any other");

		KeySchemeConflicts(active, kModern, got, 0, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], 0, "inert, the Secondary slot claims nothing");
	}

	// Every OTHER slot is untouched by the flag: a claim made by a live role
	// stands whether the Secondary role is offered or not.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 0, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "the flag moves only the Secondary slot");
	}

	// The engine's own case: with the Secondary slot at its default key, the
	// answer is the same in both settings, because neither default row holds
	// that key, which is the byte-identity claim this test makes.
	memcpy(active, kModern, sizeof(active));
	KeySchemeConflicts(active, kModern, got, 1, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		long other[KEY_SCHEME_ROLES];

		KeySchemeConflicts(active, kModern, other, 0, 1);
		check((int)other[i], (int)(i == KEY_ROLE_SECOND ? 0 : got[i]), "at the defaults only the inert slot itself differs");
	}
}

/* The five cheat slots are inert with Cheats off, one flag further than the
 * block above: with Cheats off the five rows are not offered and nothing
 * reads them, so a key bound to one of them may not silence another role's
 * default-key fallback. */
static void key_conflicts_cheats_inert()
{
	long got[KEY_SCHEME_ROLES];
	short active[KEY_SCHEME_ROLES];
	int i, r, c;

	// One cheat slot at a time holds another role's default key. Cheats on, it
	// silences that role; Cheats off, it silences nothing.
	for (c = KEY_ROLE_CHEAT_FLY; c <= KEY_ROLE_CHEAT_SKIP; c++)
		for (r = 0; r < KEY_SCHEME_ROLES; r++)
		{
			if (r == c)
				continue;

			memcpy(active, kOther, sizeof(active));
			active[c] = kModern[r];

			KeySchemeConflicts(active, kModern, got, 1, 1);

			for (i = 0; i < KEY_SCHEME_ROLES; i++)
				check((int)got[i], i == r ? 1 : 0, "live, a cheat slot claims like any other");

			KeySchemeConflicts(active, kModern, got, 1, 0);

			for (i = 0; i < KEY_SCHEME_ROLES; i++)
				check((int)got[i], 0, "with Cheats off a cheat slot claims nothing");
		}

	// Every slot that is not one of the five is untouched by the flag.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got, 1, 0);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "the Cheats flag moves only the five cheat slots");
	}

	// The engine's own case, and the byte-identity claim this fix makes: with
	// every slot at its default key the answer is the same with Cheats off as
	// with Cheats on for every role but the five themselves, because F1, F2,
	// F7, F8 and F11 appear in no other slot of the row.
	memcpy(active, kModern, sizeof(active));
	KeySchemeConflicts(active, kModern, got, 1, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		long other[KEY_SCHEME_ROLES];
		const int cheat = (i >= KEY_ROLE_CHEAT_FLY && i <= KEY_ROLE_CHEAT_SKIP);

		KeySchemeConflicts(active, kModern, other, 1, 0);
		check((int)other[i], cheat ? 0 : (int)got[i], "at the defaults only the five inert slots themselves differ");
	}
}

/* The mouse's five buttons are key codes above every scan code. What has to
 * hold: the codes are the five numbers the engine's mask is built on, the
 * predicate and the bit agree with them, the names are the page's, the Modern
 * row carries the three buttons of the Remaster's own keyboard page and stays
 * sound, and the conflict rule treats a button exactly as a key. */
static void key_mouse()
{
	long got[KEY_SCHEME_ROLES];
	short active[KEY_SCHEME_ROLES];
	const char* const english[KEY_MOUSE_BUTTONS] = { "Mouse L", "Mouse R", "Mouse M", "Mouse 4", "Mouse 5" };
	const char* const german[KEY_MOUSE_BUTTONS] = { "Maus L", "Maus R", "Maus M", "Maus 4", "Maus 5" };
	int i, n, r;

	check(KEY_MOUSE_FIRST, 0x100, "mouse: the first button code is 0x100, one above every scan code");
	check(KEY_MOUSE_LEFT, KEY_MOUSE_FIRST + 0, "mouse: left is button 0, Raw Input's first");
	check(KEY_MOUSE_RIGHT, KEY_MOUSE_FIRST + 1, "mouse: right is button 1");
	check(KEY_MOUSE_MIDDLE, KEY_MOUSE_FIRST + 2, "mouse: middle is button 2");
	check(KEY_MOUSE_X1, KEY_MOUSE_FIRST + 3, "mouse: X1 is button 3");
	check(KEY_MOUSE_X2, KEY_MOUSE_FIRST + 4, "mouse: X2 is button 4");
	check(KEY_MOUSE_BUTTONS, 5, "mouse: five buttons");

	// The predicate over the whole short range a bind slot can hold.
	{
		int wrong_is = 0;
		int wrong_bit = 0;

		for (n = -32768; n <= 32767; n++)
		{
			const int want = (n >= 0x100 && n <= 0x104) ? 1 : 0;

			if (KeySchemeIsMouse(n) != want)
				wrong_is++;

			if (!want && KeySchemeMouseBit(n) != 0)
				wrong_bit++;
		}

		check(wrong_is, 0, "mouse: of all 65536 shorts only 0x100..0x104 are buttons");
		check(wrong_bit, 0, "mouse: and a code that is no button has no bit");
	}

	check(KeySchemeIsMouse(0xFF), 0, "mouse: the last scan code is a key");
	check(KeySchemeIsMouse(0x105), 0, "mouse: the code past X2 is nothing");

	for (n = 0; n < KEY_MOUSE_BUTTONS; n++)
	{
		check((int)KeySchemeMouseBit(KEY_MOUSE_FIRST + n), 1 << n, "mouse: button n is bit n of the mask");
		check(strcmp(KeySchemeMouseName(KEY_MOUSE_FIRST + n, 0), english[n]), 0,
			"mouse: the button's name on every page but the German one");
		check(strcmp(KeySchemeMouseName(KEY_MOUSE_FIRST + n, 1), german[n]), 0,
			"mouse: and on the German page");
		check((int)strlen(KeySchemeMouseName(KEY_MOUSE_FIRST + n, 0)) <= 7, 1,
			"mouse: no name is longer than the seven letters of Mouse L");
	}

	check(KeySchemeMouseName(0x52, 0) == 0, 1, "mouse: a key has no button name");

	// The Modern row: the Remaster's own three buttons, nothing else moved.
	check(kModern[9], KEY_MOUSE_RIGHT, "mouse: the Modern row draws on the right button");
	check(kModern[11], KEY_MOUSE_MIDDLE, "mouse: looks on the middle button");
	check(kModern[KEY_ROLE_SECOND], KEY_MOUSE_LEFT, "mouse: and takes the secondary action on the left one");
	check(KeySchemeRowSound(kModern), 1, "mouse: the Modern row with three buttons is sound");

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		const int button = (i == 9 || i == 11 || i == KEY_ROLE_SECOND);

		check(KeySchemeIsMouse(kModern[i]), button, "mouse: exactly the three slots hold a button");

		// Q, R and Numpad 0 have left the row altogether.
		check(kModern[i] != 0x10 && kModern[i] != 0x13 && kModern[i] != 0x52, 1,
			"mouse: Q, R and Numpad 0 are gone from the Modern row");
	}

	// The conflict rule, with a button claiming: a role bound to another
	// role's default button silences that role's fallback and no other.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		if (!KeySchemeIsMouse(kModern[r]))
			continue;

		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];
		KeySchemeConflicts(active, kModern, got, 1, 1);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "mouse: a button bound elsewhere claims exactly its own default");
	}

	// And a button that is no default claims nothing.
	memcpy(active, kOther, sizeof(active));
	active[0] = KEY_MOUSE_X2;
	KeySchemeConflicts(active, kModern, got, 1, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 0, "mouse: X2, on no default, claims nothing");

	// The untouched Modern set claims its own buttons, as it claims its keys.
	KeySchemeConflicts(kModern, kModern, got, 1, 1);
	check((int)got[9] + (int)got[11] + (int)got[KEY_ROLE_SECOND], 3,
		"mouse: an untouched set claims its own buttons");

	// With "One key" the inert Secondary slot's left button claims nothing:
	// the left button is on no other slot, so nothing changes at the defaults.
	KeySchemeConflicts(kModern, kModern, got, 0, 1);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], i == KEY_ROLE_SECOND ? 0 : 1, "mouse: at One key only the Secondary slot itself goes free");
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

/* Two rows of sixteen that are deliberately not copies of the engine's
 * tables: the seat swaps whatever it is given and has no business knowing
 * what a number means, so a twin of jDefPS5DInput here would only be a
 * second place to keep in step. 100+ and 200+ say "some binding" and
 * nothing more; the -1 in each is the one value with a meaning - an
 * unbound role - and it has to travel like any other. */
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

	// The count and the Secondary slot are checked in pad_role_slots():
	// the Secondary role is no longer the last of them, so "the last
	// slot" is no longer a way to say 15.

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

/* The numbers are checked here for the reason KEY_ROLE_SECOND is: they are
 * what the registry addresses a stored binding by, so a slot that moved
 * would be a player's binding that moved with it. */
static void pad_role_slots(void)
{
	check(JOY_ROLES, 28, "twenty-eight pad roles");
	check(JOY_ROLE_SECOND, 15, "the Secondary pad role is still slot 15");
	// Core's own three: a registry calls these slots JFlr/JSav/JLod and
	// SJFlr/SJSav/SJLod, so the numbers are not ours to move, and the
	// chord mask below is written out of role names.
	check(JOY_ROLE_FLARE, 6, "the flare is slot 6");
	check(JOY_ROLE_SAVE, 11, "quick save is slot 11");
	check(JOY_ROLE_LOAD, 12, "quick load is slot 12");
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

/* The chord rule, swept rather than sampled: the two ends are the
 * interesting cases here as everywhere else in this file - an unbound
 * role, an unbound Modifier, and the Modifier's own button. */
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

	/* With no Modifier bound the rule is the expression it replaces: for
	 * every button, a plain role fires exactly when its own button is
	 * down and nothing else does, which is what Key(jLayout[n] + 256)
	 * answered. */
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

/* The eight default tables, against the one rule they all have to keep: no
 * two roles on one pair, and no plain role on the Modifier's own button.
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

	// JoyDefaultRecord's own Modern override, in one place there and in
	// one place here: the medipacks move to the d-pad's own bits.
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

	/* The four one-key rows themselves are fifteen entries in
	 * specific\input.cpp and are our own pad layout, not generated here, so
	 * what there is to check is what JoyDefaultRecord puts behind them at
	 * "One key": every role from the Secondary on unbound, and a chord mask
	 * of 0. With no Modifier bound the last two of the rules above are
	 * vacuous, which is the point: at One key no chord can exist at all. The
	 * fifteen here stand in for whichever row the pad is on. */
	for (i = 0; i < 15; i++)
		one[i] = i;

	for (i = 15; i < JOY_ROLES; i++)
		one[i] = -1;

	check((int)one[JOY_ROLE_MODIFIER], -1, "One key: no Modifier on any path");
	pad_table_sound(one, 0, 0, "One key, Tank");
	pad_table_sound(one, 0, 1, "One key, Modern");

	/* The four Separate tables are bit indices; these are the seven bound
	 * pairs, read back out of the DirectInput row by name (specific\input.cpp
	 * names the bits: 0 Square, 1 Cross, 2 Circle, 3 Triangle, 5 R1, 10 L3,
	 * 11 R3, 13 Touchpad). */
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

/* ----------------------------------------------------- the mouse's rows -- */

/* A file of the tree, whole, as text; 0 when it cannot be read. The test runs
 * from its own folder, three levels under the repository. */
static char* tree_file(const char* rel)
{
	FILE* f = 0;
	char* text;
	long size;

	if (fopen_s(&f, rel, "rb") || !f)
		return 0;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = (char*)malloc((size_t)size + 1);

	if (text)
	{
		size = (long)fread(text, 1, (size_t)size, f);
		text[size] = 0;
	}

	fclose(f);
	return text;
}

// How often `what` occurs in `text`.
static int tree_count(const char* text, const char* what)
{
	int n = 0;

	for (const char* p = strstr(text, what); p; p = strstr(p + 1, what))
		n++;

	return n;
}

/* The quoted strings of the one line that starts at `at`, up to `max`. */
static int tree_quoted(const char* at, char out[][64], int max)
{
	int n = 0;

	while (*at && *at != '\n' && n < max)
	{
		if (*at++ != '"')
			continue;

		int k = 0;

		while (*at && *at != '"' && *at != '\n' && k < 63)
			out[n][k++] = *at++;

		out[n++][k] = 0;

		if (*at == '"')
			at++;
	}

	return n;
}

/* ------------------------------------------------ the pad scheme repair -- */

/* The four One-key rows of specific\input.cpp, which live there as static
 * tables and so cannot be linked here. They are stated again and held
 * against the file's own lines (pad_scheme_repair below), so a change there
 * is a failure here and not a stale copy. */
static const long kOnePS5DInput[15] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14 };
static const long kOnePS5XInput[15] = { 5, 3, 4, 2, 0, 1, 7, 10, 11, 6, 8, 9, -1, 18, 19 };
static const long kOneXbox[15] = { 11, 10, 5, 2, 0, 3, 8, 4, 1, 6, 7, -1, -1, 18, 19 };
static const long kOneGeneric[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };

// specific\input.cpp's JOY_RSTICK_BITS (Modern's) and JOY_DPAD_BITS (Tank's).
static const long kBlockedModern = 0x000F0000L;
static const long kBlockedTank = 0x00F00000L;

/* JoyDefaultRecord at One key: the fifteen, every role behind them unbound,
 * and under Modern the medipacks on the d-pad. The chord mask is 0. */
static void one_key_record(const long* row, int modern, long* out)
{
	for (int i = 0; i < JOY_ROLES; i++)
		out[i] = i < 15 ? row[i] : -1;

	if (modern)
	{
		out[13] = 20;
		out[14] = 21;
	}
}

/* ... and under Separate: the row of game\pad_scheme.h, with the same
 * Modern override. */
static void separate_record(const long* row, int modern, long* out)
{
	for (int i = 0; i < JOY_ROLES; i++)
		out[i] = row[i];

	if (modern)
	{
		out[13] = 20;
		out[14] = 21;
	}
}

/* The repair without the Modifier exemption, on arrays instead of the
 * engine's globals: the reference the rule has to equal wherever no
 * Modifier is bound. */
static void repair_before(long* layout, long* chord, const long* want, long want_chord,
	const long* other, long blocked)
{
	for (int i = 0; i < JOY_ROLES; i++)
	{
		const long broken = layout[i] >= 0 && layout[i] <= JOY_BIT_MAX &&
			((1L << layout[i]) & blocked) != 0;
		const long theirs = other[i] != want[i] && layout[i] == other[i];

		if (!broken && !theirs)
			continue;

		layout[i] = want[i];
		PadChordSet(chord, i, (want_chord >> i) & 1);

		if (want[i] >= 0)
		{
			const long wc = (want_chord >> i) & 1;

			for (int k = 0; k < JOY_ROLES; k++)
				if (k != i && layout[k] == want[i] && ((*chord >> k) & 1) == wc)
				{
					const long kc = (want_chord >> k) & 1;

					layout[k] = (want[k] >= 0 && !PadPairTaken(layout, *chord, want[k], kc, k))
						? want[k] : -1;
					PadChordSet(chord, k, layout[k] >= 0 ? kc : 0);
				}
		}
	}
}

// One flip of the Controls row on a set: into Modern (modern 1) or into Tank.
static void flip(long* layout, long* chord, long* yield, const long* wantRec, const long* otherRec,
	long want_chord, int modern, long* yielded, long* seated)
{
	PadSchemeRepair(layout, chord, yield, wantRec, want_chord, otherRec,
		modern ? kBlockedModern : kBlockedTank, yielded, seated);
}

static int same_set(const long* a, const long* b)
{
	for (int i = 0; i < JOY_ROLES; i++)
		if (a[i] != b[i])
			return 0;

	return 1;
}

/* Whether a set keeps the rule every default table keeps (pad_table_sound):
 * no two roles on one pair and no plain role on the Modifier's button. */
static int set_sound(const long* t, long chord)
{
	for (int i = 0; i < JOY_ROLES; i++)
	{
		if (t[i] < 0)
			continue;

		if (i != JOY_ROLE_MODIFIER && t[i] == t[JOY_ROLE_MODIFIER] && !((chord >> i) & 1))
			return 0;

		for (int k = i + 1; k < JOY_ROLES; k++)
			if (t[k] == t[i] && ((chord >> k) & 1) == ((chord >> i) & 1))
				return 0;
	}

	return 1;
}

/* The scheme repair (PadSchemeRepair, game\pad_scheme.h) as the Controls row
 * runs it, flip by flip. The case the checks are built on: at One key on the
 * PS5 DirectInput page the Modifier sits on the touchpad (bit 13) with chords
 * on some hotkeys, and Tank's Small Medipack default on that page is bit 13
 * as well. A flip into Tank moves the medipack off the blocked d-pad; the
 * cases prove that the Modifier keeps its bit, that the medipack yields, and
 * that the flip back seats it on the d-pad again. */
static void pad_scheme_repair()
{
	const long rows[4][15] =
	{
		{ 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14 },
		{ 5, 3, 4, 2, 0, 1, 7, 10, 11, 6, 8, 9, -1, 18, 19 },
		{ 11, 10, 5, 2, 0, 3, 8, 4, 1, 6, 7, -1, -1, 18, 19 },
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 }
	};
	static const char* const pages[4] =
	{
		"no Modifier, PS5 DirectInput: every set repairs as before",
		"no Modifier, PS5 XInput: every set repairs as before",
		"no Modifier, Xbox XInput: every set repairs as before",
		"no Modifier, generic DirectInput: every set repairs as before"
	};
	char* input = tree_file("../../../TOMB5 Code/TOMB5/specific/input.cpp");
	long tank[JOY_ROLES], modern[JOY_ROLES];
	long t[JOY_ROLES], r[JOY_ROLES];
	long chord, rchord, yield, yielded, seated;
	int i, p, role, bit, m;

	check(input != 0, 1, "repair: specific\\input.cpp is readable from here");

	if (input)
	{
		check(strstr(input, "static const long jDefPS5DInput[15] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14 };") != 0,
			1, "repair: the PS5 DirectInput One-key row is the engine's");
		check(strstr(input, "static const long jDefPS5XInput[15] = { 5, 3, 4, 2, 0, 1, 7, 10, 11, 6, 8, 9, -1, 18, 19 };") != 0,
			1, "repair: the PS5 XInput One-key row is the engine's");
		check(strstr(input, "static const long jDefXbox[15] = { 11, 10, 5, 2, 0, 3, 8, 4, 1, 6, 7, -1, -1, 18, 19 };") != 0,
			1, "repair: the Xbox One-key row is the engine's");
		check(strstr(input, "static const long jDefGeneric[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };") != 0,
			1, "repair: the generic One-key row is the engine's");
		check(strstr(input, "#define JOY_RSTICK_BITS\t\t0x000F0000L") != 0, 1,
			"repair: Modern blocks the right stick, as here");
		check(strstr(input, "#define JOY_DPAD_BITS\t\t0x00F00000L") != 0, 1,
			"repair: Tank blocks the d-pad, as here");
		check(strstr(input, "out[13] = 20;") != 0 && strstr(input, "out[14] = 21;") != 0, 1,
			"repair: Modern puts the medipacks on d-pad up and down, as here");
		check(strstr(input, "PadSchemeRepair(jLayout, &jChord, &jYield, want, want_chord, other, blocked,") != 0,
			1, "repair: ApplyJoyScheme runs this very rule on the active set");
		free(input);
	}

	for (i = 0; i < 15; i++)
	{
		check((int)kOnePS5DInput[i], (int)rows[0][i], "repair: the rows table is the four above");
		check((int)kOnePS5XInput[i], (int)rows[1][i], "repair: the rows table is the four above");
		check((int)kOneXbox[i], (int)rows[2][i], "repair: the rows table is the four above");
		check((int)kOneGeneric[i], (int)rows[3][i], "repair: the rows table is the four above");
	}

	/* (a) One key, PS5 DirectInput page, Modern seated: the medipacks on the
	 * d-pad, the Modifier on the touchpad, three hotkeys as chords. */
	const long hotChord = (1L << (JOY_ROLE_WEAPON_FIRST + 0)) | (1L << JOY_ROLE_CHEAT_GUNS) |
		(1L << JOY_ROLE_CHEAT_SKIP);

	one_key_record(kOnePS5DInput, 0, tank);
	one_key_record(kOnePS5DInput, 1, modern);

	for (i = 0; i < JOY_ROLES; i++)
		t[i] = modern[i];

	t[JOY_ROLE_MODIFIER] = 13;
	t[JOY_ROLE_WEAPON_FIRST + 0] = 1;	// Modifier+Cross
	t[JOY_ROLE_CHEAT_GUNS] = 11;		// Modifier+R3
	t[JOY_ROLE_CHEAT_SKIP] = 10;		// Modifier+L3
	chord = hotChord;
	yield = 0;

	check(set_sound(t, chord), 1, "(a) the starting set is sound");

	for (i = 0; i < JOY_ROLES; i++)
		r[i] = t[i];

	rchord = chord;

	flip(t, &chord, &yield, tank, modern, 0, 0, &yielded, &seated);

	check((int)t[JOY_ROLE_MODIFIER], 13, "(a) into Tank: the Modifier stays on the touchpad");
	check((int)chord, (int)hotChord, "(a) into Tank: the chord flags are unchanged");
	check((int)t[13], -1, "(a) into Tank: Small Medipack yields to the Modifier, unbound");
	check((int)yield, (int)(1L << 13), "(a) into Tank: and is remembered in the yield mask");
	check((int)yielded, (int)(1L << 13), "(a) into Tank: the call reports that one role yielding");
	check((int)seated, 0, "(a) into Tank: and seats nothing");
	check((int)t[14], 14, "(a) into Tank: Large Medipack goes to its Tank default, 14");
	check((int)t[JOY_ROLE_WEAPON_FIRST + 0], 1, "(a) into Tank: the pistols chord is kept");
	check((int)t[JOY_ROLE_CHEAT_GUNS], 11, "(a) into Tank: the guns chord is kept");
	check((int)t[JOY_ROLE_CHEAT_SKIP], 10, "(a) into Tank: the skip chord is kept");

	for (i = 0; i < 13; i++)
		check((int)t[i], (int)tank[i], "(a) into Tank: roles 0-12 as they were");

	check(set_sound(t, chord), 1, "(a) into Tank: the set is sound");

	// The same flip without the Modifier exemption, as the reference.
	repair_before(r, &rchord, tank, 0, modern, kBlockedTank);
	check((int)r[JOY_ROLE_MODIFIER], -1, "(a) the rule before: the Modifier fell to unbound");
	check((int)r[13], 13, "(a) the rule before: and the medipack took the touchpad");

	/* A second repair in Tank - the one a split flip or a load runs - changes
	 * nothing: the default is still the Modifier's. */
	{
		long t2[JOY_ROLES], c2 = chord, y2 = yield, yd, st;

		for (i = 0; i < JOY_ROLES; i++)
			t2[i] = t[i];

		flip(t2, &c2, &y2, tank, modern, 0, 0, &yd, &st);
		check(same_set(t2, t) && c2 == chord && y2 == yield && !yd && !st, 1,
			"(a) a second repair in Tank is idempotent");
	}

	/* (b) back into Modern. */
	flip(t, &chord, &yield, modern, tank, 0, 1, &yielded, &seated);

	check((int)t[13], 20, "(b) back into Modern: Small Medipack seated on d-pad up again");
	check((int)t[14], 21, "(b) back into Modern: Large Medipack on d-pad down");
	check((int)t[JOY_ROLE_MODIFIER], 13, "(b) back into Modern: the Modifier on the touchpad");
	check((int)chord, (int)hotChord, "(b) back into Modern: the chord flags are unchanged");
	check((int)yield, 0, "(b) back into Modern: nothing left waiting");
	check((int)seated, (int)(1L << 13), "(b) back into Modern: the call reports the medipack seated");
	check((int)yielded, 0, "(b) back into Modern: and nothing yielding");

	{
		long want_back[JOY_ROLES];

		for (i = 0; i < JOY_ROLES; i++)
			want_back[i] = modern[i];

		want_back[JOY_ROLE_MODIFIER] = 13;
		want_back[JOY_ROLE_WEAPON_FIRST + 0] = 1;
		want_back[JOY_ROLE_CHEAT_GUNS] = 11;
		want_back[JOY_ROLE_CHEAT_SKIP] = 10;
		check(same_set(t, want_back), 1, "(b) the round trip gives the starting set back whole");
	}

	/* The player binds the waiting role himself before the next flip: it
	 * keeps his binding and leaves the mask. */
	for (i = 0; i < JOY_ROLES; i++)
		t[i] = tank[i];

	t[JOY_ROLE_MODIFIER] = 13;
	t[13] = 15;
	chord = 0;
	yield = 1L << 13;
	flip(t, &chord, &yield, modern, tank, 0, 1, &yielded, &seated);
	check((int)t[13], 15, "a role the player bound since keeps his binding");
	check((int)yield, 0, "and leaves the yield mask");
	check((int)seated, 0, "and is not reported as seated");

	/* The Modifier moves away under Tank: the next repair finds the medipack's
	 * Tank default free and seats it there. */
	for (i = 0; i < JOY_ROLES; i++)
		t[i] = tank[i];

	t[JOY_ROLE_MODIFIER] = 15;
	t[13] = -1;
	chord = 0;
	yield = 1L << 13;
	flip(t, &chord, &yield, tank, modern, 0, 0, &yielded, &seated);
	check((int)t[13], 13, "a freed default seats the waiting role even in the same scheme");
	check((int)yield, 0, "and empties the mask");

	/* (c) the same two flips with no Modifier bound: the rule before's
	 * results, pinned - Small Medipack 13 under Tank, 20 under Modern. */
	for (i = 0; i < JOY_ROLES; i++)
		t[i] = modern[i];

	t[JOY_ROLE_WEAPON_FIRST + 0] = 1;
	t[JOY_ROLE_CHEAT_GUNS] = 11;
	t[JOY_ROLE_CHEAT_SKIP] = 10;
	chord = hotChord;
	yield = 0;

	for (i = 0; i < JOY_ROLES; i++)
		r[i] = t[i];

	rchord = chord;

	flip(t, &chord, &yield, tank, modern, 0, 0, &yielded, &seated);
	repair_before(r, &rchord, tank, 0, modern, kBlockedTank);
	check((int)t[13], 13, "(c) no Modifier, into Tank: Small Medipack on 13, as before");
	check((int)t[14], 14, "(c) no Modifier, into Tank: Large Medipack on 14, as before");
	check((int)t[JOY_ROLE_MODIFIER], -1, "(c) no Modifier, into Tank: none appears");
	check(same_set(t, r) && chord == rchord, 1, "(c) no Modifier, into Tank: the whole set equals the rule before's");
	check((int)(yield | yielded | seated), 0, "(c) no Modifier, into Tank: no yield, nothing seated");

	flip(t, &chord, &yield, modern, tank, 0, 1, &yielded, &seated);
	repair_before(r, &rchord, modern, 0, tank, kBlockedModern);
	check((int)t[13], 20, "(c) no Modifier, back into Modern: Small Medipack on 20, as before");
	check((int)t[14], 21, "(c) no Modifier, back into Modern: Large Medipack on 21, as before");
	check(same_set(t, r) && chord == rchord, 1, "(c) no Modifier, back into Modern: the whole set equals the rule before's");
	check((int)(yield | yielded | seated), 0, "(c) no Modifier, back into Modern: no yield, nothing seated");

	/* (d) the Separate set, parked while One key is active, is not touched by
	 * a One-key repair - the repair gets the active set and nothing else - and
	 * its own flips give what the rule before gave: its Tank medipacks are
	 * unbound, so nothing lands on its Modifier. */
	{
		const long sep[JOY_ROLES] = JOY_SEP_PS5_DINPUT;
		long active[JOY_ROLES], parked[JOY_ROLES], kept[JOY_ROLES];
		long sepTank[JOY_ROLES], sepModern[JOY_ROLES];
		long pc, pk, yd, st, ay = 0, ac = hotChord;

		one_key_record(kOnePS5DInput, 1, active);
		active[JOY_ROLE_MODIFIER] = 13;
		active[JOY_ROLE_WEAPON_FIRST + 0] = 1;
		active[JOY_ROLE_CHEAT_GUNS] = 11;
		active[JOY_ROLE_CHEAT_SKIP] = 10;
		separate_record(sep, 1, parked);

		for (i = 0; i < JOY_ROLES; i++)
			kept[i] = parked[i];

		pc = JOY_SEP_CHORD_DINPUT;
		pk = pc;
		flip(active, &ac, &ay, tank, modern, 0, 0, &yd, &st);
		flip(active, &ac, &ay, modern, tank, 0, 1, &yd, &st);
		check(same_set(parked, kept) && pc == pk, 1, "(d) the repair is handed the active set only: the parked copy is as it was");

		separate_record(sep, 0, sepTank);
		separate_record(sep, 1, sepModern);

		for (m = 0; m < 2; m++)
		{
			long s[JOY_ROLES], sr[JOY_ROLES], sc = JOY_SEP_CHORD_DINPUT, src = sc, sy = 0;

			separate_record(sep, m, s);

			for (i = 0; i < JOY_ROLES; i++)
				sr[i] = s[i];

			PadSchemeRepair(s, &sc, &sy, m ? sepTank : sepModern, JOY_SEP_CHORD_DINPUT,
				m ? sepModern : sepTank, m ? kBlockedTank : kBlockedModern, &yd, &st);
			repair_before(sr, &src, m ? sepTank : sepModern, JOY_SEP_CHORD_DINPUT,
				m ? sepModern : sepTank, m ? kBlockedTank : kBlockedModern);
			check(same_set(s, sr) && sc == src, 1, "(d) a Separate flip equals the rule before's");
			check((int)(sy | yd | st), 0, "(d) and yields and seats nothing");
			check((int)s[JOY_ROLE_MODIFIER], 13, "(d) its Modifier stays on the touchpad");
			check((int)s[13], m ? -1 : 20, "(d) its Small Medipack: -1 under Tank, 20 under Modern");
		}
	}

	/* (e) the XInput pages bind no Modifier: both flips from both defaults
	 * give the rule before's results, the medipacks on the right stick under
	 * Tank and on the d-pad under Modern. */
	for (p = 1; p <= 2; p++)
		for (m = 0; m < 2; m++)
		{
			one_key_record(rows[p], 0, tank);
			one_key_record(rows[p], 1, modern);

			for (i = 0; i < JOY_ROLES; i++)
				t[i] = r[i] = m ? tank[i] : modern[i];

			chord = rchord = yield = 0;
			flip(t, &chord, &yield, m ? modern : tank, m ? tank : modern, 0, m, &yielded, &seated);
			repair_before(r, &rchord, m ? modern : tank, 0, m ? tank : modern,
				m ? kBlockedModern : kBlockedTank);
			check(same_set(t, r) && chord == rchord, 1, "(e) XInput page: a flip equals the rule before's");
			check((int)(yield | yielded | seated), 0, "(e) XInput page: no yield, nothing seated");
			check((int)t[13], m ? 20 : 18, "(e) XInput page: Small Medipack 18 under Tank, 20 under Modern");
			check((int)t[14], m ? 21 : 19, "(e) XInput page: Large Medipack 19 under Tank, 21 under Modern");
		}

	/* The sweep behind (c) and (e): on every page, from each scheme's default
	 * into the other, with every role put on every bit (and unbound) in turn
	 * and no Modifier bound, the new rule gives exactly the old one's set and
	 * flags and never yields. */
	for (p = 0; p < 4; p++)
	{
		one_key_record(rows[p], 0, tank);
		one_key_record(rows[p], 1, modern);

		for (m = 0; m < 2; m++)
			for (role = 0; role < JOY_ROLES; role++)
			{
				if (role == JOY_ROLE_MODIFIER)
					continue;

				for (bit = -1; bit <= JOY_BIT_MAX; bit++)
				{
					int same = 1;

					for (i = 0; i < JOY_ROLES; i++)
						t[i] = m ? tank[i] : modern[i];

					t[role] = bit;
					chord = hotChord;

					for (i = 0; i < JOY_ROLES; i++)
						r[i] = t[i];

					rchord = chord;
					yield = 0;
					flip(t, &chord, &yield, m ? modern : tank, m ? tank : modern, 0, m,
						&yielded, &seated);
					repair_before(r, &rchord, m ? modern : tank, 0, m ? tank : modern,
						m ? kBlockedModern : kBlockedTank);

					if (!same_set(t, r) || chord != rchord || yield || yielded || seated)
						same = 0;

					check(same, 1, pages[p]);
				}
			}
	}

	/* And with a Modifier bound, on every page and every bit neither scheme
	 * blocks: a flip into either scheme and back never moves the Modifier,
	 * leaves every chord flag as it was, keeps the set sound, and the
	 * round trip gives the starting set back. */
	for (p = 0; p < 4; p++)
	{
		one_key_record(rows[p], 0, tank);
		one_key_record(rows[p], 1, modern);

		for (m = 0; m < 2; m++)
			for (bit = 0; bit < 16; bit++)
			{
				long start[JOY_ROLES];

				for (i = 0; i < JOY_ROLES; i++)
					t[i] = m ? modern[i] : tank[i];

				// The capture's own rule: the role on that button gives it up.
				for (i = 0; i < JOY_ROLES; i++)
					if (t[i] == bit)
						t[i] = -1;

				t[JOY_ROLE_MODIFIER] = bit;
				chord = 0;
				yield = 0;

				for (i = 0; i < JOY_ROLES; i++)
					start[i] = t[i];

				flip(t, &chord, &yield, m ? tank : modern, m ? modern : tank, 0, !m,
					&yielded, &seated);
				check((int)t[JOY_ROLE_MODIFIER], bit, "Modifier bound: a flip never moves it");
				check((int)chord, 0, "Modifier bound: a flip keeps the chord flags");
				check(set_sound(t, chord), 1, "Modifier bound: the set stays sound");

				flip(t, &chord, &yield, m ? modern : tank, m ? tank : modern, 0, m,
					&yielded, &seated);
				check((int)t[JOY_ROLE_MODIFIER], bit, "Modifier bound: nor does the flip back");
				check(same_set(t, start), 1, "Modifier bound: the round trip gives the set back");
			}
	}
}

/* The two mouse rows live in game\menu_frame.cpp's Controls table, which
 * needs the engine to run and so cannot be linked here. What can be checked
 * without it is what the tree says: the table's rows in order, the two rows'
 * kind, gate and save class, their five strings in game\gameflow.cpp's
 * table, and the registry names in tomb5\tomb5.cpp. The slider's own step is
 * game\menu_nav.h's MenuSliderStep, swept whole. */
static void controls_mouse_rows()
{
	static const char* const want[12] =
	{
		"Key bindings", "View", "Modes", "Control type", "Action", "Control layout",
		"Camera speed", "Dead zone", "Mouse sensitivity", "Invert mouse", "Targeting",
		"Ammotype hotkeys"
	};
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char label[16][64];
	char line[16][512];
	int rows = 0, i, v, d;

	check(frame && flow && opts, 1, "rows: the three engine files are readable from here");

	if (!frame || !flow || !opts)
	{
		free(frame);
		free(flow);
		free(opts);
		return;
	}

	// The Controls table, row by row.
	{
		const char* p = strstr(frame, "static const menu_row menu_rows_controls[] =");
		const char* end = p ? strstr(p, "};") : 0;

		check(p != 0 && end != 0, 1, "rows: the Controls table is found");

		while (p && end && p < end && rows < 16)
		{
			const char* nl = strchr(p, '\n');
			const char* s = p;

			while (*s == '\t' || *s == ' ')
				s++;

			if (!strncmp(s, "MENU_", 5) && s < end)
			{
				char q[2][64];
				int k = 0;

				tree_quoted(s, q, 1);
				strcpy_s(label[rows], sizeof(label[rows]), q[0]);

				while (s[k] && s[k] != '\n' && s[k] != '\r' && k < 511)
				{
					line[rows][k] = s[k];
					k++;
				}

				line[rows][k] = 0;
				rows++;
			}

			p = nl ? nl + 1 : 0;
		}
	}

	check(rows, 12, "rows: the Controls table holds twelve rows, the page's maximum");

	for (i = 0; i < rows && i < 12; i++)
		check(strcmp(label[i], want[i]), 0, "rows: the Controls rows stand in this order");

	if (rows == 12)
	{
		check(!strncmp(line[8], "MENU_SLIDER(\"Mouse sensitivity\"", 31), 1,
			"rows: Mouse sensitivity is a slider, right after Dead zone");
		check(strstr(line[8], "MenuNumMouseSensitivity, MenuStepMouseSensitivity") != 0, 1,
			"rows: it reads and steps tomb5.mouse_sensitivity through its own two functions");
		check(strstr(line[8], "MENU_SAVE_TOMB5") != 0, 1, "rows: it is saved with the tomb5 settings");
		check(strstr(line[8], "MenuEnabledModern") != 0, 1, "rows: and greyed under Tank");

		check(!strncmp(line[9], "MENU_VALUE_IF(\"Invert mouse\"", 28), 1,
			"rows: Invert mouse is a gated value row, right after the slider");
		check(strstr(line[9], "MenuValInvertMouse, MenuStepInvertMouse") != 0, 1,
			"rows: it shows and flips tomb5.mouse_invert through its own two functions");
		check(strstr(line[9], "MENU_SAVE_TOMB5") != 0, 1, "rows: it is saved with the tomb5 settings");
		check(strstr(line[9], "MenuEnabledModern") != 0, 1, "rows: and greyed under Tank");

		check(strstr(line[7], "MenuEnabledModern") != 0 && strstr(line[6], "MenuEnabledModern") != 0, 1,
			"rows: the same gate as Camera speed and Dead zone above them");
	}

	check(strstr(frame, "MenuSliderStep((int)tomb5.mouse_sensitivity, (int)dir, 100)") != 0, 1,
		"rows: the slider steps through MenuSliderStep over 0..100");
	check(strstr(frame, "tomb5.mouse_invert ? \"on\" : \"off\"") != 0, 1,
		"rows: the invert shows the table's own on and off");

	// The five strings of each label: English first, German translated, the
	// other three repeating the English, as every other row of the table.
	{
		static const char* const names[2] = { "Mouse sensitivity", "Invert mouse" };
		static const char* const deutsch[2] = { "Mausempfindlichkeit", "Maus invertieren" };

		for (i = 0; i < 2; i++)
		{
			char key[64];
			char col[5][64];
			const char* at;
			int n;

			snprintf(key, sizeof(key), "{ \"%s\",", names[i]);
			at = strstr(flow, key);
			check(at != 0, 1, "rows: the label has its row in CustomText's table");

			if (!at)
				continue;

			n = tree_quoted(at, col, 5);
			check(n, 5, "rows: five columns, one per language");
			check(strcmp(col[0], names[i]), 0, "rows: the English column is the label itself");
			check(strcmp(col[2], deutsch[i]), 0, "rows: the German column is its translation");
			check(!strcmp(col[1], names[i]) && !strcmp(col[3], names[i]) && !strcmp(col[4], names[i]), 1,
				"rows: French, Italian and Spanish repeat the English");
			check((int)strlen(col[2]) <= (int)strlen("Kamerageschwindigkeit"), 1,
				"rows: the German label is no longer than the longest the column holds");
			check(tree_count(flow, key), 1, "rows: and the label has exactly one row");
		}

		check(strstr(flow, "{ \"on\",") != 0 && strstr(flow, "{ \"off\",") != 0, 1,
			"rows: the invert's two values are in the table already");
	}

	// The registry: written on a first run, read, and saved - three times each.
	check(tree_count(opts, "sprintf(buf, \"mouse_sens\");"), 3,
		"rows: mouse_sens is written on a first run, read and saved");
	check(tree_count(opts, "sprintf(buf, \"mouse_invert\");"), 3,
		"rows: mouse_invert likewise");
	check(strstr(opts, "REG_ReadLong(buf, tomb5.mouse_sensitivity, 50);") != 0, 1,
		"rows: a registry without mouse_sens reads as the middle, 50");
	check(strstr(opts, "REG_ReadBool(buf, tomb5.mouse_invert, 0);") != 0, 1,
		"rows: and one without mouse_invert as off");

	// The slider's step, swept over its whole range and one past each end.
	for (v = -1; v <= 101; v++)
		for (d = -1; d <= 1; d++)
		{
			int w = v;

			if (d < 0 && v > 0)
				w = v - 1;

			if (d > 0 && v < 100)
				w = v + 1;

			check(MenuSliderStep(v, d, 100), w, "rows: one frame of the slider is one step, and the ends hold");
		}

	check(MenuSliderStep(0, -1, 100), 0, "rows: the bottom holds");
	check(MenuSliderStep(100, 1, 100), 100, "rows: the top holds");
	check(MenuSliderStep(50, 0, 100), 50, "rows: no direction, no step");
	check(MenuSliderStep(50, -7, 100), 49, "rows: a direction is a sign, not a size");

	free(frame);
	free(flow);
	free(opts);
}

int main()
{
	printf("MenuTest - game\\menu_nav.h, game\\key_scheme.h, game\\pad_scheme.h\n\n");

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
	key_conflicts_cheats_inert();
	key_mouse();
	key_seat();
	pad_seat();
	pad_role_slots();
	pad_chord_rule();
	pad_default_tables();
	pad_scheme_repair();
	controls_mouse_rows();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

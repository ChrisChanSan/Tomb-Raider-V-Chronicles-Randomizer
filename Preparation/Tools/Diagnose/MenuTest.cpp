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
 * Build with bmenu.bat, run with no arguments from this folder: the last
 * nine families (pad_scheme_repair, controls_mouse_rows,
 * graphics_softbody_row, graphics_water_row, graphics_shadow_row,
 * hd_switch_rows, graphics_hd_row, display_gamma_row and slider_numbers)
 * read engine files as text, by their paths relative to it, because the
 * tables and rows they check live in files that cannot be linked here.
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
#include <math.h>

#include "../../../TOMB5 Code/TOMB5/game/menu_nav.h"
#include "../../../TOMB5 Code/TOMB5/game/key_scheme.h"
#include "../../../TOMB5 Code/TOMB5/game/pad_scheme.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_shadow.h"	// the Shadow mode row's numbers and blob mode
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_switch.h"	// HDSwitchSet
// game\menu_frame.h's MENU_SLIDER_TOP and MenuSliderHeld, compiled as the
// engine compiles them; the header needs the engine's own headers first.
#include "../../../TOMB5 Code/TOMB5/tomb5/pch.h"
#include "../../../TOMB5 Code/TOMB5/game/menu_frame.h"

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
 * the three F keys the engine keeps for itself, the numeric keypad and
 * Insert - and nothing else about them matters. */
static const short kOther[KEY_SCHEME_ROLES] =
{
	0x14, 0x15, 0x16, 0x17, 0x18,	/* T Y U I O           */
	0x22, 0x23, 0x24, 0x25, 0x26,	/* G H J K L           */
	0x2F, 0x30, 0x31, 0x32,			/* V B N M             */
	0x08, 0x09,						/* 7 8                 */
	0x3D, 0x3E, 0x44, 0x13,			/* F3 F4 F10 R         */
	0x45, 0x47, 0x48, 0x49, 0x4A,	/* NumLock Num7 Num8 Num9 Num- */
	0x4B, 0x4C, 0x4D, 0x4E,			/* Num4 Num5 Num6 Num+ */
	0x4F, 0x50, 0x51, 0x53,			/* Num1 Num2 Num3 Num. */
	0xD2, 0x10						/* Insert Q            */
};

/* Every slot is addressed by number in the registry, so a number that
 * moves is a player's bindings silently meaning something else. Slot 18 is
 * the Secondary action and must stay 18 now that the hotkeys sit behind it
 * rather than before it - it is no longer "the last slot" and no longer
 * derivable from the count, which is why it is checked directly. */
static void key_secondary_slot()
{
	check(KEY_SCHEME_ROLES, 35, "thirty-five roles with the HD/SD switch");
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
	check(KEY_ROLE_HD_SWITCH, KEY_ROLE_CHEAT_SKIP + 1,
		"the HD/SD switch follows skip level");
	check(KEY_ROLE_HD_SWITCH, KEY_SCHEME_ROLES - 1,
		"and the HD/SD switch is the last slot");
	check(kModern[KEY_ROLE_HD_SWITCH], 0x58, "the Modern HD/SD key is F12");

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
 * row carries the three buttons the camera hand rests on and stays sound, and
 * the conflict rule treats a button exactly as a key. */
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

	// The Modern row: the camera hand's three buttons, nothing else moved.
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
	check(JOY_ROLES, 29, "twenty-nine pad roles");
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
	check(JOY_ROLE_HD_SWITCH, JOY_ROLE_CHEAT_SKIP + 1,
		"the HD/SD switch follows the cheats");
	check(JOY_ROLE_HD_SWITCH, JOY_ROLES - 1, "and is the last of the roles");
	check(JOY_ROLE_HD_SWITCH, 28, "the HD/SD switch is slot 28");
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
			i == JOY_ROLE_HD_SWITCH ||
			(i >= JOY_ROLE_WEAPON_FIRST && i < JOY_ROLE_WEAPON_FIRST + 4)) ? 1 : 0;

		check((int)((JOY_SEP_CHORD_DINPUT >> i) & 1), want,
			"and all eight of them are chords");
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

	// The HD/SD switch: Modifier+Options on the two DirectInput-numbered
	// rows, where plain Options (bit 9) is pause; none on the XInput rows.
	check((int)kSepPS5DInput[JOY_ROLE_HD_SWITCH], 9, "HD/SD: Modifier+Options");
	check((int)kSepGeneric[JOY_ROLE_HD_SWITCH], 9, "the generic row the same");
	check((int)kSepPS5DInput[10], 9, "beside plain Options, pause");
	check((int)kSepPS5XInput[JOY_ROLE_HD_SWITCH], -1, "PS5 XInput: no HD/SD");
	check((int)kSepXbox[JOY_ROLE_HD_SWITCH], -1, "Xbox: no HD/SD either");
}

/* ----------------------------------------------------- the mouse's rows -- */

/* Opens a file of the tree by its path relative to Diagnose\, the folder the
 * EXE is built in: first beside the EXE (_get_pgmptr, the EXE's full path),
 * so that a start from any working directory reads the same sources, then
 * relative to the working directory. 0 when neither opens. */
static FILE* tree_open(const char* rel)
{
	FILE* f = 0;
	char* exe = 0;
	char path[1024];

	if (!_get_pgmptr(&exe) && exe)
	{
		const char* cut = strrchr(exe, '\\');
		const size_t dir = cut ? (size_t)(cut - exe) + 1 : 0;

		if (dir && dir + strlen(rel) < sizeof(path))
		{
			memcpy(path, exe, dir);
			strcpy_s(path + dir, sizeof(path) - dir, rel);

			if (!fopen_s(&f, path, "rb") && f)
				return f;
		}
	}

	if (fopen_s(&f, rel, "rb") || !f)
		return 0;

	return f;
}

/* A file of the tree, whole, as text; 0 when it cannot be read. The test sits
 * in its own folder, three levels under the repository. */
static char* tree_file(const char* rel)
{
	FILE* f = tree_open(rel);
	char* text;
	long size;

	if (!f)
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

	check(strstr(frame, "MenuSliderStep(\r\n\t\t(int)tomb5.mouse_sensitivity, "
		"(int)dir, MENU_SLIDER_TOP)") != 0 && MENU_SLIDER_TOP == 100, 1,
		"rows: the slider steps through MenuSliderStep over 0..100");
	check(strstr(frame, "tomb5.mouse_invert ? \"on\" : \"off\"") != 0, 1,
		"rows: the invert shows the table's own on and off");

	// The five strings of each label: English first, German translated, the
	// other three repeating the English, as every other row of the table.
	{
		static const char* const names[2] = { "Mouse sensitivity", "Invert mouse" };
		static const char* const german[2] =
			{ "Mausempfindlichkeit", "Maus invertieren" };

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
			check(strcmp(col[2], german[i]), 0,
				"rows: the German column is its translation");
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

/* ------------------------------------------------- the Body physics row -- */

/* The row of the chest's soft motion lives in game\menu_frame.cpp's Graphics
 * table, which cannot be linked here, so what is checked is what the tree
 * says, the way controls_mouse_rows reads the Controls table: the table's
 * fifteen rows in order and the split they page into, the row's kind, save
 * class and lack of a gate, the four values it shows, their five strings
 * in game\gameflow.cpp's table, the registry name in tomb5\tomb5.cpp, and
 * the draw's reset on a change in specific\hd\hd_laradraw.cpp. The step is
 * game\menu_nav.h's MenuSliderStep over 0..3, swept whole. */
static void graphics_softbody_row()
{
	static const char* const want[15] =
	{
		"Display", "HD textures", "Body physics", "HD water", "Reflections",
		"Fog", "Distance fog", "Shimmer", "Mono screen style", "Shadow mode",
		"PSX skies", "Static lighting", "UW Effect", "Look transparency",
		"Footprints"
	};
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char* draw = tree_file("../../../TOMB5 Code/TOMB5/specific/hd/hd_laradraw.cpp");
	char label[16][64];
	char line[16][512];
	int rows = 0, i, v, d;

	check(frame && flow && opts && draw, 1, "softbody row: the four engine files are readable from here");

	if (!frame || !flow || !opts || !draw)
	{
		free(frame);
		free(flow);
		free(opts);
		free(draw);
		return;
	}

	// The Graphics table, row by row.
	{
		const char* p = strstr(frame, "static const menu_row menu_rows_graphics[] =");
		const char* end = p ? strstr(p, "};") : 0;

		check(p != 0 && end != 0, 1, "softbody row: the Graphics table is found");

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

	check(rows, 15, "softbody row: the Graphics table holds fifteen rows");

	for (i = 0; i < rows && i < 15; i++)
		check(strcmp(label[i], want[i]), 0,
			"softbody row: the Graphics rows stand in this order");

	if (rows == 15)
	{
		check(!strncmp(line[2], "MENU_VALUE(\"Body physics\"", 25), 1,
			"softbody row: Body physics is an ungated value row, "
			"right after HD textures");
		check(strstr(line[2], "MenuValBodyPhysics, MenuStepBodyPhysics")
			!= 0, 1, "softbody row: it shows and steps tomb5.hd_softbody "
			"through its own two functions");
		check(strstr(line[2], "MENU_SAVE_TOMB5") != 0, 1,
			"softbody row: it is saved with the tomb5 settings");
		check(!strncmp(line[1], "MENU_VALUE(\"HD textures\"", 24) &&
			!strncmp(line[5], "MENU_VALUE(\"Fog\"", 16), 1,
			"softbody row: ungated like its neighbours, which no page row "
			"greys while HD is off");
	}

	// Fifteen rows at MENU_MAX_ROWS 12 and no fixed row: two pages, eight
	// and seven, and the second starts at Mono screen style.
	check(MenuPageCount(15, 12), 2, "softbody row: Graphics pages in two");
	check(MenuRowsOnPage(15, 12, 0), 8,
		"softbody row: eight rows on the first page");
	check(MenuRowsOnPage(15, 12, 1), 7, "softbody row: seven on the second");
	check(MenuPageFirstRow(15, 12, 1), 8,
		"softbody row: the second page starts at row 8");
	check(rows == 15 &&
		!strcmp(label[MenuPageFirstRow(15, 12, 1)], "Mono screen style"), 1,
		"softbody row: which is Mono screen style, and Body physics is on "
		"the first page beside HD textures");

	// What the row shows and how it steps.
	check(strstr(frame, "tomb5.hd_softbody == 1 ? \"Subtle\" :") != 0, 1,
		"softbody row: 1 shows Subtle");
	check(strstr(frame, "tomb5.hd_softbody == 2 ? \"Normal\" :") != 0, 1,
		"softbody row: 2 shows Normal");
	check(strstr(frame, "tomb5.hd_softbody == 3 ? \"Strong\" : \"off\"") != 0, 1,
		"softbody row: 3 shows Strong, and 0 or anything out of range the table's own off");
	check(strstr(frame, "ulong old = tomb5.hd_softbody > 3 ? 0 : tomb5.hd_softbody;") != 0, 1,
		"softbody row: a stored value out of range steps from off");
	check(strstr(frame, "MenuSliderStep((int)old, (int)dir, 3)") != 0, 1,
		"softbody row: the step is MenuSliderStep over 0..3");

	// The strings: the label and the two new values with five columns each,
	// and the two values the table already had.
	{
		static const char* const names[3] = { "Body physics", "Subtle", "Normal" };
		static const char* const german[3] =
			{ "K\\xF6rperphysik", "Dezent", "Normal" };

		for (i = 0; i < 3; i++)
		{
			char key[64];
			char col[5][64];
			const char* at;
			int n;

			snprintf(key, sizeof(key), "{ \"%s\",", names[i]);
			at = strstr(flow, key);
			check(at != 0, 1, "softbody row: the string has its row in CustomText's table");

			if (!at)
				continue;

			n = tree_quoted(at, col, 5);
			check(n, 5, "softbody row: five columns, one per language");
			check(strcmp(col[0], names[i]), 0, "softbody row: the English column is the string itself");
			check(strcmp(col[2], german[i]), 0,
				"softbody row: the German column is its translation");
			check(!strcmp(col[1], names[i]) && !strcmp(col[3], names[i]) && !strcmp(col[4], names[i]), 1,
				"softbody row: French, Italian and Spanish repeat the English");
			check((int)strlen(col[2]) <= (int)strlen("Kamerageschwindigkeit"), 1,
				"softbody row: the German string is no longer than the longest label the column holds");
			check(tree_count(flow, key), 1, "softbody row: and the string has exactly one row");
		}

		static const char* const had[2] = { "off", "Strong" };
		static const char* const hadDe[2] = { "aus", "Stark" };

		for (i = 0; i < 2; i++)
		{
			char key[64];
			char col[5][64];
			const char* at;

			snprintf(key, sizeof(key), "{ \"%s\",", had[i]);
			at = strstr(flow, key);
			check(at != 0 && tree_quoted(at, col, 5) == 5 && !strcmp(col[2], hadDe[i]), 1,
				"softbody row: off and Strong are in the table already, aus and Stark");
		}
	}

	// The registry: written on a first run, read, and saved; a missing or
	// out-of-range value reads as off.
	check(tree_count(opts, "sprintf(buf, \"hd_softbody\");"), 3,
		"softbody row: hd_softbody is written on a first run, read and saved");
	check(strstr(opts, "REG_ReadLong(buf, tomb5.hd_softbody, 0);") != 0, 1,
		"softbody row: a registry without hd_softbody reads as off");
	check(strstr(opts, "if (tomb5.hd_softbody > 3)\r\n\t\t\ttomb5.hd_softbody = 0;") != 0
		|| strstr(opts, "if (tomb5.hd_softbody > 3)\n\t\t\ttomb5.hd_softbody = 0;") != 0, 1,
		"softbody row: and one out of range as off");

	// A change of the row reaches the running level: the draw compares the
	// option with the strength it used last and starts again from rest.
	{
		const char* at = strstr(draw, "if (strength != g_soft_logged && g_soft_logged >= 0)");
		const char* reset = at ? strstr(at, "soft_reset();") : 0;

		check(at != 0 && reset != 0 && reset - at < 64, 1,
			"softbody row: the draw resets to rest on the next frame after a change of the option");
	}

	// The step, swept over the four states and one past each end.
	for (v = -1; v <= 4; v++)
		for (d = -1; d <= 1; d++)
		{
			int w = v + d;

			if (w < 0 || w > 3)
				w = v < 0 ? (d > 0 ? 0 : v) : v > 3 ? (d < 0 ? 3 : v) : v;

			check(MenuSliderStep(v, d, 3), w, "softbody row: one press is one state, and off and Strong hold");
		}

	free(frame);
	free(flow);
	free(opts);
	free(draw);
}

/* ------------------------------------------------------ the HD water row -- */

/* The HD water row on the Graphics page, read from the tree as the Body
 * physics row is: its table line (an ungated value row right under Body
 * physics, saved with the tomb5 settings), the two expressions of its value
 * and its step, its string's five columns in game\gameflow.cpp, the
 * registry name in tomb5\tomb5.cpp (written on a first run, read with on as
 * the default, saved), and the two places in specific\hd\hd_level.cpp
 * that read the option, the water sheets' build and its log line. */
static void graphics_water_row()
{
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char* level = tree_file(
		"../../../TOMB5 Code/TOMB5/specific/hd/hd_level.cpp");

	check(frame && flow && opts && level, 1,
		"water row: the four engine files are readable from here");

	if (!frame || !flow || !opts || !level)
	{
		free(frame);
		free(flow);
		free(opts);
		free(level);
		return;
	}

	// The table line, right under Body physics.
	{
		const char* body = strstr(frame, "MENU_VALUE(\"Body physics\"");
		const char* row = strstr(frame, "MENU_VALUE(\"HD water\", "
			"MenuValHdWater, MenuStepHdWater, MENU_SAVE_TOMB5),");
		const char* between = body ? strchr(body, '\n') : 0;

		check(tree_count(frame, "MENU_VALUE(\"HD water\""), 1,
			"water row: one row of that name");
		check(row != 0, 1, "water row: an ungated value row with its own two "
			"functions, saved with the tomb5 settings");
		check(body != 0 && row != 0 && between != 0 &&
			strstr(between + 1, "MENU_VALUE(") == row, 1,
			"water row: the next row after Body physics");
	}

	// What it shows and how it steps: on and off, one press toggles.
	check(strstr(frame, "CustomText(tomb5.hd_water ? \"on\" : \"off\")")
		!= 0, 1, "water row: it shows the table's own on and off");
	check(strstr(frame, "tomb5.hd_water = !tomb5.hd_water;") != 0, 1,
		"water row: a press either way toggles it");

	// The string, five columns.
	{
		char col[5][64];
		const char* at = strstr(flow, "{ \"HD water\",");

		check(at != 0 && tree_quoted(at, col, 5) == 5, 1,
			"water row: the label has its row in CustomText's table");

		if (at)
		{
			check(!strcmp(col[0], "HD water") && !strcmp(col[1], "HD water") &&
				!strcmp(col[3], "HD water") && !strcmp(col[4], "HD water"),
				1, "water row: English, French, Italian and Spanish alike");
			check(strcmp(col[2], "HD-Wasser"), 0,
				"water row: the German column is its translation");
			check(tree_count(flow, "{ \"HD water\","), 1,
				"water row: and the string has exactly one row");
		}
	}

	// The registry, and the engine's two readers.
	check(tree_count(opts, "sprintf(buf, \"hd_water\");"), 3,
		"water row: hd_water is written on a first run, read and saved");
	check(strstr(opts, "REG_ReadBool(buf, tomb5.hd_water, 1);") != 0, 1,
		"water row: a registry without hd_water reads as on");
	check(tree_count(level, "tomb5.hd_water ? 1 : 0"), 2, "water row: the "
		"water sheets' build and its log line read the option");

	free(frame);
	free(flow);
	free(opts);
	free(level);
}

/* --------------------------------------------------- the Reflections row -- */

/* The Reflections row on the Graphics page, read from the tree as the HD
 * water row is: its table line (an ungated value row right under HD water,
 * which writes its own key, so it saves nothing more), what it shows - the
 * registry's hd_marble, absent read as on - and its step, which writes the
 * other value to that key and hands it to the renderer at once with the
 * ceiling kept (HDRenderMarbleSet, planes 0); in tomb5\tomb5.cpp the same
 * key written on a first run and read with on as the default; and the
 * round trip of a value through the row's write, the start's read and the
 * row's own reading, a stored value over 1 included. */
static void graphics_reflections_row()
{
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char* render = tree_file(
		"../../../TOMB5 Code/TOMB5/specific/hd/hd_render.cpp");
	int v;

	check(frame && opts && render, 1,
		"reflections row: the three engine files are readable from here");

	if (!frame || !opts || !render)
	{
		free(frame);
		free(opts);
		free(render);
		return;
	}

	// The table line, right under HD water.
	{
		const char* water = strstr(frame, "MENU_VALUE(\"HD water\"");
		const char* row = strstr(frame, "MENU_VALUE(\"Reflections\", "
			"MenuValReflect, MenuStepReflect, MENU_SAVE_NONE),");
		const char* between = water ? strchr(water, '\n') : 0;

		check(tree_count(frame, "MENU_VALUE(\"Reflections\""), 1,
			"reflections row: one row of that name");
		check(row != 0, 1, "reflections row: an ungated value row with its "
			"own two functions, saving nothing more than its own key");
		check(water != 0 && row != 0 && between != 0 &&
			strstr(between + 1, "MENU_VALUE(") == row, 1,
			"reflections row: the next row after HD water");
	}

	// What it shows: the key as the start reads it, absent as on.
	check(strstr(frame, "ulong on = 1;\r\n\r\n\tREG_PeekLong(\"tomb5\", "
		"\"hd_marble\", on);") != 0 || strstr(frame, "ulong on = 1;\n\n\t"
		"REG_PeekLong(\"tomb5\", \"hd_marble\", on);") != 0, 1,
		"reflections row: it reads hd_marble, absent as on");
	check(strstr(frame, "CustomText(MenuReflectOn() ? \"on\" : \"off\")")
		!= 0, 1, "reflections row: it shows the table's own on and off");

	// The step: the other value, written and handed over at once.
	check(strstr(frame, "const bool on = !MenuReflectOn();") != 0 &&
		strstr(frame, "sprintf(key, \"hd_marble\");") != 0 &&
		strstr(frame, "REG_WriteLong(key, on ? 1 : 0);") != 0, 1,
		"reflections row: a press writes the other value to hd_marble");
	check(strstr(frame, "HDRenderMarbleSet(on ? 1 : 0, 0);") != 0 &&
		strstr(render, "\tif (planes > 0)\r\n\t\tg_marble_cap = "
		"HDMarblePlanesCap(planes);") != 0, 1,
		"reflections row: and hands it over at once, the ceiling kept");

	// The start: written on a first run, read with on as the default.
	check(tree_count(opts, "sprintf(buf, \"hd_marble\");"), 2,
		"reflections row: hd_marble is written on a first run and read");
	check(strstr(opts, "REG_ReadLong(buf, marble, 1);") != 0 &&
		strstr(opts, "HDRenderMarbleSet(marble > 1 ? 1 : (int)marble, "
		"(int)planes);") != 0, 1,
		"reflections row: the start reads it, absent as on, over 1 as on");

	// The round trip, the three expressions as the tree holds them.
	for (v = 0; v <= 7; v++)
	{
		const unsigned long stored = (unsigned long)v;
		const int started = stored > 1 ? 1 : (int)stored;
		const int shown = stored != 0 ? 1 : 0;
		const int pressed = !shown ? 1 : 0;
		const int again = pressed > 1 ? 1 : pressed;

		check(started == shown && again == !shown, 1,
			"reflections row: a stored value reads alike at the start and "
			"in the row, and a press flips it");
	}

	free(frame);
	free(opts);
	free(render);
}

/* -------------------------------------------------- the Shadow mode row -- */

/* What the row shows and steps to: the expressions of game\menu_frame.cpp's
 * MenuShadowModeShown, MenuValShadowMode and MenuStepShadowMode, stated again
 * here because that file cannot be linked. graphics_shadow_row holds each
 * one against the file's own line; HDShadowBlobMode and the mode numbers are
 * specific\hd\hd_shadow.h's own, included. */
static unsigned long shadow_row_shown(bool hd, unsigned long stored)
{
	return hd ? stored : (unsigned long)HDShadowBlobMode((long)stored);
}

static const char* shadow_row_text(bool hd, unsigned long stored)
{
	const unsigned long shown = shadow_row_shown(hd, stored);

	return shown == 1 ? "Original" : shown == 2 ? "Circle" :
		shown == 3 ? "Faded circle" : shown == 5 ? "Projected" : "PSX";
}

static unsigned long shadow_row_step(bool hd, unsigned long stored, long dir)
{
	const unsigned long shown = shadow_row_shown(hd, stored);
	const unsigned long top = hd ? HD_SHADOW_MODE_PROJECTED : HD_SHADOW_MODE_PSX;

	if (dir > 0)
		return shown >= top ? 1 : shown + 1;

	if (dir < 0)
		return shown <= 1 ? top : shown - 1;

	return stored;
}

// The four-state row of base, which the row with HD off keeps for every
// stored value but 5.
static unsigned long shadow_row_step_four(unsigned long v, long dir)
{
	if (dir > 0)
		return v >= 4 ? 1 : v + 1;

	if (dir < 0)
		return v <= 1 ? 4 : v - 1;

	return v;
}

static const char* shadow_row_text_four(unsigned long v)
{
	return v == 1 ? "Original" : v == 2 ? "Circle" : v == 3 ? "Faded circle" : "PSX";
}

/* The Shadow mode row on the Graphics page and its fifth state, Projected.
 * Read from the tree: the row's table line, the three functions' lines in
 * game\menu_frame.cpp (HD is HDEnabled and HDDrawn, a stored 5 shown and
 * stepped through HDShadowBlobMode without HD, the top 5 with HD and 4
 * without, the sound,
 * no write but the two steps), the new string's five columns in
 * game\gameflow.cpp, and specific\hd\hd_laradraw.cpp's per-frame decision,
 * which reads the option every frame and forgets the following in a frame
 * without the projected shadow. Swept: both cases over the five states, the
 * wrap both ways, the texts, every other stored value against base's rule,
 * and hd_shadow.h's own HDShadowFollowStep snapping after that reset. */
static void graphics_shadow_row()
{
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* draw = tree_file("../../../TOMB5 Code/TOMB5/specific/hd/hd_laradraw.cpp");
	unsigned long v;
	int i, d;

	check(frame && flow && draw, 1, "shadow row: the three engine files are readable from here");

	if (!frame || !flow || !draw)
	{
		free(frame);
		free(flow);
		free(draw);
		return;
	}

	// The table line: the same kind, functions and save class as before.
	check(tree_count(frame, "MENU_VALUE(\"Shadow mode\", MenuValShadowMode, MenuStepShadowMode, MENU_SAVE_TOMB5),"), 1,
		"shadow row: an ungated value row, saved with the tomb5 settings");
	check(strstr(frame, "#include \"../specific/hd/hd_level.h\"") != 0
		&& strstr(frame, "#include \"../specific/hd/hd_shadow.h\"") != 0, 1,
		"shadow row: the menu reads HDEnabled and HDShadowBlobMode from the engine's own headers");

	// The three functions, as one block and its two halves.
	{
		const char* begin = strstr(frame, "static ulong MenuShadowModeShown(bool hd)");
		const char* step = begin ? strstr(begin, "static bool MenuStepShadowMode(long, long dir)") : 0;
		const char* end = step ? strstr(step, "static const char* MenuValFog") : 0;

		check(begin != 0 && step != 0 && end != 0, 1, "shadow row: its three functions stand together");

		if (begin && step && end)
		{
			const size_t showLen = (size_t)(step - begin);
			const size_t stepLen = (size_t)(end - step);
			char* show = (char*)malloc(showLen + 1);
			char* walk = (char*)malloc(stepLen + 1);

			memcpy(show, begin, showLen);
			show[showLen] = 0;
			memcpy(walk, step, stepLen);
			walk[stepLen] = 0;

			check(strstr(show, "return hd ? tomb5.shadow_mode : (ulong)HDShadowBlobMode((long)tomb5.shadow_mode);") != 0, 1,
				"shadow row: with HD the stored value is shown, without it what the blob draws");
			check(strstr(show, "const ulong shown = "
				"MenuShadowModeShown(HDEnabled() && HDDrawn());") != 0, 1,
				"shadow row: the shown value asks the build and the draw");
			check(strstr(show, "CustomText(shown == 1 ? \"Original\" : shown == 2 ? \"Circle\" :") != 0, 1,
				"shadow row: 1 shows Original and 2 Circle, through CustomText");
			check(strstr(show, "shown == 3 ? \"Faded circle\" : shown == 5 ? \"Projected\" : \"PSX\"));") != 0, 1,
				"shadow row: 3 shows Faded circle, 5 Projected, everything else PSX");
			check(tree_count(show, "tomb5.shadow_mode ="), 0,
				"shadow row: showing the row writes nothing, so a stored 5 survives a visit with HD off");

			check(strstr(walk, "const bool hd = HDEnabled() && HDDrawn();")
				!= 0, 1, "shadow row: a step asks the build and the draw once");
			check(strstr(walk, "const ulong shown = MenuShadowModeShown(hd);") != 0, 1,
				"shadow row: and steps from the value it shows");
			check(strstr(walk, "const ulong top = hd ? HD_SHADOW_MODE_PROJECTED : HD_SHADOW_MODE_PSX;") != 0, 1,
				"shadow row: five states with HD, four without");
			check(strstr(walk, "tomb5.shadow_mode = shown >= top ? 1 : shown + 1;") != 0, 1,
				"shadow row: right past the top wraps to Original");
			check(strstr(walk, "tomb5.shadow_mode = shown <= 1 ? top : shown - 1;") != 0, 1,
				"shadow row: left below Original wraps to the top");
			check(tree_count(walk, "tomb5.shadow_mode ="), 2, "shadow row: the two steps are the only writes");
			check(tree_count(walk, "SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);"), 2,
				"shadow row: each step plays the menu's select sound, as the rows beside it");
			check(strstr(walk, "\treturn 0;") != 0, 1, "shadow row: no direction, no change");

			free(show);
			free(walk);
		}
	}

	// The numbers and the blob mode the row is built on.
	check(HD_SHADOW_MODE_ORIGINAL * 10000 + HD_SHADOW_MODE_CIRCLE * 1000 + HD_SHADOW_MODE_FADED * 100
		+ HD_SHADOW_MODE_PSX * 10 + HD_SHADOW_MODE_PROJECTED, 12345, "shadow row: the five mode numbers are 1..5");

	for (v = 0; v <= 9; v++)
		check(HDShadowBlobMode((long)v), v == 5 ? 3 : (int)v,
			"shadow row: the blob draws a stored 5 as the faded circle and every other value as stored");

	// HD on: five states, the wrap both ways, the texts.
	{
		static const char* const text[6] = { 0, "Original", "Circle", "Faded circle", "PSX", "Projected" };

		for (v = 1; v <= 5; v++)
		{
			check(strcmp(shadow_row_text(true, v), text[v]), 0, "shadow row: with HD each of the five states has its text");
			check((int)shadow_row_step(true, v, 1), v == 5 ? 1 : (int)v + 1, "shadow row: with HD right goes 1 2 3 4 5 1");
			check((int)shadow_row_step(true, v, -1), v == 1 ? 5 : (int)v - 1, "shadow row: with HD left goes 1 5 4 3 2 1");
			check((int)shadow_row_step(true, v, 0), (int)v, "shadow row: with HD no direction keeps the state");
		}

		unsigned long seen = 0, at = 1;

		for (i = 0; i < 5; i++)
		{
			seen |= 1ul << at;
			at = shadow_row_step(true, at, 1);
		}

		check((int)seen, 0x3E, "shadow row: with HD five presses visit all five states");
		check((int)at, 1, "shadow row: and come back to the first");
	}

	// HD off: four states, a stored 5 shown and stepped as Faded circle.
	{
		static const char* const text[5] = { 0, "Original", "Circle", "Faded circle", "PSX" };
		unsigned long seen = 0, at = 1;

		for (i = 0; i < 4; i++)
		{
			seen |= 1ul << at;
			at = shadow_row_step(false, at, 1);
		}

		check((int)seen, 0x1E, "shadow row: without HD four presses visit the four blobs");
		check((int)at, 1, "shadow row: and come back to the first");

		for (v = 1; v <= 4; v++)
		{
			check(strcmp(shadow_row_text(false, v), text[v]), 0, "shadow row: without HD each of the four states has its text");
			check((int)shadow_row_step(false, v, 1), v == 4 ? 1 : (int)v + 1, "shadow row: without HD right goes 1 2 3 4 1");
			check((int)shadow_row_step(false, v, -1), v == 1 ? 4 : (int)v - 1, "shadow row: without HD left goes 1 4 3 2 1");
		}

		for (v = 1; v <= 4; v++)
			for (d = -1; d <= 1; d++)
				check((int)shadow_row_step(false, v, d) <= 4, 1, "shadow row: without HD every step stays in 1..4");

		check(strcmp(shadow_row_text(false, 5), "Faded circle"), 0, "shadow row: without HD a stored 5 shows Faded circle");
		check((int)shadow_row_step(false, 5, 1), 4, "shadow row: and right from it gives PSX");
		check((int)shadow_row_step(false, 5, -1), 2, "shadow row: and left gives Circle");
		check((int)shadow_row_step(false, 5, 0), 5, "shadow row: and no direction leaves the stored 5 alone");

		static const unsigned long odd[13] = { 0, 1, 2, 3, 4, 6, 7, 8, 9, 100, 0x7FFFFFFFul, 0x80000000ul, 0xFFFFFFFFul };

		for (i = 0; i < 13; i++)
		{
			check(strcmp(shadow_row_text(false, odd[i]), shadow_row_text_four(odd[i])), 0,
				"shadow row: without HD every value but 5 shows as base's row shows it");

			for (d = -1; d <= 1; d++)
				check(shadow_row_step(false, odd[i], d) == shadow_row_step_four(odd[i], d), 1,
					"shadow row: without HD every value but 5 steps as base's row steps it");
		}

		// With HD only the values below 1 differ from base's row: left wraps
		// to the new top.
		for (i = 0; i < 13; i++)
			for (d = -1; d <= 1; d++)
				if (odd[i] < 1 || odd[i] > 5)
					check(shadow_row_step(true, odd[i], d) == (odd[i] == 0 && d < 0 ? 5 : shadow_row_step_four(odd[i], d)), 1,
						"shadow row: with HD a value outside 1..5 steps as base's row, but left from 0 to the top");
	}

	// The string: one row, five columns, the German its translation.
	{
		char col[5][64];
		const char* at = strstr(flow, "{ \"Projected\",");

		check(at != 0 && tree_quoted(at, col, 5) == 5, 1, "shadow row: Projected has its row in CustomText's table");

		if (at)
		{
			check(strcmp(col[0], "Projected"), 0, "shadow row: the English column is the string itself");
			check(strcmp(col[2], "Projiziert"), 0, "shadow row: the German column is Projiziert");
			check(!strcmp(col[1], "Projected") && !strcmp(col[3], "Projected") && !strcmp(col[4], "Projected"), 1,
				"shadow row: French, Italian and Spanish repeat the English");
		}

		check(tree_count(flow, "{ \"Projected\","), 1, "shadow row: and the string has exactly one row");

		at = strstr(flow, "{ \"Faded circle\",");
		check(at != 0 && tree_quoted(at, col, 5) == 5 && !strcmp(col[2], "Weicher Kreis"), 1,
			"shadow row: Faded circle is in the table already, Weicher Kreis");
		check(tree_count(flow, "{ \"PSX\",") + tree_count(flow, "{ \"Original\","), 0,
			"shadow row: PSX and Original have no row and fall back to the English");
	}

	// The running level: the draw reads the option every frame, forgets the
	// following in a frame without the projected shadow, and the next step
	// after that places the shadow at once.
	{
		const char* at = strstr(draw, "if (!g_shadow_frame.runs)");
		const char* reset = at ? strstr(at, "HDShadowFollowReset(&g_shadow_follow);") : 0;

		check(strstr(draw, "int gate = HDShadowGate((long)tomb5.shadow_mode,") != 0, 1,
			"shadow row: the draw reads the option in every frame's decision");
		check(at != 0 && reset != 0 && reset - at < 64, 1,
			"shadow row: a frame without the projected shadow forgets the following");
		check(strstr(draw, "HDShadowFollowStep(&g_shadow_follow, &f.target, reference, dt)") != 0, 1,
			"shadow row: a frame with it steps the following");

		HDShadowFollow f;
		HDShadowKey a = { { 0.6f, 0.8f, 0.0f }, 0.4f, 1.0f, 1.0f, 37.0f, 1 };
		HDShadowKey b = { { 0.0f, 1.0f, 0.0f }, 0.27f, 0.5f, 1.0f, 0.0f, 1 };
		const float P[3] = { 100.0f, -200.0f, 300.0f };

		memset(&f, 0, sizeof(f));
		HDShadowFollowReset(&f);
		check(HDShadowFollowStep(&f, &a, P, 1.0f / 60.0f), 1, "shadow row: the first frame with the shadow snaps");
		check(f.dir[0] == a.dir[0] && f.dir[1] == a.dir[1] && f.dir[2] == a.dir[2] && f.strength == a.strength, 1,
			"shadow row: to the target's direction and strength");
		check(HDShadowFollowStep(&f, &b, P, 1.0f / 60.0f), 0, "shadow row: the next frame glides");
		check(f.strength < a.strength && f.strength > b.strength, 1, "shadow row: part of the way to a new target");
		HDShadowFollowReset(&f);
		check(HDShadowFollowStep(&f, &b, P, 1.0f / 60.0f) && f.strength == b.strength, 1,
			"shadow row: and after a frame without it, the first frame with it snaps again");
	}

	free(frame);
	free(flow);
	free(draw);
}

/* ------------------------------------------------- the HD/SD switch row -- */

/* A role's number out of a bind row's third argument as menu_frame.cpp
 * writes it: a plain number, a role name, or a role name plus a number. */
static long bind_role(const char* text)
{
	static const struct { const char* name; long value; } names[] =
	{
		{ "KEY_ROLE_SECOND", KEY_ROLE_SECOND },
		{ "KEY_ROLE_MEDI_SMALL", KEY_ROLE_MEDI_SMALL },
		{ "KEY_ROLE_MEDI_LARGE", KEY_ROLE_MEDI_LARGE },
		{ "KEY_ROLE_SAVE", KEY_ROLE_SAVE },
		{ "KEY_ROLE_LOAD", KEY_ROLE_LOAD },
		{ "KEY_ROLE_WEAPON_FIRST", KEY_ROLE_WEAPON_FIRST },
		{ "KEY_ROLE_CHEAT_FLY", KEY_ROLE_CHEAT_FLY },
		{ "KEY_ROLE_CHEAT_GUNS", KEY_ROLE_CHEAT_GUNS },
		{ "KEY_ROLE_CHEAT_ITEMS", KEY_ROLE_CHEAT_ITEMS },
		{ "KEY_ROLE_CHEAT_HEAL", KEY_ROLE_CHEAT_HEAL },
		{ "KEY_ROLE_CHEAT_SKIP", KEY_ROLE_CHEAT_SKIP },
		{ "KEY_ROLE_HD_SWITCH", KEY_ROLE_HD_SWITCH },
		{ "JOY_ROLE_SECOND", JOY_ROLE_SECOND },
		{ "JOY_ROLE_MODIFIER", JOY_ROLE_MODIFIER },
		{ "JOY_ROLE_WEAPON_FIRST", JOY_ROLE_WEAPON_FIRST },
		{ "JOY_ROLE_CHEAT_FLY", JOY_ROLE_CHEAT_FLY },
		{ "JOY_ROLE_CHEAT_GUNS", JOY_ROLE_CHEAT_GUNS },
		{ "JOY_ROLE_CHEAT_ITEMS", JOY_ROLE_CHEAT_ITEMS },
		{ "JOY_ROLE_CHEAT_HEAL", JOY_ROLE_CHEAT_HEAL },
		{ "JOY_ROLE_CHEAT_SKIP", JOY_ROLE_CHEAT_SKIP },
		{ "JOY_ROLE_HD_SWITCH", JOY_ROLE_HD_SWITCH }
	};

	while (*text == ' ')
		text++;

	if (*text >= '0' && *text <= '9')
		return atol(text);

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
	{
		const size_t n = strlen(names[i].name);

		if (strncmp(text, names[i].name, n) || text[n] == '_' ||
			(text[n] >= 'A' && text[n] <= 'Z'))
			continue;

		const char* plus = strchr(text + n, '+');
		const char* close = strchr(text + n, ')');
		const int add = plus && (!close || plus < close);

		return names[i].value + (add ? atol(plus + 1) : 0);
	}

	return -100;
}

/* The bind rows of one list: the label and the role of every MENU_BIND line
 * between `head` and the table's closing brace. */
static int bind_rows(const char* frame, const char* head, char label[][64],
	long* role, int max)
{
	const char* p = strstr(frame, head);
	const char* end = p ? strstr(p, "};") : 0;
	int n = 0;

	while (p && end && p < end && n < max)
	{
		const char* s = p;
		const char* nl = strchr(p, '\n');

		while (*s == '\t' || *s == ' ')
			s++;

		if (!strncmp(s, "MENU_BIND(", 10) && s < end)
		{
			char q[2][64];
			const char* comma = s;

			tree_quoted(s, q, 1);
			strcpy_s(label[n], 64, q[0]);

			// The role is the third argument: past the second comma.
			for (int c = 0; c < 2 && comma; c++)
				comma = strchr(comma + 1, ',');

			role[n++] = comma ? bind_role(comma + 1) : -100;
		}

		p = nl ? nl + 1 : 0;
	}

	return n;
}

/* The rows MenuResolveKeybinds hands out: Secondary only under Separate, the
 * five cheats only with Cheats on - and nothing else held back. */
static int bind_shown(const long* role, int n, long second, long first,
	long last, int split, int cheats)
{
	int shown = 0;

	for (int i = 0; i < n; i++)
	{
		if (role[i] == second && !split)
			continue;

		if (role[i] >= first && role[i] <= last && !cheats)
			continue;

		shown++;
	}

	return shown;
}

/* A registry key in miniature: REG_ReadLong's contract (a miss writes the
 * default and says so) and REG_WriteLong, over names. */
struct TestStore
{
	char name[96][16];
	long value[96];
	int count;
};

static int store_read(TestStore* s, const char* name, long* value, long def)
{
	for (int i = 0; i < s->count; i++)
		if (!strcmp(s->name[i], name))
		{
			*value = s->value[i];
			return 1;
		}

	if (s->count < 96)
	{
		strcpy_s(s->name[s->count], 16, name);
		s->value[s->count++] = def;
	}

	*value = def;
	return 0;
}

static void store_write(TestStore* s, const char* name, long value)
{
	for (int i = 0; i < s->count; i++)
		if (!strcmp(s->name[i], name))
		{
			s->value[i] = value;
			return;
		}

	if (s->count < 96)
	{
		strcpy_s(s->name[s->count], 16, name);
		s->value[s->count++] = value;
	}
}

/* specific\registry.cpp's read of the Separate set from the Modifier on:
 * the twelve names with the set's own value as default, then SJHd through
 * PadSeatAppended. `defs` and `defChord` are JoySplitDefaults' answer. */
static void load_split(TestStore* s, long* set, long* chord,
	const long* defs, long defChord)
{
	static const char* const names[JOY_ROLES - JOY_ROLE_MODIFIER] =
	{
		"SJMod", "SJWp1", "SJWp2", "SJWp3", "SJWp4", "SJWp5", "SJWp6",
		"SJCt1", "SJCt2", "SJCt3", "SJCt4", "SJCt5", "SJHd"
	};
	const long hd = JOY_ROLE_HD_SWITCH;
	const char* hdName = names[hd - JOY_ROLE_MODIFIER];
	long i;

	for (i = 0; i < JOY_ROLES; i++)
		set[i] = defs[i];

	*chord = defChord;
	store_read(s, "SJChord", chord, *chord);

	for (i = JOY_ROLE_MODIFIER; i < hd; i++)
		store_read(s, names[i - JOY_ROLE_MODIFIER], &set[i], set[i]);

	const long def = set[hd];

	if (!store_read(s, hdName, &set[hd], def))
	{
		PadSeatAppended(set, chord, hd, def, (defChord >> hd) & 1);
		store_write(s, hdName, set[hd]);
		store_write(s, "SJChord", *chord);
	}
}

// Whether the role fires on this reading, out of a loaded set.
static int set_fires(const long* set, long chord, long role, unsigned long fire)
{
	return PadRoleFires(fire, set[role], (int)((chord >> role) & 1),
		set[JOY_ROLE_MODIFIER]);
}

/* The two lists of the Key bindings page out of game\menu_frame.cpp: one
 * HD / SD row each, the last, on its role, and the page's sizes. One key and
 * Cheats off is 27 keyboard rows (sixteen base rows, ten hotkeys, HD / SD)
 * and 23 pad rows (fifteen, the Modifier, six weapons, HD / SD). */
static void hd_rows_lists(const char* frame)
{
	const long ks = KEY_ROLE_SECOND, kf = KEY_ROLE_CHEAT_FLY;
	const long kl = KEY_ROLE_CHEAT_SKIP;
	const long js = JOY_ROLE_SECOND, jf = JOY_ROLE_CHEAT_FLY;
	const long jl = JOY_ROLE_CHEAT_SKIP;
	char label[48][64];
	long role[48];
	int n, i;

	n = bind_rows(frame, "static const menu_row menu_rows_keybinds_kbd[] =",
		label, role, 48);
	// Every role but pause and select (16, 17), which the page never offers.
	check(n, KEY_SCHEME_ROLES - 2, "hd rows: one keyboard row per role");
	check(n > 0 && !strcmp(label[n - 1], "HD / SD"), 1,
		"hd rows: HD / SD is the keyboard list's last row");
	check(n > 0 ? (int)role[n - 1] : -1, KEY_ROLE_HD_SWITCH,
		"hd rows: on KEY_ROLE_HD_SWITCH");

	for (i = 0; i < n; i++)
		check(role[i] >= 0 && role[i] < KEY_SCHEME_ROLES, 1,
			"hd rows: every keyboard role resolves");

	check(bind_shown(role, n, ks, kf, kl, 0, 0), 27, "hd rows: keyboard 27");
	check(bind_shown(role, n, ks, kf, kl, 1, 0), 28, "hd rows: Separate 28");
	check(bind_shown(role, n, ks, kf, kl, 0, 1), 32, "hd rows: Cheats 32");
	check(bind_shown(role, n, ks, kf, kl, 1, 1), 33, "hd rows: both 33");

	n = bind_rows(frame, "static const menu_row menu_rows_keybinds_joy[] =",
		label, role, 48);
	check(n, JOY_ROLES, "hd rows: one pad row per role");
	check(n > 0 && !strcmp(label[n - 1], "HD / SD"), 1,
		"hd rows: HD / SD is the pad list's last row");
	check(n > 0 ? (int)role[n - 1] : -1, JOY_ROLE_HD_SWITCH,
		"hd rows: on JOY_ROLE_HD_SWITCH");
	check(bind_shown(role, n, js, jf, jl, 0, 0), 23, "hd rows: pad 23");
	check(bind_shown(role, n, js, jf, jl, 1, 0), 24, "hd rows: Separate 24");
	check(bind_shown(role, n, js, jf, jl, 0, 1), 28, "hd rows: Cheats 28");
	check(bind_shown(role, n, js, jf, jl, 1, 1), 29, "hd rows: both 29");

	// The resolve's own filter is named at both ends, as bind_shown is.
	check(tree_count(frame,
		"row->param >= cheat && row->param <= cheat_last &&"), 1,
		"hd rows: the resolve holds back the five cheats and no more");
}

/* The keyboard layouts in registry.cpp's names: a set saved with the role
 * reads its key back; a set saved by a build without it has no Key34 or
 * MKey34 and reads the row's default, F12, and the miss writes it. */
static void hd_rows_keyboard(const char* input)
{
	TestStore s;
	long v;

	// F12 has a name in both key-name tables (a nameless key is neither
	// shown nor captured) and closes both Tank rows.
	check(tree_count(input, "\t\"F12\", 0, 0,"), 2,
		"hd rows: F12 named in both key-name tables");
	check(tree_count(input,
		"DIK_F2, DIK_F7, DIK_F8, DIK_F11, DIK_F1, DIK_F12 },"), 2,
		"hd rows: F12 closes both Tank rows");

	s.count = 0;
	store_write(&s, "Key34", 0x13);
	store_write(&s, "MKey34", 0x10);
	store_read(&s, "Key34", &v, 0x58);
	check((int)v, 0x13, "round trip: the saved Tank key reads back");
	store_read(&s, "MKey34", &v, kModern[KEY_ROLE_HD_SWITCH]);
	check((int)v, 0x10, "round trip: the saved Modern key reads back");

	s.count = 0;
	store_write(&s, "Key33", 0x3B);
	store_write(&s, "MKey33", 0x3B);
	check(store_read(&s, "Key34", &v, 0x58), 0, "older set: no Key34");
	check((int)v, 0x58, "older set: the Tank slot reads F12");
	check(store_read(&s, "MKey34", &v, kModern[KEY_ROLE_HD_SWITCH]), 0,
		"older set: no MKey34");
	check((int)v, 0x58, "older set: the Modern slot reads F12");
	check(store_read(&s, "Key34", &v, 0), 1,
		"older set: the miss wrote the default back");
}

/* The pad's Separate set in each state a registry can hold it in. */
static void hd_rows_pad(const char* reg)
{
	const long hd = JOY_ROLE_HD_SWITCH;
	const long without = JOY_SEP_CHORD_DINPUT & ~(1L << hd);
	const unsigned long opt = 1UL << 9, both = (1UL << 9) | (1UL << 13);
	long set[JOY_ROLES], chord, v, i;
	TestStore s;

	check(tree_count(reg, "\"SJHd\""), 1, "hd rows: the Separate set's name");
	check(tree_count(reg, "\"JHd\""), 1, "hd rows: the One-key set's name");
	check(tree_count(reg, "PadSeatAppended(sset, schord, hd, def,"), 1,
		"hd rows: registry.cpp seats a missing SJHd with PadSeatAppended");

	// No SJ* at all: the whole default row, HD/SD on Modifier+Options.
	s.count = 0;
	load_split(&s, set, &chord, kSepPS5DInput, JOY_SEP_CHORD_DINPUT);
	check((int)set[hd], 9, "pad, new registry: HD/SD on Options");
	check((int)((chord >> hd) & 1), 1, "pad, new registry: as a chord");

	// Saved by a build without the role: an SJChord without its bit and no
	// SJHd. The pair is seated whole and both names are written back.
	s.count = 0;
	store_write(&s, "SJChord", without);
	store_write(&s, "SJMod", 13);
	load_split(&s, set, &chord, kSepPS5DInput, JOY_SEP_CHORD_DINPUT);
	check((int)set[hd], 9, "pad, older set: the default button");
	check((int)((chord >> hd) & 1), 1, "pad, older set: and its chord flag");
	check(set_fires(set, chord, hd, opt), 0, "pad: plain Options: no switch");
	check(set_fires(set, chord, 10, opt), 1, "pad: plain Options: pause");
	check(set_fires(set, chord, hd, both), 1, "pad: Mod+Options: the switch");
	check(set_fires(set, chord, 10, both), 0, "pad: Mod+Options: no pause");
	check(store_read(&s, "SJHd", &v, -7), 1, "pad, older set: SJHd written");
	check((int)v, 9, "pad, older set: as Options");
	store_read(&s, "SJChord", &v, 0);
	check((int)((v >> hd) & 1), 1, "pad, older set: SJChord with the bit");

	// The pair already the player's own: the role stays unbound.
	s.count = 0;
	store_write(&s, "SJChord", without | (1L << JOY_ROLE_CHEAT_FLY));
	store_write(&s, "SJCt1", 9);
	load_split(&s, set, &chord, kSepPS5DInput, JOY_SEP_CHORD_DINPUT);
	check((int)set[JOY_ROLE_CHEAT_FLY], 9, "pad, taken pair: player's kept");
	check((int)set[hd], -1, "pad, taken pair: HD/SD unbound");
	check((int)((chord >> hd) & 1), 0, "pad, taken pair: with no flag");

	// The Modifier moved onto Options: a chord on it can never fire.
	s.count = 0;
	store_write(&s, "SJChord", without);
	store_write(&s, "SJMod", 9);
	load_split(&s, set, &chord, kSepPS5DInput, JOY_SEP_CHORD_DINPUT);
	check((int)set[hd], -1, "pad, Modifier on Options: HD/SD unbound");

	// A set registry.cpp stripped of its chords has -1 from the Modifier on.
	{
		long stripped[JOY_ROLES];

		for (i = 0; i < JOY_ROLES; i++)
			stripped[i] = i >= JOY_ROLE_MODIFIER ? -1 : kSepPS5DInput[i];

		s.count = 0;
		store_write(&s, "SJChord", 0);
		load_split(&s, set, &chord, stripped, 0);
		check((int)set[hd], -1, "pad, stripped set: HD/SD stays unbound");
	}

	// Saved with the role on Modifier+L1, read back as saved.
	s.count = 0;
	store_write(&s, "SJChord", JOY_SEP_CHORD_DINPUT);
	store_write(&s, "SJHd", 4);
	load_split(&s, set, &chord, kSepPS5DInput, JOY_SEP_CHORD_DINPUT);
	check((int)set[hd], 4, "pad, round trip: the saved button");
	check((int)((chord >> hd) & 1), 1, "pad, round trip: its chord flag");
}

static void hd_switch_rows()
{
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* input = tree_file("../../../TOMB5 Code/TOMB5/specific/input.cpp");
	char* reg = tree_file("../../../TOMB5 Code/TOMB5/specific/registry.cpp");

	check(frame && input && reg, 1, "hd rows: the three files are readable");

	if (frame && input && reg)
	{
		hd_rows_lists(frame);
		hd_rows_keyboard(input);
		hd_rows_pad(reg);
	}

	free(frame);
	free(input);
	free(reg);
}

/* -------------------------------------------------- the HD textures row -- */

/* The row's step as game\menu_frame.cpp's MenuStepHdTextures does it,
 * stated again because that file cannot be linked: the row's value is the
 * HD/SD switch's state, and a step sets it to the other value through
 * specific\hd\hd_switch.h's own HDSwitchSet, included - one value, not an
 * option beside a state. `hd` is whether the level on screen has HD data.
 * graphics_hd_row holds the engine's lines against it. */
static int hd_row_step(HDSwitchState* s, int hd)
{
	return HDSwitchSet(s, !s->drawn, hd);
}

static const char* hd_row_text(int stored)
{
	return stored ? "on" : "off";
}

/* The HD textures row on the Graphics page: one table line, saved with the
 * tomb5 settings, reached by the title's and the pause menu's options alike
 * (both call DoOptions, which runs the one table); its text and its stored
 * value as before; its step the switch's own setter, so the value and the
 * state are one thing, swept over both values, both held states and both
 * answers of the level's data. */
static void graphics_hd_row()
{
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* save = tree_file("../../../TOMB5 Code/TOMB5/specific/LoadSave.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");

	check(frame && save && flow && opts, 1,
		"hd textures row: the four engine files are readable from here");

	if (frame && save && flow && opts)
	{
		check(tree_count(frame, "MENU_VALUE(\"HD textures\", "
			"MenuValHdTextures, MenuStepHdTextures, MENU_SAVE_TOMB5),"), 1,
			"hd textures row: one value row, saved with the tomb5 settings");
		check(tree_count(frame, "\"HD textures\""), 1,
			"hd textures row: and no second row of that name");

		// Both menus run the one table.
		check(strstr(save, "void DoOptions()\r\n{\r\n\tMenuFrameRun();") !=
			0 || strstr(save, "void DoOptions()\n{\n\tMenuFrameRun();") != 0,
			1, "hd textures row: DoOptions runs the one table");
		check(tree_count(save, "\t\tDoOptions();") + tree_count(flow,
			"\t\tDoOptions();"), 2,
			"hd textures row: the pause menu and the title both call it");

		// The text and the stored value, as before.
		check(strstr(frame, "CustomText(tomb5.hd_enable ? \"on\" : \"off\")")
			!= 0, 1, "hd textures row: it shows the option's on and off");
		check(tree_count(opts, "sprintf(buf, \"hd_enable\");"), 3,
			"hd textures row: hd_enable written first, read and saved");
		check(strstr(opts, "REG_ReadBool(buf, tomb5.hd_enable, 0);") != 0,
			1, "hd textures row: a registry without it reads as off");

		// The build gate beside it: registry only, 1 when absent.
		check(tree_count(opts, "sprintf(buf, \"hd_build\");"), 3,
			"hd textures row: hd_build written first, read and saved");
		check(strstr(opts, "REG_ReadLong(buf, tomb5.hd_build, 1);") != 0,
			1, "hd textures row: a registry without hd_build builds");
		check(tree_count(frame, "hd_build"), 1,
			"hd textures row: hd_build has no row (named once, in a note)");

		// The step: the switch's own setter, and no write of its own.
		{
			const char* fn = strstr(frame,
				"static bool MenuStepHdTextures(long, long dir)");
			const char* end = fn ? strstr(fn, "\n}") : 0;
			const char* set = fn ? strstr(fn, "\tHDSwitchOption();") : 0;

			check(fn && end && set && set < end, 1, "hd textures row: the "
				"step is the switch's setter");
			check(tree_count(frame, "HDSwitchOption("), 1,
				"hd textures row: the one caller of the switch's setter");
			check(tree_count(frame, "tomb5.hd_enable ="), 0,
				"hd textures row: the menu writes no draw of its own");
		}
	}

	// The step's table: the value flips, it is the state, held alone.
	for (int v = 0; v < 2; v++)
		for (int held = 0; held < 2; held++)
			for (int hd = 0; hd < 2; hd++)
			{
				HDSwitchState s = { v, held };
				const int what = hd_row_step(&s, hd);
				const int want = hd ? HD_SWITCH_FLIPPED : HD_SWITCH_NOTHING;

				check(s.drawn, !v,
					"hd textures row: a step either way toggles the value");
				check(strcmp(hd_row_text(s.drawn), v ? "off" : "on"), 0,
					"hd textures row: and the text shows the new value");
				check(what, want, "hd textures row: a flip with data, "
					"nothing to switch without, the value moved either way");
				check(s.held, held,
					"hd textures row: the key's held state untouched");
			}

	// Two steps give the value back, with and without data.
	for (int hd = 0; hd < 2; hd++)
	{
		HDSwitchState s = HD_SWITCH_START;

		hd_row_step(&s, hd);
		hd_row_step(&s, hd);
		check(s.drawn, 1, "hd textures row: two steps, the value as before");
	}

	/* After a press of the key the row reads the key's value - there is no
	 * other value to read - and a step of the row goes on from there. */
	for (int v = 0; v < 2; v++)
		for (int hd = 0; hd < 2; hd++)
		{
			HDSwitchState s = { v, 0 };
			const int pressed = hd ? !v : v;

			HDSwitchStep(&s, 1, 1, hd);
			HDSwitchStep(&s, 0, 1, hd);
			check(strcmp(hd_row_text(s.drawn), pressed ? "on" : "off"), 0,
				"hd textures row: after a press it reads the key's value");
			hd_row_step(&s, hd);
			check(s.drawn, !pressed,
				"hd textures row: a step goes on from the key's value");
		}

	free(frame);
	free(save);
	free(flow);
	free(opts);
}

/* ------------------------------------------------------- the Gamma row -- */

/* The Gamma row on the Display page, read from the tree as the mouse's rows
 * are: the Display table's rows in order, the row's kind, functions, save
 * class and the absence of a gate; that its keys and pad are the volume
 * rows' (the same slider kind, driven by MenuSliderDir); its step, swept
 * whole; the registry name in tomb5\tomb5.cpp (written as 50 on a first
 * run, read with 50 for a missing or out-of-range value, saved) and a round
 * trip through that read; the field in global\types.h; and in
 * specific\dxshell.cpp that the pass runs nowhere at 50, that the picture
 * goes back after the present, and that the device's release and reset
 * take the pass's objects with them; that both of the engine's presents,
 * DXShowFrame and the loading bar's held one (specific\drawbars.cpp),
 * take the pass. */
static void display_gamma_row()
{
	static const char* const want[3] = { "Frame rate", "VSync", "Gamma" };
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* flow = tree_file("../../../TOMB5 Code/TOMB5/game/gameflow.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char* types = tree_file("../../../TOMB5 Code/TOMB5/global/types.h");
	char* dx = tree_file("../../../TOMB5 Code/TOMB5/specific/dxshell.cpp");
	char* fx = tree_file(
		"../../../TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_gamma.hlsl");
	char* bars = tree_file("../../../TOMB5 Code/TOMB5/specific/drawbars.cpp");
	char label[8][64];
	char line[8][512];
	int rows = 0, i, v, d;

	check(frame && flow && opts && types && dx && fx, 1,
		"gamma row: the six engine files are readable from here");

	if (!frame || !flow || !opts || !types || !dx || !fx)
	{
		free(frame);
		free(flow);
		free(opts);
		free(types);
		free(dx);
		free(fx);
		free(bars);
		return;
	}

	// The Display table, row by row, as controls_mouse_rows reads Controls.
	{
		const char* p = strstr(frame,
			"static const menu_row menu_rows_display[] =");
		const char* end = p ? strstr(p, "};") : 0;

		check(p != 0 && end != 0, 1, "gamma row: the Display table is found");

		while (p && end && p < end && rows < 8)
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

	check(rows, 3, "gamma row: the Display table holds three rows");

	for (i = 0; i < rows && i < 3; i++)
		check(strcmp(label[i], want[i]), 0,
			"gamma row: Frame rate, VSync, Gamma, in this order");

	if (rows == 3)
		check(strcmp(line[2], "MENU_SLIDER(\"Gamma\", -1, MenuNumGamma, "
			"MenuStepGamma, MENU_SAVE_TOMB5, 0)"), 0,
			"gamma row: a slider with its own two functions, the literal "
			"label, saved with the tomb5 settings, never greyed");

	check(strstr(frame, "MENU_TAB(\"Display\", MENU_PAGE_DISPLAY)") != 0, 1,
		"gamma row: the Display page is the Graphics page's tab");
	check(tree_count(frame, "MENU_SLIDER(\"Gamma\""), 1,
		"gamma row: one row of that name");

	// The label is the same word in all five columns, so it has no row.
	check(tree_count(flow, "{ \"Gamma\","), 0,
		"gamma row: no CustomText row - the literal is every column");

	// The keys and the pad: the volume rows' own, by being their kind.
	check(strstr(frame, "MENU_SLIDER(\"Music Volume\"") != 0 &&
		strstr(frame, "MENU_SLIDER(\"SFX Volume\"") != 0, 1,
		"gamma row: the two volume rows are the same slider kind");
	{
		const char* drive = strstr(frame, "case MROW_SLIDER:");
		const char* next = drive ? strstr(drive, "case MROW_VALUE:") : 0;
		const char* dir = drive ? strstr(drive, "dir = MenuSliderDir();") : 0;

		check(drive != 0 && next != 0 && dir != 0 && dir < next, 1,
			"gamma row: every slider is driven by MenuSliderDir");
	}
	check(strstr(frame, "if (input & IN_LEFT || keymap[DIK_LEFT])") != 0 &&
		strstr(frame, "if (input & IN_RIGHT || keymap[DIK_RIGHT])") != 0, 1,
		"gamma row: held LEFT/RIGHT of the pad's and keys' input, and the "
		"arrow keys");

	// The step.
	check(strstr(frame, "return (long)tomb5.gamma;") != 0, 1,
		"gamma row: the bar shows tomb5.gamma");
	check(strstr(frame, "tomb5.gamma = (ulong)MenuSliderStep("
		"(int)tomb5.gamma, (int)dir,\r\n\t\tMENU_SLIDER_TOP);") != 0
		&& MENU_SLIDER_TOP == 100, 1,
		"gamma row: the slider steps through MenuSliderStep over 0..100");

	for (v = -1; v <= 101; v++)
		for (d = -1; d <= 1; d++)
		{
			int w = v;

			if (d < 0 && v > 0)
				w = v - 1;

			if (d > 0 && v < 100)
				w = v + 1;

			check(MenuSliderStep(v, d, 100), w,
				"gamma row: one frame of the slider is one step");
		}

	check(MenuSliderStep(50, 1, 100) - 50, 1,
		"gamma row: from the middle, one step up");
	check(50 - MenuSliderStep(50, -1, 100), 1,
		"gamma row: and one step down");

	// The field, the registry and its round trip.
	check(strstr(types, "\tulong gamma;") != 0, 1,
		"gamma row: tomb5.gamma is a ulong, as the registry wants");
	check(tree_count(opts, "sprintf(buf, \"gamma\");"), 3,
		"gamma row: gamma is written on a first run, read and saved");
	check(tree_count(opts, "REG_WriteLong(buf, tomb5.gamma);"), 2,
		"gamma row: written on the first run and in the save");
	check(strstr(opts, "tomb5.gamma = 50;\r\n\t\tREG_WriteLong(buf, "
		"tomb5.gamma);") != 0, 1, "gamma row: a first run writes 50");
	check(strstr(opts, "REG_ReadLong(buf, tomb5.gamma, 50);\r\n\t\t"
		"tomb5.gamma = MenuSliderHeldUlong(tomb5.gamma, 50);") != 0,
		1, "gamma row: missing reads 50, outside 0..100 reads 50");

	/* The round trip: the save writes the field as it stands, and the read
	 * replaces only what lies outside 0..100 - the slider's own span - so
	 * every value the row can reach comes back as itself. */
	{
		int same = 1;

		for (v = 0; v <= 100; v++)
			if (MenuSliderHeldUlong((ulong)v, 50) != (ulong)v)
				same = 0;

		check(same, 1, "gamma row: every value 0..100 reads back as itself");
	}
	check(tree_count(opts, "tomb5.gamma = 50;"), 1,
		"gamma row: 50 is set in one place only, the first run");
	check(tree_count(opts, "tomb5.gamma =") + tree_count(frame,
		"tomb5.gamma =") + tree_count(dx, "tomb5.gamma ="), 3,
		"gamma row: and the slider's step is the one other write");

	// The pass: never at 50, the picture back after the present, released.
	check(strstr(dx, "#define GAMMA_MIDDLE\t50") != 0, 1,
		"gamma row: the middle is 50");
	check(strstr(dx, "if (row != GAMMA_MIDDLE && row <= 100)\r\n\t\treturn "
		"GammaPass(G_dxptr->lpD3DDevice, row);") != 0, 1,
		"gamma row: the pass runs only away from the middle");
	check(tree_count(dx, "GammaPass("), 2,
		"gamma row: one pass, one caller");
	check(strstr(dx, "if (curved && hr != D3DERR_DEVICELOST)\r\n\t\t"
		"GammaRestore(G_dxptr->lpD3DDevice);") != 0, 1,
		"gamma row: the engine's picture goes back after the present");
	check(strstr(dx, "FmvDeviceLost();\r\n\t\tGammaRelease();") != 0, 1,
		"gamma row: DXClose releases the pass's objects");
	check(strstr(dx, "HDLevelDeviceLost();\r\n\t\t\tGammaRelease();") != 0, 1,
		"gamma row: and so does the reset");

	/* Both of the engine's presents take the pass: DXShowFrame and the
	 * loading bar's held frames, each with the start before its Present and
	 * the finish after it, and the device's Present nowhere else. */
	check(tree_count(dx, "->Present(0, 0, 0, 0);"), 1,
		"gamma row: dxshell.cpp presents in DXShowFrame only");
	check(tree_count(dx, "const bool curved = DXGammaStart();"), 1,
		"gamma row: DXShowFrame starts with the pass");
	check(tree_count(dx, "\tDXGammaFinish(curved, hr);"), 1,
		"gamma row: and ends with the copy back");
	check(bars != 0, 1, "gamma row: drawbars.cpp is readable from here");

	if (bars)
	{
		const char* s = strstr(bars, "static void LoadBarPresentHeld()");
		const char* st = s ? strstr(s, "const bool curved = DXGammaStart();\r\n"
			"\tconst HRESULT hr = App.dx.lpD3DDevice->Present(0, 0, 0, 0);")
			: 0;
		const char* fin = s ? strstr(s, "DXGammaFinish(curved, hr);") : 0;
		const char* end = s ? strstr(s, "\r\n}\r\n") : 0;

		check(tree_count(bars, "->Present("), 1,
			"gamma row: the loading bar presents in one place");
		check(st != 0 && end != 0 && st < end, 1,
			"gamma row: its held present starts with the pass");
		check(fin != 0 && st != 0 && end != 0 && st < fin && fin < end, 1,
			"gamma row: and ends with the copy back");
	}

	check(strstr(fx, "exp2((50.0 - gRow.x) / 50.0)") != 0, 1,
		"gamma row: the shader's power is 2 ^ ((50 - n) / 50)");

	free(frame);
	free(flow);
	free(opts);
	free(types);
	free(dx);
	free(fx);
	free(bars);
}

/* ------------------------------------------------ the sliders' numbers -- */

/* What the screen of the menu does, stated again from the engine files and
 * held against their text (game\menu_frame.cpp cannot be linked here):
 * specific\output.cpp's GetFixedScale, MulDiv rounding to nearest. */
static long fixed_scale(long unit, long w, long h)
{
	const long x = w > 640 ? (long)(((long long)w * unit + 320) / 640) : unit;
	const long y = h > 480 ? (long)(((long long)h * unit + 240) / 480) : unit;

	return x < y ? x : y;
}

// game\text.cpp's InitFont: a glyph's width at the screen's width.
static long glyph_width(long base, long w)
{
	return (short)((float)base * ((float)(w - 1) / 512.0F));
}

/* The pixels a full-size string of digits covers, as PrintString advances:
 * each glyph is a quad of its own width, the next one starts at its end. */
static long digits_width(const char* s, const long* digit, long w)
{
	long n = 0;

	for (; *s; s++)
		n += glyph_width(digit[*s - '0'], w);

	return n;
}

/* The first pixel right of the bar's white frame. DoSlider draws the frame
 * from sx - 1 to sx + w + 1, sx = 400 * phd_winxmax / 640 in floats, and
 * DrawColoredRect passes those x unshifted as pre-transformed vertices.
 * Direct3D 9 puts pixel centres on whole numbers and covers a pixel when
 * left <= x < right, so the first free pixel is the edge rounded up. */
static long slider_frame_free(long w, long h)
{
	const float sx = 400.0F * (float)(w - 1) / 640.0F;
	const float right = sx + (float)fixed_scale(200, w, h) + 1.0F;

	return (long)ceil(right);
}

// MenuSliderNumberX, the same arithmetic.
static long slider_number_x(long w, long h)
{
	return (long)(400.0F * (float)(w - 1) / 640.0F) + fixed_scale(200, w, h)
		+ 1 + fixed_scale(4, w, h);
}

/* A draw's colour rule as the engine's text writes it at p,
 * "(uchar)(!enabled[i] ? A : (cur == i ? B : C))": A greyed, B under the
 * cursor, C elsewhere. Returns 0 when the text has another form. */
static int colour_rule(const char* p, int c[3])
{
	static const char* const part[3] =
	{
		"(uchar)(!enabled[i] ? ", " : (cur == i ? ", " : "
	};
	int k;

	for (k = 0; k < 3; k++)
	{
		const size_t n = strlen(part[k]);
		char* end;

		if (!p || strncmp(p, part[k], n))
			return 0;

		c[k] = (int)strtol(p + n, &end, 10);

		if (end == p + n)
			return 0;

		p = end;
	}

	return !strncmp(p, "))", 2);
}

// The colour a rule read by colour_rule gives a row.
static int colour_of(const int c[3], int enabled, int selected)
{
	return !enabled ? c[0] : (selected ? c[1] : c[2]);
}

/* A value written with the engine's own format: one "%d", "%%" as a percent
 * sign and no other conversion, as sprintf would write it; 0 for any other
 * format. */
static int format_value(const char* fmt, long v, char* out, size_t n)
{
	size_t k = 0;
	int conv = 0;

	for (; *fmt; fmt++)
	{
		char part[24] = { *fmt, 0 };

		if (*fmt == '%' && fmt[1] == '%')
			fmt++;
		else if (*fmt == '%' && fmt[1] == 'd' && !conv++)
		{
			snprintf(part, sizeof(part), "%ld", v);
			fmt++;
		}
		else if (*fmt == '%')
			return 0;

		for (const char* p = part; *p; p++)
		{
			if (k + 1 >= n)
				return 0;

			out[k++] = *p;
		}
	}

	if (n)
		out[k] = 0;

	return conv == 1;
}

/* A step's top as the engine's text writes it right after the prefix: the
 * span's name, MENU_SLIDER_TOP, read as the value game\menu_frame.h gives
 * it, or a whole number. */
static int number_after(const char* text, const char* prefix, long* v);

static int top_after(const char* text, const char* prefix, long* v)
{
	const char* p = text ? strstr(text, prefix) : 0;

	if (p && !strncmp(p + strlen(prefix), "MENU_SLIDER_TOP", 15))
	{
		*v = MENU_SLIDER_TOP;
		return 1;
	}

	return number_after(text, prefix, v);
}

// The whole number the engine's text writes right after the prefix.
static int number_after(const char* text, const char* prefix, long* v)
{
	const char* p = text ? strstr(text, prefix) : 0;
	char* end;

	if (!p)
		return 0;

	*v = strtol(p + strlen(prefix), &end, 10);
	return end != p + strlen(prefix);
}

/* The pixels a slider's text covers, its digits and its percent sign, as
 * PrintString advances: each glyph is a quad of its own width. */
static long number_width(const char* s, const long* digit, long sign, long w)
{
	long n = 0;

	for (; *s; s++)
		n += glyph_width(*s == '%' ? sign : digit[*s - '0'], w);

	return n;
}

// MenuSliderSignFits, the same arithmetic: phd_winxmax + 1 is the width.
static int slider_sign_fits(long w, long h, const long* digit, long sign)
{
	char top[16];

	snprintf(top, sizeof(top), "%d%%", MENU_SLIDER_TOP);
	return slider_number_x(w, h) + number_width(top, digit, sign, w)
		+ fixed_scale(4, w, h) <= w;
}

/* Where a slider's number carries its percent sign: MenuSliderSignFits's text
 * in the engine, its rule at the layouts the record names, and every width
 * from 640 to 3840 in seven shapes - never at 5:4, 4:3 or 1:1, at every width
 * of the wide shapes whose height needs no 480 floor, and wherever the sign
 * is drawn, every value's text ends at least one gap before the edge. */
static void slider_sign_room(const char* frame, const long* digit, long sign,
	const int (*shape)[2])
{
	static const int never[7] = { 1, 1, 0, 0, 0, 0, 1 };
	int i, k;

	check(strstr(frame, "static bool MenuSliderSignFits()\r\n{\r\n"
		"\tchar top[16];\r\n\r\n\tsprintf(top, \"%d%%\", MENU_SLIDER_TOP);"
		"\r\n\treturn MenuSliderNumberX() + GetStringLength(top, 0, 0)\r\n"
		"\t\t+ GetFixedScale(4) <= phd_winxmax + 1;") != 0, 1,
		"slider sign: MenuSliderSignFits is the arithmetic stated here");
	check(strstr(frame, "#include \"text.h\"") != 0, 1,
		"slider sign: GetStringLength's header is included");
	check(tree_count(frame, "MenuSliderSignFits()"), 2,
		"slider sign: one rule, asked by the number's draw only");

	// 640x480, the tightest: "100%" is 49 and would run 13 past the edge.
	check(glyph_width(sign, 640), 18, "slider sign: 640x480, the sign is 18");
	check(number_width("100%", digit, sign, 640), 49,
		"slider sign: 640x480, \"100%\" is 49 pixels");
	check(640 - (slider_number_x(640, 480)
		+ number_width("100%", digit, sign, 640)), -13,
		"slider sign: 640x480, \"100%\" would run 13 past the edge");
	check(slider_sign_fits(640, 480, digit, sign), 0,
		"slider sign: 640x480, no room - the number stands alone");
	check(slider_sign_fits(1600, 1200, digit, sign), 0,
		"slider sign: 1600x1200, no room either");
	check(slider_sign_fits(1280, 1024, digit, sign), 0,
		"slider sign: 1280x1024, 5:4, no room");
	check(slider_sign_fits(1280, 800, digit, sign), 1,
		"slider sign: 1280x800, 16:10, the sign is drawn");
	check(slider_sign_fits(1440, 960, digit, sign), 1,
		"slider sign: 1440x960, 3:2, the sign is drawn");
	check(slider_sign_fits(1920, 1080, digit, sign), 1,
		"slider sign: 1920x1080, the sign is drawn");
	check(number_width("100%", digit, sign, 1920), 152,
		"slider sign: 1920x1080, \"100%\" is 152 pixels");
	check(1920 - (slider_number_x(1920, 1080)
		+ number_width("100%", digit, sign, 1920)), 109,
		"slider sign: 1920x1080, 109 pixels to the edge");
	check(slider_sign_fits(2560, 1440, digit, sign), 1,
		"slider sign: 2560x1440, the sign is drawn");
	check(slider_number_x(2560, 1440), 2212,
		"slider sign: 2560x1440, the number starts at 2212");
	check(number_width("100%", digit, sign, 2560), 201,
		"slider sign: 2560x1440, \"100%\" is 201 pixels");
	check(2560 - (slider_number_x(2560, 1440)
		+ number_width("100%", digit, sign, 2560)), 147,
		"slider sign: 2560x1440, 147 pixels to the edge");
	// The rule's edge, near 1.46:1, at 1024 wide.
	check(slider_sign_fits(1024, 706, digit, sign), 1,
		"slider sign: 1024x706 still has room");
	check(slider_sign_fits(1024, 707, digit, sign), 0,
		"slider sign: 1024x707 has none");

	for (i = 0; i < 7; i++)
	{
		int drawn = 0, natural = 0, natural_fit = 0, clear = 1, widest = 1;

		for (long w = 640; w <= 3840; w++)
		{
			long h = (w * shape[i][1] + shape[i][0] / 2) / shape[i][0];
			const int raised = h < 480;
			int fits;

			if (raised)
				h = 480;

			fits = slider_sign_fits(w, h, digit, sign);
			drawn += fits;

			if (!raised)
			{
				natural++;
				natural_fit += fits;
			}

			for (k = 0; fits && k <= MENU_SLIDER_TOP; k++)
			{
				char buf[16];
				long tw;

				snprintf(buf, sizeof(buf), "%d%%", k);
				tw = number_width(buf, digit, sign, w);

				if (tw > number_width("100%", digit, sign, w))
					widest = 0;

				if (w - (slider_number_x(w, h) + tw) < fixed_scale(4, w, h))
					clear = 0;
			}
		}

		if (never[i])
			check(drawn, 0, "slider sign: never drawn at 5:4, 4:3 or 1:1");
		else
			check(natural > 0 && natural_fit == natural, 1,
				"slider sign: drawn at every natural height of a wide shape");

		check(clear, 1, "slider sign: every value with it ends a gap early");
		check(widest, 1, "slider sign: \"100%\" is the widest text with it");
	}
}

/* The six slider keys as the registry hands them back. Each read is followed
 * at once by game\menu_frame.h's MenuSliderHeld - the engine's own function,
 * compiled here - with the fallback the read names, so that the row and the
 * engine see the held value from the first frame. A value under the span, at
 * its ends, inside it and over it comes out as stated: at the nearer end for
 * the volumes and cam_speed, on the middle for dead_zone, mouse_sens and
 * gamma; and nothing of it writes the registry. Each key is held in its
 * field's own type: the volumes are long, so a negative DWORD is under the
 * span, the four tomb5 keys ulong, so it is over it, as their reads had it. */
static long held_as_read(int key, unsigned long stored, long out)
{
	if (key < 2)
		return MenuSliderHeld((long)stored, out);

	return (long)MenuSliderHeldUlong(stored, out);
}

static void slider_reads()
{
	static const char* const key[6] =
	{
		"MusicVolume", "SFXVolume", "cam_speed", "dead_zone", "mouse_sens",
		"gamma"
	};
	static const char* const read[6] =
	{
		"REG_ReadLong((char*)\"MusicVolume\", (ulong&)MusicVolume, 80);",
		"REG_ReadLong((char*)\"SFXVolume\", (ulong&)SFXVolume, 90);",
		"REG_ReadLong(buf, tomb5.camera_speed, 40);\r\n",
		"REG_ReadLong(buf, tomb5.dead_zone, 50);\r\n",
		"REG_ReadLong(buf, tomb5.mouse_sensitivity, 50);\r\n",
		"REG_ReadLong(buf, tomb5.gamma, 50);\r\n"
	};
	// The hold, up to its fallback; the tomb5 keys' stand right after the read.
	static const char* const hold[6] =
	{
		"\tMusicVolume = MenuSliderHeld(MusicVolume, ",
		"\tSFXVolume = MenuSliderHeld(SFXVolume, ",
		"\t\ttomb5.camera_speed = MenuSliderHeldUlong(tomb5.camera_speed,\r\n"
		"\t\t\t",
		"\t\ttomb5.dead_zone = MenuSliderHeldUlong(tomb5.dead_zone, ",
		"\t\ttomb5.mouse_sensitivity = MenuSliderHeldUlong(\r\n"
		"\t\t\ttomb5.mouse_sensitivity, ",
		"\t\ttomb5.gamma = MenuSliderHeldUlong(tomb5.gamma, "
	};
	static const long def[6] = { 80, 90, 40, 50, 50, 50 };
	static const long want_out[6] =
	{
		MENU_SLIDER_NEAREST, MENU_SLIDER_NEAREST, MENU_SLIDER_NEAREST, 50, 50,
		50
	};
	// As the DWORD arrives: -1, -50, 0, inside, 100, 101, 150 and 1000.
	static const unsigned long probe[8] =
	{
		0xFFFFFFFFUL, 0xFFFFFFCEUL, 0, 37, 100, 101, 150, 1000
	};
	static const long want[3][8] =
	{
		{ 0, 0, 0, 37, 100, 100, 100, 100 },	// long, to the nearer end
		{ 100, 100, 0, 37, 100, 100, 100, 100 },	// ulong, the nearer end
		{ 50, 50, 0, 37, 100, 50, 50, 50 }	// to the middle
	};
	static const int row[6] = { 0, 0, 1, 2, 2, 2 };
	char* reg = tree_file("../../../TOMB5 Code/TOMB5/specific/registry.cpp");
	char* opts = tree_file("../../../TOMB5 Code/TOMB5/tomb5/tomb5.cpp");
	char* head = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.h");
	const char* body = head ? strstr(head, "inline long MenuSliderHeld(") : 0;
	const char* body_end = body ? strstr(body, "\r\n}\r\n") : 0;
	const char* ubody = head ? strstr(head,
		"inline ulong MenuSliderHeldUlong(") : 0;
	const char* ubody_end = ubody ? strstr(ubody, "\r\n}\r\n") : 0;
	char* types = tree_file("../../../TOMB5 Code/TOMB5/global/types.h");
	char* save = tree_file("../../../TOMB5 Code/TOMB5/specific/LoadSave.cpp");
	const char* control = reg ? strstr(reg,
		"REG_ReadLong((char*)\"ControlMethod\"") : 0;
	int i, k;

	check(reg && opts && head && body && body_end && ubody && ubody_end
		&& types && save, 1,
		"slider reads: registry.cpp, tomb5.cpp and the hold are readable");

	if (!reg || !opts || !head || !body || !body_end || !ubody || !ubody_end
		|| !types || !save)
	{
		free(reg);
		free(opts);
		free(head);
		free(types);
		free(save);
		return;
	}

	check(strstr(save, "\nlong MusicVolume = ") && strstr(save,
		"\nlong SFXVolume = ") && strstr(types, "\tulong camera_speed;")
		&& strstr(types, "\tulong dead_zone;") && strstr(types,
		"\tulong mouse_sensitivity;") && strstr(types, "\tulong gamma;"), 1,
		"slider reads: the volumes are long, the four tomb5 keys ulong");

	for (i = 0; i < 6; i++)
	{
		const char* text = i < 2 ? reg : opts;
		const char* r = strstr(text, read[i]);
		const char* h = r ? strstr(r, hold[i]) : 0;
		long out = -2;
		char what[160];
		int next;

		// The volumes' holds follow both reads, before the next key's read.
		if (i < 2)
			next = h && control && h < control;
		else
			next = h && h == r + strlen(read[i]);

		snprintf(what, sizeof(what), "slider reads: %s is held right after "
			"its read", key[i]);
		check(next, 1, what);

		if (h)
		{
			const char* a = h + strlen(hold[i]);
			char* e;

			if (!strncmp(a, "MENU_SLIDER_NEAREST);", 21))
				out = MENU_SLIDER_NEAREST;
			else
			{
				out = strtol(a, &e, 10);

				if (e == a || strncmp(e, ");", 2))
					out = -2;
			}
		}

		snprintf(what, sizeof(what), "slider reads: %s's fallback", key[i]);
		check((int)out, (int)want_out[i], what);
		snprintf(what, sizeof(what), "slider reads: %s's default stays",
			key[i]);
		check((int)held_as_read(i, (unsigned long)def[i], out), (int)def[i],
			what);

		for (k = 0; k < 8; k++)
		{
			if (i < 2)
				snprintf(what, sizeof(what), "slider reads: %s read as %ld",
					key[i], (long)probe[k]);
			else
				snprintf(what, sizeof(what), "slider reads: %s read as %lu",
					key[i], probe[k]);

			check((int)held_as_read(i, probe[k], out), (int)want[row[i]][k],
				what);
		}
	}

	check(MENU_SLIDER_NEAREST < 0, 1,
		"slider reads: MENU_SLIDER_NEAREST lies outside the span");
	check(tree_count(reg, "MenuSliderHeld("), 2,
		"slider reads: registry.cpp holds the two volumes");
	check(tree_count(opts, "MenuSliderHeldUlong("), 4,
		"slider reads: tomb5.cpp holds its four slider keys");
	check(tree_count(opts, "MenuSliderHeld("), 0,
		"slider reads: and none of them as a long");
	{
		const char* over = strstr(ubody,
			"return (ulong)MenuSliderHeld(MENU_SLIDER_TOP + 1, out);");

		check(over && over < ubody_end, 1, "slider reads: the ulong hold "
			"sends what lies over the span through the one rule");
	}
	check(tree_count(opts, "if (tomb5.camera_speed > 100)")
		+ tree_count(opts, "if (tomb5.dead_zone > 100)")
		+ tree_count(opts, "if (tomb5.mouse_sensitivity > 100)")
		+ tree_count(opts, "if (tomb5.gamma > 100)"), 0,
		"slider reads: no second copy of the top at the reads");
	check(strstr(head, "#define MENU_SLIDER_TOP\t100") != 0, 1,
		"slider reads: the span's one definition");

	{
		int writes = 0;

		for (const char* p = body; p < body_end; p++)
			if (!strncmp(p, "REG_", 4))
				writes++;

		check(writes, 0, "slider reads: the hold writes nothing back");
		writes = 0;

		for (const char* p = ubody; p < ubody_end; p++)
			if (!strncmp(p, "REG_", 4))
				writes++;

		check(writes, 0, "slider reads: nor does the ulong hold");
	}

	check(strstr(reg, "REG_WriteLong((char*)\"MusicVolume\", MusicVolume);")
		!= 0 && strstr(reg, "REG_WriteLong((char*)\"SFXVolume\", "
		"SFXVolume);") != 0, 1,
		"slider reads: the save writes the held volumes as they stand");
	check(tree_count(opts, "REG_WriteLong(buf, tomb5.camera_speed);")
		+ tree_count(opts, "REG_WriteLong(buf, tomb5.dead_zone);")
		+ tree_count(opts, "REG_WriteLong(buf, tomb5.mouse_sensitivity);")
		+ tree_count(opts, "REG_WriteLong(buf, tomb5.gamma);"), 8,
		"slider reads: the four tomb5 keys are written on a first run and "
		"in the save");

	free(reg);
	free(opts);
	free(head);
	free(types);
	free(save);
}

/* Every slider row draws its number beside its bar. Read from the tree as
 * the rows above are: the six MENU_SLIDER rows in their tables, each one's
 * number function and the 0..100 its step keeps the value in; the draw, in
 * the slider kind's own block and no other, written as MenuSettleSlider
 * writes the value, at the value rows' y in their colour rule; and the
 * position, from DoSlider's, GetFixedScale's and the font's own arithmetic,
 * clear of the bar and of the screen's edge at every width from 640 to
 * 3840 in seven shapes of screen. */
static void slider_numbers()
{
	static const char* const label[6] =
	{
		"Music Volume", "SFX Volume", "Camera speed", "Dead zone",
		"Mouse sensitivity", "Gamma"
	};
	static const char* const num[6] =
	{
		"MenuNumMusicVolume", "MenuNumSfxVolume", "MenuNumCameraSpeed",
		"MenuNumDeadZone", "MenuNumMouseSensitivity", "MenuNumGamma"
	};
	static const char* const field[6] =
	{
		"return MusicVolume;", "return SFXVolume;",
		"return (long)tomb5.camera_speed;", "return (long)tomb5.dead_zone;",
		"return (long)tomb5.mouse_sensitivity;", "return (long)tomb5.gamma;"
	};
	// Each row's own clamp to 0..100, one step a frame.
	static const char* const top[6] =
	{
		"if (MusicVolume > MENU_SLIDER_TOP)\r\n\t\t"
		"MusicVolume = MENU_SLIDER_TOP;",
		"if (SFXVolume > MENU_SLIDER_TOP)\r\n\t\tSFXVolume = MENU_SLIDER_TOP;",
		"if (dir > 0 && tomb5.camera_speed < MENU_SLIDER_TOP)\r\n\t\t"
		"tomb5.camera_speed++;",
		"if (dir > 0 && tomb5.dead_zone < MENU_SLIDER_TOP)\r\n\t\t"
		"tomb5.dead_zone++;",
		"MenuSliderStep(\r\n\t\t(int)tomb5.mouse_sensitivity, (int)dir, "
		"MENU_SLIDER_TOP)",
		"MenuSliderStep((int)tomb5.gamma, (int)dir,\r\n\t\tMENU_SLIDER_TOP)"
	};
	static const char* const bottom[6] =
	{
		"if (MusicVolume < 0)\r\n\t\tMusicVolume = 0;",
		"if (SFXVolume < 0)\r\n\t\tSFXVolume = 0;",
		"if (dir < 0 && tomb5.camera_speed > 0)\r\n\t\ttomb5.camera_speed--;",
		"if (dir < 0 && tomb5.dead_zone > 0)\r\n\t\ttomb5.dead_zone--;",
		"MenuSliderStep(\r\n\t\t(int)tomb5.mouse_sensitivity, (int)dir, "
		"MENU_SLIDER_TOP)",
		"MenuSliderStep((int)tomb5.gamma, (int)dir,\r\n\t\tMENU_SLIDER_TOP)"
	};
	// Where each row's step names its top and its bottom; 0 where the
	// bottom is MenuSliderStep's own (game\menu_nav.h, called here).
	static const char* const top_at[6] =
	{
		"if (MusicVolume > ", "if (SFXVolume > ",
		"if (dir > 0 && tomb5.camera_speed < ",
		"if (dir > 0 && tomb5.dead_zone < ",
		"MenuSliderStep(\r\n\t\t(int)tomb5.mouse_sensitivity, (int)dir, ",
		"MenuSliderStep((int)tomb5.gamma, (int)dir,\r\n\t\t"
	};
	static const char* const bottom_at[6] =
	{
		"if (MusicVolume < ", "if (SFXVolume < ",
		"if (dir < 0 && tomb5.camera_speed > ",
		"if (dir < 0 && tomb5.dead_zone > ", 0, 0
	};
	static const char* const table[3] =
	{
		"static const menu_row menu_rows_audio[] =",
		"static const menu_row menu_rows_controls[] =",
		"static const menu_row menu_rows_display[] ="
	};
	static const int in_table[3] = { 2, 3, 1 };
	// game\chardef.inc's '0' to '9', unscaled, stated again.
	static const long want_digit[10] = { 10, 6, 9, 8, 11, 9, 9, 9, 9, 9 };
	// Screens as width : height, swept over every width from 640 to 3840.
	static const int shape[7][2] =
	{
		{ 5, 4 }, { 4, 3 }, { 16, 10 }, { 16, 9 }, { 21, 9 }, { 32, 9 },
		{ 1, 1 }
	};
	char* frame = tree_file("../../../TOMB5 Code/TOMB5/game/menu_frame.cpp");
	char* bars = tree_file("../../../TOMB5 Code/TOMB5/specific/drawbars.cpp");
	char* out = tree_file("../../../TOMB5 Code/TOMB5/specific/output.cpp");
	char* text = tree_file("../../../TOMB5 Code/TOMB5/game/text.cpp");
	char* defs = tree_file("../../../TOMB5 Code/TOMB5/game/chardef.inc");
	long digit[10];
	char found[8][64];
	char line[8][512];
	char fmt_s[64] = "", sign_s[64] = "", arg_s[160] = "", pos_s[128] = "";
	long sign = -1;
	int num_c[3] = { -1, -1, -1 }, val_c[3] = { -2, -2, -2 };
	int num_ok = 0, val_ok = 0;
	int rows = 0, i, k;

	check(frame && bars && out && text && defs, 1,
		"slider numbers: the five engine files are readable from here");

	if (!frame || !bars || !out || !text || !defs)
	{
		free(frame);
		free(bars);
		free(out);
		free(text);
		free(defs);
		return;
	}

	// The slider rows of every table, in the file's order.
	for (const char* p = frame; p && *p; )
	{
		const char* nl = strchr(p, '\n');
		const char* s = p;

		while (*s == '\t' || *s == ' ')
			s++;

		if (!strncmp(s, "MENU_SLIDER(\"", 13) && rows < 8)
		{
			char q[2][64];

			tree_quoted(s, q, 1);
			strcpy_s(found[rows], sizeof(found[rows]), q[0]);

			for (k = 0; s[k] && s[k] != '\n' && s[k] != '\r' && k < 511; k++)
				line[rows][k] = s[k];

			line[rows][k] = 0;
			rows++;
		}

		p = nl ? nl + 1 : 0;
	}

	check(rows, 6, "slider numbers: six slider rows in the tables");

	for (i = 0; i < rows && i < 6; i++)
	{
		char body[128];

		check(strcmp(found[i], label[i]), 0,
			"slider numbers: the six rows, in the tables' order");
		check(strstr(line[i], num[i]) != 0, 1,
			"slider numbers: each row's own number function");
		snprintf(body, sizeof(body), "static long %s(long)\r\n{\r\n\t%s",
			num[i], field[i]);
		check(strstr(frame, body) != 0, 1,
			"slider numbers: the function returns the stored value as is");
		check(strstr(frame, top[i]) != 0 && strstr(frame, bottom[i]) != 0, 1,
			"slider numbers: its step keeps the value in 0..100");
	}

	// Which pages carry them: Audio two, Controls three, Display one.
	{
		int total = 0;

		for (const char* p = strstr(frame, "static const menu_row menu_rows_");
			p; p = strstr(p + 1, "static const menu_row menu_rows_"))
		{
			const char* end = strstr(p, "};");
			int n = 0, t;

			const char* s = strstr(p, "\tMENU_SLIDER(\"");

			for (; s && end && s < end; s = strstr(s + 1, "\tMENU_SLIDER(\""))
				n++;

			for (t = 0; t < 3; t++)
				if (!strncmp(p, table[t], strlen(table[t])))
					break;

			check(n, t < 3 ? in_table[t] : 0,
				"slider numbers: sliders on Audio, Controls, Display only");
			total += n;
		}

		check(total, 6, "slider numbers: and those are all six");
	}

	// The draw: in the slider kind's block, and nowhere else.
	{
		const char* tabs = strstr(frame,
			"\t\tif (row->kind == MROW_TAB || row->kind == MROW_ACTION)\r\n");
		const char* tabs_end = tabs ? strstr(tabs, "\t\t\tcontinue;\r\n") : 0;
		const char* blk = strstr(frame,
			"\t\tif (row->kind == MROW_SLIDER)\r\n\t\t{\r\n");
		const char* blk_end = blk ? strstr(blk, "\t\t\tcontinue;\r\n") : 0;
		const char* bar = blk ? strstr(blk, "DoSlider(400, y + 4 - "
			"(font_height >> 1), 200, 16, row->slider ? "
			"row->slider(row->param) : 0,") : 0;
		const char* fmt = blk ? strstr(blk, "sprintf(buf, MenuSliderSignFits() "
			"? \"%d%%\" : \"%d\",\r\n\t\t\t\t"
			"(int)(row->slider ? row->slider(row->param) : 0));") : 0;
		const char* put = blk ? strstr(blk, "PrintString(MenuSliderNumberX(), "
			"y,\r\n\t\t\t\t(uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 6)), "
			"buf, 0);") : 0;
		const char* val = blk_end ? strstr(blk_end, "if (row->value)\r\n\t\t\t"
			"PrintString(phd_centerx + (phd_centerx >> 2), y, (uchar)"
			"(!enabled[i] ? 3 : (cur == i ? 1 : 6)),") : 0;

		check(tabs && tabs_end && blk && tabs_end < blk, 1,
			"slider numbers: tabs and actions leave before the slider block");
		check(blk && blk_end && bar && bar < blk_end, 1,
			"slider numbers: the bar is drawn in the slider block");
		check(fmt && put && bar < fmt && fmt < put && put < blk_end, 1,
			"slider numbers: after it, the number, inside the same block");
		check(val != 0, 1,
			"slider numbers: values and bindings draw after the block");
		check(tree_count(frame, "row->kind == MROW_SLIDER"), 1,
			"slider numbers: one slider block in the draw");
		check(tree_count(frame, "MenuSliderNumberX()"), 3,
			"slider numbers: one position, for the draw and the sign's room");
		check(tree_count(frame, "PrintString(MenuSliderNumberX(), "), 1,
			"slider numbers: one draw of the number");
		check(tree_count(frame, "(uchar)(!enabled[i] ? 3 : (cur == i ? 1 : "
			"6))"), 2, "slider numbers: the value rows' colour rule, twice");
		check(strstr(frame, "sprintf(buf, \"%d\", (int)(row->slider ? "
			"row->slider(row->param) : 0));") != 0, 1,
			"slider numbers: MenuSettleSlider names the value the same way");
		check(strstr(frame, "#include \"../specific/output.h\"") != 0, 1,
			"slider numbers: GetFixedScale's header is included");

		/* What the engine writes, read out of its text for the checks
		 * below: the bar's value (DoSlider's pos), the number's format
		 * and argument, and the two draws' colour rules. */
		{
			const char* b = bar ? bar + strlen("DoSlider(400, y + 4 - "
				"(font_height >> 1), 200, 16, ") : 0;
			static const char* const lead =
				"sprintf(buf, MenuSliderSignFits() ? \"";
			const char* f = bar ? strstr(bar, lead) : 0;
			const char* s0 = f ? f + strlen(lead) : 0;
			const char* s1 = s0 ? strchr(s0, '"') : 0;
			const char* d0 = s1 && !strncmp(s1, "\" : \"", 5) ? s1 + 5 : 0;
			const char* q = d0 ? strchr(d0, '"') : 0;
			const char* n = bar ? strstr(bar,
				"PrintString(MenuSliderNumberX(), ") : 0;
			size_t m;

			for (m = 0; b && b[m] && b[m] != ',' && m + 1 < sizeof(pos_s); m++)
				pos_s[m] = b[m];

			pos_s[m] = 0;

			// The format with the sign, then the one without it.
			if (q && q < blk_end && (size_t)(s1 - s0) < sizeof(sign_s)
				&& (size_t)(q - d0) < sizeof(fmt_s))
			{
				const char* a = q + 2;

				memcpy(sign_s, s0, (size_t)(s1 - s0));
				sign_s[s1 - s0] = 0;
				memcpy(fmt_s, d0, (size_t)(q - d0));
				fmt_s[q - d0] = 0;

				while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n')
					a++;

				for (m = 0; a[m] && strncmp(a + m, ");", 2)
					&& m + 1 < sizeof(arg_s); m++)
					arg_s[m] = a[m];

				arg_s[m] = 0;
			}

			num_ok = n && n < blk_end
				&& colour_rule(strstr(n, "(uchar)"), num_c);
			val_ok = val && colour_rule(strstr(val, "(uchar)"), val_c);
		}
	}

	// The text: the value the bar is given, written in the engine's format.
	{
		char want[160];

		snprintf(want, sizeof(want), "(int)(%s)", pos_s);
		check(pos_s[0] && !strcmp(arg_s, want), 1,
			"slider numbers: the number writes the value the bar is given");
		check(format_value(fmt_s, 0, want, sizeof(want)), 1,
			"slider numbers: the number's format is one %d, nothing else");
		check(format_value(sign_s, 0, want, sizeof(want)), 1,
			"slider numbers: the sign's format is one %d and a percent sign");
		check(!strcmp(sign_s, "%d%%") && !strcmp(fmt_s, "%d"), 1,
			"slider numbers: the two formats differ by the sign only");
	}

	/* Each row at its bottom, its middle and its top, read from its own
	 * step in the engine's text, written with the engine's format: "0",
	 * "50" and "100", and the top is the value that fills DoSlider's bar. */
	{
		long full = -1;

		check(number_after(bars, "w = pos * w / ", &full) && full == 100, 1,
			"slider numbers: DoSlider fills the whole bar at 100");
		check(full == MENU_SLIDER_TOP, 1,
			"slider numbers: MENU_SLIDER_TOP is the value of the full bar");

		for (i = 0; i < 6; i++)
		{
			static const char* const want[3] = { "0", "50", "100" };
			long lo = -1, hi = -1, at[3];
			int ok = top_after(frame, top_at[i], &hi);

			if (bottom_at[i])
				ok = ok && number_after(frame, bottom_at[i], &lo);
			else if (ok)
			{
				// The bottom MenuSliderStep holds, walked down from the top.
				int v = (int)hi;

				for (k = 0; k < 1000; k++)
					v = MenuSliderStep(v, -1, (int)hi);

				lo = v;

				for (k = 0; k < 1000; k++)
					v = MenuSliderStep(v, 1, (int)hi);

				ok = v == hi;
			}

			check(ok, 1, "slider numbers: each row's step names its ends");
			check(hi == full, 1,
				"slider numbers: each row's top fills the bar");

			at[0] = lo;
			at[1] = (lo + hi) / 2;
			at[2] = hi;

			for (k = 0; k < 3; k++)
			{
				static const char* const want_sign[3] = { "0%", "50%", "100%" };
				char buf[32];

				check(format_value(fmt_s, at[k], buf, sizeof(buf))
					&& !strcmp(buf, want[k]), 1,
					"slider numbers: the text at the bottom, middle and top");
				check(format_value(sign_s, at[k], buf, sizeof(buf))
					&& !strcmp(buf, want_sign[k]), 1,
					"slider numbers: with the sign, \"0%\", \"50%\", \"100%\"");
			}
		}
	}

	/* The colours: the number's rule read from its draw is the value rows'
	 * rule read from theirs, 3 greyed, 1 under the cursor, 6 elsewhere. */
	check(num_ok, 1, "slider numbers: the number's colour rule is read");
	check(val_ok, 1, "slider numbers: the value rows' colour rule is read");

	for (k = 0; k < 4; k++)
		check(colour_of(num_c, k >> 1, k & 1), colour_of(val_c, k >> 1, k & 1),
			"slider numbers: the number's colour is the value rows'");

	check(colour_of(num_c, 1, 1), 1,
		"slider numbers: colour 1 under the cursor");
	check(colour_of(num_c, 1, 0), 6, "slider numbers: colour 6 elsewhere");
	check(colour_of(num_c, 0, 1), 3, "slider numbers: colour 3 when greyed");
	check(colour_of(num_c, 0, 0), 3,
		"slider numbers: greyed off the cursor too");

	// The arithmetic the position is held against, in the engine's text.
	check(strstr(frame, "return long(400.0F * (float)phd_winxmax / 640.0F) "
		"+ GetFixedScale(200) + 1\r\n\t\t+ GetFixedScale(4);") != 0, 1,
		"slider numbers: MenuSliderNumberX is the arithmetic stated here");
	check(strstr(bars, "sx = (float)x * (float)phd_winxmax / 640.0F;") != 0
		&& strstr(bars, "w = (float)GetFixedScale(width);") != 0
		&& strstr(bars, "DrawColoredRect(sx - 1, sy - 1, sx + w + 1,") != 0,
		1, "slider numbers: DoSlider's x, width and frame");
	check(strstr(bars, "v[0].sx = x0;") != 0 && strstr(bars, "v[1].sx = x1;")
		!= 0 && strstr(bars, "v[2].sx = x1;") != 0
		&& strstr(bars, "v[3].sx = x0;") != 0, 1,
		"slider numbers: DrawColoredRect puts the frame's x unshifted");
	check(strstr(out, "x = (phd_winwidth > w) ? MulDiv(phd_winwidth, unit, "
		"w) : unit;") != 0 && strstr(out, "y = (phd_winheight > h) ? "
		"MulDiv(phd_winheight, unit, h) : unit;") != 0 && strstr(out,
		"return x < y ? x : y;") != 0, 1,
		"slider numbers: GetFixedScale, the smaller of the two scales");
	check(strstr(text, "w = short((float)copy[i].w * float(phd_winxmax / "
		"512.0F));") != 0 && strstr(text, "x2 += def->w;") != 0, 1,
		"slider numbers: the font scales by phd_winxmax / 512");

	// The digits' widths, read from game\chardef.inc's rows.
	{
		const char* p = strstr(defs, "\t{");
		int n = 0;

		while (p && n < '9' - '!' + 1)
		{
			if (n >= '0' - '!' || n == '%' - '!')
			{
				const char* c = strchr(p, ',');

				c = c ? strchr(c + 1, ',') : 0;

				if (n == '%' - '!')
					sign = c ? strtol(c + 1, 0, 10) : -1;
				else
					digit[n - ('0' - '!')] = c ? strtol(c + 1, 0, 10) : -1;
			}

			n++;
			p = strstr(p + 1, "\n\t{");
		}

		for (k = 0; k < 10; k++)
			check((int)digit[k], (int)want_digit[k],
				"slider numbers: the digits' widths in game\\chardef.inc");

		check((int)sign, 15,
			"slider numbers: the percent sign is 15 wide in game\\chardef.inc");
		check('%' - '!' >= 0 && '%' - '!' < 106, 1,
			"slider numbers: the classic font's 106 rows hold the sign");
	}

	// The tightest layout and a wide one, to the pixel.
	check(slider_frame_free(640, 480), 601,
		"slider numbers: 640x480, the frame's last pixel is 600");
	check(slider_number_x(640, 480), 604,
		"slider numbers: 640x480, the number starts at 604");
	check(slider_number_x(640, 480) - slider_frame_free(640, 480), 3,
		"slider numbers: 640x480, three pixels to the frame");
	check(digits_width("100", digit, 640), 31,
		"slider numbers: 640x480, \"100\" is 31 pixels");
	check(640 - (slider_number_x(640, 480) + digits_width("100", digit, 640)),
		5, "slider numbers: 640x480, five pixels to the edge");
	check(slider_frame_free(1920, 1080), 1651,
		"slider numbers: 1920x1080, the frame's last pixel is 1650");
	check(slider_number_x(1920, 1080), 1659,
		"slider numbers: 1920x1080, the number starts at 1659");
	check(digits_width("100", digit, 1920), 96,
		"slider numbers: 1920x1080, \"100\" is 96 pixels");

	/* Every width from 640 to 3840 in seven shapes: the widest number of
	 * 0..100 clear of the frame by 3 pixels and of the edge by 4. */
	for (i = 0; i < 7; i++)
	{
		int to_bar = 1 << 30, to_edge = 1 << 30, left = 1;

		for (long w = 640; w <= 3840; w++)
		{
			long h = (w * shape[i][1] + shape[i][0] / 2) / shape[i][0];
			long x, widest = 0;
			char buf[8];

			if (h < 480)
				h = 480;

			for (k = 0; k <= 100; k++)
			{
				snprintf(buf, sizeof(buf), "%d", k);

				if (digits_width(buf, digit, w) > widest)
					widest = digits_width(buf, digit, w);
			}

			x = slider_number_x(w, h);

			if (x - slider_frame_free(w, h) < to_bar)
				to_bar = (int)(x - slider_frame_free(w, h));

			if (w - (x + widest) < to_edge)
				to_edge = (int)(w - (x + widest));

			// Right of the value column, where a value row's text starts.
			if (x <= w / 2 + ((w / 2) >> 2))
				left = 0;
		}

		check(to_bar >= 3, 1, "slider numbers: 3 pixels clear of the frame");
		check(to_edge >= 4, 1, "slider numbers: 4 pixels clear of the edge");
		check(left, 1, "slider numbers: right of the value column");
	}

	slider_sign_room(frame, digit, sign, shape);

	free(frame);
	free(bars);
	free(out);
	free(text);
	free(defs);
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
	graphics_softbody_row();
	graphics_water_row();
	graphics_reflections_row();
	graphics_shadow_row();
	hd_switch_rows();
	graphics_hd_row();
	display_gamma_row();
	slider_numbers();
	slider_reads();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

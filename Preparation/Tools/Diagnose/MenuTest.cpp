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
 * named after the two lists the engine really has: sixteen keyboard roles and
 * fifteen pad roles, and the balanced split the user asked for (8+8 and 8+7).
 */

#include <stdio.h>
#include <string.h>

#include "../../../TOMB5 Code/TOMB5/game/menu_nav.h"
#include "../../../TOMB5 Code/TOMB5/game/key_scheme.h"

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
 * of keys the Modern row does not use (the function keys and the numeric
 * keypad), and nothing else about them matters. */
static const short kOther[KEY_SCHEME_ROLES] =
{
	0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43,
	0x44, 0x57, 0x58, 0x47, 0x48, 0x49, 0x4B, 0x4C, 0x4D
};

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
	KeySchemeConflicts(kOther, kModern, got);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 0, "a default key nobody claimed is free");

	// A player who never rebinds: the active set IS the default row, so every
	// default key is "claimed" - by the role that owns it. The fallback is
	// dead and costs nothing, because the primary binding is the same key.
	KeySchemeConflicts(kModern, kModern, got);

	for (i = 0; i < KEY_SCHEME_ROLES; i++)
		check((int)got[i], 1, "an untouched set claims its own defaults");

	// One claim at a time, swept over all eighteen roles: only the role whose
	// DEFAULT key was taken may go silent, and no other.
	for (r = 0; r < KEY_SCHEME_ROLES; r++)
	{
		memcpy(active, kOther, sizeof(active));
		active[0] = kModern[r];

		KeySchemeConflicts(active, kModern, got);

		for (i = 0; i < KEY_SCHEME_ROLES; i++)
			check((int)got[i], i == r ? 1 : 0, "exactly the claimed default is flagged");
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

int main()
{
	printf("MenuTest - game\\menu_nav.h, game\\key_scheme.h (#161, rounds 161b1 and 161b2a)\n\n");

	cursor_all_enabled();
	cursor_disabled();
	paging_engine_cases();
	paging_ends();
	paging_sweep();
	page_step();
	key_modern_row_is_sound();
	key_row_sound_rejects();
	key_conflicts();
	key_seat();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

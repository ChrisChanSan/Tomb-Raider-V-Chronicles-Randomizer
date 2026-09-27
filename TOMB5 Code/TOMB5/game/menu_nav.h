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
#pragma once

/* The options menu's row and paging arithmetic, kept free of drawing.
 *
 * Where a row sits, whether the cursor may stand on it, and which rows a
 * page shows when there are more of them than fit on the screen. These
 * were previously answered by hand at each call site, each with its own
 * bit shift and its own clamp, which made them easy to get out of step
 * with each other as rows were added.
 *
 * Includes no engine header on purpose: it has to compile on its own,
 * because Preparation/Tools/Diagnose/MenuTest.cpp includes this file
 * directly - not a copy of it - and asserts on it, the same way
 * InterpTest.cpp does with game/interpolate_math.h. There is no twin to
 * keep in step.
 *
 * Deliberately excludes pixels: the row pitch, the title line and the
 * bottom line are the drawing's business (game/menu_frame.cpp) and need
 * font_height, which needs the engine. This file counts rows, not
 * pixels.
 *
 * The enabled mask: every function below takes the page's rows as an
 * array of one byte per row, nonzero meaning the cursor may stand
 * there. A null pointer means every row is enabled, the common case,
 * saving the caller a buffer. A disabled row is still drawn (in colour
 * 3); only the cursor skips it, which is how "Special Features" outside
 * the title screen and "Camera speed" under Tank are expressed.
 */

/* The cursor steps and stops; it does not wrap. That matches the base
 * behaviour of every page ("if (!sel) sel = 1;" / "if (sel >
 * 1<<(n-1)) sel = ...", specific/LoadSave.cpp): a menu whose cursor
 * runs off the bottom back to the top is a different menu, not a
 * tidier one. A step that finds no enabled row in its direction leaves
 * the cursor exactly where it was. */
inline int MenuCursorStep(const unsigned char* enabled, int n, int cur, int dir)
{
	int i;

	if (n <= 0)
		return 0;

	if (dir == 0)
		return cur;

	for (i = cur + dir; i >= 0 && i < n; i += dir)
		if (!enabled || enabled[i])
			return i;

	return cur;
}

/* The first row the cursor may stand on, or -1 when a page has none at all
 * (which the frame draws as a page with no cursor rather than refusing it). */
inline int MenuFirstEnabled(const unsigned char* enabled, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (!enabled || enabled[i])
			return i;

	return -1;
}

/* A cursor remembered from an earlier visit is not to be trusted: the
 * page may have fewer rows now, or the row it stands on may have been
 * switched off since (Camera speed the moment the Control type row
 * above it goes back to Tank). Clamps into range first, then onto the
 * nearest enabled row, searching downward first, since that is the
 * direction base's own clamp moved the cursor. */
inline int MenuClampCursor(const unsigned char* enabled, int n, int cur)
{
	int i;

	if (n <= 0)
		return 0;

	if (cur < 0)
		cur = 0;

	if (cur > n - 1)
		cur = n - 1;

	if (!enabled || enabled[cur])
		return cur;

	for (i = cur + 1; i < n; i++)
		if (enabled[i])
			return i;

	for (i = cur - 1; i >= 0; i--)
		if (enabled[i])
			return i;

	return cur;
}

/* How many pages, and balanced. Sixteen rows at a maximum of thirteen
 * is two pages, and splitting them as thirteen and three would be
 * uneven - so the page count is decided first (ceil(n / max)) and the
 * rows are then spread evenly over it (ceil(n / pages)), giving 8 + 8.
 * Fifteen gives 8 + 7. Same arithmetic whether the list is the sixteen
 * keyboard roles or the fifteen pad ones. A page always exists, even
 * for an empty list. */
inline int MenuPageCount(int n, int max)
{
	if (max < 1 || n <= 0)
		return 1;

	return (n + max - 1) / max;
}

inline int MenuRowsPerPage(int n, int max)
{
	int pages;

	if (n <= 0)
		return 0;

	pages = MenuPageCount(n, max);
	return (n + pages - 1) / pages;
}

inline int MenuPageFirstRow(int n, int max, int page)
{
	return page * MenuRowsPerPage(n, max);
}

/* The last page is the short one: rows per page everywhere, whatever is left
 * over at the end. Never negative, so a page index past the end reads as
 * empty rather than as a buffer overrun waiting to happen. */
inline int MenuRowsOnPage(int n, int max, int page)
{
	int per, left;

	per = MenuRowsPerPage(n, max);
	left = n - MenuPageFirstRow(n, max, page);

	if (left <= 0)
		return 0;

	return left < per ? left : per;
}

/* The arrow row wraps in both directions - unlike base's tomb5-options
 * paging, where either arrow advanced and neither went back
 * (tomb5/troyestuff.cpp). With two pages that is the same thing; with
 * three it is not. */
inline int MenuPageStep(int page, int pages, int dir)
{
	if (pages < 1)
		return 0;

	page = (page + dir) % pages;

	if (page < 0)
		page += pages;

	return page;
}

/* One frame of a 0..top slider: one down for a negative dir, one up for a
 * positive one, nothing for 0, and the two ends hold. The rule Camera speed
 * and Dead zone write out by hand (game/menu_frame.cpp), put here for the
 * Mouse sensitivity row so that MenuTest can sweep it. */
inline int MenuSliderStep(int value, int dir, int top)
{
	if (dir < 0 && value > 0)
		value--;

	if (dir > 0 && value < top)
		value++;

	return value;
}

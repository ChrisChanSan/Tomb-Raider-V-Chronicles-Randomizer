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
#include "../global/types.h"

/* The options menu as a table.
 *
 * The menu is a list of pages, each a title and an array of rows, drawn
 * and driven by one piece of shared code, rather than a separate
 * hand-written state machine per page. Adding a row is a line in a
 * table; adding a page is a table plus a line in the page list.
 *
 * The row layout is a navigation aid, not a binary constraint: a row
 * that opens something is centred; a row that is a setting has its
 * label at the left and its value in the right-hand column. That
 * difference is the only thing the row kinds below really encode.
 *
 * The arithmetic is not here. Cursor walking, clamping and the balanced
 * paging live in game/menu_nav.h, which has no engine dependency and is
 * checked by Preparation/Tools/Diagnose/MenuTest.cpp. This file is the
 * drawing, the tables and the rows' own logic - carried over from
 * specific/LoadSave.cpp and tomb5/troyestuff.cpp row by row.
 */

/* What a row is. The kind decides where it is drawn and which of the
 * callbacks below are asked for anything; everything a kind does not
 * use stays null. */
enum menu_row_kind
{
	MROW_TAB,		// centred - opens another page
	MROW_ACTION,		// centred - does something and stays
	MROW_VALUE,		// left label, right value, LEFT/RIGHT step it
	MROW_SLIDER,		// left label, a DoSlider bar in the value column
	MROW_BINDING		// left label or glyph, right binding name, SELECT captures a key
				// (the arrow row is not a kind - the frame appends it itself)
};

/* Named pages instead of magic numbers. MENU_PAGE_OPTIONS must stay 0,
 * it is the root the menu always opens on. */
enum menu_page_id
{
	MENU_PAGE_OPTIONS = 0,
	MENU_PAGE_AUDIO,
	MENU_PAGE_CONTROLS,
	MENU_PAGE_KEYBINDS,
	MENU_PAGE_VIEW,		// the six first-person rows, always offered
	MENU_PAGE_GRAPHICS,
	MENU_PAGE_DISPLAY,
	MENU_PAGE_HUD,
	MENU_PAGE_TOMB5,	// a table now, no longer the old TroyeMenu
	MENU_PAGE_SPECIAL,	// the existing Special Features screen, untouched inside
	MENU_PAGE_CREDITS,	// the existing Credits screen, untouched inside
	/* The four mode rows live on a sub-page of their own. Appended here
	 * rather than inserted after MENU_PAGE_CONTROLS where it logically
	 * belongs, so that no existing id changes its number: menu_pages[]
	 * below is positional to this enum, and the frame's cursor and page
	 * memories are indexed by it. The visible tab order is the Controls
	 * page's own tab row, not this list order. */
	MENU_PAGE_MODES,
	MENU_PAGE_COUNT
};

/* Which registry block a change belongs to. Block A is written whole
 * on every edit, as it always was; block B is now written when a page
 * on which one of its values changed is left, rather than only at a
 * clean program exit. */
#define MENU_SAVE_NONE	0
#define MENU_SAVE_TOMB5	1	// save_new_tomb5_settings()
#define MENU_SAVE_GAME	2	// SaveSettings()

/* Every callback gets the row's own `param`, so one set of functions
 * can serve a whole list of rows - which is what the sixteen key
 * bindings are: one value function, one select function, and the role
 * index in the table. That index also retires the old Controls page's
 * shift-then-count blocks (specific/LoadSave.cpp at base), which turned
 * a selection bit back into an array index and had to be recounted
 * every time a row was added above the list. */
typedef bool (*menu_pred)(long param);			// may the cursor stand here?
typedef bool (*menu_step)(long param, long dir);	// LEFT/RIGHT; true when the setting really changed
typedef void (*menu_act)(long param);			// SELECT
typedef const char* (*menu_text)(long param, char* buf);// the value column; buf is 256 bytes
typedef long (*menu_num)(long param);			// a slider's 0..100

struct menu_row
{
	long kind;
	const char* label;	// the English literal: the CustomText key, the log's name, or a glyph
	long text_id;		// a SCRIPT_TEXT id to draw instead of the literal, else -1
	menu_text value;
	menu_step step;
	long save;
	menu_pred enabled;	// null = always selectable
	long target;		// MROW_TAB: the page it opens
	menu_act select;
	menu_num slider;
	long param;
};

struct menu_page
{
	const char* name;	// the English title: the CustomText key and the MENULOG name
	long title_id;		// a SCRIPT_TEXT id for the title instead, else -1
	const menu_row* rows;
	long num_rows;
	long fixed_rows;	// leading rows repeated on every page of a paged list
	void (*resolve)(const menu_row** rows, long* num);	// null for a page whose rows never change
};

/* One geometry for every page; font_height is never touched.
 *
 * font_height is GetFixedScale(28) (game/text.cpp), which scales with
 * the window, so the number of rows that fit is the same at every
 * resolution: 640x480 is the tightest case and phd_winymax is 479
 * there. An ordinary capital's glyph cell reaches 4 pixels below its
 * anchor (game/chardef.inc h=13/YOffset=-11, rescaled by
 * phd_winymax/240 in game/text.cpp), so the last usable anchor is
 * 16 * 28 = 448, bottom 452. That line is the arrow row's, pinned
 * there, and the rows run from 4 to 15 - twelve of them, which is
 * exactly what the fullest page (Graphics: the Display tab and eleven
 * settings) needs. */
#define MENU_TITLE_ROW	2
#define MENU_FIRST_ROW	4
#define MENU_MAX_ROWS	12
#define MENU_BOTTOM_ROW	16

// The whole menu, one frame of it. Called from DoOptions() and nowhere else.
void MenuFrameRun();

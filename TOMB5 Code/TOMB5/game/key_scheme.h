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

/*
 * The two keyboard sets - the part that is pure arithmetic.
 *
 * Modern has a default row of its own, Tank keeps Core's. The
 * keyboard therefore has two default rows and two player-editable sets,
 * one per control scheme, and three small questions come with them: which
 * default key a role still answers to, which set sits in the active
 * table, and whether a default row is sound at all. All three are
 * arithmetic over KEY_SCHEME_ROLES shorts and none of them needs the
 * engine.
 *
 * It includes no engine header on purpose, exactly as menu_nav.h does and
 * for the same reason: MenuTest.cpp includes this file directly - not a
 * copy of it - and asserts on it, so there is no twin to keep in step.
 *
 * What is not here: dinput.h. The scan codes below are therefore plain
 * numbers, and specific/input.cpp carries one static_assert per role
 * saying which DIK_ each one is - that is the other half of "one row, not
 * two".
 */

/* Thirty-five roles, and every one of the first nineteen keeps its
 * number. Slot 18 is the Secondary action and slots 19..33 are the
 * hotkeys that used to be hard-wired scan codes in specific/input.cpp -
 * the two medipacks, quick save and quick load, the six weapons of
 * DoWeaponHotkey, and the five cheats. Slot 34 is the HD/SD switch.
 *
 * New roles are always appended, never inserted, because every slot is
 * addressed by number - in the registry (Key0..Key34, MKey0..MKey34), in
 * the engine's own Key(n) calls and in the bind rows of menu_frame.cpp -
 * so a set written before a role was added reads back unchanged, with
 * only the new slots missing from it; those then take their row's
 * default, which is the key that was hard-wired there before the role
 * existed. */
#define KEY_SCHEME_ROLES	35

/* The Secondary role's slot, so that the one number lives in one place. It
 * was the last slot while there were nineteen roles; since the hotkeys
 * were appended behind it, it must stay the literal 18 - a saved registry
 * calls that slot Key18/MKey18 and means the Secondary action. The number
 * is no longer derivable from the role count, so it is written out and
 * checked by MenuTest. */
#define KEY_ROLE_SECOND		18

/* The hotkey slots by name, so that no call site carries a bare number.
 * The order is the order of the two default rows below and of the bind
 * rows on the Key bindings page; the weapons are in DoWeaponHotkey's own
 * order, which is the order of the keys 1..6 it used to read directly. */
#define KEY_ROLE_MEDI_SMALL	19
#define KEY_ROLE_MEDI_LARGE	20
#define KEY_ROLE_SAVE		21
#define KEY_ROLE_LOAD		22
#define KEY_ROLE_WEAPON_FIRST	23	/* 23..28: pistols, shotgun, uzi, revolver, HK, crossbow */
#define KEY_ROLE_WEAPONS	6
#define KEY_ROLE_CHEAT_FLY	29
#define KEY_ROLE_CHEAT_GUNS	30
#define KEY_ROLE_CHEAT_ITEMS	31
#define KEY_ROLE_CHEAT_HEAL	32
#define KEY_ROLE_CHEAT_SKIP	33

/* The HD/SD switch: one press draws the world classic, the next HD again
 * (specific/hd/hd_switch.h). Live in every game, not only with Cheats on,
 * and F12 in both default rows. A set stored before the slot existed has no
 * Key34/MKey34 and reads F12, which no older set can hold: the binding page
 * named no F12, so no capture could put it on another role. */
#define KEY_ROLE_HD_SWITCH	34

/* The mouse's five buttons as key codes. A key code is a DIK_ scan code,
 * 0x01..0xFF, and indexes keymap[256]; the buttons sit just above that range,
 * 0x100 + n, so they can live in the same bind slots, the same registry values
 * and the same conflict table as any key without a second field anywhere.
 * n is the button's Raw Input number - left, right, middle, X1, X2 - which is
 * also the bit specific\winmain.cpp gives it in the mouse's button mask.
 *
 * Key() (specific\input.cpp) reads the mask for a code at or above
 * KEY_MOUSE_FIRST and keymap below it, and specific\input.cpp asserts that
 * no DIK_ code reaches the range. */
#define KEY_MOUSE_FIRST		0x100
#define KEY_MOUSE_LEFT		0x100
#define KEY_MOUSE_RIGHT		0x101
#define KEY_MOUSE_MIDDLE	0x102
#define KEY_MOUSE_X1		0x103
#define KEY_MOUSE_X2		0x104
#define KEY_MOUSE_BUTTONS	5

inline int KeySchemeIsMouse(long code)
{
	return (code >= KEY_MOUSE_FIRST && code < KEY_MOUSE_FIRST + KEY_MOUSE_BUTTONS) ? 1 : 0;
}

// The button's bit in the mouse's mask, or 0 for anything that is not a button.
inline long KeySchemeMouseBit(long code)
{
	return KeySchemeIsMouse(code) ? (1L << (code - KEY_MOUSE_FIRST)) : 0;
}

/* The button's name on the Key bindings page. The key-name tables in
 * specific\input.cpp are one German table and one for every other language,
 * so the buttons have the same two columns. The names are as short as the
 * keys' own ("Pad0", "Leert.") so the value column never runs out. */
inline const char* KeySchemeMouseName(long code, int german)
{
	static const char* const english[KEY_MOUSE_BUTTONS] = { "Mouse L", "Mouse R", "Mouse M", "Mouse 4", "Mouse 5" };
	static const char* const deutsch[KEY_MOUSE_BUTTONS] = { "Maus L", "Maus R", "Maus M", "Maus 4", "Maus 5" };

	if (!KeySchemeIsMouse(code))
		return 0;

	return (german ? deutsch : english)[code - KEY_MOUSE_FIRST];
}

/* The Modern default row, in the engine's own slot order: 0-3
 * forward/back/left/right, 4 duck, 5 dash, 6 walk, 7 jump, 8 action, 9 draw,
 * 10 flare, 11 look, 12 roll, 13 inventory, 14 step left, 15 step right,
 * 16 pause, 17 select, 18 secondary action.
 *
 * A single table row, not a negotiated one: it is a starting point meant
 * to be corrected key by key after playing, not a claim that every key is
 * final. The scan codes are positional, so the two letter keys that move
 * between layouts print differently on a QWERTZ keyboard: 0x2C is the key
 * left of X, which is Z on QWERTY and Y on QWERTZ.
 *
 *   W S A D  move          C duck      LShift dash   LAlt walk
 *   Space jump             E action    Mouse R draw  F flare
 *   Mouse M look           LCtrl roll  Escape inventory
 *   Z step left            X step right               P pause   Return select
 *   Mouse L secondary
 *
 * Draw, look and the secondary action are on the right, the middle and
 * the left mouse button, this project's choice: they are the three the
 * camera hand already rests on. The three sit on no other slot of either
 * default row, so the row stays sound. Tank's own slot 18 is
 * DIK_HOME (specific/input.cpp), free in Core's cluster; Tank's row has no
 * mouse button at all.
 *
 * Slots 19..33 are the keys the engine already answered to, moved out of
 * specific/input.cpp's own `keymap[DIK_x]` reads and into roles a player
 * can rebind. That is why the Modern half of this row is the same
 * fifteen keys as Tank's: they are not a new layout, they are the layout
 * the engine has had all along, so an untouched registry answers to
 * exactly the keys it answered to before.
 *
 * The one deliberate difference: quick load is F9 under Modern (a free
 * choice of this project) and stays F6 under Tank, which is
 * Core's. F1/F2/F7/F8/F11 are the five cheat roles; they are the free F
 * keys once F4 (the outfit changer, hd_outfitmenu.cpp) and F10 (IN_E) are
 * left alone, and F5/F6/F9 belong to save and load. F12 is the HD/SD
 * switch (slot 34). F3 was also considered, but the outfit changer reads F4
 * alone, and the one DIK_F3 in the whole tree is Core's level skip in the
 * _DEBUG branch of LaraCheatyBits (laramisc.cpp), which no release build
 * contains. F3 is still left unassigned here, so a debug build keeps it. */
#define KEY_SCHEME_MODERN_ROW	\
{							\
	0x11, 0x1F, 0x1E, 0x20,	/* W S A D                 */	\
	0x2E, 0x2A, 0x38, 0x39,	/* C LShift LAlt Space     */	\
	0x12, KEY_MOUSE_RIGHT, 0x21, KEY_MOUSE_MIDDLE,	/* E, Mouse R, F, Mouse M */	\
	0x1D, 0x01, 0x2C, 0x2D,	/* LCtrl Escape Z X        */	\
	0x19, 0x1C, KEY_MOUSE_LEFT,	/* P, Return, Mouse L      */	\
	0x0B, 0x0A, 0x3F, 0x43,	/* 0 9 F5 F9 - medipacks, save, load */	\
	0x02, 0x03, 0x04, 0x05,	/* 1 2 3 4 - pistols shotgun uzi revolver */	\
	0x06, 0x07,				/* 5 6 - HK crossbow       */	\
	0x3C, 0x41, 0x42, 0x57, 0x3B,	/* F2 F7 F8 F11 F1 - five cheats */	\
	0x58					/* F12 - the HD/SD switch  */	\
}

/* One measured collision in this row, named here rather than quietly
 * changed. Slot 6, walk, is 0x38 = DIK_LMENU, Left Alt - and three screens
 * that own the input suppress confirm while Left Alt is down: gameflow.cpp
 * (the title), specific/LoadSave.cpp (load/save) and
 * specific/hd/hd_outfitmenu.cpp (the outfit changer) all ask
 * `dbinput & IN_SELECT && !keymap[DIK_LALT]`. That guard is not ours and
 * is not a defect: it exists so that Alt+Enter, the fullscreen toggle,
 * does not also choose the row under the cursor. A Modern player holding
 * walk on one of those three screens therefore cannot confirm until the
 * key is released; Escape still leaves, and nothing is locked.
 *
 * It stays as it is on purpose. Dropping the guard under Modern would
 * hand back exactly the double action it was written to prevent, and this
 * default row is meant to be corrected key by key after playing rather
 * than guessed at here. */

/* Which roles still answer to their default key. Core's rule, carried
 * over statement for statement from specific/LoadSave.cpp's
 * CheckKeyConflicts(): a role keeps its default key as a second trigger
 * unless some active binding has claimed that key, in which case the
 * default must stay silent or one press would do two things. `def` is
 * the active scheme's default row.
 *
 * An inert binding claims nothing. `second` says whether the Secondary
 * role is live, which is `tomb5.action_split`. With "One key" that role
 * produces no bit whatever it is bound to - the two producers in
 * specific/input.cpp are behind the same flag - so counting its key as a
 * claimer would silence another role's default-key fallback in favour of
 * a key that cannot do anything. A player who binds Secondary onto, say,
 * End while in "Separate" and then goes back to "One key" would have lost
 * the default End with nothing put in its place. At the defaults this
 * changes no byte: neither default row contains the Secondary slot's own
 * default key on another slot (Tank DIK_HOME, Modern KEY_MOUSE_LEFT), so
 * the claim is never made there.
 *
 * A mouse button is a key code like any other here: the comparison is of
 * codes, so a button claims and is claimed exactly as a key is.
 *
 * The same rule applies to the five cheat roles. `cheats` is
 * `tomb5.enable_cheats`. With Cheats off those five rows are not even
 * offered on the binding page (menu_frame.cpp) and nothing reads them -
 * laramisc.cpp asks them only from inside LaraCheatyBits, which is behind
 * the same flag - so a key bound to one of them is exactly as inert as
 * Secondary under "One key", and counting it as a claimer would silence
 * some other role's default-key fallback in favour of a binding that
 * cannot fire. At the defaults this changes no byte either: F1, F2, F7,
 * F8 and F11 appear in no other slot of either default row, so the claim
 * is never made there. The row that makes the five live - the Cheats
 * setting itself - recomputes the table, exactly as Reset, a capture and
 * the Action row do. */
inline void KeySchemeConflicts(const short* active, const short* def, long* out, long second, long cheats)
{
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		out[i] = 0;

		for (int j = 0; j < KEY_SCHEME_ROLES; j++)
		{
			if (j == KEY_ROLE_SECOND && !second)
				continue;

			// Named at both ends, so a slot appended behind SKIP later
			// does not quietly join the exemption.
			if (j >= KEY_ROLE_CHEAT_FLY && j <= KEY_ROLE_CHEAT_SKIP && !cheats)
				continue;

			if (def[i] == active[j])
			{
				out[i] = 1;
				break;
			}
		}
	}
}

/* Seats the other set. The engine keeps the active scheme's set in the
 * table every existing reader reads, and the other scheme's set parked
 * beside it; a scheme change exchanges the two, so the set that leaves is
 * kept rather than overwritten. `seated` is which scheme is in `active`
 * today (0 Tank, 1 Modern), `want` which one should be. The answer is the
 * new `seated`.
 *
 * It is idempotent - asking for the scheme that is already seated costs one
 * comparison and moves nothing - because the call sites are a menu row and a
 * one-time seat at startup, and neither of them may double-swap. */
inline int KeySchemeSeat(short* active, short* parked, int seated, int want)
{
	want = want ? 1 : 0;

	if (seated == want)
		return seated;

	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		const short t = active[i];

		active[i] = parked[i];
		parked[i] = t;
	}

	return want;
}

/* Is a default row sound? Every slot bound and no key twice. A default row
 * with a hole in it would leave a role unreachable until the player found the
 * binding page, and a key on two roles would make one press do two things from
 * the very first launch - the case CheckKeyConflicts exists to stop, arriving
 * through the front door. Scan code 0 is no key at all (dinput.h starts at
 * DIK_ESCAPE = 1), so it is what "unbound" looks like here. */
inline int KeySchemeRowSound(const short* row)
{
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		if (row[i] <= 0)
			return 0;

		for (int j = i + 1; j < KEY_SCHEME_ROLES; j++)
			if (row[i] == row[j])
				return 0;
	}

	return 1;
}

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

/* THE TWO KEYBOARD SETS - THE PART THAT IS PURE ARITHMETIC (#161, round
 * 161b2a, item 1).
 *
 * The user's decision of 2026-09-16: "Tastatur modern = Remaster, klassisch =
 * Core". So the keyboard has two default rows and two user sets, one per
 * control scheme, and three small questions come with them: which default key
 * a role still answers to, which set sits in the active table, and whether a
 * default row is sound at all. All three are arithmetic over eighteen shorts
 * and none of them needs the engine.
 *
 * It includes NO engine header on purpose, exactly as game\menu_nav.h does and
 * for the same reason: Preparation\Tools\Diagnose\MenuTest.cpp includes THIS
 * FILE - not a copy of it - and asserts on it. There is no twin to keep in
 * step.
 *
 * WHAT IS NOT HERE: dinput.h. The scan codes below are therefore numbers, and
 * specific\input.cpp carries eighteen static_asserts saying which DIK_ each one
 * is - that is the other half of "one row, not two".
 */

/* THIRTY-FOUR SINCE ROUND 161b2d (#161, item 2), and every one of the
 * nineteen before them keeps its number. Slot 18 is the SECONDARY action
 * (round 161b2c, item 2) and slots 19..33 are the hotkeys that used to be
 * hard-wired scan codes in specific\input.cpp - the two medipacks, quick save
 * and quick load, the six weapons of DoWeaponHotkey, and the five cheats.
 *
 * APPENDED, NEVER INSERTED, for the same reason the nineteenth slot was: every
 * slot is addressed by NUMBER - in the registry (Key0..Key33, MKey0..MKey33),
 * in the engine's own Key(n) calls and in the bind rows of
 * game\menu_frame.cpp - so a set written by the 161b2c build reads back
 * unchanged and only the fifteen new slots are missing from it, which is
 * true: they then take their row's default, which is the key that was
 * hard-wired there before. */
#define KEY_SCHEME_ROLES	34

/* The Secondary role's slot, so that the one number lives in one place. It was
 * "the last slot" while there were nineteen; since the hotkeys were appended
 * behind it, it is the LITERAL 18 and must stay there - a registry written by
 * the 161b2c build calls that slot Key18/MKey18 and means the Secondary
 * action. The number is no longer derivable from the count, so it is written
 * out and checked by MenuTest. */
#define KEY_ROLE_SECOND		18

/* THE HOTKEY SLOTS BY NAME, so that no call site carries a bare number
 * (#161, round 161b2d, item 2). The order is the order of the two default rows
 * below and of the bind rows on the Key bindings page; the weapons are in
 * DoWeaponHotkey's own order, which is the order of the keys 1..6 it used to
 * read directly. */
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

/* THE MODERN DEFAULT ROW, in the engine's own slot order: 0-3
 * forward/back/left/right, 4 duck, 5 dash, 6 walk, 7 jump, 8 action, 9 draw,
 * 10 flare, 11 look, 12 roll, 13 inventory, 14 step left, 15 step right,
 * 16 pause, 17 select, 18 secondary action.
 *
 * ONE TABLE ROW AND NOT A NEGOTIATION - the user asked for exactly that and
 * said he would correct individual keys after playing, which is what makes the
 * row a starting point rather than a claim. The scan codes are POSITIONAL, so
 * the two letter keys that move between layouts print differently on a QWERTZ
 * keyboard: 0x2C is the key left of X, which is Z on QWERTY and Y on QWERTZ.
 *
 *   W S A D  move          C duck      LShift dash   LAlt walk
 *   Space jump             E action    Q draw        F flare
 *   R look                 LCtrl roll  Escape inventory
 *   Z step left            X step right               P pause   Return select
 *   Numpad0 secondary
 *
 * SLOT 18 IS A PLACEHOLDER AND SAYS SO (round 161b2c, item 2). His keyboard
 * page has the secondary action on Mouse Left, and the mouse is a later round
 * - Key() reads `keymap` and has no mouse button to read yet. Numpad 0 is a
 * key the Modern row does not otherwise use, so the row stays sound and the
 * role is reachable from the first launch; it is meant to be replaced, not
 * defended. Tank's own slot 18 is DIK_HOME (specific\input.cpp), free in
 * Core's cluster for the same reason.
 *
 * SLOTS 19..33 ARE THE KEYS THE ENGINE ALREADY ANSWERED TO (round 161b2d,
 * item 2), moved out of specific\input.cpp's own `keymap[DIK_x]` reads and
 * into roles a player can rebind. That is why the Modern half of this row is
 * the SAME fifteen keys as Tank's: they are not a new layout, they are the
 * layout the engine has had all along, so an untouched registry answers to
 * exactly the keys it answered to before.
 *
 * THE ONE DELIBERATE DIFFERENCE, and it is the user's own: quick load is F9
 * under Modern (the binding page he compares with has it there) and stays F6
 * under Tank, which is Core's. F1/F2/F7/F8/F11 are the five cheat roles; they
 * are the free F keys once F4 (the outfit changer,
 * specific\hd\hd_outfitmenu.cpp), F10 (IN_E) and F12 (specific\output.cpp) are
 * left alone, and F5/F6/F9 belong to save and load. F3 stood in that list
 * beside F4 until audit round A1 measured it: the outfit changer reads F4
 * alone, and the one DIK_F3 in the whole tree is Core's level skip in the
 * _DEBUG branch of LaraCheatyBits (game\laramisc.cpp), which no release build
 * contains. It is still left unassigned here, so a debug build keeps it. */
#define KEY_SCHEME_MODERN_ROW	\
{							\
	0x11, 0x1F, 0x1E, 0x20,	/* W S A D                 */	\
	0x2E, 0x2A, 0x38, 0x39,	/* C LShift LAlt Space     */	\
	0x12, 0x10, 0x21, 0x13,	/* E Q F R                 */	\
	0x1D, 0x01, 0x2C, 0x2D,	/* LCtrl Escape Z X        */	\
	0x19, 0x1C, 0x52,		/* P Return Numpad0        */	\
	0x0B, 0x0A, 0x3F, 0x43,	/* 0 9 F5 F9 - medipacks, save, load */	\
	0x02, 0x03, 0x04, 0x05,	/* 1 2 3 4 - pistols shotgun uzi revolver */	\
	0x06, 0x07,				/* 5 6 - HK crossbow       */	\
	0x3C, 0x41, 0x42, 0x57, 0x3B	/* F2 F7 F8 F11 F1 - the five cheats */	\
}

/* ONE MEASURED COLLISION IN THAT ROW, named here rather than quietly changed
 * (round 161b2a's review). Slot 6, walk, is 0x38 = DIK_LMENU, Left Alt - and
 * three screens that own the input suppress confirm while Left Alt is down:
 * game\gameflow.cpp (the title), specific\LoadSave.cpp (load/save) and
 * specific\hd\hd_outfitmenu.cpp (the outfit changer) all ask
 * `dbinput & IN_SELECT && !keymap[DIK_LALT]`. That guard is NOT ours and is
 * not a defect: it exists so that Alt+Enter, the fullscreen toggle, does not
 * also choose the row under the cursor. A Modern player holding walk on one of
 * those three screens therefore cannot confirm until he lets the key go;
 * Escape still leaves, and nothing is locked.
 *
 * It stays as it is on purpose. Dropping the guard under Modern would hand
 * back exactly the double action it was written to prevent, and the row is the
 * user's own listing, which he said he would correct key by key after playing
 * - so this is his decision to make with a pad in his hand, not one to guess
 * at here. The fact is written down so the next round does not rediscover it.
 */

/* WHICH ROLES STILL ANSWER TO THEIR DEFAULT KEY. Core's rule, carried over
 * statement for statement from specific\LoadSave.cpp's CheckKeyConflicts():
 * a role keeps its default key as a second trigger unless some ACTIVE binding
 * has claimed that key, in which case the default must stay silent or one
 * press would do two things. `def` is the active scheme's default row - that is
 * the whole of what round 161b2a changed about it.
 *
 * AN INERT BINDING CLAIMS NOTHING (#161, round 161b2c, fix round). `second`
 * says whether the Secondary role is live, which is `tomb5.action_split`. With
 * "One key" that role produces no bit whatever it is bound to - the two
 * producers in specific\input.cpp are behind the same flag - so counting its
 * key as a claimer would silence another role's default-key fallback in favour
 * of a key that cannot do anything. A player who binds Secondary onto, say,
 * End while in "Separate" and then goes back to "One key" would have lost the
 * default End with nothing put in its place. At the defaults this changes no
 * byte: neither default row contains the Secondary slot's own default key
 * (Tank DIK_HOME, Modern DIK_NUMPAD0), so the claim was never made there.
 *
 * AND THE SAME SENTENCE FOR THE FIVE CHEAT ROLES (#161, round 161b2d, fix
 * round - the review's fifth finding). `cheats` is `tomb5.enable_cheats`. With
 * Cheats off those five rows are not even offered on the binding page
 * (game\menu_frame.cpp) and nothing reads them - game\laramisc.cpp asks them
 * only from inside LaraCheatyBits, which is behind the same flag - so a key
 * bound to one of them is exactly as inert as Secondary under "One key", and
 * counting it as a claimer would silence some other role's default-key
 * fallback in favour of a binding that cannot fire. At the defaults this
 * changes no byte either: F1, F2, F7, F8 and F11 appear in no other slot of
 * either default row, so the claim was never made there. The row that makes
 * the five live - the Cheats setting itself - recomputes the table, exactly as
 * Reset, a capture and the Action row do. */
inline void KeySchemeConflicts(const short* active, const short* def, long* out, long second, long cheats)
{
	for (int i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		out[i] = 0;

		for (int j = 0; j < KEY_SCHEME_ROLES; j++)
		{
			if (j == KEY_ROLE_SECOND && !second)
				continue;

			// Named at both ends, so a slot appended behind SKIP in a later
			// round does not quietly join the exemption.
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

/* SEATING THE OTHER SET. The engine keeps the ACTIVE scheme's set in the table
 * every existing reader reads, and the other scheme's set parked beside it; a
 * scheme change exchanges the two, so the set that leaves is kept rather than
 * overwritten. `seated` is which scheme is in `active` today (0 Tank, 1
 * Modern), `want` which one should be. The answer is the new `seated`.
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

/* IS A DEFAULT ROW SOUND? Every slot bound and no key twice. A default row
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

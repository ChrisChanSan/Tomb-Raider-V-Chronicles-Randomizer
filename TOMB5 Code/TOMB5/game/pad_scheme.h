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

/* The pad has two role-binding sets, one per Action control scheme ("One key"
 * and "Separate"), so switching schemes does not lose the player's bindings
 * for the other one. This header holds only the arithmetic on those sets
 * (role indices, bit tests, table swap); it needs no engine header, so that
 * Preparation\Tools\Diagnose\MenuTest.cpp can include it directly and check
 * the same tables the engine uses, with no copy to keep in step.
 *
 * The swap is not done by calling the keyboard's KeySchemeSeat twice: the
 * keyboard set is thirty-four shorts (scan codes), the pad set is twenty-eight
 * longs (bit indices, where -1 means unbound). Two small functions matched to
 * each array are simpler than one generic one.
 *
 * A set's chord flags are a single long, not an array, so they are exchanged
 * separately from PadSetSeat at the one call site (ApplyJoySplit) rather than
 * through a second loop and signature here. PadRoleFires below reads them.
 */

/* The pad's role count. This header's own macro, defined here (not the
 * engine's) so that MenuTest checks the same number the engine uses.
 *
 * The first sixteen slots keep their original numbers, as the keyboard's
 * thirty-four do (game\key_scheme.h): slot 16 is the Modifier, 17-22 the six
 * weapon hotkeys in DoWeaponHotkey's own order, 23-27 the five cheats.
 *
 * jLayout holds only the active set. It previously also held an unread
 * snapshot of the other scheme's set (`long[32]`, sixteen roles plus a copy
 * written by ApplyJoyScheme but never read); that snapshot is gone. Should
 * something need to show the other scheme's set, ApplyJoyScheme can compute
 * it directly (JoyDefaultRecord) rather than reading a stored copy. */
#define JOY_ROLES		28

/* The Secondary role's slot. Fixed at 15: the registry stores this slot under
 * the names JSec/SJSec for the Secondary action, so the index cannot move even
 * though the hotkey roles were appended after it. Same reasoning as
 * KEY_ROLE_SECOND's fixed 18. */
#define JOY_ROLE_SECOND		15

/* Three of the engine's role indices by name, used by the chord mask below.
 * They follow the engine's own role order (duck dash walk jump action draw
 * flare look roll inventory pause save load smallmedi largemedi secondary)
 * and cannot move: the registry stores those slots under the names
 * JFlr/JSav/JLod and SJFlr/SJSav/SJLod. The remaining roles are used as plain
 * numbers since nothing else here names them. */
#define JOY_ROLE_FLARE		6
#define JOY_ROLE_SAVE		11
#define JOY_ROLE_LOAD		12

/* The added roles by name, so no call site carries a bare number. The order
 * matches the four Separate default tables below and the bind rows on the
 * Key bindings page. */
#define JOY_ROLE_MODIFIER	16
#define JOY_ROLE_WEAPON_FIRST	17	/* 17..22: pistols, shotgun, uzi, revolver, HK, crossbow */
#define JOY_ROLE_WEAPONS	6
#define JOY_ROLE_CHEAT_FLY	23
#define JOY_ROLE_CHEAT_GUNS	24
#define JOY_ROLE_CHEAT_ITEMS	25
#define JOY_ROLE_CHEAT_HEAL	26
#define JOY_ROLE_CHEAT_SKIP	27

/* The highest bit either joystick path writes: 0-11 the face buttons, 12-15
 * whatever the path has there, 16-19 the right stick and 20-23 the d-pad
 * (specific\dxshell.cpp). A binding comes out of the registry as a plain
 * number, so this bound is checked rather than assumed. */
#define JOY_BIT_MAX		23

/* Turns a stored binding into a down/not-down answer, so that an unbound
 * role (-1) and any out-of-range value the registry should never hold are
 * both handled once instead of at every caller. Equivalent to
 * `joy_fire & (1 << bit)` for every bit the engine can actually produce, but
 * closed at both ends: below 0 or above JOY_BIT_MAX returns "not down" with
 * no shift attempted, instead of shifting by an out-of-range amount. */
inline int PadBitDown(unsigned long fire, long bit)
{
	if (bit < 0 || bit > JOY_BIT_MAX)
		return 0;

	return (fire & (1UL << bit)) != 0 ? 1 : 0;
}

/* Whether a pad role fires this reading. A role is either plain or a chord;
 * the flag sits beside the button number rather than inside it, since a bit
 * index has no spare bits and the registry value must stay a plain button
 * number:
 *
 *   chord: the Modifier is held AND the role's own button is down.
 *   plain: the role's own button is down AND the Modifier is NOT held.
 *
 * The second half of the plain rule keeps a button and Modifier+button as two
 * separate bindings instead of one press doing both.
 *
 * With the Modifier unbound (-1), PadBitDown reports it as not held, so every
 * plain role reduces to "is its button down" and no chord role can ever fire
 * - matching the One-key default tables, which bind no Modifier and no chord.
 *
 * The Modifier's own button is never accepted as a role's trigger: a chord on
 * it would fire on the Modifier press alone, and a plain role on it could
 * never fire (holding it down holds the Modifier down). One comparison
 * (button == modifier) rules out both cases regardless of how the two values
 * reached the table. */
inline int PadRoleFires(unsigned long fire, long button, int chord, long modifier)
{
	const int held = PadBitDown(fire, modifier);

	if (!PadBitDown(fire, button) || button == modifier)
		return 0;

	return chord ? held : !held;
}

/* The four Separate default layout tables, and their chord masks, as macros:
 * the engine instantiates these (specific\input.cpp) and MenuTest checks the
 * same macros, so the check runs on the exact rows a player gets.
 *
 * The One-key tables (jDef*) are a separate, fifteen-entry default layout and
 * are untouched by this file; JoyDefaultRecord fills their roles 15..27 with
 * -1 and their chord mask with 0, meaning all twelve added roles are unbound
 * on every pad path.
 *
 * Role order: duck dash walk jump action draw flare look roll inventory pause
 * save load smallmedi largemedi secondary | modifier | pistols shotgun uzi
 * revolver HK crossbow | fly guns items heal skip. -1 is unbound.
 *
 * The two DirectInput-numbered rows bind: Modifier = bit 13 (the DualSense's
 * touchpad click; bit 14 is its mute button), quick save Modifier+R3, quick
 * load Modifier+L3, flare Modifier+R1, pistols Modifier+Cross, shotgun
 * Modifier+Circle, Uzi Modifier+Triangle, revolver Modifier+Square. The
 * previous plain R1 flare and PS-button save default are replaced by these
 * chords, and both medipacks are left unbound (the small one sat on bit 13,
 * now the Modifier's button); under Modern control JoyDefaultRecord still
 * places them on the d-pad.
 *
 * The two XInput-ordered rows get no Modifier: those pad paths name nothing
 * past bit 11 (specific\input.cpp), leaving no spare button, so flare stays
 * on R1, save and load stay unbound, and the twelve added roles are -1. */
#define JOY_SEP_PS5_DINPUT	{ 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 11, 10, -1, -1, 7, 13, 1, 2, 3, 0, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_PS5_XINPUT	{ 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_XBOX		{ 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_GENERIC		{ 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 11, 10, -1, -1, 7, 13, 1, 2, 3, 0, -1, -1, -1, -1, -1, -1, -1 }

/* Which roles of the rows above are chords. Written out of the role names
 * rather than as a hex number, so that a role moving would move its bit with
 * it: flare, quick save, quick load and the first four weapons - the seven
 * roles bound as chords above, and nothing else. */
#define JOY_SEP_CHORD_DINPUT	((1L << JOY_ROLE_FLARE) | (1L << JOY_ROLE_SAVE) | (1L << JOY_ROLE_LOAD) | \
				 (1L << (JOY_ROLE_WEAPON_FIRST + 0)) | (1L << (JOY_ROLE_WEAPON_FIRST + 1)) | \
				 (1L << (JOY_ROLE_WEAPON_FIRST + 2)) | (1L << (JOY_ROLE_WEAPON_FIRST + 3)))
#define JOY_SEP_CHORD_XINPUT	0L

/* Swaps which Action setting's role set sits in `active` (jLayout - the table
 * the binding page, Key() and every producer read), parking the other set in
 * `parked` rather than overwriting it. `seated` is which setting is active
 * today (0 One key, 1 Separate), `want` is which one should be; the return
 * value is the new `seated`.
 *
 * Asking for the setting already seated costs one comparison and moves
 * nothing, since the call sites (a menu row and a one-time seat at startup)
 * must not double-swap.
 *
 * -1 (unbound) is swapped like any other value: the function has no need to
 * distinguish it from a bound role. */
inline int PadSetSeat(long* active, long* parked, int seated, int want)
{
	want = want ? 1 : 0;

	if (seated == want)
		return seated;

	for (int i = 0; i < JOY_ROLES; i++)
	{
		const long t = active[i];

		active[i] = parked[i];
		parked[i] = t;
	}

	return want;
}

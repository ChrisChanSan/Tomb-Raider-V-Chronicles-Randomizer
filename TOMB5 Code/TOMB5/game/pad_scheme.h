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

/* THE TWO PAD SETS - THE PART THAT IS PURE ARITHMETIC (#161, round 161b2c,
 * item 3).
 *
 * "Separate" is a pad layout of its own - action on Triangle, secondary on R2,
 * duck on L1 - and the player's One-key bindings must survive the player trying
 * it. So the pad has two user sets, one per Action setting, exactly as the
 * keyboard has one per control scheme since round 161b2a; and the one question
 * that comes with them is the keyboard's third one: which set sits in the table
 * every existing reader reads. That is arithmetic over sixteen longs and does
 * not need the engine.
 *
 * It includes NO engine header on purpose, exactly as game\key_scheme.h and
 * game\menu_nav.h do and for the same reason:
 * Preparation\Tools\Diagnose\MenuTest.cpp includes THIS FILE - not a copy of it
 * - and asserts on it. There is no twin to keep in step.
 *
 * WHY THIS IS NOT KeySchemeSeat CALLED TWICE. Same three statements, other
 * array: the keyboard's set is thirty-four shorts and the pad's is twenty-eight
 * longs (a binding is a BIT INDEX there, and -1 means unbound, which a scan code
 * never needs). One function over both would have to be a template or take a
 * size and a stride, and the codebase is C with comments. Two small ones that
 * say what they seat are the cheaper honesty.
 *
 * ROUND 161b2d ADDED THE CHORD FLAGS BESIDE THE BINDINGS (item 3), and they do
 * NOT travel through PadSetSeat: a set's flags are one long, not an array, so
 * the swap is one exchange at the one call site (ApplyJoySplit) rather than a
 * second loop and a second signature here. The rule that reads them is
 * PadRoleFires below.
 */

/* The pad's role count (#161, round 161b2c, item 2, moved here by item 3 so
 * that MenuTest checks the ENGINE'S number; grown to twenty-eight by round
 * 161b2d, item 3).
 *
 * TWENTY-EIGHT, AND THE SIXTEEN BEFORE THEM KEEP THEIR NUMBERS, exactly as the
 * keyboard's thirty-four do (game\key_scheme.h): slot 16 is the Modifier, 17-22
 * the six weapon hotkeys in DoWeaponHotkey's own order, 23-27 the five cheats.
 *
 * jLayout IS THE ACTIVE SET AND NOTHING ELSE NOW. It was `long[32]` - sixteen
 * roles and, behind them, a snapshot of what the OTHER control scheme's set
 * would be. That snapshot was written by ApplyJoyScheme twice over and read by
 * nothing; its own comment in specific\input.cpp said so ("a record and not a
 * source"). Twelve more roles left it nowhere to live, and rather than give a
 * thing nobody reads an array of its own, round 161b2d retired it. If a later
 * round wants to SHOW the other scheme's set, ApplyJoyScheme computes it in one
 * line (JoyDefaultRecord) and always fresher than a stored copy would be. */
#define JOY_ROLES		28

/* The Secondary role's slot. It was "the last one" while there were sixteen;
 * since the hotkey roles were appended behind it, it is the LITERAL 15 and must
 * stay there - a registry written by the 161b2c build calls that slot JSec/SJSec
 * and means the Secondary action. The same shape, and the same reason, as
 * KEY_ROLE_SECOND's literal 18. MenuTest checks the number. */
#define JOY_ROLE_SECOND		15

/* THE NEW ROLES BY NAME, so that no call site carries a bare number (#161,
 * round 161b2d, item 3). The order is the order of the four Separate default
 * tables below and of the bind rows on the Key bindings page. */
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
 * (specific\dxshell.cpp). ApplyJoyScheme has carried this bound since round
 * 161a2 and for the same reason - a binding comes out of the registry, so it is
 * a number and not a promise. */
#define JOY_BIT_MAX		23

/* IS THIS BIT DOWN IN THIS READING? The one place that turns a stored binding
 * into a yes or a no, so that the -1 of an unbound role and a value the
 * registry should never have held are answered once instead of at every caller.
 *
 * Key(jLayout[n] + 256) - the expression every pad producer used until this
 * round - is `joy_fire & (1 << bit)` with -1 caught by a sentinel at 255. This
 * is that expression with its two ends closed: below 0 and above JOY_BIT_MAX
 * there is no button, and no shift is attempted. For every value the engine can
 * actually produce the two agree term for term; what changes is only that a
 * junk value shifts by 40 in nobody's undefined behaviour. */
inline int PadBitDown(unsigned long fire, long bit)
{
	if (bit < 0 || bit > JOY_BIT_MAX)
		return 0;

	return (fire & (1UL << bit)) != 0 ? 1 : 0;
}

/* DOES THIS PAD ROLE FIRE? THE WHOLE RULE, IN ONE PLACE (#161, round 161b2d,
 * item 3).
 *
 * "Pad: CHORDS with one modifier button (the touchpad click): quick save
 * Touchpad+R3, quick load Touchpad+L3, pistols Touchpad+Cross..." - so a role
 * is PLAIN or a CHORD, and the flag lives beside the button number and never
 * inside it (a bit index has no spare bits to hide a flag in, and the registry
 * value must stay a plain button number the 161b2c build can read).
 *
 *   chord: the Modifier is held AND the role's own button is down.
 *   plain: the role's own button is down AND the Modifier is NOT held.
 *
 * The second half of the plain rule is what makes R3 and Modifier+R3 two
 * bindings instead of one press doing two things - the same sentence the
 * medipacks got on the d-pad in round 161a2b.
 *
 * WITH THE MODIFIER UNBOUND THIS IS TODAY'S EXPRESSION, CHARACTER FOR
 * CHARACTER, and that is the round's byte-identity argument: `modifier` is -1,
 * PadBitDown answers 0 for it, so `held` is 0, `button == modifier` is false for
 * every bound role, and a plain role returns exactly "is its button down" -
 * which is what Key(jLayout[n] + 256) returned. A chord role returns 0, and
 * there are none: the four One-key default tables bind no Modifier and no chord
 * (JoyDefaultRecord fills every new role with -1 there), so no chord can exist
 * at the default at all.
 *
 * THE MODIFIER'S OWN BUTTON IS NEVER A ROLE'S TRIGGER. A chord sitting on it
 * would fire when the Modifier alone is pressed, and a plain role on it could
 * never fire (its own press holds the Modifier down). One test answers both,
 * unconditionally, however the two values got into the table - the capture
 * refuses the case as well, but the rule does not have to trust it. */
inline int PadRoleFires(unsigned long fire, long button, int chord, long modifier)
{
	const int held = PadBitDown(fire, modifier);

	if (!PadBitDown(fire, button) || button == modifier)
		return 0;

	return chord ? held : !held;
}

/* THE FOUR "SEPARATE" DEFAULT TABLES, AND THEIR CHORD MASKS, AS MACROS (#161,
 * round 161b2d, item 3) - the same bargain game\key_scheme.h struck with the
 * Modern keyboard row: the ENGINE instantiates these (specific\input.cpp) and
 * MenuTest instantiates the very same macros, so the table check runs on the
 * rows a player really gets and not on a copy that can drift.
 *
 * The four ONE-KEY tables (jDef*) stay where they are, untouched and fifteen
 * entries long - the user's decision of 2026-09-16 keeps them as our pad layout
 * and this round does not touch them. JoyDefaultRecord fills roles 15..27 with
 * -1 and the chord mask with 0 for them, which is what "One key: all twelve
 * unbound on every path" means, and it is why they need no macro here: there is
 * nothing in them a chord rule could be wrong about.
 *
 * Role order: duck dash walk jump action draw flare look roll inventory pause
 * save load smallmedi largemedi secondary | modifier | pistols shotgun uzi
 * revolver HK crossbow | fly guns items heal skip. -1 is unbound.
 *
 * THE TWO DIRECTINPUT-NUMBERED ROWS carry the user's own second binding page:
 * Modifier = bit 13 (the DualSense's touchpad click - bit 14 is its mute
 * button), quick save Modifier+R3, quick load Modifier+L3, flare Modifier+R1,
 * pistols Modifier+Cross, shotgun Modifier+Circle, Uzi Modifier+Triangle,
 * revolver Modifier+Square. The PS-button save default goes (it is a chord
 * now), the plain R1 flare goes with it, and BOTH MEDIPACKS GO UNBOUND: the
 * small one sat on bit 13, which is the Modifier's button now, and the two are
 * a pair - 0 and 9 still work, and under Modern JoyDefaultRecord moves them to
 * the d-pad as it always has.
 *
 * THE TWO XINPUT-ORDERED ROWS GET NO MODIFIER. Those two pages name nothing
 * past bit 11 (specific\input.cpp): there is no spare button, so a Modifier
 * would have to take one away from a role. The 161b2c defaults stand - flare on
 * R1, save and load unbound - and every one of the twelve new roles is -1. */
#define JOY_SEP_PS5_DINPUT	{ 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 11, 10, -1, -1, 7, 13, 1, 2, 3, 0, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_PS5_XINPUT	{ 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_XBOX		{ 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
#define JOY_SEP_GENERIC		{ 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 11, 10, -1, -1, 7, 13, 1, 2, 3, 0, -1, -1, -1, -1, -1, -1, -1 }

/* Which roles of the rows above are chords. Written out of the role names
 * rather than as a hex number, so that a role moving would move its bit with
 * it. Flare, quick save, quick load and the first four weapons - the seven the
 * user's own page has, and nothing else. */
#define JOY_SEP_CHORD_DINPUT	((1L << 6) | (1L << 11) | (1L << 12) | \
				 (1L << (JOY_ROLE_WEAPON_FIRST + 0)) | (1L << (JOY_ROLE_WEAPON_FIRST + 1)) | \
				 (1L << (JOY_ROLE_WEAPON_FIRST + 2)) | (1L << (JOY_ROLE_WEAPON_FIRST + 3)))
#define JOY_SEP_CHORD_XINPUT	0L

/* SEATING THE OTHER SET. The engine keeps the ACTIVE Action setting's set in
 * jLayout - the table the binding page, Key() and every producer read - and the
 * other setting's set parked beside it; flipping the Action row exchanges the
 * two, so the set that leaves is kept rather than overwritten. `seated` is
 * which setting is in `active` today (0 One key, 1 Separate), `want` which one
 * should be. The answer is the new `seated`.
 *
 * It is idempotent - asking for the setting that is already seated costs one
 * comparison and moves nothing - because the call sites are a menu row and a
 * one-time seat at startup, and neither of them may double-swap.
 *
 * -1 travels through it like any other value: an unbound role is data here, not
 * a hole, and the swap has no business knowing which is which. */
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

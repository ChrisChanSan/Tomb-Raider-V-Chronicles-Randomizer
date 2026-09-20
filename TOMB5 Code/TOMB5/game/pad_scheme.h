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
 * array: the keyboard's set is nineteen shorts and the pad's is sixteen longs
 * (a binding is a BIT INDEX there, and -1 means unbound, which a scan code
 * never needs). One function over both would have to be a template or take a
 * size and a stride, and the codebase is C with comments. Two small ones that
 * say what they seat are the cheaper honesty.
 */

/* The pad's role count, and the Secondary action's slot in it (#161, round
 * 161b2c, item 2, moved here by item 3 so that MenuTest checks the ENGINE'S
 * number). Sixteen roles in jLayout[0..15]; jLayout[16..31] is the other
 * control scheme's record, which is why that array is 32 long
 * (specific\input.cpp). */
#define JOY_ROLES		16

/* The Secondary role's slot. It is the last one by construction and cannot
 * drift from the count above - the same shape as KEY_ROLE_SECOND. */
#define JOY_ROLE_SECOND		(JOY_ROLES - 1)

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

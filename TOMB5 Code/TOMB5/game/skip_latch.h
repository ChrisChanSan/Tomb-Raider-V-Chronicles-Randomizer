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

/* The button that skipped a scene does nothing more until it is released.
 *
 * With the Cutscene skipper on, the pad's confirm button - Cross, A - skips
 * a cutscene (game\control.cpp) and a camera flyby (game\spotcam.cpp), and
 * on every default pad page it is the button of the Action role as well
 * (specific\input.cpp). The press that ended a scene was still down when
 * play came back, and the input took it for whatever role sits on that
 * button: at a pull rope Lara pulled once more, and a second pull of a
 * SWITCH trigger turns every target's code bits back.
 *
 * The latch remembers the bit the skip fired on and takes it out of every
 * reading of the pad until a reading has it up. Taken out of the reading,
 * not out of a role: every role is read from that one word
 * (JoyRole, PadRoleFires in game\pad_scheme.h), so whatever the binding page
 * put on the button is silent, and every other button is left alone.
 *
 * Pure, with no engine header, so that
 * Preparation\Tools\Diagnose\ModernTest.cpp includes this file itself. The
 * state lives in specific\input.cpp, which applies it to each reading
 * before any role is read from it; the two skippers engage it, and so does
 * the skip of a Zip call that holds her with no scene (game\twogun.cpp). */

// No bit latched.
#define SKIP_LATCH_OFF	(-1)

// Which skipper engaged the latch, for its log line.
enum
{
	SKIP_LATCH_CUTSEQ = 1,
	SKIP_LATCH_FLYBY = 2,
	SKIP_LATCH_ZIP = 3	// a Zip call skipped with no scene running
};

struct SKIP_LATCH
{
	long bit;	// the pad bit the skip fired on, SKIP_LATCH_OFF for none
	long kind;	// SKIP_LATCH_CUTSEQ, SKIP_LATCH_FLYBY or SKIP_LATCH_ZIP
	long since;	// the tick the skip fired on
};

// The `set=` word of a kind in the SKIPLATCH line (specific\input.cpp).
inline const char* SkipLatchWord(long kind)
{
	if (kind == SKIP_LATCH_CUTSEQ)
		return "cutseq";

	return kind == SKIP_LATCH_ZIP ? "zip" : "flyby";
}

/* A fresh press: down on this reading and not on the last one. `held`
 * carries the reading to the next call. Whoever begins a scene sets it to
 * the state the button is in at that moment, so a button already down when
 * the scene begins - the pull that started it - never skips it. */
inline int SkipConfirmEdge(int down, char* held)
{
	const int fresh = down && !*held;

	*held = down ? 1 : 0;
	return fresh;
}

/* A skip fired on the pad's `bit`. Only the bits a pad path writes, 0 to
 * 23 (JOY_BIT_MAX, game\pad_scheme.h), are taken; anything else leaves the
 * latch as it is. */
inline void SkipLatchEngage(SKIP_LATCH* l, long bit, long kind, long tick)
{
	if (bit < 0 || bit > 23)
		return;

	l->bit = bit;
	l->kind = kind;
	l->since = tick;
}

/* One reading of the pad, one bit per button. While the latched bit is down
 * it is taken out and the rest passes; the first reading with it up drops
 * the latch, passes whole and says so in *released. Without a latch the
 * reading passes unchanged. */
inline unsigned long SkipLatchApply(SKIP_LATCH* l, unsigned long fire,
	int* released)
{
	*released = 0;

	if (l->bit == SKIP_LATCH_OFF)
		return fire;

	const unsigned long bit = 1UL << l->bit;

	if (fire & bit)
		return fire & ~bit;

	l->bit = SKIP_LATCH_OFF;
	*released = 1;
	return fire;
}

// Whether the latch holds; `kind` 0 asks about either skipper.
inline int SkipLatchHolds(const SKIP_LATCH* l, long kind)
{
	return l->bit != SKIP_LATCH_OFF && (!kind || l->kind == kind);
}

// Lets go without a release - a level change.
inline void SkipLatchDrop(SKIP_LATCH* l)
{
	l->bit = SKIP_LATCH_OFF;
}

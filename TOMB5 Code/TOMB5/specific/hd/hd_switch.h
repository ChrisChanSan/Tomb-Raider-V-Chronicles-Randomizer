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
 * The HD/SD switch: one key flips the world between the HD draw and the
 * classic draw, for comparing the two at one spot. It is a third state
 * beside tomb5.hd_enable 0 and 1 - the HD level built at the load as ever,
 * drawn HD or classic as the switch says - so a flip costs no load: both
 * sets of data stay resident for the whole level.
 *
 * The state starts at "drawn HD" with every game and holds for the session,
 * across level loads, so one spot can be compared before and after a load.
 * It is not saved: the registry's hd_enable decides how a game starts. With
 * hd_enable 0 there is no HD data to switch to; a press then changes
 * nothing and says so in the log. The HD textures row, which is hd_enable,
 * sets the state to its own new value at every step, so the row switches
 * the whole world at once as a press does, and a level loaded after it
 * starts drawn as the row says.
 *
 * The rule is pure arithmetic here, without an engine header, so that
 * Preparation\Tools\Diagnose\ModernTest.cpp checks the engine's own rule;
 * hd_switch.cpp holds the state and the log line.
 */

// One switch: the draw it asks for, and whether its key was down at the
// last reading.
struct HDSwitchState
{
	int drawn;	// 1 the HD draw, 0 the classic draw
	int held;	// the role was down at the last reading
};

// The start of every game: drawn HD, nothing held.
#define HD_SWITCH_START	{ 1, 0 }

// What one reading did.
enum
{
	HD_SWITCH_NONE = 0,	// no press, a held key, or no place to switch in
	HD_SWITCH_FLIPPED,	// a press flipped the draw
	HD_SWITCH_NOTHING	// a press with no HD data: nothing to switch to
};

/* One reading of the role: `down` is its key or its pad chord, `live`
 * whether a press may switch here (in a level, no menu open), `hd` whether
 * the session has HD data (HDEnabled). A press is the edge from up to down,
 * so a held key flips once and does not repeat; the held state is kept on
 * every reading, live or not, so a key still down when a menu closes is no
 * new press. Key and pad are one `down`, so one press of either - or of
 * both at once - is one edge. */
inline int HDSwitchStep(HDSwitchState* s, int down, int live, int hd)
{
	const int edge = down && !s->held;

	s->held = down ? 1 : 0;

	if (!edge || !live)
		return HD_SWITCH_NONE;

	if (!hd)
		return HD_SWITCH_NOTHING;

	s->drawn = !s->drawn;
	return HD_SWITCH_FLIPPED;
}

/* One step of the HD textures row: the draw takes the row's new value `on`
 * (1 HD, 0 classic), so the row and the key name one state. `hd` whether
 * the level on screen has HD data to draw (HDSwitchOption says how that is
 * asked). The state is set either way: without HD data the step switches
 * nothing now and says so, as a press does, and the state is how the next
 * level built HD starts. A flip only when the draw changed - a row stepped
 * to off while a press has the world classic already is no flip. `held` is
 * not touched: the row reads no key, so its step is never an edge of one,
 * and a key held across it is no new press. */
inline int HDSwitchSet(HDSwitchState* s, int on, int hd)
{
	const int was = s->drawn;

	s->drawn = on ? 1 : 0;

	if (!hd)
		return HD_SWITCH_NOTHING;

	return s->drawn != was ? HD_SWITCH_FLIPPED : HD_SWITCH_NONE;
}

// The engine's half, in hd_switch.cpp.

// Whether the world is to be drawn HD. True at the start of every game.
bool HDDrawn();

/* One reading per S_UpdateInput (specific\input.cpp): the role's key, its
 * pad chord and whether a press may switch here. A flip, and a press with
 * nothing to switch to, writes one HDLOG line behind -log. */
void HDSwitchInput(bool key, bool pad, bool live);

/* The HD textures row's step (game\menu_frame.cpp, MenuStepHdTextures),
 * after it wrote tomb5.hd_enable: `on` the row's new value, `roomsBefore`
 * HDLevelActive() as the menu asked it before that write. A flip, and a
 * step with nothing to switch, writes the same HDLOG line with by=option. */
void HDSwitchOption(bool on, bool roomsBefore);

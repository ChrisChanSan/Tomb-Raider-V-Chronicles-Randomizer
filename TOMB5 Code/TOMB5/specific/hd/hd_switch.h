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
 * The HD/SD switch: one key flips the world and the interface between the
 * HD draw and the classic draw, for comparing the two at one spot. A
 * level's HD data are built at its load whenever the build gate is open
 * (hd_level.h, HDEnabled: hd_build and an installation found), whatever is
 * drawn, so both draws are resident for the whole level and a flip costs
 * no load.
 *
 * The switch's state is the setting hd_enable, the HD textures row: a press
 * and a step of the row flip the one value through one setter, the row
 * shows every flip of the key, and the value is saved as every row's is -
 * so a game starts drawn as the last one was left. There is nothing to
 * switch only where the level on screen has no HD data: the build gate off
 * (hd_build 0, or no installation) or a level the .TRG had nothing for. A
 * press there changes nothing and says so in the log; a step of the row
 * still moves the setting, as a step of any option does.
 *
 * The rule is pure arithmetic here, without an engine header, so that
 * Preparation\Tools\Diagnose\ModernTest.cpp checks the engine's own rule;
 * hd_switch.cpp holds the key's held state, the setter and the log line.
 */

// One switch: the draw it asks for, and whether its key was down at the
// last reading.
struct HDSwitchState
{
	int drawn;	// 1 the HD draw, 0 the classic draw (tomb5.hd_enable)
	int held;	// the role was down at the last reading
};

// A state of the tools' own (RoomProbe, the suites): drawn HD, nothing held.
// The engine's draw is the setting hd_enable as the registry gave it.
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
 * the level on screen has HD data (HDSwitchHasData). A press is the edge
 * from up to down, so a held key flips once and does not repeat; the held
 * state is kept on every reading, live or not, so a key still down when a
 * menu closes is no new press. Key and pad are one `down`, so one press of
 * either - or of both at once - is one edge. */
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
 * (1 HD, 0 classic). `hd` whether the level on screen has HD data to draw.
 * The value is set either way, as a step of an option is: without HD data
 * the step switches nothing on screen and says so, as a press does, and
 * the value is how the next level built HD is drawn. A flip only when the
 * draw changed. `held` is not touched: the row reads no key, so its step is
 * never an edge of one, and a key held across it is no new press. */
inline int HDSwitchSet(HDSwitchState* s, int on, int hd)
{
	const int was = s->drawn;

	s->drawn = on ? 1 : 0;

	if (!hd)
		return HD_SWITCH_NOTHING;

	return s->drawn != was ? HD_SWITCH_FLIPPED : HD_SWITCH_NONE;
}

// The engine's half, in hd_switch.cpp.

// Whether the world and the interface are drawn HD: the setting hd_enable.
bool HDDrawn();

/* Whether the level on screen has HD data to switch to: HD rooms
 * (HDLevelBuilt), HD objects or an HD Lara, built at its load. The one test
 * of the key, the pad chord and the row. */
bool HDSwitchHasData();

/* One reading per S_UpdateInput (specific\input.cpp): the role's key, its
 * pad chord and whether a press may switch here. A flip, and a press with
 * nothing to switch to, writes one HDLOG line behind -log; a flip is saved
 * at once, as a step of the row is. */
void HDSwitchInput(bool key, bool pad, bool live);

/* The HD textures row's step (game\menu_frame.cpp, MenuStepHdTextures): the
 * draw flips through the same setter as a press and writes the same HDLOG
 * line with by=option. The menu saves the value with the tomb5 settings
 * (MenuAfterChange). */
void HDSwitchOption();

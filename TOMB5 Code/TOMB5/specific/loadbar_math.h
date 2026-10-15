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

/* The loading screen's rules, and nothing else: the Loadbar style row's
 * state and the fade of the third state.
 *
 * No engine header on purpose: Preparation\Tools\Diagnose\ModernTest.cpp
 * includes this file and runs the rules as the engine runs them
 * (tomb5\tomb5.cpp reads the row with them, specific\drawbars.cpp draws
 * with them). */

// The Loadbar style row: the TR5 bar, the TR4 bar, or no loading screen.
enum
{
	LOADBAR_TR5 = 0,
	LOADBAR_TR4 = 1,
	LOADBAR_OFF = 2
};

/* The row's state out of the registry. `found` and `value` are the DWORD
 * loadbar as REG_ReadLong answers; `tr4_bar` is the older bool tr4LB, the
 * row's whole state before the third one existed. Without the DWORD, or
 * with a value no state has, the bool decides, so that no player's
 * loading screen changes by the update: 1 the TR4 bar, 0 the TR5 one. */
inline long LoadBarModeOf(bool found, unsigned long value, bool tr4_bar)
{
	if (found && value <= LOADBAR_OFF)
		return (long)value;

	return tr4_bar ? LOADBAR_TR4 : LOADBAR_TR5;
}

/* The third state's fade: how dark the black over the last frame is,
 * 0..255, `elapsed` ms after the fade began, over `fadeMs`. 0 ms is a
 * cut: black at once. */
inline long LoadFadeAlpha(double elapsed, double fadeMs)
{
	if (fadeMs <= 0.0 || elapsed >= fadeMs)
		return 255;

	if (elapsed <= 0.0)
		return 0;

	return (long)(255.0 * elapsed / fadeMs);
}

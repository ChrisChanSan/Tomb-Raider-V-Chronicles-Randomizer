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

/*
 * The HD/SD switch's state and its log line; hd_switch.h says what the
 * switch is and holds its rule.
 */
#include "../../tomb5/pch.h"
#include "hd_switch.h"
#include "hd_level.h"			// HDEnabled - whether there is HD data
#include "../function_stubs.h"		// Log, gLogEnabled
#include "../../game/gameflow.h"	// the level's file name
#include "../../game/control.h"		// GlobalCounter
#include "../../game/text.h"		// GnFrameCounter

#include <string.h>

namespace
{
	/* The one switch of the session. Nothing resets it: it lives as long as
	 * the process, so it holds across every level load. */
	HDSwitchState g_state = HD_SWITCH_START;

	// The level's file stem, "ANDREA2" out of "DATA\ANDREA2".
	const char* LevelStem()
	{
		if (!gfFilenameWad || !gfFilenameOffset)
			return "?";

		const char* name = &gfFilenameWad[gfFilenameOffset[gfLevelId]];
		const char* slash = strrchr(name, '\\');
		const char* other = strrchr(name, '/');

		if (other && (!slash || other > slash))
			slash = other;

		return slash ? slash + 1 : name;
	}
}

bool HDDrawn()
{
	return g_state.drawn != 0;
}

/* HDLOG switch, behind -log, one line per press that reached a level:
 *
 *   drawn  the draw after the press: 1 HD, 0 classic
 *   level  the level's file stem
 *   tick   GlobalCounter, the logic tick
 *   frame  GnFrameCounter, the drawn-frame clock (one step per drawn frame
 *          that is not an in-between frame)
 *   by     key or pad - the keyboard role or the pad chord
 *
 * A press with hd_enable 0 writes the same line with the state it left
 * alone and "nothing to switch" at the end. */
void HDSwitchInput(bool key, bool pad, bool live)
{
	const int what = HDSwitchStep(&g_state, (key || pad) ? 1 : 0,
		live ? 1 : 0, HDEnabled() ? 1 : 0);

	if (what == HD_SWITCH_NONE || !gLogEnabled)
		return;

	Log("HDLOG switch drawn=%d level=%s tick=%ld frame=%ld by=%s%s",
		g_state.drawn, LevelStem(), (long)GlobalCounter, (long)GnFrameCounter,
		key ? "key" : "pad",
		what == HD_SWITCH_NOTHING ? " hd_enable=0 nothing to switch" : "");
}

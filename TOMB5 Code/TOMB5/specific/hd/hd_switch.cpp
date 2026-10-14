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
#include "hd_object.h"			// HDObjectActive - the level's HD objects
#include "hd_lara.h"			// HDLaraActive - the level's HD Lara
#include "../function_stubs.h"		// Log, gLogEnabled
#include "../../tomb5/tomb5.h"		// tomb5.hd_enable - the log's cause
#include "../../game/gameflow.h"	// the level's file name
#include "../../game/control.h"		// GlobalCounter
#include "../../game/text.h"		// GnFrameCounter

#include <string.h>

namespace
{
	/* The one switch of the session. Nothing resets it: it lives as long as
	 * the process, so it holds across every level load. */
	HDSwitchState g_state = HD_SWITCH_START;

	/* The level's file stem, "ANDREA2" out of "DATA\ANDREA2". gfLevelId is
	 * the level's identity, not its place in the file table, so the table
	 * is searched for the one file whose identity it is; a level that no
	 * file or more than one file answers for is named "?". */
	const char* LevelStem()
	{
		if (!gfFilenameWad || !gfFilenameOffset || !Gameflow)
			return "?";

		int found = -1;

		for (int i = 0; i < Gameflow->nFileNames; i++)
		{
			if (LevelIdFromFile((uchar)i) != gfLevelId)
				continue;

			if (found >= 0)
				return "?";

			found = i;
		}

		if (found < 0)
			return "?";

		const char* name = &gfFilenameWad[gfFilenameOffset[found]];
		const char* slash = strrchr(name, '\\');
		const char* other = strrchr(name, '/');

		if (other && (!slash || other > slash))
			slash = other;

		return slash ? slash + 1 : name;
	}

	// The HDLOG switch line, described at HDSwitchInput below.
	void SwitchLog(const char* by, const char* tail)
	{
		Log("HDLOG switch drawn=%d level=%s tick=%ld frame=%ld by=%s%s",
			g_state.drawn, LevelStem(), (long)GlobalCounter,
			(long)GnFrameCounter, by, tail);
	}
}

/* The draw asks the row as well: HDEnabled is the build gate and does not
 * read hd_enable, so the HD textures row decides the draw here. */
bool HDDrawn()
{
	return g_state.drawn != 0 && tomb5.hd_enable;
}

/* HDLOG switch, behind -log, one line per press that reached a level:
 *
 *   drawn  the draw after the press: 1 HD, 0 classic
 *   level  the level's file stem
 *   tick   GlobalCounter, the logic tick
 *   frame  GnFrameCounter, the drawn-frame clock (one step per drawn frame
 *          that is not an in-between frame)
 *   by     key or pad - the keyboard role or the pad chord; option - the
 *          HD textures row (HDSwitchOption below)
 *
 * A press with no HD data writes the same line with the state it left
 * alone and the cause at the end: "hd_enable=0 nothing to switch" with the
 * option off, "hd_enable=1 install=none nothing to switch" with the option
 * on and no installation found (HDEnabled asks both; the install is not
 * looked for here, so with the option off none is searched). */
void HDSwitchInput(bool key, bool pad, bool live)
{
	const int what = HDSwitchStep(&g_state, (key || pad) ? 1 : 0,
		live ? 1 : 0, tomb5.hd_enable && HDEnabled() ? 1 : 0);

	if (what == HD_SWITCH_NONE || !gLogEnabled)
		return;

	const char* tail = "";

	if (what == HD_SWITCH_NOTHING)
		tail = tomb5.hd_enable ? " hd_enable=1 install=none nothing to switch" :
			" hd_enable=0 nothing to switch";

	SwitchLog(key ? "key" : "pad", tail);
}

/* The row's step. Whether the level on screen has HD data is whether its
 * load built HD rooms, HD objects or an HD Lara. The objects and Lara keep
 * flags of their own, which neither the row nor the state touch. The rooms
 * answer only through HDLevelActive, which asks the row and the state as
 * well, so they are asked where both stand at HD: by the menu before its
 * write for a step to off (`roomsBefore`), here after the state took the
 * row's value for a step to on. So every step that changes the draw knows
 * its level's data. A step to off that finds the state classic already
 * changes no draw; in a level with HD rooms and neither HD objects nor an
 * HD Lara its line says there is nothing to switch, which is so.
 *
 * The line's cause when there is nothing to switch: with the row off as
 * for a press, "hd_enable=0 nothing to switch"; with it on and no
 * installation found "hd_enable=1 install=none nothing to switch"; with it
 * on, an installation found and a level that was loaded without HD data,
 * "hd_enable=1 built=none nothing to switch" - the state is then how the
 * next level, built HD, starts. */
void HDSwitchOption(bool on, bool roomsBefore)
{
	const HDSwitchState was = g_state;

	HDSwitchSet(&g_state, on ? 1 : 0, 1);

	const bool hd = roomsBefore || HDLevelActive() || HDObjectActive() ||
		HDLaraActive();

	g_state = was;

	const int what = HDSwitchSet(&g_state, on ? 1 : 0, hd ? 1 : 0);

	if (what == HD_SWITCH_NONE || !gLogEnabled)
		return;

	const char* tail = "";

	if (what == HD_SWITCH_NOTHING)
		tail = !tomb5.hd_enable ? " hd_enable=0 nothing to switch" :
			!HDEnabled() ? " hd_enable=1 install=none nothing to switch" :
			" hd_enable=1 built=none nothing to switch";

	SwitchLog("option", tail);
}

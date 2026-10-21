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
 * The HD/SD switch's setter and its log line; hd_switch.h says what the
 * switch is and holds its rule.
 */
#include "../../tomb5/pch.h"
#include "hd_switch.h"
#include "hd_level.h"			// HDEnabled, HDLevelBuilt - the HD data
#include "hd_object.h"			// HDObjectActive - the level's HD objects
#include "hd_lara.h"			// HDLaraActive - the level's HD Lara
#include "../function_stubs.h"		// Log, gLogEnabled
#include "../registry.h"		// a press saves the draw
#include "../../tomb5/tomb5.h"		// tomb5.hd_enable - the draw itself
#include "../../game/gameflow.h"	// the level's file name
#include "../../game/control.h"		// GlobalCounter
#include "../../game/text.h"		// GnFrameCounter
#include "../input.h"			// the ammo caption - the off notice

#include <string.h>

namespace
{
	/* The key's half of the switch that is not the setting: whether the
	 * role was down at the last reading. It lives as long as the process. */
	int g_held = 0;

	// The switch as the rule sees it: the draw is the setting hd_enable.
	HDSwitchState Current()
	{
		HDSwitchState s = { tomb5.hd_enable ? 1 : 0, g_held };

		return s;
	}

	/* The one setter: the rule's answer written back, the draw into the
	 * setting itself, so that the HD textures row shows it when it is next
	 * drawn and every gate reads it on the next frame. */
	void Commit(const HDSwitchState& s)
	{
		tomb5.hd_enable = s.drawn != 0;
		g_held = s.held;
	}

	/* A press keeps the new draw as a step of the row keeps it: the tomb5
	 * settings are written when one of them changes (game\menu_frame.cpp,
	 * MenuAfterChange) and not again at the game's exit, so a flip that is
	 * not written now would be lost. One value and not the whole block,
	 * which is what changed. */
	void SaveDrawn()
	{
		OpenRegistry("tomb5");
		REG_WriteBool((char*)"hd_enable", tomb5.hd_enable);
		CloseRegistry();
	}

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

	/* Why there is nothing to switch, in the order the build asks: the
	 * build gate's setting, the installation, the level's own data. */
	const char* NothingCause()
	{
		if (!tomb5.hd_build)
			return " hd_build=0 nothing to switch";

		if (!HDEnabled())
			return " install=none nothing to switch";

		return " built=none nothing to switch";
	}

	// The HDLOG switch line, described at HDSwitchInput below.
	void SwitchLog(const char* by, int what)
	{
		Log("HDLOG switch drawn=%d level=%s tick=%ld frame=%ld by=%s%s",
			tomb5.hd_enable ? 1 : 0, LevelStem(), (long)GlobalCounter,
			(long)GnFrameCounter, by,
			what == HD_SWITCH_NOTHING ? NothingCause() : "");
	}
}

bool HDDrawn()
{
	return tomb5.hd_enable;
}

bool HDSwitchHasData()
{
	return HDLevelBuilt() || HDObjectActive() || HDLaraActive();
}

/* HDLOG switch, behind -log, one line per press that reached a level and
 * per step of the HD textures row:
 *
 *   drawn  the draw after it: 1 HD, 0 classic
 *   level  the level's file stem
 *   tick   GlobalCounter, the logic tick
 *   frame  GnFrameCounter, the drawn-frame clock (one step per drawn frame
 *          that is not an in-between frame)
 *   by     key or pad - the keyboard role or the pad chord; option - the
 *          HD textures row (HDSwitchOption below)
 *
 * With no HD data in the level on screen the same line ends in the cause:
 * "hd_build=0 nothing to switch" with the build gate's setting at 0,
 * "install=none nothing to switch" with no installation found, and
 * "built=none nothing to switch" for a level the .TRG had nothing for. */
void HDSwitchInput(bool key, bool pad, bool live)
{
	HDSwitchState s = Current();
	const int what = HDSwitchStep(&s, (key || pad) ? 1 : 0, live ? 1 : 0,
		HDSwitchHasData() ? 1 : 0);

	Commit(s);

	if (what == HD_SWITCH_FLIPPED)
		SaveDrawn();

	/* A press with the build gate off says so on screen: the HD build row
	 * (game\menu_frame.cpp) turned the HD art off, so the key has nothing
	 * to switch until that row is on again and a level is loaded. Shown
	 * where the switch line names the cause hd_build=0, in the caption the
	 * ammo hotkeys use at the top of the screen (specific\input.cpp), for
	 * the same 30 drawn frames; its buffer holds eleven letters. */
	if (what == HD_SWITCH_NOTHING && !tomb5.hd_build)
	{
		strcpy(ammo_change_buf, "HD is off");
		ammo_change_timer = 30;
	}

	if (what == HD_SWITCH_NONE || !gLogEnabled)
		return;

	SwitchLog(key ? "key" : "pad", what);
}

/* The row's step: the draw flips as a press flips it, and the row's value
 * is that draw. Without HD data the value moves all the same - the row is
 * the setting a level built HD is drawn by - and the line says there is
 * nothing to switch. */
void HDSwitchOption()
{
	HDSwitchState s = Current();
	const int what = HDSwitchSet(&s, !s.drawn, HDSwitchHasData() ? 1 : 0);

	Commit(s);

	if (what == HD_SWITCH_NONE || !gLogEnabled)
		return;

	SwitchLog("option", what);
}

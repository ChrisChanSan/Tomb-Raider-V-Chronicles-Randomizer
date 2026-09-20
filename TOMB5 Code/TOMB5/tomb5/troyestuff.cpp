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
#include "../tomb5/pch.h"
#include "troyestuff.h"
#include "../game/text.h"
#include "../game/sound.h"
#include "../specific/3dmath.h"
#include "tomb5.h"
#include "../specific/input.h"

/* THE PAGE FUNCTIONS THAT USED TO LIVE HERE - TroyeMenu/TroyeMenuPage,
 * Page0, Page1, PageDisplay, Page2, and the paging state TroyePage - are
 * gone as of #161, round 161b1 step 2. Every row they drew is now a table
 * of game\menu_frame.cpp ("tomb5 options", "Display", "View", "HUD"), its
 * logic carried over verbatim (see the row ledger this round wrote before
 * touching any code). Only CreditsScreen stayed its own screen, because it
 * always was one on purpose - see the comment at its definition below. */

// Credits (#33). GPLv3 courtesy and user requirement: the names without
// which this project would not exist belong IN THE GAME, not only in the
// README. Text only - no URLs, the game font does not carry every glyph
// (PrintString guard); the links stand in the README.
//
// A SCREEN OF ITS OWN, not a page of the tomb5 options. It was one at
// first, and that was wrong twice over: paging left/right through the
// settings ran into the credits unasked, and TroyeMenu draws its paging
// arrows at a fixed row below the last option - on a page of pure text
// they landed ACROSS a line of names ("Dear ImGui by Omar Cornut" wore
// two orange arrows). Nothing here is selectable and nothing pages, so
// the only input this screen takes is "back".
void CreditsScreen(long textY, long& menu, ulong& selection, ulong selection_bak)
{
	long y;

	PrintString(phd_centerx, font_height, 6, "Credits", FF_CENTER);

	// GROUPS ARE SEPARATED BY COLOUR, NOT BY BLANK LINES - and that is a
	// space decision, not a taste one. The screen holds fifteen rows
	// (y = 2..16, font_height apart, the last one ending just above the
	// bottom edge). Fifteen are names; four blank separators would eat
	// the entire remaining budget and push the tool credits off screen.
	// Headers therefore use the same gold as the title (colour 6) and the
	// names stay white (colour 2).
	y = 2;

	PrintString(phd_centerx, textY + y++ * font_height, 6, "TOMB5 Engine", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Troye - ChocolateFan", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "thatguyfrombb - Raildex", FF_CENTER);

	// The TR3 enemies (T-Rex, raptor, compy, vulture) are GPLv3 code taken
	// from Troye's tomb3 decompilation, not our own work - it belongs in the
	// game's credits, not only in a source comment.
	PrintString(phd_centerx, textY + y++ * font_height, 6, "TR3 Enemies from tomb3", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Troye and contributors", FF_CENTER);

	PrintString(phd_centerx, textY + y++ * font_height, 6, "Randomizer and Windows 11 Port", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "ChrisChanSan", FF_CENTER);

	// The AI belongs HERE, next to the person who directed it - not under
	// Contributors, where it sat among people who played the seeds and
	// reported what broke. It did neither, and that placement understated
	// what it actually did. This is the last free row on the screen.
	PrintString(phd_centerx, textY + y++ * font_height, 2, "with Claude AI by Anthropic", FF_CENTER);

	// Testing, findings and ideas - the people who played the seeds and
	// reported what broke.
	PrintString(phd_centerx, textY + y++ * font_height, 6, "Contributors", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "XeNoN3560 - Lucifer230 - Todesengel1931", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Crockett - Grylle", FF_CENTER);

	PrintString(phd_centerx, textY + y++ * font_height, 6, "Tools and Libraries", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "trview by chreden", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "Dear ImGui by Omar Cornut", FF_CENTER);
	PrintString(phd_centerx, textY + y++ * font_height, 2, "stb by Sean Barrett", FF_CENTER);

	// Back to the options menu, with the row the player came from restored.
	// Confirm counts as back as well - there is nothing here to confirm,
	// and a screen that only reacts to one of the two keys feels stuck.
	if (dbinput & (IN_DESELECT | IN_SELECT))
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		menu = 0;
		selection = selection_bak;
		dbinput &= ~(IN_DESELECT | IN_SELECT);
	}
}

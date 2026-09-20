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
#include "../global/types.h"

// #161, round 161b1 step 2: TroyeMenu/TroyeMenuPage/Page0/Page1/Page2/
// PageDisplay are gone - "tomb5 options", "Display", "View" and "HUD" are
// tables of the frame now (game\menu_frame.cpp), their rows' logic carried
// over verbatim. Only the credits stayed their own screen, unchanged.
//
// Credits (#33): a screen of its own, deliberately NOT a page of the tomb5
// options - see the comment at the definition.
void CreditsScreen(long textY, long& menu, ulong& selection, ulong selection_bak);

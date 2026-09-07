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

void TroyeMenu(long textY, long& menu, ulong& selection, ulong selection_bak);
ulong TroyeMenuPage(long page);
bool Page0(long& num, long textY, ulong selection);
bool Page1(long& num, long textY, ulong selection);
bool Page2(long& num, long textY, ulong selection);
// The frame rate and VSync, a page of their own since order 6 of #148.
bool PageDisplay(long& num, long textY, ulong selection);

// Credits (#33): a screen of its own, deliberately NOT a page of the tomb5
// options - see the comment at the definition.
void CreditsScreen(long textY, long& menu, ulong& selection, ulong selection_bak);

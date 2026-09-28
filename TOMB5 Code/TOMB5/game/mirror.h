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

void Draw_Mirror_Lara();
/* The HD level's mirrors (specific\hd\hd_render.h, HDMirror): Lara and the
 * items in front of each glass whose room is drawn, drawn once more through
 * a view reflected in the glass's plane. game\draw.cpp calls it only while
 * HDLevelActive(). */
void Draw_Mirror_Items();

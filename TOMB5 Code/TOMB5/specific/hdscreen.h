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

// Optional high resolution loading screens, read from screens\HD\*.DDS if the
// player put them there. See hdscreen.cpp for where they come from and why
// none of them is ever shipped with this project.

// Reads screens\HD\<name>.DDS and decodes it. On success returns a malloc'd
// width*height buffer of 0xAARRGGBB pixels the caller frees, and fills
// *width / *height; on failure returns 0 and has already said why in the log.
// maxSide is the largest edge the caller can put in a texture.
ulong* HDScreenRead(const char* name, long maxSide, long* width, long* height);

// The base name of the localised logo picture for the language the gameflow
// settled on - "LOGO_DE" and so on. Never null.
const char* HDScreenLogo();

// The bare level name out of a gameflow file name ("DATA\ANDREA1" ->
// "ANDREA1"). Returns null when there is nothing usable.
const char* HDScreenLevelName(const char* wadName);

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

// HDSCREEN_NO_ENGINE builds the two picture composing routines at the bottom
// of this header, and nothing else, without an engine around them. The
// diagnostic tool Preparation\Tools\Diagnose\DdsProbe.cpp compiles hdscreen.cpp
// that way, so the wordmark it blends into a PNG is blended by the very code
// the game runs - the same deal bc7.cpp already has with that tool.
#ifdef HDSCREEN_NO_ENGINE
typedef unsigned char uchar;
typedef unsigned long ulong;
#else
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
#endif

// The bounding box of everything a picture actually draws: the smallest
// rectangle holding every pixel whose alpha is above 8. Returns 0 for a
// picture that draws nothing at all, and then leaves the four out values
// alone. The box is inclusive on all four sides.
long HDScreenAlphaBox(const ulong* px, long w, long h, long* x0, long* y0, long* x1, long* y1);

// Cuts the wordmark out of a logo picture and blends it over a base picture,
// horizontally centred (leftAlign 0) or in the top left corner (leftAlign 1,
// the US emblem) near the top edge; base is written in place.
// Returns 0 when the logo picture carries no wordmark, and then leaves the
// base picture untouched. Both pictures are 0xAARRGGBB, alpha NOT
// premultiplied. See hdscreen.cpp for the placement and why it is what it is.
long HDScreenComposeLogo(ulong* base, long bw, long bh, const ulong* logo, long lw, long lh, long leftAlign);

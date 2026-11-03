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

/* The films' picture in the texture's form: yuv420p to X8R8G8B8.
 *
 * The Bink decoder of the 64-bit build's FFmpeg gives each frame as three
 * planes - Y at full size, U and V at half width and half height, so one
 * U and one V serve a block of 2x2 pixels - in limited range ("tv": Y from
 * 16 to 235, U and V from 16 to 240 around 128), with the colour matrix of
 * BT.601. The build has no scaler, so the conversion is this file's:
 *
 *   R = 1.164 (Y - 16)                  + 1.596 (V - 128)
 *   G = 1.164 (Y - 16) - 0.391 (U - 128) - 0.813 (V - 128)
 *   B = 1.164 (Y - 16) + 2.018 (U - 128)
 *
 * in integers, each factor times 65536 and rounded, the sum shifted back
 * by 16 with rounding and clamped to 0..255. Row by row with each plane's
 * own linesize (the decoder may pad a row), so no frame is copied whole.
 *
 * No engine header: the engine (specific\fmv_av.cpp), BinkProbe's picture
 * file and ModernTest's three pixels compile this same file. */

#include <stdint.h>

#define YUV_RGB_Y	76309	// 1.164 * 65536
#define YUV_RGB_RV	104597	// 1.596 * 65536
#define YUV_RGB_GU	25675	// 0.391 * 65536
#define YUV_RGB_GV	53279	// 0.813 * 65536
#define YUV_RGB_BU	132201	// 2.018 * 65536

// One pixel: Y, U, V in, R, G, B out.
void YuvRgbPixel(int y, int u, int v, uint8_t* r, uint8_t* g, uint8_t* b);

/* One row of `width` pixels into 32-bit words 0xFFRRGGBB - X8R8G8B8 in
 * memory as B, G, R, X. `u` and `v` are the chroma row of this row (its
 * number halved); pixel x takes chroma x / 2. */
void YuvRgbRow(const uint8_t* y, const uint8_t* u, const uint8_t* v,
	uint32_t* out, int width);

/* A whole picture: the three planes and their linesizes as the decoder
 * gives them, into `dst` with `pitch` bytes per row. */
void YuvRgbImage(const uint8_t* const plane[3], const int linesize[3],
	int width, int height, void* dst, int pitch);

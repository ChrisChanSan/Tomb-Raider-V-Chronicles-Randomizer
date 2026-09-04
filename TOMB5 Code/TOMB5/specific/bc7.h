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

// CPU decoder for BC7 (DXGI_FORMAT_BC7_UNORM, the BPTC unorm block format)
// and for the .DDS container the remaster stores its pictures in. Direct3D 9
// has no BC7 at all, so the blocks have to be unpacked here and handed to the
// device as plain 32 bit pixels.
//
// This file is written against the format definition and deliberately has no
// engine dependencies - not even a Log() - so that the same code can be
// compiled into the diagnostic tool Preparation\Tools\Diagnose\DdsProbe.cpp,
// which decodes a .DDS to a .PNG. What the tool proves about the decoder is
// therefore true of the engine's decoder as well.

// One 16 byte BC7 block -> 16 pixels, row major, 0xAARRGGBB.
void BC7DecodeBlock(const unsigned char* src, unsigned long* dst);

// A whole DDS. Accepts only the shape the remaster's pictures actually have:
// a DX10 header, DXGI_FORMAT_BC7_UNORM or _UNORM_SRGB, 2D, one array slice,
// width and height a multiple of 8 (BC7 needs 4; the extra factor of two
// keeps the engine's 1/256 texture coordinates exact) and at most maxSide in
// either direction. Everything else is refused with a reason in *why.
//
// On success returns a malloc'd width*height buffer of 0xAARRGGBB pixels that
// the caller frees, and fills *width / *height. On failure returns 0 and sets
// *why to a short static string.
unsigned long* BC7DecodeDDS(const void* file, unsigned long size, long maxSide,
	long* width, long* height, const char** why);

// Consistency check of the built-in partition and anchor tables against the
// invariants the format guarantees (see bc7.cpp). Returns true when all of
// them hold; on failure *why names the first table and row that broke.
bool BC7SelfTest(const char** why);

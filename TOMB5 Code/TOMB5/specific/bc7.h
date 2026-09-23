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

// How many mip levels the file carries; 0 when it is not a DDS at all. A file
// without the DDSD_MIPMAPCOUNT flag counts as one level whatever its mip count
// field says. The remaster's 512x512 pages carry six (512..16).
long BC7DDSMipCount(const void* file, unsigned long size);

// The shape of the picture without decoding it: width, height and mip count
// out of the 148 byte header alone. The texture budget has to know what a
// page will cost before it spends a millisecond on it, and the only honest
// answer is the one in the file - four times the file's length is an
// estimate, this is the number. Reads nothing past the header, so 148 bytes
// of the file are enough. False when it is not a BC7 DDS.
bool BC7DDSShape(const void* file, unsigned long size, long* width, long* height,
	long* mips);

// The same as BC7DecodeDDS, but for one level of the mip chain; mip 0 is the
// full size picture and BC7DecodeDDS is exactly this call with mip = 0.
// maxSide is still measured against the FULL size, so a page is judged as a
// whole. *width / *height come back as that level's size. Levels below 4
// pixels on a side are refused - only whole 4x4 blocks are decoded - which no
// level of a remaster page ever is (the chain stops at 16x16).
unsigned long* BC7DecodeDDSMip(const void* file, unsigned long size, long maxSide,
	long mip, long* width, long* height, const char** why);

// Consistency check of the built-in partition and anchor tables against the
// invariants the format guarantees (see bc7.cpp). Returns true when all of
// them hold; on failure *why names the first table and row that broke.
bool BC7SelfTest(const char** why);

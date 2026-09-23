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

// A8R8G8B8 -> DXT5 (BC3) compression on the CPU.
//
// The normal path decodes every remaster page from BC7 to A8R8G8B8 losslessly
// and hands it to the card as-is, which for TITLE's 220 pages is 376 MiB of
// video memory. This file is the other end of the hd_texture_format switch:
// the same pixels compressed to BC3, a quarter of that size, at the cost of a
// measurable amount of quality. It is not the default - it is what a card
// with too little memory can still show, next to the alternative of dropping
// mip 0 (which the budget does on its own) or of no HD art at all.
//
// Why an encoder of our own and not D3DX: D3DXCompressShader and friends live
// in d3dx9_XX.dll, which this port deliberately does not ship and does not
// depend on - the whole D3D9 port runs against nothing but d3d9.dll. The
// encoder below is about a hundred and fifty lines of the plain range fit
// every BC1..BC3 encoder starts from; its quality is a number, not a claim:
// RoomProbe -format dxt5 -psnr measures peak signal to noise over a whole
// page through DXT5Decode, which is this file's own inverse and follows the
// block layout the card decodes in hardware.
//
// Engine-free like the rest of the decoders: no Direct3D type, no Log, so the
// diagnostic tool compiles the same code the engine runs.

// The bytes one picture takes as BC3: one 16 byte block per 4x4 texels.
unsigned long DXT5Size(long width, long height);

// pixels is width*height of 0xAARRGGBB, row major - what BC7DecodeDDSMip
// returns and what D3DFMT_A8R8G8B8 holds. out takes DXT5Size(width, height)
// bytes. Width and height must be whole numbers of 4x4 blocks; a remaster
// page's mip chain stops at 16x16, so every level of every page is.
void DXT5Encode(const unsigned long* pixels, long width, long height,
	unsigned char* out);

// The inverse, following the block layout of BC3 as Direct3D defines it - the
// proof's half of the pair. The engine never calls it; RoomProbe does, to say
// in a number what the switch costs.
void DXT5Decode(const unsigned char* blocks, long width, long height,
	unsigned long* pixels);

// Peak signal to noise ratio in decibels between two A8R8G8B8 pictures over
// all four channels, or 999 when they are identical. The measure used
// to judge the DXT5 switch.
double DXT5Psnr(const unsigned long* a, const unsigned long* b, long count);

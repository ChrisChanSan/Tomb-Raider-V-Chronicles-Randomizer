/*
 * Modified 2026 by ChrisChanSan.
 *
 * The search order for a missing page and the "no page is ever black, it is
 * WHITE.DDS" rule are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3, host\render\hd_pages.cpp, upload2 / fill_missing_tiles). The upload
 * itself is new: TROne hands the driver BC7 blocks in one texture array,
 * Direct3D 9 has neither. See THIRD-PARTY.md.
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

struct IDirect3DDevice9;
struct IDirect3DTexture9;

// THE HD TEXTURE PAGES OF ONE LEVEL (#123, stage S1).
//
// One Direct3D texture per .DDS page the level names - no atlas and no array.
// That is a measurement, not a compromise: over 1,590,768 triangles of RICH1
// and ANDREA1 not one triangle spans two pages, and after the sort by
// (room, pass, page) a level needs about 7.6 draw calls per room. Sharing
// pages in an atlas would buy nothing and cost bilinear bleeding and rebased
// texture coordinates.
//
// The pages are BC7, which Direct3D 9 does not know, so every one of them is
// decoded on the processor (specific\bc7.cpp) and uploaded as A8R8G8B8 - the
// user's decision: lossless, whatever it costs in video memory. The whole mip
// chain comes along; without it a wall at a distance sparkles.
//
// Engine-free like the rest of specific\hd: it takes a device and a list of
// page numbers, and it says what it did through the numbers and notes below
// instead of writing a log line itself.

struct HDPageCacheStats
{
	int pages;		// page numbers handed in
	int slots;		// distinct pages actually kept (duplicates folded)
	int fromGame5;		// found in <install>\TEX
	int fromGame4;		// found in the sibling 4\TEX (three RICH3 pages)
	int fromGame6;		// found in the sibling 6\TEX
	int fromWhite;		// no file anywhere - WHITE.DDS, the remaster's slot 0
	int failed;		// not even WHITE.DDS - that slot has no texture
	int mipLevels;		// mip levels uploaded over all pages
	int widest;		// widest page seen
	int bigPages;		// pages past 1024 on a side - 85 MiB each at 4096
	double bytes;		// texture bytes now on the device
	double readMs;		// wall clock: reading the files
	double decodeMs;	// wall clock: BC7 -> A8R8G8B8, over all threads
	double uploadMs;	// wall clock: lock, copy, UpdateTexture
	double totalMs;
	int managed;		// 1 = D3DPOOL_MANAGED, 0 = D3DPOOL_DEFAULT
	int threads;
};

// Builds the cache. `installRoot` is the remaster's game 5 folder (the one
// HDInstallRoot() returns), `pages` the .DDS numbers of the level.
//
// managed = false puts the textures in D3DPOOL_DEFAULT and pushes every mip
// through ONE reused system-memory staging texture, so the address space of
// the process never holds more than a single page at a time; managed = true
// lets Direct3D keep its own system-memory copy of everything, which survives
// a device reset but doubles the footprint. Measured numbers for both ways
// are in the report of stage S1-1 and in Preparation\Tools\README.md.
//
// threads is 0 or 1 for one core, otherwise that many decoding threads. The
// decode runs in batches so that only a batch's worth of decoded pages is in
// memory at once, whatever the level's page count.
//
// Refuses and reports through *why if the device cannot make the textures.
// Call HDPageCacheDestroy() before creating another one.
bool HDPageCacheCreate(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why);

// The same, but keeping what is already there (#123, stage S3-1). The models
// of a level name their own pages and a good many of them are pages the rooms
// already wear - a level's item pages and room pages overlap - so the models
// are ADDED to the cache rather than given a cache of their own: one texture
// per page for the whole level, one slot number, one place that frees them.
// Pages already in the cache are skipped silently; `stats` counts only what
// this call brought in. Creates the cache when there is none yet, which is
// what the diagnostic tool does when it draws a model with no level behind it.
bool HDPageCacheAppend(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why);

void HDPageCacheDestroy();

// The texture of a page number, or null when the level never named it.
IDirect3DTexture9* HDPageCacheTexture(int page);

// The same by slot, 0..HDPageCacheSlots()-1, which is the order the pages
// were handed in with the duplicates folded out. hd_render keeps slot numbers
// in its ranges so that a draw call is one array lookup, not a search.
int HDPageCacheSlots();
int HDPageCacheSlotOf(int page);			// -1 when unknown
IDirect3DTexture9* HDPageCacheSlotTexture(int slot);

// Everything that was not found where it should have been, in words, for the
// caller's log. Valid until the next Create or Destroy.
int HDPageCacheNoteCount();
const char* HDPageCacheNote(int at);

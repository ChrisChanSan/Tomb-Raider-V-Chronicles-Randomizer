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

// What a level's HD side contains.
//
// Reads <5>\DATA\<STEM>.TRG and <5>\DATA\<STEM>.MAP, counts what is in them,
// binds every loaded slot to its .TRM and looks whether that file is there.
// It draws nothing and builds no GPU object, and it frees everything again
// before it returns.
//
// Engine-free on purpose: this is the translation unit the vendored TRForge
// reader lives in (C++17, <filesystem>), and it is the same code the
// diagnostic tools TrgProbe and MapProbe run, so what they measure is what the
// game measures. The interface is plain C shapes only.

struct HDScanStats
{
	int  trgVersion;
	int  groups;			// what the .TRG has
	int  rooms;			// what the classic level has, as handed in
	int  skyGroups;			// groups beyond the room count - the sky dome
	int  emptyGroups;		// groups with no index at all -> that room classic
	int  pages;			// .DDS numbers the .TRG names
	int  vertices;
	int  indices;
	int  ranges;			// distinct (group, pass, page) after a stable sort
	int  pageChanges;		// page changes walking the stream unsorted
	int  mixedPageTriangles;	// triangles whose three corners disagree
	int  lightCubes;
	int  fogBoxes;
	int  boxVolumes;
	int  pointLights;

	int  mapEntries;
	int  slotsLoaded;
	int  trmBound;			// a model name that exists on disk
	int  trmMissing;		// a model name with no file
	int  trmDummy;			// the level says DUMMY on purpose
	int  trmLara;			// Lara's composite
	int  headsBound;		// <name>_HEAD.TRM found beside the model

	int  bc7Pages;			// pages actually decoded for the measurement
	int  bc7Mips;			// mip levels decoded over all of them
	double bc7Texels;
	double bc7Ms;
};

// numRooms is the classic level's room count. slotLoaded is one byte per slot
// (objects[i].loaded), slotCount its length - pass 0 to bind nothing.
// measureBc7 decodes every page of the level with its whole mip chain and
// times it; that costs seconds, so the engine only does it with -log.
// threads is 0 or 1 for one core, otherwise that many.
bool HDScanLevel(const char* installRoot, const char* stem, int numRooms,
	const unsigned char* slotLoaded, int slotCount,
	bool measureBc7, int threads, HDScanStats* out, const char** why);

// Everything the last HDScanLevel could not bind, for the log. Valid until the
// next call.
int  HDScanMissingCount();
const char* HDScanMissingSlot(int at);	// slot name
const char* HDScanMissingModel(int at);	// the .TRM it asked for

// The pages of the last scan, for a caller that wants to decode them itself.
int  HDScanPageCount();
int  HDScanPage(int at);

// Where a page lives: "<installRoot>\TEX\<page>.DDS". Written into buffer,
// which must hold at least 300 characters. Returns buffer.
const char* HDScanPagePath(const char* installRoot, int page, char* buffer, int size);

// Decodes `count` pages with their whole mip chains and returns the
// milliseconds it took; *texels and *mips count what was done. threads is 0 or
// 1 for one core. Used by HDScanLevel and, with more than one thread, by
// TrgProbe.
double HDScanDecodePages(const char* installRoot, const int* pages, int count,
	int threads, double* texels, int* mips, int* failed);

/* The .TEX table of one level.
 *
 * <install>\DATA\<STEM>.TEX, and it is the one file that says which HD
 * picture belongs to which classic tile:
 *
 *     uint16              how many HD pages this level uses
 *     uint16 * that many  their numbers in <install>\TEX
 *     uint16 * one per object texture of the level, in the level's own
 *                         order: which of the pages above paints that tile,
 *                         or 0xFFFF for a tile the remaster leaves classic
 *
 * The format is TRForge's measurement - its trf_remaster/Remaster.h writes it
 * down, and trf_remaster/HdPages.h writes down what a page is. Neither of
 * those two files is vendored here: vendor/trforge holds only what this tree
 * compiles (trf_core and Trg.cpp/.h), and both of those headers pull in
 * TRForge's own asset and level libraries, so they were used as a
 * description and nothing else - exactly as ItemModels.h was for the .TRM
 * format. See THIRD-PARTY.md, which says the same. This reader is written
 * from that description, not copied from it.
 *
 * What a page is, because the number alone is misleading: an HD page is not
 * a repaint of the tile's rectangle. It is a repaint of the whole 64 by 64
 * cell the rectangle sits in, at eight times the size - one 512 by 512 file
 * per cell. TRForge measured that over the shipped games: of 17,164 tiles
 * with HD art not one crosses a cell boundary, and of 4,335 occupied cells
 * not one wants two files. So the way from a classic tile to its HD texels
 * is
 *
 *     cell   = (u * 256 / 64, v * 256 / 64)      of the tile's own corner
 *     the HD coordinate of a classic u is  u * 4 - cell.x
 *
 * and that is all the arithmetic there is to it.
 *
 * What it is good for, and what it is not. The classic engine never opens
 * this file at all - nothing in this tree reads it but the function below -
 * and it names a page for only about a seventh of a level's tiles, so it is
 * not a way to draw a level in HD (the .TRG is, and that is what the room
 * renderer uses). It is exactly right for one question: the classic water
 * surfaces the .TRG has no geometry for are drawn from the classic level,
 * with the classic level's own animated texture range - and those frames are
 * among the tiles the .TEX does name.
 *
 * `out` takes one entry per tile, each the .DDS number or 0xFFFF; the return
 * is how many were written, 0 for a level whose file is missing or short
 * (which is not an error - the water then keeps the classic tile). */
int HDScanLevelTextures(const char* installRoot, const char* stem,
	unsigned short* out, int maxTiles);

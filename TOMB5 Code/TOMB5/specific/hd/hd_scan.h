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

// WHAT A LEVEL'S HD SIDE CONTAINS (#123, stage S0).
//
// Reads <5>\DATA\<STEM>.TRG and <5>\DATA\<STEM>.MAP, counts what is in them,
// binds every loaded slot to its .TRM and looks whether that file is there.
// It draws NOTHING and builds no GPU object - that is stage S1 - and it frees
// everything again before it returns.
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
	int  trmLara;			// Lara's composite - stage S4
	int  headsBound;		// <name>_HEAD.TRM found beside the model

	int  bc7Pages;			// pages actually decoded for the measurement
	int  bc7Mips;			// mip levels decoded over all of them
	double bc7Texels;
	double bc7Ms;
};

// numRooms is the classic level's room count. slotLoaded is one byte per slot
// (objects[i].loaded), slotCount its length - pass 0 to bind nothing.
// measureBc7 decodes every page of the level with its whole mip chain and
// times it; that is the point of stage S0 and it costs seconds, so the engine
// only does it with -log. threads is 0 or 1 for one core, otherwise that many.
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

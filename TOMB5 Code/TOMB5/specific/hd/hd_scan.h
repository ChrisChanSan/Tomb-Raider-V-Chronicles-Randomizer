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
// before it returns - but the parsed .TRG, while HDScanKeepTrg asks for it.
//
// Engine-free on purpose: this is the translation unit the vendored TRForge
// reader lives in (C++17, <filesystem>), and it is the same code the
// diagnostic tools TrgProbe and MapProbe run, so what they measure is what the
// game measures. The interface is plain C shapes only, but for the kept
// parse, which is the reader's own type.

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

/* The parsed .TRG of a scan, kept for the level's build so that a level load
 * reads the file once: the scan on the load thread reads it, the build on
 * the main thread takes the parse (hd_render.cpp, HDRenderLevelBegin) and
 * frees it with its own data. One parse at most is kept, with the path it
 * came from; the next scan replaces it. Off by default, so that a tool that
 * only scans keeps nothing.
 *
 * HDScanTakeTrg moves the kept parse of `path` into `out` and is true, or
 * leaves `out` alone and is false when none of that file is kept - the
 * caller reads it then. Either way nothing is kept after it.
 * HDScanTrgTaken says whether the last take found one, for the log.
 *
 * HDScanReadTrg reads and keeps the level's parse ahead of its scan, on a
 * thread of the level load's own (hd_level.cpp, HDLevelKeyNext), so that the
 * read runs beside the classic level's; the scan takes it as its own read.
 * HDScanKeptPageCount and HDScanKeptPage hand out the kept parse's page
 * numbers in the file's order, for the pages decoded ahead. */
namespace trf { namespace remaster { struct Trg; } }

void HDScanKeepTrg(bool keep);
bool HDScanTakeTrg(const char* path, trf::remaster::Trg* out);
bool HDScanTrgTaken();
void HDScanDropTrg();
bool HDScanReadTrg(const char* installRoot, const char* stem);
int HDScanKeptPageCount();
int HDScanKeptPage(int at);

/* --- a room group stored off its height ------------------------------------
 *
 * A .TRG corner is int16 x, y, z, with x and z room local and y absolute
 * (Trg.h). A room group whose art reaches above y -32768 cannot be stored
 * so, and the file then holds the whole group a multiple of 16,384 lower
 * (less negative) than it stands, the corners that still do not fit
 * clamped at -32768. Such a group's world y is its stored y plus its shift.
 *
 * The shift is read out of the classic room the group belongs to (group g
 * is room g's; the sky groups have no room): the room's distinct classic
 * corners are looked for among the group's corners at each shift k *
 * 16,384, k = -2..2. A classic corner counts at a shift when a corner of
 * the group lies within 4 units of (x, y - shift, z) on every axis, x and z
 * room local on both sides. The best shift is taken when it is not 0, has
 * 16 hits or more and at least twice the hits of shift 0; otherwise the
 * group's shift is 0. The rule and its thresholds are TROne's
 * (THIRD-PARTY.md).
 *
 * The shift is per group: a corner another group names as well keeps its
 * stored y for that group. The file's y stays int16; a world y may lie
 * below -32768 and is held as an int or a float wherever it is read. The
 * count is a template so that this header needs no Trg.h of its own and a
 * test can run the rule on a .TRG it makes itself. */
#include <algorithm>
#include <cstdio>
#include <vector>

#define HD_YSHIFT_STEP		16384
#define HD_YSHIFT_SHIFTS	5	// k = -2..2, the middle one no shift
#define HD_YSHIFT_TOLERANCE	4
#define HD_YSHIFT_LEAST		16

// The shift of candidate k, 0 .. HD_YSHIFT_SHIFTS - 1.
inline int HDYShiftOf(int k)
{
	return (k - HD_YSHIFT_SHIFTS / 2) * HD_YSHIFT_STEP;
}

/* One room's classic corners for the count: `count` corners `stride` floats
 * apart, x, y and z the first three of each, x and z room local. */
struct HDYShiftRoom
{
	const float* vtx;
	int count;
	int stride;
};

/* One group's count: its room's distinct classic corners, how many of them
 * were found at each shift, the shift taken (0 for most) and the group's own
 * distinct corners, the ones its shift moves. */
struct HDYShiftGroup
{
	int corners;
	int hits[HD_YSHIFT_SHIFTS];
	int shift;
	int moved;
};

// The rule's pick out of the hits at the five shifts.
inline int HDYShiftPick(const int* hits)
{
	const int none = HD_YSHIFT_SHIFTS / 2;
	int best = none;

	for (int k = 0; k < HD_YSHIFT_SHIFTS; k++)
		if (hits[k] > hits[best])
			best = k;

	if (best == none || hits[best] < HD_YSHIFT_LEAST ||
		hits[best] < 2 * hits[none])
		return 0;

	return HDYShiftOf(best);
}

/* A world y stored as int16 again, clamped into its range: what a shifted
 * corner above y -32768 becomes where only a short holds it. */
inline short HDYShiftClamp16(int y)
{
	return (short)(y < -32768 ? -32768 : (y > 32767 ? 32767 : y));
}

namespace hdyshift
{
	struct Corner
	{
		long long cell;
		int x, y, z;

		bool operator<(const Corner& o) const { return cell < o.cell; }
	};

	// The 8-unit cell of a coordinate: a tolerance of 4 reaches two of them.
	inline long long CellAxis(int v)
	{
		return (long long)((v + 1048576) >> 3) & 0xFFFFF;
	}

	inline long long Cell(int x, int y, int z)
	{
		return (CellAxis(x) << 40) | (CellAxis(y) << 20) | CellAxis(z);
	}

	// A corner of the sorted list within the tolerance of (x, y, z).
	inline bool Near(const std::vector<Corner>& sorted, int x, int y, int z)
	{
		const int t = HD_YSHIFT_TOLERANCE;

		for (int cx = x - t; cx <= x + t; cx += 2 * t)
			for (int cy = y - t; cy <= y + t; cy += 2 * t)
				for (int cz = z - t; cz <= z + t; cz += 2 * t)
				{
					// The same cell twice when both ends fall in one.
					if ((cx != x - t && CellAxis(cx) == CellAxis(x - t)) ||
						(cy != y - t && CellAxis(cy) == CellAxis(y - t)) ||
						(cz != z - t && CellAxis(cz) == CellAxis(z - t)))
						continue;

					Corner key;

					key.cell = Cell(cx, cy, cz);

					for (std::vector<Corner>::const_iterator it =
						std::lower_bound(sorted.begin(), sorted.end(), key);
						it != sorted.end() && it->cell == key.cell; ++it)
						if (it->x - x <= t && x - it->x <= t &&
							it->y - y <= t && y - it->y <= t &&
							it->z - z <= t && z - it->z <= t)
							return true;
				}

		return false;
	}

	struct Classic
	{
		int x, y, z;

		bool operator<(const Classic& o) const
		{
			return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z);
		}

		bool operator==(const Classic& o) const
		{
			return x == o.x && y == o.y && z == o.z;
		}
	};

	inline int Round(float v)
	{
		return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
	}
}

/* Group `group`'s count against its room's classic corners. `found`, when
 * given, gets one byte per distinct classic corner, bit k set where it was
 * found at shift k, and `classic` those corners as x, y, z triples, in the
 * same order. */
template <class Trg>
void HDYShiftCount(const Trg& trg, int group, const HDYShiftRoom& room,
	HDYShiftGroup* out, std::vector<unsigned char>* found = 0,
	std::vector<int>* classic = 0)
{
	*out = HDYShiftGroup();

	if (found)
		found->clear();

	if (classic)
		classic->clear();

	if (group < 0 || (size_t)group >= trg.groups.size())
		return;

	std::vector<unsigned int> own;

	for (const auto& part : trg.groups[(size_t)group].parts)
		for (int pass = 0; pass < 3; pass++)
			for (unsigned int at = 0; at < part.pass[pass].count; at++)
			{
				const size_t i = (size_t)part.pass[pass].start + at;

				if (i >= trg.indices.size())
					break;

				if (trg.indices[i] < trg.vertices.size())
					own.push_back(trg.indices[i]);
			}

	std::sort(own.begin(), own.end());
	own.erase(std::unique(own.begin(), own.end()), own.end());
	out->moved = (int)own.size();

	std::vector<hdyshift::Corner> sorted;

	sorted.reserve(own.size());

	for (unsigned int i : own)
	{
		hdyshift::Corner c;

		c.x = trg.vertices[i].x;
		c.y = trg.vertices[i].y;
		c.z = trg.vertices[i].z;
		c.cell = hdyshift::Cell(c.x, c.y, c.z);
		sorted.push_back(c);
	}

	std::sort(sorted.begin(), sorted.end());

	std::vector<hdyshift::Classic> corners;

	for (int k = 0; room.vtx && k < room.count; k++)
	{
		const float* p = room.vtx + (size_t)k * (size_t)room.stride;
		hdyshift::Classic c;

		c.x = hdyshift::Round(p[0]);
		c.y = hdyshift::Round(p[1]);
		c.z = hdyshift::Round(p[2]);
		corners.push_back(c);
	}

	std::sort(corners.begin(), corners.end());
	corners.erase(std::unique(corners.begin(), corners.end()),
		corners.end());
	out->corners = (int)corners.size();

	for (const hdyshift::Classic& c : corners)
	{
		unsigned char bits = 0;

		// stored = world - shift
		for (int k = 0; k < HD_YSHIFT_SHIFTS && !sorted.empty(); k++)
			if (hdyshift::Near(sorted, c.x, c.y - HDYShiftOf(k), c.z))
			{
				out->hits[k]++;
				bits |= (unsigned char)(1 << k);
			}

		if (found)
			found->push_back(bits);

		if (classic)
		{
			classic->push_back(c.x);
			classic->push_back(c.y);
			classic->push_back(c.z);
		}
	}

	out->shift = HDYShiftPick(out->hits);
}

/* Every room group's count, `rooms[g]` room g's corners: groups beyond
 * `count` or beyond the file's are not asked (the sky's). Returns the groups
 * that take a shift. */
template <class Trg>
int HDYShiftScan(const Trg& trg, const HDYShiftRoom* rooms, int count,
	HDYShiftGroup* out)
{
	int shifted = 0;

	for (int g = 0; rooms && g < count && (size_t)g < trg.groups.size(); g++)
	{
		HDYShiftCount(trg, g, rooms[g], &out[g]);
		shifted += out[g].shift != 0 ? 1 : 0;
	}

	return shifted;
}

/* The log's two lines, written alike by the engine (after its HDLOG) and by
 * RoomProbe: one per shifted group, one for the level. */
inline int HDYShiftGroupLine(char* buffer, size_t size, const char* stem,
	int group, const HDYShiftGroup& c)
{
	int at = HD_YSHIFT_SHIFTS / 2;

	for (int k = 0; k < HD_YSHIFT_SHIFTS; k++)
		if (HDYShiftOf(k) == c.shift)
			at = k;

	return snprintf(buffer, size, "yshift level=%s group=%d shift=%d "
		"corners=%d/%d unshifted=%d moved=%d", stem, group, c.shift,
		c.hits[at], c.corners, c.hits[HD_YSHIFT_SHIFTS / 2], c.moved);
}

inline int HDYShiftLevelLine(char* buffer, size_t size, const char* stem,
	int shifted, int groups)
{
	return snprintf(buffer, size, "yshift level=%s groups=%d of %d", stem,
		shifted, groups);
}

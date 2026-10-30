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

/* The art of a static mesh carried to another place of the same static.
 *
 * The remaster bakes the static meshes into the rooms' art, and the
 * statics' rule (hd_cover.h, HDStaticAnswer) leaves a static to the art
 * wherever the art draws its place. Where the art leaves a static out, the
 * classic mesh is drawn among the art: RICH2's stairwell (rooms 14 to 19,
 * all of its art in group 14) has fifteen flights of slot 60, and the art
 * models twelve of them with their railings and posts; the other three
 * flights, their railings and two posts are drawn classic, blue among the
 * art's steel. The same slot stands at the other placements in the same
 * art, and a static is placed by a position and a turn about the vertical
 * axis alone: two placements of one slot whose turns differ by whole
 * quarter turns are one rigid motion apart, and the art that models the
 * static at one carries to the other exactly, in whole units.
 *
 * The rule, at the HD level build, as the .TRG is read and before the
 * build reads it (hd_render.cpp, HDRenderLevelBegin), so that every table
 * built after it - the cells, the faces' coverage, the statics' answers,
 * the second shells, the cards, the visibility path's guests - sees the
 * copy as ordinary art of its group. On the visibility path only
 * (tomb5.hd_vis 1, the default): hd_level.cpp hands the statics in there
 * alone, as it does the shells' rooms.
 *
 *   - The group that draws a static's room: the room's own group where one
 *     of its triangles has its centroid in the room's box, grown by the
 *     overhang slack - the test that leaves a room classic before the
 *     build (hd_render.cpp, the holders of stand art) -, and otherwise the
 *     room group with the most triangle corners in the box. No room and no
 *     group of a flipmap pair.
 *   - A static's points on that art: its own mesh vertices that stand in a
 *     256-unit cell holding a corner of the group's art, a vertex on a
 *     cell's boundary in both cells - the statics' rule's measure
 *     (hd_render.cpp, GroupVertsIn).
 *   - A donor: every one of its points on the art.
 *   - A target: a static the art does not hold - fewer than nine twentieths
 *     of its points on the art (HDStaticOtherHolds), in a room drawn by its
 *     own art fewer than a twentieth of its box's cells as well
 *     (HDStaticCellsHold) -, which no other group's art holds either; a
 *     flat static asks no other group, as in the statics' rule.
 *   - Its donor: a donor of the same slot in a room the same group draws,
 *     its turn a whole number of quarter turns from the target's; the one
 *     with no turn first, then the nearest, then the first in the level's
 *     order. The art must model the slot alike at its donors - the
 *     triangles wholly inside each donor's grown box, the most at most
 *     HD_ARTCOPY_ALIKE times the fewest -, or the slot gets no copy: where
 *     the donors differ, the art round one is not the slot's own.
 *   - The copy: the targets whose donors one motion moves onto them - the
 *     turn about the vertical axis through the donor's place, then the
 *     shift to the target's - take together the group's triangles every
 *     corner of which lies in their donors' boxes grown by
 *     HD_ARTCOPY_MARGIN, so that a handrail running on through the boxes
 *     of two flights and a post comes whole; less those that are a room's
 *     surface at the donor or at the target (lying whole on a classic face
 *     at HD_ARTCOPY_WALL, or in a face's plane within HD_ARTCOPY_WALL_GAP
 *     of it), and less those whose image stands on the group's art already
 *     there (more than half of its corners, centroid and samples within
 *     HD_ARTCOPY_STANDS of a triangle facing its way: a second surface).
 *     A triangle is taken once per motion.
 *   - The faces beside a copy: a classic face of the target's room that
 *     touches its box, whose image by the motion taken back is a face at
 *     the donor with the group's art on it - the landing at a flight's
 *     end -, takes that art as well, less what stands at the target
 *     already; the face then falls to the art as any face the art covers.
 *   - Its vertices keep the donor's texture, both colours and sway, their
 *     normal turned with them; they go into the vertex list right after
 *     the group's last vertex, and each triangle goes into the run it was
 *     taken from, right after the run's own triangles: it is drawn with the
 *     donor's material, page and pass, and every run that started there or
 *     later moves up, as the sky's caps do (hd_sky_cap.h). A copy one of
 *     whose corners would leave the .TRG's 16 bit range is left out.
 *
 * The same rule for an item the run added (the sidecar's `item` line) to a
 * slot the art carries - the .MAP calls it DUMMY, and the art models the
 * slot where Core placed it and nowhere else (hd_object.cpp,
 * begin_decision). The items of such slots are handed in beside the
 * statics (HDArtCopyItem), each with its box out of its object's first
 * frame at its place and yaw (HDStandItemBox), and asked only in a level
 * where one of them is the run's own:
 *
 *   - A donor: an item of the slot Core placed, in a room a group draws
 *     (as a static's), its yaw a whole number of quarter turns, the art in
 *     its grown box lit: where fewer than HD_ARTCOPY_LIT of its corners
 *     carry any light - one of JOBY3's store rooms has its cupboards so,
 *     every colour byte zero but on the wall behind them - the copy would
 *     be black (`unlit`).
 *   - A target: an added item in a room drawn by its own art, where
 *     begin_decision bakes the slot's items (elsewhere `no_group`); one
 *     whose yaw is no whole number of quarter turns gets no copy
 *     (`skewed`).
 *   - Its donor: of the same slot anywhere in the level, the one with no
 *     turn first, then the nearest, then the first in the level's order;
 *     the art must model the slot alike at all of its donors, the level
 *     over (HD_ARTCOPY_ALIKE).
 *   - The copy: as a static's, out of the donor's group, less what lies on
 *     a classic face at either place and what stands on the art of the
 *     group that draws the target's room, which the copy goes into, read
 *     in the frame of the one group and written in the frame of the other.
 *     A triangle's page is its own - its first corner's layer byte,
 *     one-based into the file's one texture list - and a part's material
 *     and four numbers draw alike in every group: the triangle goes into
 *     the run of its pass of the target group's first part with the same
 *     material and numbers (hdartcopy::Level::RunIn); where that group has
 *     no such part the item gets no copy (`no_part`).
 *   - An added item a copy holds is drawn by the art as the slot's other
 *     items are (hd_object.cpp, begin_decision, HDRenderArtCopyItemHeld);
 *     its logic runs on, and the art stands still as theirs does.
 *
 * The static the copy is for is still answered by the statics' rule, at its
 * own points: nothing of it is given to the art that the art does not draw.
 * No engine header, like hd_cover.h: the engine, RoomProbe and ModernTest
 * run the same code; the walks over the groups' triangles run on the level
 * load's pool (hd_pagecache.h, HDPoolRun), each group writing its own. */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../vendor/trforge/src/trf_remaster/Trg.h"
#include "hd_cover.h"
#include "hd_pagecache.h"		// HDPoolRun

#define HD_ARTCOPY_MARGIN	32.0f
#define HD_ARTCOPY_WALL		16.0
#define HD_ARTCOPY_STANDS	16.0
#define HD_ARTCOPY_QUARTER	16384
/* A wall's art across its classic faces' gaps: a triangle in a face's
 * plane, facing its way within this cosine, within half a sector of it. */
#define HD_ARTCOPY_WALL_GAP	512.0f
/* A copy's triangle stands on the art already there when more than half
 * of its points do (hdartcopy::OnArt). */
#define HD_ARTCOPY_STANDING_NUM	1
#define HD_ARTCOPY_STANDING_DEN	2
#define HD_ARTCOPY_WALL_DOT	0.98
/* How far the art of one slot's donors may differ, as the triangles in
 * their grown boxes: the most at three halves of the fewest. */
#define HD_ARTCOPY_ALIKE_NUM	3
#define HD_ARTCOPY_ALIKE_DEN	2
/* An item's donor carries light when at least a twentieth of the corners
 * of the art in its grown box have a colour byte above zero. */
#define HD_ARTCOPY_LIT_NUM	1
#define HD_ARTCOPY_LIT_DEN	20

// One room of the level as the rule asks it: its origin, its own box (room
// local, HDRoomBox) where `box` says it has one, its flipmap twin or -1.
struct HDArtCopyRoom
{
	int originX, originZ;
	float mins[3], maxs[3];
	int box;
	int twin;
};

struct HDArtCopyParams
{
	float margin;		// HD_ARTCOPY_MARGIN
	double wall;		// HD_ARTCOPY_WALL
	double stands;		// HD_ARTCOPY_STANDS
	float ownSlack;		// the renderer's HD_OVERHANG_SLACK
	int offered;		// 1: every donor a target was offered, for the tools
};

// A donor a target was offered, with what it would give (params.offered).
struct HDArtCopyOffer
{
	int target, donor, group;
	int dx, dy, dz, turn;
	int inside, crossing, walls, standing;
	int chosen;
};

// One slot of one group whose donors were weighed (HD_ARTCOPY_ALIKE).
struct HDArtCopyAlike
{
	int group, slot;
	int donors;			// the statics of the slot the group's art holds
	int fewest, most;	// the triangles wholly inside their grown boxes
	int alike;
};

struct HDArtCopyPlan
{
	std::vector<HDArtCopy> copies;
	// Per copy, the donor's triangles it takes: the first index into the
	// .TRG's index list and the run (part * 3 + pass) of the copy's group.
	std::vector<uint32_t> tris;
	std::vector<uint32_t> runs;
	std::vector<HDArtCopyOffer> offered;
	std::vector<HDArtCopyAlike> weighed;
	// Per room, the group that draws it before the build, -1 for none.
	std::vector<int> drawn;
	/* Per static: the group that draws its room (-1 for none or a room of
	 * a flipmap pair), its points on that art, and its part: 1 a donor, 2
	 * a target, 3 held by another group's art, 4 a target whose donors'
	 * art differs (HD_ARTCOPY_ALIKE), 0 none of these. */
	std::vector<int> group, held;
	std::vector<unsigned char> role;
	int statics = 0;		// asked
	int donors = 0;		// every point on the art
	int targets = 0;		// the art does not hold them
	int heldElsewhere = 0;	// of those the art does not hold, held by
							// another group's art
	int noDonor = 0;		// of the targets, with no donor
	int unlike = 0;		// of the targets, whose donors' art differs
	int addedTris = 0, addedVerts = 0;
	/* The items of the slots the art carries (HDArtCopyItem), beside the
	 * statics: per item the group that draws its room (-1 for none), its
	 * drawing group's triangles wholly inside its grown box and its answer
	 * (HD_ARTCOPY_ITEM_*); the copies (their target and donor index the
	 * items, `fromGroup` the donor's group), every donor each added item
	 * was offered and every slot whose donors were weighed (group -1: the
	 * donors of the whole level). */
	std::vector<HDArtCopy> itemCopies;
	std::vector<int> itemGroup, itemInside;
	std::vector<unsigned char> itemRole;
	std::vector<HDArtCopyOffer> itemOffered;
	std::vector<HDArtCopyAlike> itemWeighed;
	int items = 0;			// the added items asked
	int itemDonors = 0;		// the Core-placed items that may give theirs
	int itemTris = 0, itemVerts = 0;
};

/* A point of the donor's art moved to the target: turned by `turn` quarter
 * turns about the vertical axis through the donor's place - the turn of
 * MESH_INFO::y_rot, hd_level.cpp's StaticBox: x' = x c + z s,
 * z' = z c - x s - and moved to the target's place. World units. */
inline void HDArtCopyMove(int turn, const int from[3], const int to[3],
	const int p[3], int out[3])
{
	const int x = p[0] - from[0], z = p[2] - from[2];
	int tx = x, tz = z;

	switch (turn & 3)
	{
	case 1: tx = z; tz = -x; break;
	case 2: tx = -x; tz = -z; break;
	case 3: tx = -z; tz = x; break;
	default: break;
	}

	out[0] = to[0] + tx;
	out[1] = to[1] + p[1] - from[1];
	out[2] = to[2] + tz;
}

// A biased normal byte negated: (n - 127) / 128 becomes its negative.
inline uint8_t HDArtCopyNegate(uint8_t n)
{
	const int m = 254 - (int)n;

	return (uint8_t)(m < 0 ? 0 : m > 255 ? 255 : m);
}

// A vertex's normal turned with it.
inline void HDArtCopyTurnNormal(int turn, trf::remaster::TrgVertex& v)
{
	const uint8_t nx = v.nx, nz = v.nz;

	switch (turn & 3)
	{
	case 1: v.nx = nz; v.nz = HDArtCopyNegate(nx); break;
	case 2: v.nx = HDArtCopyNegate(nx); v.nz = HDArtCopyNegate(nz); break;
	case 3: v.nx = HDArtCopyNegate(nz); v.nz = nx; break;
	default: break;
	}
}

// A copy's motion as one key: its turn and its shift.
inline uint64_t HDArtCopyMotionKey(int turn, int dx, int dy, int dz)
{
	return ((uint64_t)(uint32_t)turn << 60) ^
		((uint64_t)(uint32_t)dx << 40) ^ ((uint64_t)(uint32_t)dy << 20) ^
		(uint64_t)(uint32_t)dz;
}

namespace hdartcopy
{
	/* A 256-unit cell of the world, by its cell coordinates (a world unit
	 * >> 8). The rooms' origins are whole sectors, so a cell of the world
	 * is a cell of every group's own frame, as the statics' rule counts
	 * them (hd_render.cpp, CellKey). */
	inline uint64_t Key(int cx, int cy, int cz)
	{
		return ((uint64_t)(uint32_t)(cx + 0x100000) << 42) |
			((uint64_t)(uint32_t)(cy + 0x100000) << 21) |
			(uint64_t)(uint32_t)(cz + 0x100000);
	}

	inline int KeyX(uint64_t key)
	{
		return (int)((key >> 42) & 0x1FFFFF) - 0x100000;
	}

	inline int KeyZ(uint64_t key)
	{
		return (int)(key & 0x1FFFFF) - 0x100000;
	}

	/* The cells a point asks: the cell its coordinate falls into and, on an
	 * axis where it lies on a cell's boundary, the cell before as well
	 * (hd_render.cpp, GroupVertsIn). */
	inline int PointCells(const float* p, uint64_t out[8])
	{
		int at[3][2], n[3];

		for (int k = 0; k < 3; k++)
		{
			at[k][0] = (int)floorf(p[k]);
			at[k][1] = at[k][0] - 1;
			n[k] = p[k] == (float)at[k][0] && (at[k][0] & 255) == 0 ? 2 : 1;
		}

		int count = 0;

		for (int xi = 0; xi < n[0]; xi++)
			for (int yi = 0; yi < n[1]; yi++)
				for (int zi = 0; zi < n[2]; zi++)
					out[count++] = Key(at[0][xi] >> 8, at[1][yi] >> 8,
						at[2][zi] >> 8);

		return count;
	}

	/* The cells of a static's box as HDRenderStaticArt counts them: a cell
	 * whose corner lies within the box or 255 units before it on every
	 * axis (HDRenderGroupCellsIn), and the box's cell count, ((max >> 8) -
	 * (min >> 8) + 1) an axis. World units. */
	template <class Visit>
	inline int BoxCells(const float* mins, const float* maxs, Visit visit)
	{
		int from[3], to[3], count = 1;

		for (int k = 0; k < 3; k++)
		{
			from[k] = (int)ceilf((mins[k] - 255.0f) / 256.0f);
			to[k] = (int)floorf(maxs[k] / 256.0f);
			count *= ((int)maxs[k] >> 8) - ((int)mins[k] >> 8) + 1;
		}

		for (int x = from[0]; x <= to[0]; x++)
			for (int y = from[1]; y <= to[1]; y++)
				for (int z = from[2]; z <= to[2]; z++)
					visit(Key(x, y, z));

		return count;
	}

	/* One group's art as the rule reads it: whether it has any, the world
	 * box of its corners, whether one of its triangles has its centroid in
	 * its own room's box grown by the slack, its corners in the classic
	 * rooms' boxes (`counts`, Level::Count) and - for a group a copy is
	 * taken from - its triangles (first index and run). */
	struct Art
	{
		bool any = false, own = false, trisMade = false;
		int lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
		std::vector<int> counts;
		std::vector<uint32_t> first, run;
	};

	struct Level
	{
		const trf::remaster::Trg* trg = 0;
		int groups = 0;
		const int* shift = 0;
		int shifts = 0;
		const HDArtCopyRoom* rooms = 0;
		int roomCount = 0;
		int threads = 1;
		std::vector<Art> art;
		/* The cells the rule asks, sorted, the columns (x and z) they stand
		 * in, and per asked cell the groups whose art has a corner there,
		 * as (cell, group), sorted. */
		std::vector<uint64_t> asked;
		int cx0 = 0, cz0 = 0, cxs = 0, czs = 0;
		std::vector<unsigned char> columns;
		std::vector<std::pair<uint32_t, int> > hits;

		int Shift(int g) const
		{
			return shift && g >= 0 && g < shifts ? shift[g] : 0;
		}

		int OriginX(int g) const
		{
			return g >= 0 && g < roomCount ? rooms[g].originX : 0;
		}

		int OriginZ(int g) const
		{
			return g >= 0 && g < roomCount ? rooms[g].originZ : 0;
		}

		// A corner of group g in world units.
		void Corner(int g, uint32_t index, int out[3]) const
		{
			const trf::remaster::TrgVertex& v = trg->vertices[index];

			out[0] = (int)v.x + OriginX(g);
			out[1] = (int)v.y + Shift(g);
			out[2] = (int)v.z + OriginZ(g);
		}

		/* Every whole triangle of group g, by its first index and its run;
		 * `visit` answers false to stop. */
		template <class Visit>
		void Triangles(int g, Visit visit) const
		{
			const trf::remaster::TrgGroup& group = trg->groups[(size_t)g];
			const uint32_t* index = trg->indices.empty() ? 0 :
				&trg->indices[0];
			const size_t verts = trg->vertices.size();

			for (size_t p = 0; p < group.parts.size(); p++)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run =
						group.parts[p].pass[pass];

					if ((size_t)run.start + run.count > trg->indices.size())
						continue;

					for (uint32_t at = 0; at + 2 < run.count; at += 3)
					{
						const uint32_t first = run.start + at;

						if (index[first] < verts && index[first + 1] < verts &&
							index[first + 2] < verts &&
							!visit(first, (uint32_t)(p * 3 + (size_t)pass)))
							return;
					}
				}
		}

		/* The room groups' runs in pieces of at most kPieceTris triangles,
		 * group by group: the walks below run a piece at a time on the
		 * pool, each piece writing its own, so that one large group does
		 * not hold up the rest. */
		struct Piece
		{
			int g;
			uint32_t first, count;	// index positions, whole triangles
			uint32_t run;
		};

		static const uint32_t kPieceTris = 16384;
		std::vector<Piece> pieces;

		void MakePieces()
		{
			pieces.clear();

			for (int g = 0; g < groups; g++)
			{
				const trf::remaster::TrgGroup& group = trg->groups[(size_t)g];

				for (size_t p = 0; p < group.parts.size(); p++)
					for (int pass = 0; pass < 3; pass++)
					{
						const trf::remaster::TrgPart::Pass& run =
							group.parts[p].pass[pass];

						if ((size_t)run.start + run.count > trg->indices.size())
							continue;

						const uint32_t whole = run.count / 3 * 3;

						for (uint32_t at = 0; at < whole; at += kPieceTris * 3)
						{
							Piece one;

							one.g = g;
							one.first = run.start + at;
							one.count = (std::min)(whole - at, kPieceTris * 3);
							one.run = (uint32_t)(p * 3 + (size_t)pass);
							pieces.push_back(one);
						}
					}
			}
		}

		// Every whole triangle of a piece; `visit` as for Triangles.
		template <class Visit>
		void PieceTriangles(const Piece& piece, Visit visit) const
		{
			const uint32_t* index = &trg->indices[0];
			const size_t verts = trg->vertices.size();

			for (uint32_t at = 0; at + 2 < piece.count; at += 3)
			{
				const uint32_t first = piece.first + at;

				if (index[first] < verts && index[first + 1] < verts &&
					index[first + 2] < verts && !visit(first, piece.run))
					return;
			}
		}

		void Ask(uint64_t key)
		{
			asked.push_back(key);
		}

		/* One walk over every room group's triangles, a piece at a time on
		 * the pool: each group's box, whether one of its triangles has its
		 * centroid in its own room's box grown by `slack` (a room with no
		 * box: whether it has any art), and per asked cell the groups whose
		 * art has a corner in it - a corner in a column no asked cell
		 * stands in is passed over at once. */
		void Walk(float slack)
		{
			art.assign((size_t)groups, Art());
			MakePieces();
			std::sort(asked.begin(), asked.end());
			asked.erase(std::unique(asked.begin(), asked.end()), asked.end());
			hits.clear();
			cxs = czs = 0;
			columns.clear();

			if (!asked.empty())
			{
				int x0 = 0, x1 = -1, z0 = 0, z1 = -1;

				for (uint64_t key : asked)
				{
					const int x = KeyX(key), z = KeyZ(key);

					x0 = x1 < x0 || x < x0 ? x : x0;
					x1 = x1 < x0 || x > x1 ? x : x1;
					z0 = z1 < z0 || z < z0 ? z : z0;
					z1 = z1 < z0 || z > z1 ? z : z1;
				}

				cx0 = x0;
				cz0 = z0;
				cxs = x1 - x0 + 1;
				czs = z1 - z0 + 1;
				columns.assign((size_t)cxs * (size_t)czs, 0);

				for (uint64_t key : asked)
					columns[(size_t)(KeyX(key) - cx0) * (size_t)czs +
						(size_t)(KeyZ(key) - cz0)] = 1;
			}

			struct Out
			{
				bool any, own;
				int lo[3], hi[3];
				std::vector<uint32_t> cells;
			};

			std::vector<Out> out(pieces.size());

			HDPoolRun((int)pieces.size(), threads, [&](int at)
			{
				const Piece& piece = pieces[(size_t)at];
				const int g = piece.g;
				const HDArtCopyRoom& room = rooms[g];
				const int ox = OriginX(g), oz = OriginZ(g), dy = Shift(g);
				Out& o = out[(size_t)at];

				o.any = o.own = false;

				for (int i = 0; i < 3; i++)
					o.lo[i] = o.hi[i] = 0;

				PieceTriangles(piece, [&](uint32_t first, uint32_t)
				{
					int c[3] = { 0, 0, 0 };

					for (int k = 0; k < 3; k++)
					{
						const trf::remaster::TrgVertex& v =
							trg->vertices[trg->indices[first + (uint32_t)k]];
						const int p[3] = { (int)v.x + ox, (int)v.y + dy,
							(int)v.z + oz };

						for (int i = 0; i < 3; i++)
						{
							o.lo[i] = !o.any || p[i] < o.lo[i] ? p[i] : o.lo[i];
							o.hi[i] = !o.any || p[i] > o.hi[i] ? p[i] : o.hi[i];
							c[i] += p[i];
						}

						o.any = true;

						const int x = p[0] >> 8, z = p[2] >> 8;
						const int i = x - cx0, j = z - cz0;

						if (i < 0 || i >= cxs || j < 0 || j >= czs ||
							!columns[(size_t)i * (size_t)czs + (size_t)j])
							continue;

						const uint64_t key = Key(x, p[1] >> 8, z);
						const std::vector<uint64_t>::const_iterator it =
							std::lower_bound(asked.begin(), asked.end(), key);

						if (it != asked.end() && *it == key)
							o.cells.push_back((uint32_t)(it - asked.begin()));
					}

					if (room.box && !o.own)
					{
						const float centre[3] =
						{
							(float)c[0] / 3.0f - (float)room.originX,
							(float)c[1] / 3.0f,
							(float)c[2] / 3.0f - (float)room.originZ
						};
						bool in = true;

						for (int i = 0; i < 3; i++)
							in = in && centre[i] >= room.mins[i] - slack &&
								centre[i] <= room.maxs[i] + slack;

						o.own = in;
					}

					return true;
				});

				std::sort(o.cells.begin(), o.cells.end());
				o.cells.erase(std::unique(o.cells.begin(), o.cells.end()),
					o.cells.end());
			});

			for (size_t k = 0; k < pieces.size(); k++)
			{
				const Out& o = out[k];
				const int g = pieces[k].g;
				Art& a = art[(size_t)g];

				for (uint32_t cell : o.cells)
					hits.push_back(std::make_pair(cell, g));

				if (!o.any)
					continue;

				for (int i = 0; i < 3; i++)
				{
					a.lo[i] = !a.any || o.lo[i] < a.lo[i] ? o.lo[i] : a.lo[i];
					a.hi[i] = !a.any || o.hi[i] > a.hi[i] ? o.hi[i] : a.hi[i];
				}

				a.any = true;
				a.own = a.own || o.own;
			}

			for (int g = 0; g < groups; g++)
				if (!rooms[g].box)
					art[(size_t)g].own = art[(size_t)g].any;

			std::sort(hits.begin(), hits.end());
			hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
		}

		/* Per group, its corners in each of the rooms' boxes (world units,
		 * six floats a box), a piece at a time on the pool; `skip` names
		 * the groups not counted. */
		void Count(const std::vector<float>& boxes,
			const std::vector<unsigned char>& skip)
		{
			const size_t n = boxes.size() / 6;
			std::vector<int> out(pieces.size() * n, 0);

			HDPoolRun((int)pieces.size(), threads, [&](int at)
			{
				const Piece& piece = pieces[(size_t)at];
				std::vector<size_t> meet;

				if (skip[(size_t)piece.g])
					return;

				for (size_t k = 0; k < n; k++)
					if (BoxMeets(piece.g, &boxes[k * 6], &boxes[k * 6 + 3],
						0.0f))
						meet.push_back(k);

				if (meet.empty())
					return;

				PieceTriangles(piece, [&](uint32_t first, uint32_t)
				{
					for (int c = 0; c < 3; c++)
					{
						int p[3];

						Corner(piece.g, trg->indices[first + (uint32_t)c], p);

						for (size_t k : meet)
						{
							const float* b = &boxes[k * 6];
							bool in = true;

							for (int i = 0; i < 3 && in; i++)
								in = (float)p[i] >= b[i] &&
									(float)p[i] <= b[i + 3];

							out[(size_t)at * n + k] += in ? 1 : 0;
						}
					}

					return true;
				});
			});

			for (int g = 0; g < groups; g++)
				art[(size_t)g].counts.assign(n, 0);

			for (size_t k = 0; k < pieces.size(); k++)
				for (size_t b = 0; b < n; b++)
					art[(size_t)pieces[k].g].counts[b] += out[k * n + b];
		}

		// Whether group g's art has a corner in the cell (an asked one).
		bool Holds(int g, uint64_t key) const
		{
			const std::vector<uint64_t>::const_iterator it =
				std::lower_bound(asked.begin(), asked.end(), key);

			if (it == asked.end() || *it != key)
				return false;

			return std::binary_search(hits.begin(), hits.end(),
				std::make_pair((uint32_t)(it - asked.begin()), g));
		}

		// Its triangles, for a group a copy is taken from.
		const Art& Tris(int g)
		{
			Art& a = art[(size_t)g];

			if (a.trisMade)
				return a;

			a.trisMade = true;
			Triangles(g, [&](uint32_t first, uint32_t run)
			{
				a.first.push_back(first);
				a.run.push_back(run);
				return true;
			});
			return a;
		}

		bool BoxMeets(int g, const float* mins, const float* maxs,
			float grow) const
		{
			const Art& a = art[(size_t)g];

			if (!a.any)
				return false;

			for (int k = 0; k < 3; k++)
				if ((float)a.hi[k] < mins[k] - grow ||
					(float)a.lo[k] > maxs[k] + grow)
					return false;

			return true;
		}

		/* The run of group `to` that a triangle of group `from`'s run
		 * `run` goes into: the same pass of the first part of `to` whose
		 * material and four numbers are those of the run's own part; -1
		 * for none. A part draws alike in every group, and the page is the
		 * triangle's own: its first corner's layer byte, one-based into
		 * the file's one texture list (Trg.h, TrgVertex::layer; the
		 * renderer's group build, HDRenderLevelBegin), so the triangle
		 * wears the same picture in either group. */
		int RunIn(int from, uint32_t run, int to) const
		{
			if (from == to)
				return (int)run;

			const std::vector<trf::remaster::TrgPart>& a =
				trg->groups[(size_t)from].parts;
			const std::vector<trf::remaster::TrgPart>& b =
				trg->groups[(size_t)to].parts;
			const size_t p = run / 3;

			if (p >= a.size())
				return -1;

			for (size_t k = 0; k < b.size(); k++)
				if (b[k].material == a[p].material &&
					!memcmp(b[k].param, a[p].param, sizeof(a[p].param)))
					return (int)(k * 3 + run % 3);

			return -1;
		}
	};

	/* How many of a static's points stand in a cell holding a corner of
	 * group g's art, or in one of `extra` (sorted): cells a copy fills. */
	inline int PointsOn(const Level& level, int g, const float* points,
		int count, const std::vector<uint64_t>* extra = 0)
	{
		int found = 0;

		for (int v = 0; v < count; v++)
		{
			uint64_t keys[8];
			const int n = PointCells(&points[v * 3], keys);
			bool in = false;

			for (int k = 0; k < n && !in; k++)
				in = level.Holds(g, keys[k]) || (extra &&
					std::binary_search(extra->begin(), extra->end(), keys[k]));

			found += in ? 1 : 0;
		}

		return found;
	}

	// The cells of a static's box holding a corner of group g's art, and
	// the box's cells.
	inline int BoxCellsOn(const Level& level, int g, const float* mins,
		const float* maxs, int* boxCells)
	{
		int found = 0;

		*boxCells = BoxCells(mins, maxs, [&](uint64_t key)
		{
			found += level.Holds(g, key) ? 1 : 0;
		});

		return found;
	}

	// A triangle out of three world corners for hd_cover.h's tests.
	inline bool CoverTri(const int v[3][3], int g, HDCoverTri* out)
	{
		float f[3][3];
		const float origin[3] = { 0.0f, 0.0f, 0.0f };

		for (int c = 0; c < 3; c++)
			for (int k = 0; k < 3; k++)
				f[c][k] = (float)v[c][k];

		return HDCoverMakeTri(f, origin, g, out);
	}

	inline bool Inside(const int p[3], const float* mins, const float* maxs,
		float grow)
	{
		for (int k = 0; k < 3; k++)
			if ((float)p[k] < mins[k] - grow || (float)p[k] > maxs[k] + grow)
				return false;

		return true;
	}

	// The faces whose box, grown by `reach`, meets a box grown by `grow`.
	inline void FacesNear(const HDCoverFace* faces, int count,
		const float* mins, const float* maxs, float grow, double reach,
		std::vector<HDCoverFace>& out)
	{
		out.clear();

		for (int f = 0; f < count; f++)
		{
			const HDCoverFace& face = faces[f];
			bool meets = true;

			for (int k = 0; k < 3 && meets; k++)
			{
				double lo = face.v[0][k], hi = face.v[0][k];

				for (int c = 1; c < face.corners && c < 4; c++)
				{
					lo = face.v[c][k] < lo ? face.v[c][k] : lo;
					hi = face.v[c][k] > hi ? face.v[c][k] : hi;
				}

				meets = hi + reach >= (double)(mins[k] - grow) &&
					lo - reach <= (double)(maxs[k] + grow);
			}

			if (meets)
				out.push_back(face);
		}
	}

	// A copy's turn out of the two statics: whole quarter turns or none.
	inline bool Motion(const HDArtCopyStatic& donor,
		const HDArtCopyStatic& target, int* turn)
	{
		const int d = (target.rot - donor.rot) & 0xFFFF;

		if (d % HD_ARTCOPY_QUARTER)
			return false;

		*turn = d / HD_ARTCOPY_QUARTER;
		return true;
	}

	/* A group's art standing round its targets' places: its triangles that
	 * reach into one of the boxes, as faces for HDCoverPointOnFaces,
	 * bucketed by the 256-unit cells their box, grown by the reach, fills. */
	struct Standing
	{
		std::vector<HDCoverFace> faces;
		std::vector<std::pair<uint64_t, int> > cells;

		void Add(const int v[3][3], double reach)
		{
			HDCoverFace face;
			int lo[3], hi[3];

			face.corners = 3;
			face.doubleSided = 0;

			for (int c = 0; c < 4; c++)
				for (int k = 0; k < 3; k++)
					face.v[c][k] = (double)v[c < 3 ? c : 0][k];

			for (int k = 0; k < 3; k++)
			{
				lo[k] = (int)floor((std::min)((std::min)(v[0][k], v[1][k]),
					v[2][k]) - reach) >> 8;
				hi[k] = (int)ceil((std::max)((std::max)(v[0][k], v[1][k]),
					v[2][k]) + reach) >> 8;
			}

			const int at = (int)faces.size();

			faces.push_back(face);

			for (int x = lo[0]; x <= hi[0]; x++)
				for (int y = lo[1]; y <= hi[1]; y++)
					for (int z = lo[2]; z <= hi[2]; z++)
						cells.push_back(std::make_pair(Key(x, y, z), at));
		}

		void Done()
		{
			std::sort(cells.begin(), cells.end());
		}

		bool On(const double p[3], const float n[3], double reach) const
		{
			const uint64_t key = Key((int)floor(p[0]) >> 8,
				(int)floor(p[1]) >> 8, (int)floor(p[2]) >> 8);
			std::vector<std::pair<uint64_t, int> >::const_iterator it =
				std::lower_bound(cells.begin(), cells.end(),
				std::make_pair(key, -1));

			for (; it != cells.end() && it->first == key; ++it)
				if (HDCoverPointOnFaces(p, n, &faces[(size_t)it->second], 1,
					reach))
					return true;

			return false;
		}
	};

	// A face's box grown by `grow` meets a box.
	inline bool FaceMeets(const HDCoverFace& face, const float* mins,
		const float* maxs, double grow)
	{
		for (int k = 0; k < 3; k++)
		{
			double lo = face.v[0][k], hi = face.v[0][k];

			for (int c = 1; c < face.corners && c < 4; c++)
			{
				lo = face.v[c][k] < lo ? face.v[c][k] : lo;
				hi = face.v[c][k] > hi ? face.v[c][k] : hi;
			}

			if (hi + grow < (double)mins[k] || lo - grow > (double)maxs[k])
				return false;
		}

		return true;
	}

	/* The face among `faces` whose corners are those of `face` (each one
	 * within a unit of one of the other's), -1 for none. */
	inline int SameFace(const HDCoverFace* faces, int count,
		const HDCoverFace& face)
	{
		for (int f = 0; f < count; f++)
		{
			const HDCoverFace& other = faces[f];
			bool same = other.corners == face.corners;

			for (int a = 0; a < face.corners && same; a++)
			{
				bool found = false;

				for (int b = 0; b < other.corners && !found; b++)
					found = fabs(face.v[a][0] - other.v[b][0]) <= 1.0 &&
						fabs(face.v[a][1] - other.v[b][1]) <= 1.0 &&
						fabs(face.v[a][2] - other.v[b][2]) <= 1.0;

				same = found;
			}

			if (same)
				return f;
		}

		return -1;
	}

	/* Group g's triangles lying whole on a face, seen from either side:
	 * within HD_COVER_REACH of its plane and inside its outline at every
	 * corner and sample (HDCoverTriOnFaces). */
	inline void OnFace(Level& level, int g, const HDCoverFace& face,
		std::vector<size_t>& out)
	{
		const Art& art = level.Tris(g);
		HDCoverFace both = face;
		float lo[3], hi[3];

		both.doubleSided = 1;
		out.clear();

		for (int k = 0; k < 3; k++)
		{
			double a = face.v[0][k], b = face.v[0][k];

			for (int c = 1; c < face.corners && c < 4; c++)
			{
				a = face.v[c][k] < a ? face.v[c][k] : a;
				b = face.v[c][k] > b ? face.v[c][k] : b;
			}

			lo[k] = (float)(a - HD_COVER_REACH);
			hi[k] = (float)(b + HD_COVER_REACH);
		}

		for (size_t t = 0; t < art.first.size(); t++)
		{
			int v[3][3];
			bool in = true;

			for (int c = 0; c < 3; c++)
			{
				level.Corner(g, level.trg->indices[art.first[t] + (uint32_t)c],
					v[c]);
				in = in && Inside(v[c], lo, hi, 0.0f);
			}

			HDCoverTri tri;

			if (in && CoverTri(v, g, &tri) &&
				HDCoverTriOnFaces(tri, &both, 1, HD_COVER_REACH))
				out.push_back(t);
		}
	}

	/* Whether a triangle's image stands on the art already there: more
	 * than HD_ARTCOPY_STANDING_NUM / _DEN of its points - its corners, its
	 * centroid and its samples every HD_COVER_STEP / 2 units - lie on a
	 * triangle of the art facing its way within `reach`. A triangle only
	 * part of which lies on the art keeps that part's fight small rather
	 * than leave a hole where the art has nothing. */
	inline bool OnArt(const Standing& standing, const int w[3][3],
		const HDCoverTri& image, double reach)
	{
		HDCoverFace self;
		int total = 0, on = 0;

		self.corners = 3;
		self.doubleSided = 1;

		for (int c = 0; c < 4; c++)
			for (int i = 0; i < 3; i++)
				self.v[c][i] = (double)w[c < 3 ? c : 0][i];

		for (int c = 0; c < 4; c++)
		{
			double q[3];

			for (int i = 0; i < 3; i++)
				q[i] = c < 3 ? (double)w[c][i] :
					((double)w[0][i] + w[1][i] + w[2][i]) / 3.0;

			total++;
			on += standing.On(q, image.n, reach) ? 1 : 0;
		}

		HDCoverSamples(self, HD_COVER_STEP / 2.0, [&](const double* q,
			const double*)
		{
			total++;
			on += standing.On(q, image.n, reach) ? 1 : 0;
		});

		return (long long)on * HD_ARTCOPY_STANDING_DEN >
			(long long)total * HD_ARTCOPY_STANDING_NUM;
	}

	struct Taken
	{
		int inside = 0, crossing = 0, walls = 0, standing = 0;
		std::vector<uint32_t> first, run;
	};

	/* The classic faces near a box with their boxes grown by `reach`, so
	 * that a triangle is asked of the faces near it alone (Walls). */
	struct Walls
	{
		std::vector<HDCoverFace> faces;
		std::vector<float> boxes;
		mutable std::vector<HDCoverFace> nearFaces;
		double reach = 0.0;

		void Make(const HDCoverFace* all, int count, const float* lo,
			const float* hi, float grow, double wall)
		{
			reach = wall;
			FacesNear(all, count, lo, hi, grow, wall, faces);
			boxes.assign(faces.size() * 6, 0.0f);

			for (size_t f = 0; f < faces.size(); f++)
				for (int i = 0; i < 3; i++)
				{
					double a = faces[f].v[0][i], b = faces[f].v[0][i];

					for (int c = 1; c < faces[f].corners && c < 4; c++)
					{
						a = faces[f].v[c][i] < a ? faces[f].v[c][i] : a;
						b = faces[f].v[c][i] > b ? faces[f].v[c][i] : b;
					}

					boxes[f * 6 + (size_t)i] = (float)(a - wall - 1.0);
					boxes[f * 6 + 3 + (size_t)i] = (float)(b + wall + 1.0);
				}
		}

		/* Whether the triangle is the room's surface: it lies whole on the
		 * faces (HDCoverTriOnFaces), or every corner of it lies in the
		 * plane of one of them, facing the way it does, within `reach` of
		 * the plane and HD_ARTCOPY_WALL_GAP of the face - the classic
		 * faces of a wall are not always the wall's whole outline (a
		 * triangle where a flight meets it, a gap), and the art's wall
		 * goes on across them. */
		bool On(const HDCoverTri& tri) const
		{
			nearFaces.clear();

			for (size_t f = 0; f < faces.size(); f++)
			{
				bool meets = true;

				for (int i = 0; i < 3 && meets; i++)
					meets = tri.hi[i] >= boxes[f * 6 + (size_t)i] -
						HD_ARTCOPY_WALL_GAP && tri.lo[i] <=
						boxes[f * 6 + 3 + (size_t)i] + HD_ARTCOPY_WALL_GAP;

				if (meets)
					nearFaces.push_back(faces[f]);
			}

			if (nearFaces.empty())
				return false;

			if (HDCoverTriOnFaces(tri, &nearFaces[0], (int)nearFaces.size(),
				reach))
				return true;

			const double corner[3][3] =
			{
				{ tri.a[0], tri.a[1], tri.a[2] },
				{ (double)tri.a[0] + tri.e1[0], (double)tri.a[1] + tri.e1[1],
					(double)tri.a[2] + tri.e1[2] },
				{ (double)tri.a[0] + tri.e2[0], (double)tri.a[1] + tri.e2[1],
					(double)tri.a[2] + tri.e2[2] }
			};

			for (const HDCoverFace& face : nearFaces)
			{
				const double* c0 = face.v[0];
				const double e1[3] = { face.v[1][0] - c0[0],
					face.v[1][1] - c0[1], face.v[1][2] - c0[2] };
				const double e2[3] = { face.v[2][0] - c0[0],
					face.v[2][1] - c0[1], face.v[2][2] - c0[2] };
				double m[3] = { e1[1] * e2[2] - e1[2] * e2[1],
					e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
				const double length = sqrt(m[0] * m[0] + m[1] * m[1] +
					m[2] * m[2]);

				if (length <= 0.0)
					continue;

				for (int i = 0; i < 3; i++)
					m[i] /= length;

				if (tri.n[0] * m[0] + tri.n[1] * m[1] + tri.n[2] * m[2] <
					HD_ARTCOPY_WALL_DOT)
					continue;

				bool in = true;

				for (int c = 0; c < 3 && in; c++)
				{
					const double d = (corner[c][0] - c0[0]) * m[0] +
						(corner[c][1] - c0[1]) * m[1] + (corner[c][2] - c0[2]) * m[2];

					in = d >= -reach && d <= reach;

					for (int i = 0; i < 3 && in; i++)
					{
						double lo = face.v[0][i], hi = face.v[0][i];

						for (int k = 1; k < face.corners && k < 4; k++)
						{
							lo = face.v[k][i] < lo ? face.v[k][i] : lo;
							hi = face.v[k][i] > hi ? face.v[k][i] : hi;
						}

						in = corner[c][i] >= lo - HD_ARTCOPY_WALL_GAP - reach &&
							corner[c][i] <= hi + HD_ARTCOPY_WALL_GAP + reach;
					}
				}

				if (in)
					return true;
			}

			return false;
		}
	};

	/* What the donors of one motion give their targets: `donors` the
	 * statics whose art is taken, all moved by the one motion `turn`,
	 * `from` -> `to` (a pair of places it moves one onto the other); the
	 * triangles go to `out`, one Taken per donor. Taken are the copy
	 * group's triangles every corner of which lies in one of the donors'
	 * boxes grown by the margin - in the boxes together, so that a
	 * handrail that runs on through the boxes of the railings of two
	 * flights and the post between them, all moved alike, comes whole -,
	 * less those lying whole on a classic face (a wall or a floor) and
	 * those whose image lies on the group's art already standing at the
	 * target (`standing`). Each goes to the first donor whose grown box
	 * holds its centroid, or else one of its corners. */
	inline void Take(Level& level, const HDArtCopyStatic* statics,
		const std::vector<int>& donors, int g, int turn, const int from[3],
		const int to[3], const HDCoverFace* faces, int faceCount,
		const HDArtCopyParams& p, const Standing& standing,
		std::vector<Taken>& out)
	{
		const Art& art = level.Tris(g);
		const size_t n = donors.size();
		float lo[3], hi[3], tlo[3], thi[3];

		out.assign(n, Taken());

		if (!n)
			return;

		for (size_t k = 0; k < n; k++)
		{
			const HDArtCopyStatic& s = statics[donors[k]];
			float a[3], b[3];
			const int p0[3] = { (int)floorf(s.mins[0]), (int)floorf(s.mins[1]),
				(int)floorf(s.mins[2]) };
			const int p1[3] = { (int)ceilf(s.maxs[0]), (int)ceilf(s.maxs[1]),
				(int)ceilf(s.maxs[2]) };
			int q0[3], q1[3];

			HDArtCopyMove(turn, from, to, p0, q0);
			HDArtCopyMove(turn, from, to, p1, q1);

			for (int i = 0; i < 3; i++)
			{
				a[i] = (float)(q0[i] < q1[i] ? q0[i] : q1[i]);
				b[i] = (float)(q0[i] < q1[i] ? q1[i] : q0[i]);
				lo[i] = !k || s.mins[i] < lo[i] ? s.mins[i] : lo[i];
				hi[i] = !k || s.maxs[i] > hi[i] ? s.maxs[i] : hi[i];
				tlo[i] = !k || a[i] < tlo[i] ? a[i] : tlo[i];
				thi[i] = !k || b[i] > thi[i] ? b[i] : thi[i];
			}
		}

		/* The classic faces at the donors' places and at the targets':
		 * a triangle lying whole on a wall or a floor at either is that
		 * room's surface, never part of the object - and at the target a
		 * classic face the art does not cover is drawn, so a copy lying
		 * on it would be a second surface. */
		Walls donorWalls, targetWalls;

		donorWalls.Make(faces, faceCount, lo, hi, p.margin, p.wall);
		targetWalls.Make(faces, faceCount, tlo, thi, p.margin, p.wall);

		// The donor box holding a point first, -1 for none.
		const auto boxOf = [&](const int q[3]) -> int
		{
			for (size_t k = 0; k < n; k++)
				if (Inside(q, statics[donors[k]].mins, statics[donors[k]].maxs,
					p.margin))
					return (int)k;

			return -1;
		};

		for (size_t t = 0; t < art.first.size(); t++)
		{
			const uint32_t first = art.first[t];
			int v[3][3], in = 0, firstBox = -1;

			for (int c = 0; c < 3; c++)
			{
				level.Corner(g, level.trg->indices[first + (uint32_t)c], v[c]);

				const int k = boxOf(v[c]);

				in += k >= 0 ? 1 : 0;
				firstBox = firstBox < 0 ? k : firstBox;
			}

			if (!in)
				continue;

			const int centre[3] = { (v[0][0] + v[1][0] + v[2][0]) / 3,
				(v[0][1] + v[1][1] + v[2][1]) / 3,
				(v[0][2] + v[1][2] + v[2][2]) / 3 };
			const int box = boxOf(centre);
			Taken& into = out[(size_t)(box >= 0 ? box : firstBox)];

			if (in < 3)
			{
				into.crossing++;
				continue;
			}

			into.inside++;

			HDCoverTri tri;

			if (!CoverTri(v, g, &tri))
				continue;	// no area: nothing to draw

			// The image: its corners, its normal and its centroid.
			int w[3][3];

			for (int c = 0; c < 3; c++)
				HDArtCopyMove(turn, from, to, v[c], w[c]);

			HDCoverTri image;

			CoverTri(w, g, &image);

			if (donorWalls.On(tri) || targetWalls.On(image))
			{
				into.walls++;
				continue;
			}

			if (OnArt(standing, w, image, p.stands))
			{
				into.standing++;
				continue;
			}

			into.first.push_back(first);
			into.run.push_back(art.run[t]);
		}
	}

	/* The classic faces beside a copy: a face of the target's room that
	 * touches its box, whose image by the copy's motion taken back is a
	 * face of the level with the donor group's art lying on it - the landing
	 * at the end of a flight, at the donor's. That art comes with the copy
	 * (`first` and `run`, of group `dg`), less what stands at the target
	 * already in group `tg` and what the copy's motion took (`seen`, sorted,
	 * which grows by it), and the face then falls to the art like any face
	 * the art covers (hd_cover.h). `beside` counts the faces. */
	inline void FacesBeside(Level& level, int dg, int tg, int turn,
		const int fromAt[3], const int toAt[3], int room, const float* mins,
		const float* maxs, const HDCoverFace* faces, const int* faceRooms,
		int faceCount, const HDArtCopyParams& p,
		std::vector<unsigned char>& faceDone, std::vector<uint32_t>& seen,
		std::vector<uint32_t>& first, std::vector<uint32_t>& run,
		int* beside)
	{
		for (int f = 0; faceRooms && f < faceCount; f++)
		{
			if (faceRooms[f] != room || faceDone[(size_t)f] ||
				!FaceMeets(faces[f], mins, maxs, 1.0))
				continue;

			HDCoverFace back = faces[f];

			for (int c = 0; c < back.corners && c < 4; c++)
			{
				const int q[3] = { (int)floor(faces[f].v[c][0] + 0.5),
					(int)floor(faces[f].v[c][1] + 0.5),
					(int)floor(faces[f].v[c][2] + 0.5) };
				int w[3];

				HDArtCopyMove((4 - turn) & 3, toAt, fromAt, q, w);

				for (int k = 0; k < 3; k++)
					back.v[c][k] = (double)w[k];
			}

			const int donorFace = SameFace(faces, faceCount, back);
			std::vector<size_t> there;

			if (donorFace < 0)
				continue;

			OnFace(level, dg, faces[donorFace], there);

			if (there.empty())
				continue;

			faceDone[(size_t)f] = 1;

			const Art& art = level.Tris(dg);
			const Art& own = level.Tris(tg);
			Standing nearArt;
			float lo[3], hi[3];

			for (int k = 0; k < 3; k++)
			{
				double a = faces[f].v[0][k], b = faces[f].v[0][k];

				for (int c = 1; c < faces[f].corners && c < 4; c++)
				{
					a = faces[f].v[c][k] < a ? faces[f].v[c][k] : a;
					b = faces[f].v[c][k] > b ? faces[f].v[c][k] : b;
				}

				lo[k] = (float)(a - HD_COVER_REACH - p.stands);
				hi[k] = (float)(b + HD_COVER_REACH + p.stands);
			}

			for (size_t t2 = 0; t2 < own.first.size(); t2++)
			{
				int v[3][3];
				bool meets = true;

				for (int c = 0; c < 3; c++)
					level.Corner(tg, level.trg->indices[own.first[t2] +
						(uint32_t)c], v[c]);

				for (int k = 0; k < 3 && meets; k++)
				{
					const int a = (std::min)((std::min)(v[0][k], v[1][k]),
						v[2][k]);
					const int b = (std::max)((std::max)(v[0][k], v[1][k]),
						v[2][k]);

					meets = (float)b >= lo[k] && (float)a <= hi[k];
				}

				if (meets)
					nearArt.Add(v, p.stands);
			}

			nearArt.Done();
			(*beside)++;

			for (size_t t2 : there)
			{
				const uint32_t at = art.first[t2];

				if (std::binary_search(seen.begin(), seen.end(), at))
					continue;

				int v[3][3], w[3][3];

				for (int c = 0; c < 3; c++)
				{
					level.Corner(dg, level.trg->indices[at + (uint32_t)c], v[c]);
					HDArtCopyMove(turn, fromAt, toAt, v[c], w[c]);
				}

				HDCoverTri image;

				if (!CoverTri(w, dg, &image) || OnArt(nearArt, w, image,
					p.stands))
					continue;

				first.push_back(at);
				run.push_back(art.run[t2]);
				seen.insert(std::lower_bound(seen.begin(), seen.end(), at), at);
			}
		}
	}

	/* Group g's triangles every corner of which lies in a box grown by
	 * `grow` (world units), and how many of their corners carry light: a
	 * colour byte above zero in either triple. */
	inline int Within(Level& level, int g, const float* mins,
		const float* maxs, float grow, int* lit)
	{
		*lit = 0;

		if (!level.BoxMeets(g, mins, maxs, grow))
			return 0;

		const Art& art = level.Tris(g);
		int inside = 0;

		for (size_t k = 0; k < art.first.size(); k++)
		{
			bool in = true;

			for (int c = 0; c < 3 && in; c++)
			{
				int q[3];

				level.Corner(g, level.trg->indices[art.first[k] + (uint32_t)c],
					q);
				in = Inside(q, mins, maxs, grow);
			}

			if (!in)
				continue;

			inside++;

			for (int c = 0; c < 3; c++)
			{
				const trf::remaster::TrgVertex& v = level.trg->vertices[
					level.trg->indices[art.first[k] + (uint32_t)c]];

				*lit += (v.colour1[0] | v.colour1[1] | v.colour1[2] |
					v.colour2[0] | v.colour2[1] | v.colour2[2]) != 0 ? 1 : 0;
			}
		}

		return inside;
	}

	/* The item path of the rule (HDArtCopyMake, its last step; the rule's
	 * description at the top of this file). `plan.drawn` names the group
	 * of every item's room already. The items stand in as statics for Take
	 * and Motion - their place, turn and box, no vertices -, and a copy's
	 * triangles go into the runs of the target's group that RunIn names. */
	inline void MakeItems(Level& level, HDArtCopyPlan& plan,
		const std::vector<unsigned char>& paired, const HDArtCopyItem* items,
		int itemCount, const HDCoverFace* faces, const int* faceRooms,
		int faceCount, const HDArtCopyParams& p,
		std::vector<unsigned char>& faceDone)
	{
		const int groups = level.groups;
		const size_t n = (size_t)(items && itemCount > 0 ? itemCount : 0);

		plan.itemGroup.assign(n, -1);
		plan.itemInside.assign(n, 0);
		plan.itemRole.assign(n, HD_ARTCOPY_ITEM_NONE);

		std::vector<HDArtCopyStatic> as(n);
		// An added item the rule looks for a donor for, in the level's order.
		std::vector<unsigned char> wants(n, 0);

		for (size_t i = 0; i < n; i++)
		{
			HDArtCopyStatic& one = as[i];

			memset(&one, 0, sizeof(one));
			one.room = items[i].room;
			one.mesh = items[i].item;
			one.slot = items[i].slot;
			one.x = items[i].x;
			one.y = items[i].y;
			one.z = items[i].z;
			one.rot = items[i].rot & 0xFFFF;

			for (int k = 0; k < 3; k++)
			{
				one.mins[k] = items[i].mins[k];
				one.maxs[k] = items[i].maxs[k];
			}
		}

		/* The slots an added item asks: only their items' art is counted
		 * (every item's, for a tool that asks every donor). */
		std::vector<int> asked;

		for (size_t i = 0; i < n; i++)
			if (items[i].added || p.offered)
				asked.push_back(items[i].slot);

		std::sort(asked.begin(), asked.end());

		// Each item's group and part.
		for (size_t i = 0; i < n; i++)
		{
			const int r = items[i].room;
			const bool added = items[i].added != 0;
			const int g = r >= 0 && r < groups && !paired[(size_t)r] ?
				plan.drawn[(size_t)r] : -1;

			plan.items += added ? 1 : 0;

			if (g < 0 || paired[(size_t)g])
			{
				plan.itemRole[i] = (unsigned char)(added ?
					HD_ARTCOPY_ITEM_NO_GROUP : HD_ARTCOPY_ITEM_NONE);
				continue;
			}

			plan.itemGroup[i] = g;

			/* An added item in a room drawn by another group's art is drawn
			 * classic there (begin_decision bakes an item in a room drawn
			 * HD only): a copy would stand beside it. */
			if (added && g != r)
			{
				plan.itemRole[i] = HD_ARTCOPY_ITEM_NO_GROUP;
				continue;
			}

			if (as[i].rot % HD_ARTCOPY_QUARTER)
			{
				plan.itemRole[i] = HD_ARTCOPY_ITEM_SKEWED;
				continue;
			}

			int lit = 0;

			if (std::binary_search(asked.begin(), asked.end(), as[i].slot))
				plan.itemInside[i] = Within(level, g, as[i].mins, as[i].maxs,
					p.margin, &lit);

			if (added)
				wants[i] = 1;
			else if ((long long)lit * HD_ARTCOPY_LIT_DEN <
				(long long)plan.itemInside[i] * 3 * HD_ARTCOPY_LIT_NUM)
				plan.itemRole[i] = HD_ARTCOPY_ITEM_UNLIT;
			else
			{
				plan.itemRole[i] = HD_ARTCOPY_ITEM_DONOR;
				plan.itemDonors++;
			}
		}

		// Each added item's donor: its slot's, no turn first, then the
		// nearest, then the first in the level's order.
		std::vector<int> donorOf(n, -1), turnOf(n, 0);

		for (size_t t = 0; t < n; t++)
		{
			if (!wants[t])
				continue;

			int best = -1, bestTurn = 0;
			long long bestDistance = 0;

			for (size_t s = 0; s < n; s++)
			{
				int turn = 0;

				if (plan.itemRole[s] != HD_ARTCOPY_ITEM_DONOR ||
					as[s].slot != as[t].slot || !Motion(as[s], as[t], &turn))
					continue;

				const long long distance = llabs((long long)as[t].x -
					as[s].x) + llabs((long long)as[t].y - as[s].y) +
					llabs((long long)as[t].z - as[s].z);
				const int rank = turn ? 1 : 0;
				const int bestRank = bestTurn ? 1 : 0;

				if (best < 0 || rank < bestRank ||
					(rank == bestRank && distance < bestDistance))
				{
					best = (int)s;
					bestTurn = turn;
					bestDistance = distance;
				}
			}

			donorOf[t] = best;
			turnOf[t] = bestTurn;

			if (best < 0)
				plan.itemRole[t] = HD_ARTCOPY_ITEM_NO_DONOR;
		}

		/* The art models the slot alike at its donors, the level over: the
		 * triangles wholly inside each donor's grown box, the most at most
		 * HD_ARTCOPY_ALIKE times the fewest. Asked once per slot. */
		std::vector<int> alikeAsked;
		std::vector<unsigned char> alikeSaid;

		for (size_t t = 0; t < n; t++)
		{
			if (donorOf[t] < 0)
				continue;

			const int slot = as[t].slot;
			size_t at = 0;

			while (at < alikeAsked.size() && alikeAsked[at] != slot)
				at++;

			if (at == alikeAsked.size())
			{
				HDArtCopyAlike weighed;
				int fewest = -1, most = 0, counted = 0;

				for (size_t s = 0; s < n; s++)
				{
					if (plan.itemRole[s] != HD_ARTCOPY_ITEM_DONOR ||
						as[s].slot != slot)
						continue;

					const int inside = plan.itemInside[s];

					fewest = fewest < 0 || inside < fewest ? inside : fewest;
					most = inside > most ? inside : most;
					counted++;
				}

				weighed.group = -1;
				weighed.slot = slot;
				weighed.donors = counted;
				weighed.fewest = fewest;
				weighed.most = most;
				weighed.alike = fewest > 0 && (long long)most *
					HD_ARTCOPY_ALIKE_DEN <= (long long)fewest *
					HD_ARTCOPY_ALIKE_NUM ? 1 : 0;
				plan.itemWeighed.push_back(weighed);
				alikeAsked.push_back(slot);
				alikeSaid.push_back((unsigned char)weighed.alike);
			}

			if (!alikeSaid[at])
			{
				donorOf[t] = -1;
				plan.itemRole[t] = HD_ARTCOPY_ITEM_UNLIKE;
			}
		}

		/* Per target group, its own art standing round its targets'
		 * places: the copy's image must not lie on it (OnArt). */
		const float grow = p.margin + (float)p.stands;
		std::vector<int> standingGroup;
		std::vector<Standing> standing;

		for (size_t t = 0; t < n; t++)
		{
			if (!(donorOf[t] >= 0 || (p.offered && wants[t])))
				continue;

			const int g = plan.itemGroup[t];

			if (std::find(standingGroup.begin(), standingGroup.end(), g) !=
				standingGroup.end())
				continue;

			standingGroup.push_back(g);
			standing.push_back(Standing());

			Standing& one = standing.back();
			const Art& art = level.Tris(g);

			for (size_t k = 0; k < art.first.size(); k++)
			{
				int v[3][3], lo[3], hi[3];

				for (int c = 0; c < 3; c++)
					level.Corner(g, level.trg->indices[art.first[k] +
						(uint32_t)c], v[c]);

				for (int a = 0; a < 3; a++)
				{
					lo[a] = (std::min)((std::min)(v[0][a], v[1][a]), v[2][a]);
					hi[a] = (std::max)((std::max)(v[0][a], v[1][a]), v[2][a]);
				}

				for (size_t u = 0; u < n; u++)
				{
					if (!wants[u] || plan.itemGroup[u] != g)
						continue;

					bool in = true;

					for (int a = 0; a < 3 && in; a++)
						in = (float)hi[a] >= as[u].mins[a] - grow &&
							(float)lo[a] <= as[u].maxs[a] + grow;

					if (in)
					{
						one.Add(v, p.stands);
						break;
					}
				}
			}

			one.Done();
		}

		const auto standingOf = [&](int g) -> const Standing&
		{
			const size_t at = (size_t)(std::find(standingGroup.begin(),
				standingGroup.end(), g) - standingGroup.begin());

			return standing[at];
		};

		/* The copies by motion, as the statics' (HDArtCopyMake): the
		 * targets of one target group whose donors in one donor group one
		 * rigid motion moves onto them take that art together. */
		struct Move
		{
			int dg, tg, turn;
			int from[3], to[3];
			uint64_t key;
			std::vector<int> targets, donors;
			std::vector<Taken> taken;
			std::vector<uint32_t> seen;
		};

		std::vector<Move> moves;
		std::vector<int> moveOf(n, -1), slotOf(n, -1);

		for (size_t t = 0; t < n; t++)
		{
			const int s = donorOf[t];

			if (s < 0)
				continue;

			const int from[3] = { as[(size_t)s].x, as[(size_t)s].y,
				as[(size_t)s].z };
			const int to[3] = { as[t].x, as[t].y, as[t].z };
			const int zero[3] = { 0, 0, 0 };
			int origin[3];

			HDArtCopyMove(turnOf[t], from, to, zero, origin);

			const uint64_t key = HDArtCopyMotionKey(turnOf[t], origin[0],
				origin[1], origin[2]);
			const int dg = plan.itemGroup[(size_t)s];
			const int tg = plan.itemGroup[t];
			size_t m = 0;

			while (m < moves.size() && (moves[m].dg != dg ||
				moves[m].tg != tg || moves[m].key != key))
				m++;

			if (m == moves.size())
			{
				Move one;

				one.dg = dg;
				one.tg = tg;
				one.turn = turnOf[t];
				one.key = key;

				for (int k = 0; k < 3; k++)
				{
					one.from[k] = from[k];
					one.to[k] = to[k];
				}

				moves.push_back(one);
			}

			moveOf[t] = (int)m;
			slotOf[t] = (int)moves[m].targets.size();
			moves[m].targets.push_back((int)t);
			moves[m].donors.push_back(s);
		}

		for (Move& m : moves)
		{
			Take(level, &as[0], m.donors, m.dg, m.turn, m.from, m.to, faces,
				faceCount, p, standingOf(m.tg), m.taken);

			for (const Taken& one : m.taken)
				m.seen.insert(m.seen.end(), one.first.begin(), one.first.end());

			std::sort(m.seen.begin(), m.seen.end());
		}

		// The copies, in the level's order; every donor offered, for a tool.
		for (size_t t = 0; t < n; t++)
		{
			if (!wants[t])
				continue;

			const HDArtCopyStatic& goal = as[t];
			const int tg = plan.itemGroup[t];
			const int best = donorOf[t];
			const int toAt[3] = { goal.x, goal.y, goal.z };

			for (size_t s = 0; p.offered && s < n; s++)
			{
				int turn = 0;

				if (plan.itemRole[s] != HD_ARTCOPY_ITEM_DONOR ||
					as[s].slot != goal.slot || !Motion(as[s], goal, &turn))
					continue;

				const std::vector<int> alone(1, (int)s);
				const int fromAt[3] = { as[s].x, as[s].y, as[s].z };
				std::vector<Taken> taken;
				HDArtCopyOffer offer;

				Take(level, &as[0], alone, plan.itemGroup[s], turn, fromAt,
					toAt, faces, faceCount, p, standingOf(tg), taken);
				offer.target = (int)t;
				offer.donor = (int)s;
				offer.group = plan.itemGroup[s];
				offer.dx = goal.x - as[s].x;
				offer.dy = goal.y - as[s].y;
				offer.dz = goal.z - as[s].z;
				offer.turn = turn;
				offer.inside = taken[0].inside;
				offer.crossing = taken[0].crossing;
				offer.walls = taken[0].walls;
				offer.standing = taken[0].standing;
				offer.chosen = (int)s == best ? 1 : 0;
				plan.itemOffered.push_back(offer);
			}

			if (best < 0)
				continue;

			const HDArtCopyStatic& from = as[(size_t)best];
			const int fromAt[3] = { from.x, from.y, from.z };
			Move& move = moves[(size_t)moveOf[t]];
			const Taken& taken = move.taken[(size_t)slotOf[t]];
			std::vector<uint32_t> first(taken.first), run(taken.run);
			HDArtCopy copy;
			int beside = 0;

			memset(&copy, 0, sizeof(copy));
			copy.target = (int)t;
			copy.donor = best;
			copy.group = tg;
			copy.fromGroup = move.dg;
			copy.dx = goal.x - from.x;
			copy.dy = goal.y - from.y;
			copy.dz = goal.z - from.z;
			copy.turn = turnOf[t];
			copy.inside = taken.inside;
			copy.crossing = taken.crossing;
			copy.walls = taken.walls;
			copy.standing = taken.standing;

			FacesBeside(level, move.dg, tg, copy.turn, fromAt, toAt, goal.room,
				goal.mins, goal.maxs, faces, faceRooms, faceCount, p, faceDone,
				move.seen, first, run, &beside);
			copy.faces = beside;
			copy.faceTris = (int)(first.size() - taken.first.size());

			// Each triangle into its part's twin in the target's group.
			bool parts = true;

			for (size_t k = 0; k < run.size() && parts; k++)
			{
				const int into = level.RunIn(move.dg, run[k], tg);

				parts = into >= 0;
				run[k] = (uint32_t)into;
			}

			if (!parts)
			{
				plan.itemRole[t] = HD_ARTCOPY_ITEM_NO_PART;
				continue;
			}

			if (first.empty())
			{
				plan.itemRole[t] = HD_ARTCOPY_ITEM_EMPTY;
				continue;
			}

			copy.tris = (int)first.size();
			copy.firstTri = (int)plan.tris.size();
			plan.tris.insert(plan.tris.end(), first.begin(), first.end());
			plan.runs.insert(plan.runs.end(), run.begin(), run.end());
			plan.itemRole[t] = HD_ARTCOPY_ITEM_COPIED;
			plan.itemCopies.push_back(copy);
			plan.itemTris += copy.tris;
		}

		/* Per copy, the light: the means of the copied corners' two colours
		 * (the donor's), and of the target group's own corners round the
		 * target's place. */
		for (HDArtCopy& copy : plan.itemCopies)
		{
			const HDArtCopyStatic& goal = as[(size_t)copy.target];
			double light[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
			double sum[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
			int corners = 0, around = 0;

			for (int k = 0; k < copy.tris; k++)
				for (int c = 0; c < 3; c++)
				{
					const trf::remaster::TrgVertex& v = level.trg->vertices[
						level.trg->indices[plan.tris[(size_t)(copy.firstTri +
						k)] + (uint32_t)c]];

					for (int i = 0; i < 3; i++)
					{
						light[0][i] += v.colour1[i];
						light[1][i] += v.colour2[i];
					}

					corners++;
				}

			const Art& art = level.Tris(copy.group);

			for (size_t k = 0; k < art.first.size(); k++)
				for (int c = 0; c < 3; c++)
				{
					const uint32_t index = level.trg->indices[art.first[k] +
						(uint32_t)c];
					int q[3];

					level.Corner(copy.group, index, q);

					if (!Inside(q, goal.mins, goal.maxs, HD_ARTCOPY_AROUND))
						continue;

					const trf::remaster::TrgVertex& v =
						level.trg->vertices[index];

					for (int i = 0; i < 3; i++)
					{
						sum[0][i] += v.colour1[i];
						sum[1][i] += v.colour2[i];
					}

					around++;
				}

			for (int a = 0; a < 2; a++)
				for (int i = 0; i < 3; i++)
				{
					copy.light[a][i] = corners ? (float)(light[a][i] /
						corners) : 0.0f;
					copy.around[a][i] = around ? (float)(sum[a][i] / around) :
						0.0f;
				}

			copy.aroundCorners = around;
		}
	}
}

/* The rule's plan over one level: which statics get a copy from which
 * donor, and the triangles each takes. `faces` are the level's classic
 * faces in world units (the stand faces, hd_cover.h) and `faceRooms` the
 * room of each, `verts` the statics' vertices, `threads` the pool's
 * (HDPoolThreads); `items` the items of the slots the art carries, asked
 * only when one of them is the run's own (or the tools ask every donor).
 * Nothing is changed; HDArtCopyApply puts the plan in. */
inline void HDArtCopyMake(const trf::remaster::Trg& trg, int roomGroups,
	const int* shift, int shifts, const HDArtCopyRoom* rooms, int roomCount,
	const HDCoverFace* faces, const int* faceRooms, int faceCount,
	const HDArtCopyStatic* statics, int staticCount, const float* verts,
	int vertCount, const HDArtCopyParams& p, int threads, HDArtCopyPlan* out,
	const HDArtCopyItem* items = 0, int itemCount = 0)
{
	HDArtCopyPlan& plan = *out;

	plan = HDArtCopyPlan();

	const int groups = (std::min)((std::min)(roomGroups,
		(int)trg.groups.size()), roomCount);
	bool askItems = false;

	if (!statics || !verts)
		staticCount = 0;

	if (!items)
		itemCount = 0;

	for (int i = 0; i < itemCount && !askItems; i++)
		askItems = items[i].added != 0 || p.offered != 0;

	if (groups <= 0 || !rooms || (staticCount <= 0 && !askItems))
		return;

	hdartcopy::Level level;

	level.trg = &trg;
	level.groups = groups;
	level.shift = shift;
	level.shifts = shifts;
	level.rooms = rooms;
	level.roomCount = roomCount;
	level.threads = threads > 0 ? threads : 1;

	/* A room of a flipmap pair either way round: the main room names its
	 * twin (flipped_room), and the twin is the room the main room names. */
	std::vector<unsigned char> paired((size_t)groups, 0);

	for (int r = 0; r < groups; r++)
		if (rooms[r].twin >= 0)
		{
			paired[(size_t)r] = 1;

			if (rooms[r].twin < groups)
				paired[(size_t)rooms[r].twin] = 1;
		}

	// The statics asked and the cells they ask: their points and their
	// boxes' cells.
	std::vector<unsigned char> valid((size_t)staticCount, 0);

	for (int s = 0; s < staticCount; s++)
	{
		const HDArtCopyStatic& one = statics[s];

		if (one.vertCount <= 0 || one.firstVert < 0 ||
			one.firstVert + one.vertCount > vertCount ||
			one.room < 0 || one.room >= groups || paired[(size_t)one.room])
			continue;

		valid[(size_t)s] = 1;

		for (int v = 0; v < one.vertCount; v++)
		{
			uint64_t keys[8];
			const int n = hdartcopy::PointCells(
				&verts[(size_t)(one.firstVert + v) * 3], keys);

			for (int k = 0; k < n; k++)
				level.Ask(keys[k]);
		}

		hdartcopy::BoxCells(one.mins, one.maxs, [&](uint64_t key)
		{
			level.Ask(key);
		});
	}

	level.Walk(p.ownSlack);

	/* The group that draws each room of a static, before the build: the
	 * room's own where one of its triangles has its centroid in the room's
	 * box grown by the slack, otherwise the group with the most corners in
	 * the box, no group of a flipmap pair; -1 for none. The same for the
	 * rooms of the items asked. */
	plan.drawn.assign((size_t)groups, -1);

	{
		std::vector<unsigned char> seenRoom((size_t)groups, 0);
		std::vector<int> classic;
		std::vector<float> boxes;

		const auto see = [&](int r)
		{
			seenRoom[(size_t)r] = 1;

			if (level.art[(size_t)r].own)
				plan.drawn[(size_t)r] = r;
			else if (rooms[r].box)
			{
				const HDArtCopyRoom& room = rooms[r];

				classic.push_back(r);
				boxes.push_back(room.mins[0] + (float)room.originX);
				boxes.push_back(room.mins[1]);
				boxes.push_back(room.mins[2] + (float)room.originZ);
				boxes.push_back(room.maxs[0] + (float)room.originX);
				boxes.push_back(room.maxs[1]);
				boxes.push_back(room.maxs[2] + (float)room.originZ);
			}
		};

		for (int s = 0; s < staticCount; s++)
		{
			const int r = statics[s].room;

			if (!valid[(size_t)s] || seenRoom[(size_t)r])
				continue;

			see(r);
		}

		for (int i = 0; askItems && i < itemCount; i++)
		{
			const int r = items[i].room;

			if (r < 0 || r >= groups || paired[(size_t)r] ||
				seenRoom[(size_t)r])
				continue;

			see(r);
		}

		if (!classic.empty())
		{
			level.Count(boxes, paired);

			for (size_t k = 0; k < classic.size(); k++)
			{
				int best = -1, bestCount = 0;

				for (int g = 0; g < groups; g++)
				{
					const int count = g == classic[k] ? 0 :
						level.art[(size_t)g].counts[k];

					if (count > bestCount)
					{
						best = g;
						bestCount = count;
					}
				}

				plan.drawn[(size_t)classic[k]] = best;
			}
		}
	}

	// Each static's group.
	plan.group.assign((size_t)staticCount, -1);
	plan.held.assign((size_t)staticCount, 0);
	plan.role.assign((size_t)staticCount, 0);

	for (int s = 0; s < staticCount; s++)
	{
		if (!valid[(size_t)s])
			continue;

		const int g = plan.drawn[(size_t)statics[s].room];

		if (g < 0 || paired[(size_t)g])
			continue;

		plan.statics++;
		plan.group[(size_t)s] = g;
	}

	// Each static's points on the art and its part.
	for (int s = 0; s < staticCount; s++)
	{
		const int g = plan.group[(size_t)s];

		if (g < 0)
			continue;

		const HDArtCopyStatic& one = statics[s];
		const float* points = &verts[(size_t)one.firstVert * 3];
		const int held = hdartcopy::PointsOn(level, g, points, one.vertCount);

		plan.held[(size_t)s] = held;

		if (held == one.vertCount)
		{
			plan.role[(size_t)s] = 1;
			plan.donors++;
			continue;
		}

		if (HDStaticOtherHolds(held, one.vertCount))
			continue;

		if (g == one.room)
		{
			int boxCells = 0;
			const int own = hdartcopy::BoxCellsOn(level, g, one.mins, one.maxs,
				&boxCells);

			if (HDStaticCellsHold(own, boxCells))
				continue;
		}

		// No other group's art holds it either (a flat static asks none).
		bool elsewhere = false;

		if (!HDStaticFlat(one.mins, one.maxs))
			for (int h = 0; h < groups && !elsewhere; h++)
				elsewhere = h != g && level.BoxMeets(h, one.mins, one.maxs,
					256.0f) && HDStaticOtherHolds(hdartcopy::PointsOn(level, h,
					points, one.vertCount), one.vertCount);

		if (elsewhere)
		{
			plan.role[(size_t)s] = 3;
			plan.heldElsewhere++;
			continue;
		}

		plan.role[(size_t)s] = 2;
		plan.targets++;
	}

	// Each target's donor, in the level's order.
	std::vector<int> donorOf((size_t)staticCount, -1);
	std::vector<int> turnOf((size_t)staticCount, 0);

	for (int t = 0; t < staticCount; t++)
	{
		if (plan.role[(size_t)t] != 2)
			continue;

		const HDArtCopyStatic& goal = statics[t];
		const int g = plan.group[(size_t)t];
		int best = -1, bestTurn = 0;
		long long bestDistance = 0;

		for (int s = 0; s < staticCount; s++)
		{
			int turn = 0;

			if (plan.role[(size_t)s] != 1 || plan.group[(size_t)s] != g ||
				statics[s].slot != goal.slot ||
				!hdartcopy::Motion(statics[s], goal, &turn))
				continue;

			const long long distance = llabs((long long)goal.x -
				statics[s].x) + llabs((long long)goal.y - statics[s].y) +
				llabs((long long)goal.z - statics[s].z);
			const int rank = turn ? 1 : 0;
			const int bestRank = bestTurn ? 1 : 0;

			if (best < 0 || rank < bestRank ||
				(rank == bestRank && distance < bestDistance))
			{
				best = s;
				bestTurn = turn;
				bestDistance = distance;
			}
		}

		donorOf[(size_t)t] = best;
		turnOf[(size_t)t] = bestTurn;

		if (best < 0)
			plan.noDonor++;
	}

	/* The art models a slot alike at every placement that holds it, or the
	 * missing one is no omission: the triangles wholly inside each donor's
	 * grown box, the fewest and the most, may differ by HD_ARTCOPY_ALIKE.
	 * Asked once per group and slot. */
	std::vector<std::pair<int, int> > alikeAsked;
	std::vector<unsigned char> alikeSaid;

	for (int t = 0; t < staticCount; t++)
	{
		if (donorOf[(size_t)t] < 0)
			continue;

		const int g = plan.group[(size_t)t];
		const std::pair<int, int> key(g, statics[t].slot);
		size_t at = 0;

		while (at < alikeAsked.size() && alikeAsked[at] != key)
			at++;

		if (at == alikeAsked.size())
		{
			const hdartcopy::Art& art = level.Tris(g);
			int fewest = -1, most = 0, counted = 0;

			for (int s = 0; s < staticCount; s++)
			{
				if (plan.role[(size_t)s] != 1 || plan.group[(size_t)s] != g ||
					statics[s].slot != key.second)
					continue;

				int inside = 0;

				for (size_t k = 0; k < art.first.size(); k++)
				{
					bool in = true;

					for (int c = 0; c < 3 && in; c++)
					{
						int q[3];

						level.Corner(g, trg.indices[art.first[k] + (uint32_t)c],
							q);
						in = hdartcopy::Inside(q, statics[s].mins,
							statics[s].maxs, p.margin);
					}

					inside += in ? 1 : 0;
				}

				fewest = fewest < 0 || inside < fewest ? inside : fewest;
				most = inside > most ? inside : most;
				counted++;
			}

			HDArtCopyAlike weighed;

			weighed.group = g;
			weighed.slot = key.second;
			weighed.donors = counted;
			weighed.fewest = fewest;
			weighed.most = most;
			weighed.alike = fewest > 0 && (long long)most *
				HD_ARTCOPY_ALIKE_DEN <= (long long)fewest *
				HD_ARTCOPY_ALIKE_NUM ? 1 : 0;
			plan.weighed.push_back(weighed);
			alikeAsked.push_back(key);
			alikeSaid.push_back((unsigned char)weighed.alike);
		}

		if (!alikeSaid[at])
		{
			donorOf[(size_t)t] = -1;
			plan.role[(size_t)t] = 4;
			plan.unlike++;
		}
	}

	/* Per group with a copy, its own art standing round its targets'
	 * places: its triangles that reach into a target's box grown by the
	 * margin and HD_ARTCOPY_STANDS - the image of the donor's box, which a
	 * quarter turn moves onto the target's own. */
	const float grow = p.margin + (float)p.stands;
	std::vector<int> standingGroup;
	std::vector<hdartcopy::Standing> standing;

	for (int t = 0; t < staticCount; t++)
	{
		if (donorOf[(size_t)t] < 0 && !(p.offered && (plan.role[(size_t)t] == 2 ||
			plan.role[(size_t)t] == 4)))
			continue;

		const int g = plan.group[(size_t)t];

		if (std::find(standingGroup.begin(), standingGroup.end(), g) !=
			standingGroup.end())
			continue;

		standingGroup.push_back(g);
		standing.push_back(hdartcopy::Standing());

		hdartcopy::Standing& one = standing.back();
		const hdartcopy::Art& art = level.Tris(g);
		std::vector<int> goals;

		for (int u = 0; u < staticCount; u++)
			if (plan.group[(size_t)u] == g && (plan.role[(size_t)u] == 2 ||
				plan.role[(size_t)u] == 4))
				goals.push_back(u);

		for (size_t k = 0; k < art.first.size(); k++)
		{
			int v[3][3], lo[3], hi[3];

			for (int c = 0; c < 3; c++)
				level.Corner(g, trg.indices[art.first[k] + (uint32_t)c], v[c]);

			for (int a = 0; a < 3; a++)
			{
				lo[a] = (std::min)((std::min)(v[0][a], v[1][a]), v[2][a]);
				hi[a] = (std::max)((std::max)(v[0][a], v[1][a]), v[2][a]);
			}

			for (int u : goals)
			{
				const HDArtCopyStatic& goal = statics[u];
				bool in = true;

				for (int a = 0; a < 3 && in; a++)
					in = (float)hi[a] >= goal.mins[a] - grow &&
						(float)lo[a] <= goal.maxs[a] + grow;

				if (in)
				{
					one.Add(v, p.stands);
					break;
				}
			}
		}

		one.Done();
	}

	const auto standingOf = [&](int g) -> const hdartcopy::Standing&
	{
		const size_t at = (size_t)(std::find(standingGroup.begin(),
			standingGroup.end(), g) - standingGroup.begin());

		return standing[at];
	};

	/* The copies by motion: the targets whose donors one rigid motion moves
	 * onto them, in the level's order of their first target, take their
	 * art together (hdartcopy::Take), so that an object running on through
	 * the boxes of several donors moved alike comes whole. A motion is its
	 * turn and where it puts the world's origin. */
	struct Move
	{
		int g, turn;
		int from[3], to[3];
		uint64_t key;
		std::vector<int> targets, donors;
		std::vector<hdartcopy::Taken> taken;
		std::vector<uint32_t> seen;		// every triangle it takes, sorted
	};

	std::vector<Move> moves;
	std::vector<int> moveOf((size_t)staticCount, -1);
	std::vector<int> slotOf((size_t)staticCount, -1);

	for (int t = 0; t < staticCount; t++)
	{
		const int s = donorOf[(size_t)t];

		if (plan.role[(size_t)t] != 2 || s < 0)
			continue;

		const int from[3] = { statics[s].x, statics[s].y, statics[s].z };
		const int to[3] = { statics[t].x, statics[t].y, statics[t].z };
		const int zero[3] = { 0, 0, 0 };
		int origin[3];

		HDArtCopyMove(turnOf[(size_t)t], from, to, zero, origin);

		const uint64_t key = HDArtCopyMotionKey(turnOf[(size_t)t], origin[0],
			origin[1], origin[2]);
		const int g = plan.group[(size_t)t];
		size_t m = 0;

		while (m < moves.size() && (moves[m].g != g || moves[m].key != key))
			m++;

		if (m == moves.size())
		{
			Move one;

			one.g = g;
			one.turn = turnOf[(size_t)t];
			one.key = key;

			for (int k = 0; k < 3; k++)
			{
				one.from[k] = from[k];
				one.to[k] = to[k];
			}

			moves.push_back(one);
		}

		moveOf[(size_t)t] = (int)m;
		slotOf[(size_t)t] = (int)moves[m].targets.size();
		moves[m].targets.push_back(t);
		moves[m].donors.push_back(s);
	}

	for (Move& m : moves)
	{
		hdartcopy::Take(level, statics, m.donors, m.g, m.turn, m.from, m.to,
			faces, faceCount, p, standingOf(m.g), m.taken);

		for (const hdartcopy::Taken& one : m.taken)
			m.seen.insert(m.seen.end(), one.first.begin(), one.first.end());

		std::sort(m.seen.begin(), m.seen.end());
	}

	// The copies, in the level's order; every donor offered, for a tool.
	std::vector<unsigned char> faceDone((size_t)(faceCount > 0 ?
		faceCount : 0), 0);

	for (int t = 0; t < staticCount; t++)
	{
		if (plan.role[(size_t)t] != 2 && plan.role[(size_t)t] != 4)
			continue;

		const HDArtCopyStatic& goal = statics[t];
		const int g = plan.group[(size_t)t];
		const int best = donorOf[(size_t)t];
		const int toAt[3] = { goal.x, goal.y, goal.z };

		for (int s = 0; p.offered && s < staticCount; s++)
		{
			int turn = 0;

			if (plan.role[(size_t)s] != 1 || plan.group[(size_t)s] != g ||
				statics[s].slot != goal.slot ||
				!hdartcopy::Motion(statics[s], goal, &turn))
				continue;

			const std::vector<int> alone(1, s);
			const int fromAt[3] = { statics[s].x, statics[s].y, statics[s].z };
			std::vector<hdartcopy::Taken> taken;
			HDArtCopyOffer offer;

			hdartcopy::Take(level, statics, alone, g, turn, fromAt, toAt,
				faces, faceCount, p, standingOf(g), taken);
			offer.target = t;
			offer.donor = s;
			offer.group = g;
			offer.dx = goal.x - statics[s].x;
			offer.dy = goal.y - statics[s].y;
			offer.dz = goal.z - statics[s].z;
			offer.turn = turn;
			offer.inside = taken[0].inside;
			offer.crossing = taken[0].crossing;
			offer.walls = taken[0].walls;
			offer.standing = taken[0].standing;
			offer.chosen = s == best ? 1 : 0;
			plan.offered.push_back(offer);
		}

		if (best < 0)
			continue;

		const HDArtCopyStatic& from = statics[best];
		const int fromAt[3] = { from.x, from.y, from.z };
		Move& move = moves[(size_t)moveOf[(size_t)t]];
		const hdartcopy::Taken& taken =
			move.taken[(size_t)slotOf[(size_t)t]];
		HDArtCopy copy;

		memset(&copy, 0, sizeof(copy));
		copy.target = t;
		copy.donor = best;
		copy.group = g;
		copy.fromGroup = g;
		copy.dx = goal.x - from.x;
		copy.dy = goal.y - from.y;
		copy.dz = goal.z - from.z;
		copy.turn = turnOf[(size_t)t];
		copy.inside = taken.inside;
		copy.crossing = taken.crossing;
		copy.walls = taken.walls;
		copy.standing = taken.standing;
		copy.tris = (int)taken.first.size();
		copy.before = plan.held[(size_t)t];
		copy.verts = goal.vertCount;
		copy.firstTri = (int)plan.tris.size();
		plan.tris.insert(plan.tris.end(), taken.first.begin(),
			taken.first.end());
		plan.runs.insert(plan.runs.end(), taken.run.begin(), taken.run.end());

		// The classic faces beside the copy (hdartcopy::FacesBeside).
		{
			std::vector<uint32_t> first, run;
			int beside = 0;

			hdartcopy::FacesBeside(level, g, g, copy.turn, fromAt, toAt,
				goal.room, goal.mins, goal.maxs, faces, faceRooms, faceCount, p,
				faceDone, move.seen, first, run, &beside);
			plan.tris.insert(plan.tris.end(), first.begin(), first.end());
			plan.runs.insert(plan.runs.end(), run.begin(), run.end());
			copy.faces += beside;
			copy.tris += (int)first.size();
			copy.faceTris += (int)first.size();
		}

		plan.copies.push_back(copy);
		plan.addedTris += copy.tris;
	}

	/* Per copy, the target's points on its group's art with every copy of
	 * the group in, and the light: the means of the copied corners' two
	 * colours, and of the group's own corners round the target's place. */
	for (HDArtCopy& copy : plan.copies)
	{
		const int g = copy.group;
		const HDArtCopyStatic& goal = statics[copy.target];
		std::vector<uint64_t> filled;
		double light[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
		int corners = 0;

		for (const HDArtCopy& other : plan.copies)
		{
			if (other.group != g)
				continue;

			const HDArtCopyStatic& a = statics[other.donor];
			const HDArtCopyStatic& b = statics[other.target];
			const int fromAt[3] = { a.x, a.y, a.z };
			const int toAt[3] = { b.x, b.y, b.z };

			for (int k = 0; k < other.tris; k++)
				for (int c = 0; c < 3; c++)
				{
					const uint32_t index = trg.indices[plan.tris[(size_t)(
						other.firstTri + k)] + (uint32_t)c];
					int q[3], w[3];

					level.Corner(g, index, q);
					HDArtCopyMove(other.turn, fromAt, toAt, q, w);
					filled.push_back(hdartcopy::Key(w[0] >> 8, w[1] >> 8,
						w[2] >> 8));

					if (&other != &copy)
						continue;

					const trf::remaster::TrgVertex& v = trg.vertices[index];

					for (int i = 0; i < 3; i++)
					{
						light[0][i] += v.colour1[i];
						light[1][i] += v.colour2[i];
					}

					corners++;
				}
		}

		std::sort(filled.begin(), filled.end());
		filled.erase(std::unique(filled.begin(), filled.end()), filled.end());
		copy.after = hdartcopy::PointsOn(level, g,
			&verts[(size_t)goal.firstVert * 3], goal.vertCount, &filled);

		double sum[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
		int n = 0;
		const hdartcopy::Art& art = level.Tris(g);

		for (size_t k = 0; k < art.first.size(); k++)
			for (int c = 0; c < 3; c++)
			{
				const uint32_t index = trg.indices[art.first[k] + (uint32_t)c];
				int q[3];

				level.Corner(g, index, q);

				if (!hdartcopy::Inside(q, goal.mins, goal.maxs,
					HD_ARTCOPY_AROUND))
					continue;

				const trf::remaster::TrgVertex& v = trg.vertices[index];

				for (int i = 0; i < 3; i++)
				{
					sum[0][i] += v.colour1[i];
					sum[1][i] += v.colour2[i];
				}

				n++;
			}

		for (int a = 0; a < 2; a++)
			for (int i = 0; i < 3; i++)
			{
				copy.light[a][i] = corners ? (float)(light[a][i] / corners) :
					0.0f;
				copy.around[a][i] = n ? (float)(sum[a][i] / n) : 0.0f;
			}

		copy.aroundCorners = n;
	}

	// The items the run added to a slot the art carries (MakeItems).
	if (askItems)
		hdartcopy::MakeItems(level, plan, paired, items, itemCount, faces,
			faceRooms, faceCount, p, faceDone);
}

/* The plan put into the .TRG: per group the copies' vertices, each donor
 * corner once per copy, turned and moved, inserted right after the group's
 * last vertex (every later index moves up), and per run the copies'
 * triangles right after the run's own (every run starting there or later
 * moves up). The statics' copies first, then the items' (`items`, the list
 * the plan was made with), whose corners are read in the frame of the
 * group they are taken from and written in the frame of the group they go
 * into. A copy one of whose corners would leave the 16 bit range is left
 * out whole. Returns the vertices added. */
inline int HDArtCopyApply(trf::remaster::Trg& trg, const int* shift,
	int shifts, const HDArtCopyRoom* rooms, int roomCount,
	const HDArtCopyStatic* statics, HDArtCopyPlan* plan,
	const HDArtCopyItem* items = 0)
{
	if (!plan || (plan->copies.empty() && plan->itemCopies.empty()) ||
		plan->tris.empty())
		return 0;

	const auto shiftOf = [&](int g)
	{
		return shift && g >= 0 && g < shifts ? shift[g] : 0;
	};

	// Per group: its new vertices and where they go.
	struct Block
	{
		int group;
		uint32_t at;		// before this old vertex index
		std::vector<trf::remaster::TrgVertex> verts;
		uint32_t base;		// its first new index
	};

	// Per copied triangle: its block and its three corners in the block.
	struct NewTri
	{
		int block;
		uint32_t corner[3];
		int group;
		uint32_t run;
	};

	std::vector<Block> blocks;
	std::vector<NewTri> made;
	int movedVerts[2] = { 0, 0 };

	for (int kind = 0; kind < 2; kind++)
	for (HDArtCopy& copy : kind ? plan->itemCopies : plan->copies)
	{
		const int g = copy.group;
		const int f = kind ? copy.fromGroup : g;

		if (g < 0 || g >= roomCount || g >= (int)trg.groups.size() ||
			f < 0 || f >= roomCount || f >= (int)trg.groups.size() ||
			(kind && !items) || (!kind && !statics))
			continue;

		int fromAt[3], toAt[3];

		if (kind)
		{
			const HDArtCopyItem& from = items[copy.donor];
			const HDArtCopyItem& to = items[copy.target];

			fromAt[0] = from.x, fromAt[1] = from.y, fromAt[2] = from.z;
			toAt[0] = to.x, toAt[1] = to.y, toAt[2] = to.z;
		}
		else
		{
			const HDArtCopyStatic& from = statics[copy.donor];
			const HDArtCopyStatic& to = statics[copy.target];

			fromAt[0] = from.x, fromAt[1] = from.y, fromAt[2] = from.z;
			toAt[0] = to.x, toAt[1] = to.y, toAt[2] = to.z;
		}

		std::vector<trf::remaster::TrgVertex> moved;
		std::unordered_map<uint32_t, uint32_t> slot;
		bool fits = true;

		// Each donor corner once per copy, moved.
		for (int k = 0; k < copy.tris && fits; k++)
			for (int c = 0; c < 3 && fits; c++)
			{
				const uint32_t index = trg.indices[plan->tris[(size_t)(
					copy.firstTri + k)] + (uint32_t)c];

				if (slot.count(index))
					continue;

				trf::remaster::TrgVertex v = trg.vertices[index];
				const int p[3] = { (int)v.x + rooms[f].originX,
					(int)v.y + shiftOf(f), (int)v.z + rooms[f].originZ };
				int q[3];

				HDArtCopyMove(copy.turn, fromAt, toAt, p, q);

				const int local[3] = { q[0] - rooms[g].originX,
					q[1] - shiftOf(g), q[2] - rooms[g].originZ };

				for (int a = 0; a < 3; a++)
					fits = fits && local[a] >= -32767 && local[a] <= 32767;

				v.x = (int16_t)local[0];
				v.y = (int16_t)local[1];
				v.z = (int16_t)local[2];
				HDArtCopyTurnNormal(copy.turn, v);
				slot[index] = (uint32_t)moved.size();
				moved.push_back(v);
			}

		if (!fits)
		{
			if (kind)
			{
				plan->itemTris -= copy.tris;

				if ((size_t)copy.target < plan->itemRole.size())
					plan->itemRole[(size_t)copy.target] =
						HD_ARTCOPY_ITEM_EMPTY;
			}
			else
				plan->addedTris -= copy.tris;

			copy.tris = 0;
			continue;
		}

		movedVerts[kind] += (int)moved.size();

		int b = -1;

		for (size_t k = 0; k < blocks.size(); k++)
			if (blocks[k].group == g)
				b = (int)k;

		if (b < 0)
		{
			Block one;
			uint32_t last = 0;
			bool any = false;

			for (const trf::remaster::TrgPart& part :
				trg.groups[(size_t)g].parts)
				for (int pass = 0; pass < 3; pass++)
					for (uint32_t at = 0; at < part.pass[pass].count &&
						part.pass[pass].start + at < trg.indices.size(); at++)
					{
						const uint32_t v = trg.indices[part.pass[pass].start +
							at];

						if (v < trg.vertices.size())
						{
							last = !any || v > last ? v : last;
							any = true;
						}
					}

			one.group = g;
			one.at = any ? last + 1 : (uint32_t)trg.vertices.size();
			one.base = 0;
			blocks.push_back(one);
			b = (int)blocks.size() - 1;
		}

		Block& block = blocks[(size_t)b];
		const uint32_t first = (uint32_t)block.verts.size();

		block.verts.insert(block.verts.end(), moved.begin(), moved.end());

		for (int k = 0; k < copy.tris; k++)
		{
			NewTri tri;

			tri.block = b;
			tri.group = g;
			tri.run = plan->runs[(size_t)(copy.firstTri + k)];

			for (int c = 0; c < 3; c++)
				tri.corner[c] = first + slot[trg.indices[plan->tris[(size_t)(
					copy.firstTri + k)] + (uint32_t)c]];

			made.push_back(tri);
		}
	}

	if (made.empty())
		return 0;

	// The blocks in vertex order and each one's new base.
	std::vector<int> order(blocks.size());

	for (size_t k = 0; k < blocks.size(); k++)
		order[k] = (int)k;

	std::sort(order.begin(), order.end(), [&](int a, int b)
	{
		return blocks[(size_t)a].at != blocks[(size_t)b].at ?
			blocks[(size_t)a].at < blocks[(size_t)b].at :
			blocks[(size_t)a].group < blocks[(size_t)b].group;
	});

	{
		uint32_t before = 0;

		for (int k : order)
		{
			blocks[(size_t)k].base = blocks[(size_t)k].at + before;
			before += (uint32_t)blocks[(size_t)k].verts.size();
		}
	}

	// The old indices moved up past the blocks before them.
	for (uint32_t& index : trg.indices)
	{
		uint32_t add = 0;

		for (int k : order)
			if (blocks[(size_t)k].at <= index)
				add += (uint32_t)blocks[(size_t)k].verts.size();

		index += add;
	}

	// The vertex list with the blocks in.
	{
		std::vector<trf::remaster::TrgVertex> all;
		size_t from = 0;
		size_t more = 0;

		for (const Block& block : blocks)
			more += block.verts.size();

		all.reserve(trg.vertices.size() + more);

		for (int k : order)
		{
			const Block& block = blocks[(size_t)k];
			const size_t at = (std::min)((size_t)block.at, trg.vertices.size());

			all.insert(all.end(), trg.vertices.begin() + (std::ptrdiff_t)from,
				trg.vertices.begin() + (std::ptrdiff_t)at);
			all.insert(all.end(), block.verts.begin(), block.verts.end());
			from = at;
		}

		all.insert(all.end(), trg.vertices.begin() + (std::ptrdiff_t)from,
			trg.vertices.end());
		plan->addedVerts = movedVerts[0];
		plan->itemVerts = movedVerts[1];
		trg.vertices.swap(all);
	}

	/* The triangles, per run of their group, right after the run's own.
	 * `start` is the run's own start: an empty run receiving a copy ends
	 * where it starts, and where the run ending there before it receives
	 * one too, the two go in in the file's order (Before). */
	struct Insert
	{
		uint32_t at;
		uint32_t start;
		int group;
		uint32_t run;
		std::vector<uint32_t> indices;
	};

	std::vector<Insert> inserts;

	for (const NewTri& tri : made)
	{
		const trf::remaster::TrgPart::Pass& run =
			trg.groups[(size_t)tri.group].parts[tri.run / 3].pass[tri.run % 3];
		size_t into = inserts.size();

		for (size_t k = 0; k < inserts.size(); k++)
			if (inserts[k].group == tri.group && inserts[k].run == tri.run)
				into = k;

		if (into == inserts.size())
		{
			Insert one;

			one.at = run.start + run.count;
			one.start = run.start;
			one.group = tri.group;
			one.run = tri.run;
			inserts.push_back(one);
		}

		for (int c = 0; c < 3; c++)
			inserts[into].indices.push_back(blocks[(size_t)tri.block].base +
				tri.corner[c]);
	}

	/* Whether insert a goes in before a run of group g, `self` its part
	 * times three plus its pass, starting at `start`: in front of it, or
	 * right at it for a run ending there or an empty run there earlier in
	 * the file (group, part, pass). */
	const auto before = [](const Insert& a, uint32_t start, int g,
		uint32_t self)
	{
		if (a.at != start)
			return a.at < start;

		if (a.start != start)
			return a.start < start;

		return a.group != g ? a.group < g : a.run < self;
	};

	std::stable_sort(inserts.begin(), inserts.end(),
		[&](const Insert& a, const Insert& b)
	{
		return a.at != b.at ? a.at < b.at : before(a, b.start, b.group, b.run);
	});

	std::vector<uint32_t> indices;
	size_t from = 0, more = 0;

	for (const Insert& one : inserts)
		more += one.indices.size();

	indices.reserve(trg.indices.size() + more);

	for (const Insert& one : inserts)
	{
		const size_t at = (std::min)((size_t)one.at, trg.indices.size());

		indices.insert(indices.end(), trg.indices.begin() + (std::ptrdiff_t)from,
			trg.indices.begin() + (std::ptrdiff_t)at);
		indices.insert(indices.end(), one.indices.begin(), one.indices.end());
		from = at;
	}

	indices.insert(indices.end(), trg.indices.begin() + (std::ptrdiff_t)from,
		trg.indices.end());

	// Every run moved up by what went in before it, the grown runs longer
	// by their own.
	for (size_t g = 0; g < trg.groups.size(); g++)
		for (size_t p = 0; p < trg.groups[g].parts.size(); p++)
			for (int pass = 0; pass < 3; pass++)
			{
				trf::remaster::TrgPart::Pass& run =
					trg.groups[g].parts[p].pass[pass];
				const uint32_t self = (uint32_t)(p * 3 + (size_t)pass);
				uint32_t add = 0, grow = 0;

				for (const Insert& one : inserts)
				{
					const bool mine = one.group == (int)g && one.run == self;

					if (mine)
						grow = (uint32_t)one.indices.size();
					else if (before(one, run.start, (int)g, self))
						add += (uint32_t)one.indices.size();
				}

				run.start += add;
				run.count += grow;
			}

	trg.indices.swap(indices);
	return plan->addedVerts + plan->itemVerts;
}

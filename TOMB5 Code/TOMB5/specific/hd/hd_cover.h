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

/* A classic room and another group's HD art in one place.
 *
 * A room the engine draws classic - no group, an empty group, a hollow group
 * (hd_level.cpp, the loop over g_group_hd) - can have its floor and walls
 * modelled in a NEIGHBOUR's group: ANDREA2's room 124 is drawn classic and
 * group 121, the room through its west portal, carries 14,700 triangles
 * inside it that lie on all 46 of its classic faces. A frame that lists both
 * rooms would draw the place twice, and the two surfaces fight in the depth
 * buffer; a frame that lists only one draws it once. This header holds the
 * arithmetic of the rule that keeps every place drawn once:
 *
 *   - which art triangles stand on a classic face (HDCoverOverlaps), and of
 *     those which lie on the room's faces with all of their area
 *     (HDCoverTriOnFaces) - the renderer gives those triangles draw calls of
 *     their own per holder, "stand ranges" (hd_render.cpp);
 *   - which of the room's faces one group's art covers (HDCoverFaces,
 *     HDCoverFaceCovered); the group that covers the most of them is the
 *     room's carrier (HDCoverCarrierBetter); when it covers at least half
 *     of them the room is drawn as the carrier's art where the carrier
 *     covers a face and classic where it does not - wholly as art when it
 *     covers every face (HDCoverRoomCovered, HDCoverRoomState);
 *   - which of the stand ranges a frame draws (HDStandDraw,
 *     HDStandDrawFaces);
 *   - whether a classic face is one Lara can stand on (HDCoverFaceFloor),
 *     which decides whether a room drawn as art counts as HD for her
 *     shadow.
 *
 * No engine header on purpose, like hd_pageclass.h: ModernTest asserts on the
 * rule without a device, TrgProbe measures the game with the same functions
 * and RoomProbe draws the frame's answer with them, so the three cannot
 * drift apart. Everything here is in world units with TR5's y down. */

#include <math.h>
#include <vector>
#include <unordered_map>

/* How far art may stand off a classic face and still be its surface: the
 * same 128 as HD_PAIR_FACE_REACH (hd_render.h), where the facade of ANDREA1's
 * room 101 stands within 86 units of its faces. The walls of the art that
 * stands in ANDREA2's room 124 stand 64 to 128 units behind the classic wall
 * planes (TrgProbe -classicart -arttol 64 covers 34 of its 46 faces, 128
 * covers all 46). */
#define HD_COVER_REACH		128.0
/* The spacing of the samples a face is tested at: a sixteenth of a sector,
 * so a 1024-unit wall is asked at a few hundred points and the whole game's
 * classic rooms take TrgProbe about six seconds. */
#define HD_COVER_STEP		64.0
/* Barycentric slack of the segment test, so that the seam between two art
 * triangles is not a gap. */
#define HD_COVER_SLACK		1e-3
/* The overlap an art triangle needs with a face, in units of the face's
 * plane, before it counts as standing on it - so that a triangle that only
 * touches a face's edge belongs to the face next door. */
#define HD_COVER_TOUCH		1e-3
/* The sign that turns the y of a classic face's normal (b - a) x (c - a)
 * into "up": +1, because a classic room's faces are wound so that this
 * normal points away from the side they are seen from - RICH2's room 15
 * has its floor at y -26112 under walls centred at -26368, and its normal's
 * y is +1 there, pointing down. And how far up a face Lara can stand on
 * points: more than half way, a slope of under 60 degrees. The faces the
 * rooms drawn as art keep point up by 0 (walls), -1 (ceilings), 1
 * (floors) or 0.80 (one slope of RICHCUT2's room 2) (TrgProbe -classicart
 * -perface, `up=`), so any value between 0 and 0.8 gives the same answer. */
#define HD_COVER_UP_SIGN	1.0
#define HD_COVER_FLOOR_UP	0.5

// One art triangle in world units: a corner, two edges, its unit normal
// ((b - a) x (c - a)) and its box. `group` is the .TRG group it is drawn with.
struct HDCoverTri
{
	float a[3], e1[3], e2[3];
	float n[3];
	float lo[3], hi[3];
	int group;
};

// One classic face in world units, as InsertRoomlet hands it over: a quad
// (corners 4, asked as its halves 0,1,2 and 0,2,3) or a triangle.
struct HDCoverFace
{
	int corners;
	int doubleSided;
	double v[4][3];
};

/* An art triangle out of three room-local corners and the room's origin, the
 * corner and the box moved into the world in float and the edges kept local,
 * so that the engine, TrgProbe and RoomProbe make the same bits out of the
 * same .TRG. False for a triangle of no area, which covers nothing. */
inline bool HDCoverMakeTri(const float v[3][3], const float origin[3], int group,
	HDCoverTri* out)
{
	HDCoverTri& t = *out;

	for (int k = 0; k < 3; k++)
	{
		const float lo = v[0][k] < v[1][k] ? (v[0][k] < v[2][k] ? v[0][k] : v[2][k])
			: (v[1][k] < v[2][k] ? v[1][k] : v[2][k]);
		const float hi = v[0][k] > v[1][k] ? (v[0][k] > v[2][k] ? v[0][k] : v[2][k])
			: (v[1][k] > v[2][k] ? v[1][k] : v[2][k]);

		t.a[k] = v[0][k] + origin[k];
		t.e1[k] = v[1][k] - v[0][k];
		t.e2[k] = v[2][k] - v[0][k];
		t.lo[k] = lo + origin[k];
		t.hi[k] = hi + origin[k];
	}

	const double n[3] =
	{
		(double)t.e1[1] * t.e2[2] - (double)t.e1[2] * t.e2[1],
		(double)t.e1[2] * t.e2[0] - (double)t.e1[0] * t.e2[2],
		(double)t.e1[0] * t.e2[1] - (double)t.e1[1] * t.e2[0]
	};
	const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

	t.group = group;

	if (length <= 0.0)
		return false;

	for (int k = 0; k < 3; k++)
		t.n[k] = (float)(n[k] / length);

	return true;
}

/* Does the segment p - tol n .. p + tol n meet the triangle, and does the
 * triangle face along `n` (facing 1), against it (-1) or either way (0)?
 * Moller-Trumbore with HD_COVER_SLACK. */
inline bool HDCoverHits(const double p[3], const double n[3], double tol,
	int facing, const HDCoverTri& tri)
{
	const double d = tri.n[0] * n[0] + tri.n[1] * n[1] + tri.n[2] * n[2];

	if ((facing > 0 && d <= 0.0) || (facing < 0 && d >= 0.0))
		return false;

	const double origin[3] = { p[0] - tol * n[0], p[1] - tol * n[1],
		p[2] - tol * n[2] };
	const double dir[3] = { 2.0 * tol * n[0], 2.0 * tol * n[1], 2.0 * tol * n[2] };
	const double e1[3] = { tri.e1[0], tri.e1[1], tri.e1[2] };
	const double e2[3] = { tri.e2[0], tri.e2[1], tri.e2[2] };
	const double h[3] =
	{
		dir[1] * e2[2] - dir[2] * e2[1],
		dir[2] * e2[0] - dir[0] * e2[2],
		dir[0] * e2[1] - dir[1] * e2[0]
	};
	const double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];

	if (fabs(det) < 1e-9)
		return false;	// the segment runs in the art's plane

	const double inv = 1.0 / det;
	const double s[3] = { origin[0] - tri.a[0], origin[1] - tri.a[1],
		origin[2] - tri.a[2] };
	const double u = inv * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);

	if (u < -HD_COVER_SLACK || u > 1.0 + HD_COVER_SLACK)
		return false;

	const double q[3] =
	{
		s[1] * e1[2] - s[2] * e1[1],
		s[2] * e1[0] - s[0] * e1[2],
		s[0] * e1[1] - s[1] * e1[0]
	};
	const double v = inv * (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]);

	if (v < -HD_COVER_SLACK || u + v > 1.0 + HD_COVER_SLACK)
		return false;

	const double w = inv * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);

	return w >= 0.0 && w <= 1.0;
}

/* The samples a face is tested at: the centroids of a regular subdivision of
 * each of its triangles, none farther than `step` from the next and none on
 * an edge. `visit(p, n)` is called once per sample with the half's unit
 * normal. Returns how many there were - 0 for a face of no area. */
template <class Visit>
inline long long HDCoverSamples(const HDCoverFace& face, double step, Visit visit)
{
	long long samples = 0;

	for (int half = 0; half + 2 < face.corners; half++)
	{
		const double* a = face.v[0];
		const double* b = face.v[half + 1];
		const double* c = face.v[half + 2];
		const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const double e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		double n[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};
		const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		if (length <= 0.0)
			continue;	// a half of no area

		for (int k = 0; k < 3; k++)
			n[k] /= length;

		const double e3[3] = { e2[0] - e1[0], e2[1] - e1[1], e2[2] - e1[2] };
		const double l1 = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
		const double l2 = sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
		const double l3 = sqrt(e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2]);
		const double longest = l1 > l2 ? (l1 > l3 ? l1 : l3) : (l2 > l3 ? l2 : l3);
		int cuts = (int)ceil(longest / step);

		if (cuts < 1)
			cuts = 1;

		for (int i = 0; i < cuts; i++)
			for (int j = 0; i + j < cuts; j++)
				for (int down = 0; down < 2; down++)
				{
					if (down && i + j >= cuts - 1)
						continue;

					const double u = ((double)i + (down ? 2.0 : 1.0) / 3.0) / cuts;
					const double v = ((double)j + (down ? 2.0 : 1.0) / 3.0) / cuts;
					const double p[3] =
					{
						a[0] + u * e1[0] + v * e2[0],
						a[1] + u * e1[1] + v * e2[1],
						a[2] + u * e1[2] + v * e2[2]
					};

					samples++;
					visit(p, n);
				}
	}

	return samples;
}

/* A set of art triangles with a 256-unit cell grid over their boxes, asked
 * per sample. `seen` keeps a triangle that spans several cells from being
 * tested twice for one sample. */
struct HDCoverSet
{
	std::vector<HDCoverTri> tris;
	std::unordered_map<long long, std::vector<int> > cells;

	static long long key(int x, int y, int z)
	{
		return ((long long)(x + 1048576) << 42) |
			((long long)(y + 1048576) << 21) | (long long)(z + 1048576);
	}

	void build()
	{
		cells.clear();

		for (size_t t = 0; t < tris.size(); t++)
		{
			const HDCoverTri& tri = tris[t];
			int lo[3], hi[3];

			for (int k = 0; k < 3; k++)
			{
				lo[k] = (int)floor(tri.lo[k] / 256.0f);
				hi[k] = (int)floor(tri.hi[k] / 256.0f);
			}

			for (int x = lo[0]; x <= hi[0]; x++)
				for (int y = lo[1]; y <= hi[1]; y++)
					for (int z = lo[2]; z <= hi[2]; z++)
						cells[key(x, y, z)].push_back((int)t);
		}
	}

	bool hits(const double p[3], const double n[3], double tol, int facing,
		std::vector<int>& seen, int stamp) const
	{
		double lo[3], hi[3];

		for (int k = 0; k < 3; k++)
		{
			const double s0 = p[k] - tol * n[k];
			const double s1 = p[k] + tol * n[k];

			lo[k] = s0 < s1 ? s0 : s1;
			hi[k] = s0 < s1 ? s1 : s0;
		}

		for (int x = (int)floor(lo[0] / 256.0); x <= (int)floor(hi[0] / 256.0); x++)
			for (int y = (int)floor(lo[1] / 256.0); y <= (int)floor(hi[1] / 256.0); y++)
				for (int z = (int)floor(lo[2] / 256.0); z <= (int)floor(hi[2] / 256.0); z++)
				{
					const auto it = cells.find(key(x, y, z));

					if (it == cells.end())
						continue;

					for (int t : it->second)
					{
						if (seen[(size_t)t] == stamp)
							continue;

						seen[(size_t)t] = stamp;

						if (HDCoverHits(p, n, tol, facing, tris[(size_t)t]))
							return true;
					}
				}

		return false;
	}
};

/* How much of a room's faces a set covers: faces of no area are left out,
 * a face is covered when every one of its samples is, the room when every
 * face is. `faceShare` is per face of the list, 1 for a face of no area. A
 * double-sided face takes art facing either way. */
struct HDCoverCount
{
	int faces = 0, facesCovered = 0;
	long long samples = 0, samplesCovered = 0;
	std::vector<double> faceShare;
};

inline HDCoverCount HDCoverFaces(const HDCoverFace* faces, int count,
	const HDCoverSet& set, double tol, double step, int facing)
{
	HDCoverCount out;
	std::vector<int> seen(set.tris.size(), 0);
	int stamp = 0;

	for (int f = 0; f < count; f++)
	{
		const HDCoverFace& face = faces[f];
		long long covered = 0;
		const long long samples = HDCoverSamples(face, step,
			[&](const double* p, const double* n)
			{
				if (++stamp == 0x7fffffff)
				{
					for (size_t i = 0; i < seen.size(); i++)
						seen[i] = 0;

					stamp = 1;
				}

				if (set.hits(p, n, tol, face.doubleSided ? 0 : facing, seen, stamp))
					covered++;
			});

		if (!samples)
		{
			out.faceShare.push_back(1.0);
			continue;
		}

		out.faces++;
		out.samples += samples;
		out.samplesCovered += covered;
		out.faceShare.push_back((double)covered / (double)samples);

		if (covered == samples)
			out.facesCovered++;
	}

	return out;
}

/* The threshold, per face: a face is covered when one group's art covers
 * every one of its samples, and one bare sample keeps it classic - a strict
 * rule, because a face left out where the art does not cover it shows the
 * void where the art stops (RICH2's room 16 has no art under the 16 faces of
 * its ceiling, TrgProbe -classicart -perface). A room is drawn wholly as the
 * art when it has a face and the carrier covers every one.
 *
 * One group and not the union of several, because two groups that both
 * cover a face are two copies of one surface: ANDREA2's group 127 carries a
 * second copy of 36 of room 124's 46 faces beside group 121's, and the two
 * drawn together fight as the classic faces did. So the covering group - the
 * carrier - is the one the room's covered faces are drawn as, and the stand
 * art of every other group that lies on the faces is left out while the room
 * is listed. */
inline bool HDCoverRoomCovered(int faces, int facesCovered)
{
	return faces > 0 && facesCovered >= faces;
}

/* One face covered by one group: every one of its samples is
 * (HDCoverCount::faceShare 1). A face of no area has share 1 as well and
 * draws nothing either way. */
inline bool HDCoverFaceCovered(double share)
{
	return share >= 1.0;
}

/* Is group `group`, covering `faces` of a room's faces with `tris` tagged
 * triangles, a better carrier than `best` (-1 for none yet)? The one that
 * covers the most faces, then the one with the most triangles on the faces,
 * then the lowest group; a group that covers no face is never a carrier. A
 * group that covers every face always wins over one that covers some, so
 * of the groups that cover a room wholly the choice is the one the
 * threshold above makes. */
inline bool HDCoverCarrierBetter(int faces, int tris, int group, int bestFaces,
	int bestTris, int best)
{
	if (faces <= 0)
		return false;

	if (best < 0)
		return true;

	if (faces != bestFaces)
		return faces > bestFaces;

	if (tris != bestTris)
		return tris > bestTris;

	return group < best;
}

/* What a classic-drawn room is drawn as, from its faces of any area and the
 * faces its carrier covers:
 *
 *   HD_STAND_ROOM_CLASSIC  fewer than half of them covered: every face drawn
 *                          classic
 *   HD_STAND_ROOM_PART     at least half and not all: the covered faces are
 *                          left out and drawn as the carrier's art, the
 *                          others stay classic
 *   HD_STAND_ROOM_COVERED  all: the room is drawn as the carrier's art and
 *                          none of its roomlets
 *
 * Half, because where the art covers few of a room's faces it is another
 * surface than the room's, and the faces it leaves out open cracks where
 * they meet the kept ones: TITLE's dunes cover 10 of room 70's 65 faces, 7 of
 * 101's 96 and 7 of 103's 213, and drawn in part room 103 showed 741 pixels
 * of nothing along those edges in one RoomProbe view (2,107 with the carrier
 * off the list) where it shows none drawn classic. Every other room the art
 * covers a face of has a carrier that covers at least half (RICH2 16 half of
 * 32, 18 15 of 16, 19 13 of 15; RICHCUT2 2, 3 and 4 46 of 49, 106 of 112 and
 * 22 of 29) but ANDREA3's room 74, one face of 69 (TrgProbe -classicart
 * -perface). */
#define HD_COVER_PART_SHARE	0.5

enum
{
	HD_STAND_ROOM_CLASSIC = 0,
	HD_STAND_ROOM_PART,
	HD_STAND_ROOM_COVERED
};

inline int HDCoverRoomState(int faces, int facesCovered)
{
	if (faces <= 0 || facesCovered <= 0 ||
		(double)facesCovered < HD_COVER_PART_SHARE * (double)faces)
		return HD_STAND_ROOM_CLASSIC;

	return facesCovered >= faces ? HD_STAND_ROOM_COVERED : HD_STAND_ROOM_PART;
}

/* How far a classic face points up: the upward component (-y, TR5's y runs
 * down) of the unit normal of its first half of any area, the side
 * InsertRoomlet draws it from; either side of a double-sided face, so its
 * absolute value. 0 for a face of no area. */
inline double HDCoverFaceUp(const HDCoverFace& face)
{
	for (int half = 0; half + 2 < face.corners; half++)
	{
		const double* a = face.v[0];
		const double* b = face.v[half + 1];
		const double* c = face.v[half + 2];
		const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const double e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		const double n[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};
		const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		if (length <= 0.0)
			continue;

		const double up = HD_COVER_UP_SIGN * n[1] / length;

		return face.doubleSided && up < 0.0 ? -up : up;
	}

	return 0.0;
}

/* A face Lara can stand on: one that points up by more than
 * HD_COVER_FLOOR_UP. The projected shadow lands on art alone, so a classic
 * face that can be under her is what keeps a room drawn as art from
 * counting as an HD room for her shadow (hd_level.h, HDRoomShadowHD). */
inline bool HDCoverFaceFloor(const HDCoverFace& face)
{
	return HDCoverFaceUp(face) > HD_COVER_FLOOR_UP;
}

/* Does a room count as HD for her projected shadow? A room drawn HD does; a
 * room drawn classic does when no classic face she can stand on is drawn in
 * it: it has no classic face at all (`classicFaces` 0 - the air over a
 * court, a shaft another group models), or it is drawn as its carrier's art,
 * wholly or in part (`state`, HDCoverRoomState), and none of the faces it
 * keeps is a floor (`floorsKept`, HDCoverFaceFloor). Under her there is then
 * only art, which takes the receive; on a classic floor the projection would
 * land on nothing that shows it, and the round blob stays. A room with no
 * classic face whose floor data has a sector she can stand on (no wall, no
 * opening into a room below) while no other group's art stands in its box
 * (`bareFloor`) draws nothing at all under her there, so it keeps the blob
 * too. */
inline bool HDCoverRoomShadowHD(bool roomHD, int state, int floorsKept, int classicFaces,
	bool bareFloor = false)
{
	if (roomHD)
		return true;

	if (classicFaces <= 0)
		return !bareFloor;

	return state != HD_STAND_ROOM_CLASSIC && floorsKept <= 0;
}

/* Does an art triangle stand on a classic face? True when some part of it
 * lies within `reach` of one of the face's halves and inside that half's
 * outline by more than HD_COVER_TOUCH, and - unless the face is
 * double-sided - it faces the way the half does. The triangle is clipped to
 * the slab of the half's plane and both are compared in that plane with
 * separating axes.
 *
 * This and not the centroid: a triangle whose centroid is off the face can
 * still cover part of it (RICH2's room 15 is covered by group 14's art, but
 * by the triangles whose centroid lies on a face only in 13 of its 15
 * faces), and every sample HDCoverHits finds on a face lies on a triangle
 * this answers true for. */
inline bool HDCoverOverlaps(const HDCoverTri& tri, const HDCoverFace& face,
	double reach)
{
	for (int half = 0; half + 2 < face.corners; half++)
	{
		const double* a = face.v[0];
		const double* b = face.v[half + 1];
		const double* c = face.v[half + 2];
		const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const double e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		double n[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};
		const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		const double l1 = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);

		if (length <= 0.0 || l1 <= 0.0)
			continue;

		for (int k = 0; k < 3; k++)
			n[k] /= length;

		if (!face.doubleSided &&
			tri.n[0] * n[0] + tri.n[1] * n[1] + tri.n[2] * n[2] <= 0.0)
			continue;

		// The triangle's corners, clipped to -reach <= distance <= reach.
		double poly[8][3];
		int count = 3;

		for (int k = 0; k < 3; k++)
		{
			poly[0][k] = tri.a[k];
			poly[1][k] = (double)tri.a[k] + tri.e1[k];
			poly[2][k] = (double)tri.a[k] + tri.e2[k];
		}

		for (int side = 0; side < 2 && count >= 3; side++)
		{
			double out[8][3];
			int made = 0;

			for (int i = 0; i < count; i++)
			{
				const double* p = poly[i];
				const double* q = poly[(i + 1) % count];
				const double dp = (p[0] - a[0]) * n[0] + (p[1] - a[1]) * n[1] +
					(p[2] - a[2]) * n[2];
				const double dq = (q[0] - a[0]) * n[0] + (q[1] - a[1]) * n[1] +
					(q[2] - a[2]) * n[2];
				// Inside: distance >= -reach on the first side, <= reach on the second.
				const double fp = side ? reach - dp : dp + reach;
				const double fq = side ? reach - dq : dq + reach;

				if (fp >= 0.0 && made < 8)
				{
					for (int k = 0; k < 3; k++)
						out[made][k] = p[k];

					made++;
				}

				if (((fp >= 0.0) != (fq >= 0.0)) && made < 8)
				{
					const double t = fp / (fp - fq);

					for (int k = 0; k < 3; k++)
						out[made][k] = p[k] + t * (q[k] - p[k]);

					made++;
				}
			}

			count = made;

			for (int i = 0; i < count; i++)
				for (int k = 0; k < 3; k++)
					poly[i][k] = out[i][k];
		}

		if (count < 3)
			continue;

		// Both into the half's plane: u along its first edge, w = n x u.
		const double u[3] = { e1[0] / l1, e1[1] / l1, e1[2] / l1 };
		const double w[3] =
		{
			n[1] * u[2] - n[2] * u[1],
			n[2] * u[0] - n[0] * u[2],
			n[0] * u[1] - n[1] * u[0]
		};
		double P[8][2], F[3][2];

		for (int i = 0; i < count; i++)
		{
			const double d[3] = { poly[i][0] - a[0], poly[i][1] - a[1], poly[i][2] - a[2] };

			P[i][0] = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
			P[i][1] = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
		}

		{
			const double* corner[3] = { a, b, c };

			for (int i = 0; i < 3; i++)
			{
				const double d[3] = { corner[i][0] - a[0], corner[i][1] - a[1],
					corner[i][2] - a[2] };

				F[i][0] = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
				F[i][1] = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
			}
		}

		// Separating axes: the edge normals of both convex outlines.
		bool apart = false;

		for (int shape = 0; shape < 2 && !apart; shape++)
		{
			const int edges = shape ? 3 : count;

			for (int i = 0; i < edges && !apart; i++)
			{
				const double* p = shape ? F[i] : P[i];
				const double* q = shape ? F[(i + 1) % 3] : P[(i + 1) % count];
				double ax = -(q[1] - p[1]);
				double ay = q[0] - p[0];
				const double len = sqrt(ax * ax + ay * ay);

				if (len <= 0.0)
					continue;

				ax /= len;
				ay /= len;

				double lo0 = 1e300, hi0 = -1e300, lo1 = 1e300, hi1 = -1e300;

				for (int k = 0; k < count; k++)
				{
					const double s = P[k][0] * ax + P[k][1] * ay;

					if (s < lo0) lo0 = s;
					if (s > hi0) hi0 = s;
				}

				for (int k = 0; k < 3; k++)
				{
					const double s = F[k][0] * ax + F[k][1] * ay;

					if (s < lo1) lo1 = s;
					if (s > hi1) hi1 = s;
				}

				apart = hi0 <= lo1 + HD_COVER_TOUCH || hi1 <= lo0 + HD_COVER_TOUCH;
			}
		}

		if (!apart)
			return true;
	}

	return false;
}

/* Does point `p` of an art triangle with unit normal `n` lie on one of the
 * faces: within `reach` of a half's plane, inside its outline give or take
 * HD_COVER_EDGE, and - unless the face is double-sided - on a triangle that
 * faces the way the half does? */
#define HD_COVER_EDGE		1.0

inline bool HDCoverPointOnFaces(const double p[3], const float n[3],
	const HDCoverFace* faces, int count, double reach)
{
	for (int f = 0; f < count; f++)
	{
		const HDCoverFace& face = faces[f];

		for (int half = 0; half + 2 < face.corners; half++)
		{
			const double* c[3] = { face.v[0], face.v[half + 1], face.v[half + 2] };
			const double e1[3] = { c[1][0] - c[0][0], c[1][1] - c[0][1], c[1][2] - c[0][2] };
			const double e2[3] = { c[2][0] - c[0][0], c[2][1] - c[0][1], c[2][2] - c[0][2] };
			double m[3] =
			{
				e1[1] * e2[2] - e1[2] * e2[1],
				e1[2] * e2[0] - e1[0] * e2[2],
				e1[0] * e2[1] - e1[1] * e2[0]
			};
			const double length = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);

			if (length <= 0.0)
				continue;

			for (int k = 0; k < 3; k++)
				m[k] /= length;

			if (!face.doubleSided && n[0] * m[0] + n[1] * m[1] + n[2] * m[2] <= 0.0)
				continue;

			const double d = (p[0] - c[0][0]) * m[0] + (p[1] - c[0][1]) * m[1] +
				(p[2] - c[0][2]) * m[2];

			if (d < -reach || d > reach)
				continue;

			// Inside every edge of the half, measured in the half's plane.
			bool inside = true;

			for (int e = 0; e < 3 && inside; e++)
			{
				const double* a = c[e];
				const double* b = c[(e + 1) % 3];
				const double edge[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
				// The edge's inward normal in the plane: m x edge.
				double in[3] =
				{
					m[1] * edge[2] - m[2] * edge[1],
					m[2] * edge[0] - m[0] * edge[2],
					m[0] * edge[1] - m[1] * edge[0]
				};
				const double len = sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);

				if (len <= 0.0)
					continue;

				inside = ((p[0] - a[0]) * in[0] + (p[1] - a[1]) * in[1] +
					(p[2] - a[2]) * in[2]) / len >= -HD_COVER_EDGE;
			}

			if (inside)
				return true;
		}
	}

	return false;
}

/* Does an art triangle lie on the faces with all of its area? Asked at its
 * three corners and at the samples HDCoverSamples takes of it; every one of
 * them has to lie on some face (HDCoverPointOnFaces).
 *
 * The difference to HDCoverOverlaps is what can be left out. A triangle that
 * stands on a face with only part of itself reaches beyond it - into the
 * art's own room or past the face's edge - and leaving it out would open a
 * hole there: the trunk of a palm of ANDREA2's group 117 grazes a face of
 * room 124 and would lose its lower half. Such a triangle is left out of no
 * frame; one that lies on the faces whole is a copy of the room's surface -
 * group 127's second floor in room 124 - and can be. */
inline bool HDCoverTriOnFaces(const HDCoverTri& tri, const HDCoverFace* faces,
	int count, double reach)
{
	HDCoverFace self;

	self.corners = 3;
	self.doubleSided = 1;

	for (int k = 0; k < 3; k++)
	{
		self.v[0][k] = tri.a[k];
		self.v[1][k] = (double)tri.a[k] + tri.e1[k];
		self.v[2][k] = (double)tri.a[k] + tri.e2[k];
	}

	for (int c = 0; c < 3; c++)
		if (!HDCoverPointOnFaces(self.v[c], tri.n, faces, count, reach))
			return false;

	bool all = true;

	HDCoverSamples(self, HD_COVER_STEP, [&](const double* p, const double*)
	{
		if (all && !HDCoverPointOnFaces(p, tri.n, faces, count, reach))
			all = false;
	});

	return all;
}

/* What a frame draws of one classic-drawn room and one group's art on its
 * faces or in its box (its stand ranges), from whether the room is on the
 * frame's list, whether the art's own room is, whether the room is covered,
 * whether this group is the one that covers it (the carrier, see above) and
 * whether the range's triangles lie on the room's faces whole
 * (HDCoverTriOnFaces - the renderer's kind 1) and whether they reach onto
 * them with part of their area only (the renderer's kind 0):
 *
 *   classic   the room's classic roomlets
 *   withArt   the stand range with its own group, wherever that group draws
 *   inHolder  the stand range inside the room's own portal window, with its
 *             group's matrix and lighting
 *   behind    withArt, pushed back in depth (HD_STAND_PUSH, hd_render.h)
 *   front     the stand range alone, pulled towards the eye by the same
 *             fraction, in its group's window when that group is listed and
 *             in the room's window when it is not - only for the art of an
 *             object the art carries (HDStandDrawBaked below); never here
 *
 * While the room is listed its place comes from exactly one source: a
 * covered room is drawn as its carrier's art - all of the carrier's stand
 * ranges, the relief standing in the room's box on no face included, with
 * the carrier's group when that is listed and in the room's window when it
 * is not - and every other group's art that lies on its faces whole is left
 * out; a room no group covers at least half the faces of is drawn classic
 * and all the art that lies on its faces whole is left out. Art that only
 * grazes the room's faces, and every other group's art in its box, is drawn
 * with its group whatever the room does - leaving it out would cut it where
 * it leaves the faces.
 * Where such art reaches onto the faces of a listed room that stays classic,
 * the classic faces and the art lie in one place, so the art is drawn pushed
 * back: the classic face wins every pixel the two share, and the art beyond
 * the faces stays. A room that is not listed draws nothing of its own, and
 * the art on its faces is drawn with its group as everywhere else. A room
 * its carrier covers in part is HDStandDrawFaces' below. */
struct HDStandAnswer
{
	bool classic;
	bool withArt;
	bool inHolder;
	bool behind;
	bool front;
};

inline HDStandAnswer HDStandDraw(bool holderListed, bool artListed, bool covered,
	bool carrier, bool onFaces, bool grazes)
{
	HDStandAnswer out;
	const bool drawnAs = covered && carrier;

	out.classic = holderListed && !covered;
	out.withArt = artListed && (!holderListed || drawnAs || !onFaces);
	out.inHolder = holderListed && drawnAs && !artListed;
	out.behind = out.withArt && out.classic && grazes && !onFaces;
	out.front = false;
	return out;
}

/* The faces a stand range stands on, where its group covers some of its
 * holder's faces and not all: covered ones only, kept ones only, or both
 * (HDStandRange::faces, hd_render.h). 0 for a range of kind 2 and wherever
 * the group covers none or all of the faces - every range of a room no group
 * covers in part. */
#define HD_STAND_FACES_COVERED	1
#define HD_STAND_FACES_KEPT		2
#define HD_STAND_FACES_BOTH		3

/* HDStandDraw with the room's state (HDCoverRoomState) and a range's kind
 * (0 reaching onto the faces, 1 lying on them whole, 2 in the box on no
 * face) and faces. A room drawn classic or wholly as art answers exactly as
 * HDStandDraw. A room its carrier covers in part, while it is listed: the
 * covered faces are not drawn classic, the kept ones are (`classic` - the
 * roomlets with the covered faces left out). The carrier's ranges on covered
 * faces and its relief are drawn as for a covered room - with its group when
 * that is listed, in the room's window when it is not; of those, the ones
 * that also stand on a kept face are drawn pushed back, so the kept face
 * wins the pixels the two share and no part of a covered face is ever left
 * without its art. The carrier's art that lies whole on kept faces is left
 * out, and its art that reaches onto kept faces only is drawn with its group
 * pushed back, as for a room drawn classic; every other group's art answers
 * as for a room drawn classic. */
inline HDStandAnswer HDStandDrawFaces(bool holderListed, bool artListed, int state,
	bool carrier, int kind, int faces)
{
	if (state != HD_STAND_ROOM_PART)
		return HDStandDraw(holderListed, artListed, state == HD_STAND_ROOM_COVERED,
			carrier, kind == 1, kind == 0);

	HDStandAnswer out;
	const bool drawnAs = carrier && (kind == 2 || (faces & HD_STAND_FACES_COVERED) != 0);

	out.classic = holderListed;
	out.withArt = artListed && (!holderListed || drawnAs || kind != 1);
	out.inHolder = holderListed && drawnAs && !artListed;
	out.behind = holderListed && (out.withArt || out.inHolder) && kind != 2 &&
		(!drawnAs || (faces & HD_STAND_FACES_KEPT) != 0);
	out.front = false;
	return out;
}

/* The art of an object the level's art carries, on a classic room's faces.
 *
 * A slot the level's .MAP calls DUMMY draws nothing as an item: its object
 * is baked into the room art (hd_object.cpp), and the art inside the item's
 * box is the object. Where that art lies on the faces of a room drawn
 * classic, the classic path draws the object's mesh after the room, over
 * those faces, while the answers above leave the art out (lying on the
 * faces whole) or draw it behind them (reaching onto them) - the object
 * loses that part of its surface and the room's own texture shows there
 * instead. Measured at a glass block window whose item is such an object:
 * its reveal and sill lie in the sniper room behind it, 90 triangles whole
 * on that room's walls and ceiling, 23 reaching onto its floor and walls,
 * every one of them inside the item's box, and the room's faces there wear
 * a tile that is black or clear in every texel.
 *
 * So a stand range whose triangles all stand in the box of such an item is
 * drawn as the classic draws the object: in front of the room's faces
 * (`front`), in every frame that draws those faces and would otherwise
 * leave the range out or push it back. One surface, one drawer: the art in
 * front wins every pixel it shares with the face under it, by HD_STAND_PUSH
 * of its distance, and nothing else draws the object - the item is not
 * drawn. A range the frame already draws in place, a frame that does not
 * list the room, and a relief range in the box on no face (kind 2) answer
 * as above. Asked on the visibility path only (hd_level.cpp, DecideStand):
 * the old path answers as before. */
struct HDStandBox
{
	double lo[3];
	double hi[3];
};

/* An item's box in world units out of its object's first frame's bounds
 * (x0, x1, y0, y1, z0, z1, item-relative), its place and its yaw, turned
 * in quarter turns as the level places such objects: a quarter of
 * 0x4000 units of yaw, the nearest one. */
inline HDStandBox HDStandItemBox(const short bounds[6], long x, long y, long z,
	short yRot)
{
	const int quarter = ((unsigned short)(yRot + 0x2000) >> 14) & 3;
	const double x0 = bounds[0], x1 = bounds[1], z0 = bounds[4], z1 = bounds[5];
	double rx0 = x0, rx1 = x1, rz0 = z0, rz1 = z1;

	if (quarter == 1)
	{
		rx0 = z0; rx1 = z1; rz0 = -x1; rz1 = -x0;
	}
	else if (quarter == 2)
	{
		rx0 = -x1; rx1 = -x0; rz0 = -z1; rz1 = -z0;
	}
	else if (quarter == 3)
	{
		rx0 = -z1; rx1 = -z0; rz0 = x0; rz1 = x1;
	}

	HDStandBox box;

	box.lo[0] = (double)x + rx0;
	box.hi[0] = (double)x + rx1;
	box.lo[1] = (double)y + bounds[2];
	box.hi[1] = (double)y + bounds[3];
	box.lo[2] = (double)z + rz0;
	box.hi[2] = (double)z + rz1;
	return box;
}

// Does the box hold the triangle - its centroid, the box's faces included?
inline bool HDStandBoxHolds(const HDStandBox& box, const HDCoverTri& tri)
{
	for (int k = 0; k < 3; k++)
	{
		const double c = (double)tri.a[k] +
			((double)tri.e1[k] + (double)tri.e2[k]) / 3.0;

		if (c < box.lo[k] || c > box.hi[k])
			return false;
	}

	return true;
}

// The first of `count` boxes that holds the triangle, -1 for none.
inline int HDStandBoxFind(const HDStandBox* boxes, int count,
	const HDCoverTri& tri)
{
	for (int i = 0; i < count; i++)
		if (HDStandBoxHolds(boxes[i], tri))
			return i;

	return -1;
}

/* A range is the art of such objects when every one of its `triangles`
 * stands in one of their boxes (`inBoxes` of them); one triangle outside
 * is the room's own surface, which the answers above keep. */
inline bool HDStandRangeBaked(int inBoxes, int triangles)
{
	return triangles > 0 && inBoxes >= triangles;
}

/* The answer for a range of such art (`baked`): an answer of HDStandDraw or
 * HDStandDrawFaces turned to `front` where the room is listed, its classic
 * faces are drawn, the range stands on them (kind 0 or 1) and the answer
 * leaves it out or pushes it back; every other answer as it came. */
inline HDStandAnswer HDStandDrawBaked(HDStandAnswer answer, bool holderListed,
	int kind, bool baked)
{
	answer.front = false;

	if (!baked || !holderListed || !answer.classic || kind == 2)
		return answer;

	if ((answer.withArt || answer.inHolder) && !answer.behind)
		return answer;

	answer.withArt = false;
	answer.inHolder = false;
	answer.behind = false;
	answer.front = true;
	return answer;
}

/* A view * world matrix (row vectors, row-major) pulled towards the eye by
 * `push` of every point's distance: the eye is the origin of view space, so
 * the point moves along its own line of sight and lands on the same pixel,
 * nearer by that fraction. The renderer's HD_STAND_PUSH pushes back by the
 * same fraction through the projection. */
inline void HDStandFrontMatrix(float* m, float push)
{
	const float scale = 1.0f - push;

	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 3; col++)
			m[row * 4 + col] *= scale;
}

/* Which art draws the place of a static mesh (hd_render.cpp,
 * HDRenderStaticArt; hd_level.cpp, BuildStaticTables).
 *
 * A static mesh is drawn the classic way only where the art a frame draws at
 * its place, in a frame that lists its room, has nothing for it. Three
 * sources are asked, whatever the room is drawn as:
 *
 *   the room's own group, when it is drawn HD - drawn in every frame that
 *     lists the room;
 *   the carrier's stand ranges in a room drawn as its art, wholly or in
 *     part, that every frame listing the room draws, with the carrier's
 *     group or in the room's window (HDStaticStandSure) - RICH2's stairwell
 *     rooms 15, 16, 18 and 19 have their flights, railings and wall squares
 *     there;
 *   every other group drawn HD, with the art it draws outside its own room
 *     in every frame that lists it: its overhang of no intrusion piece and
 *     no stand range, and its stand ranges a frame draws whenever their
 *     group is listed (HDStaticStandWithGroup). Such a group is drawn only
 *     in a frame that lists it, so the classic static is left out only
 *     there - ANDREA2's palm crowns stand in the rooms above the palms, in
 *     the art of the trunk's room, and the two trunks of its room 124 in the
 *     art of both rooms beside it, 117 and 127. Its overhang alone, because
 *     inside its own room a group models that room, and a static of another
 *     room standing there is not its object; and not for a flat static
 *     (HDStaticFlat).
 *
 * How much is enough is asked two ways. The room's own group: the 256-unit
 * cells of the static's own box that hold a vertex of its art, a twentieth
 * of the box's cells (HD_STATIC_SHARE). Any other source: the static's own
 * mesh vertices that stand in a cell holding a vertex of that art, nine
 * twentieths of them (HD_STATIC_OTHER_NUM / _DEN). The box count cannot tell
 * another place's floor or wall from the object: in a small box a floor
 * alone reaches a twentieth - the carrier of ANDREA2's room 124 holds 4 of
 * the 36 cells of a palm trunk's box, the floor under it. Its own vertices
 * can: the carrier holds 3 of the trunk's 12, the two groups that model the
 * trunk 9 and 6, and a crown's 50 have 27 to 29 in the art of the groups
 * that model it. Measured over the fifteen files (RoomProbe -statictable),
 * the 152 other sources of the statics their room's own art does not hold
 * fall into 17 below 0.20, 36 from 0.20 to 0.444 (8 of 18), none above that
 * and below 0.50, and 99 from 0.50 up: nine twentieths sit in that gap.
 *
 * The answer: the room's own group or the carrier reaching it gives the
 * static to the art for good (HD_STATIC_ART); otherwise every other group
 * reaching it holds it, and the classic static is drawn only in a frame that
 * lists none of them (HD_STATIC_GROUPS); otherwise it stays classic
 * (HD_STATIC_CLASSIC). The intrusion pieces are not asked: a frame draws them
 * inside a window or not at all, and a static they hold could come out
 * cut. */
#define HD_STATIC_SHARE		20
#define HD_STATIC_OTHER_NUM	9
#define HD_STATIC_OTHER_DEN	20
#define HD_STATIC_CLASSIC	(-1)
#define HD_STATIC_ART		(-2)
#define HD_STATIC_GROUPS	(-3)
// The most other groups one static keeps, the most vertices first: twice the
// most that hold one static of the game (four, ANDREA2's room 154 crown).
#define HD_STATIC_MAX_GROUPS	8

inline bool HDStaticCellsHold(int cells, int boxCells)
{
	return (long long)cells * HD_STATIC_SHARE >= (long long)boxCells;
}

inline bool HDStaticOtherHolds(int found, int verts)
{
	return found > 0 && (long long)found * HD_STATIC_OTHER_DEN >=
		(long long)verts * HD_STATIC_OTHER_NUM;
}

/* A stand range drawn in every frame that lists its holder, the carrier's
 * group listed or not. */
inline bool HDStaticStandSure(int state, bool carrier, int kind, int faces)
{
	for (int artListed = 0; artListed < 2; artListed++)
	{
		const HDStandAnswer a = HDStandDrawFaces(true, artListed != 0, state,
			carrier, kind, faces);

		if (!a.withArt && !a.inHolder)
			return false;
	}

	return true;
}

/* A stand range drawn in every frame that lists its group, its holder listed
 * or not. */
inline bool HDStaticStandWithGroup(int state, bool carrier, int kind, int faces)
{
	for (int holderListed = 0; holderListed < 2; holderListed++)
	{
		const HDStandAnswer a = HDStandDrawFaces(holderListed != 0, true, state,
			carrier, kind, faces);

		if (!a.withArt && !a.inHolder)
			return false;
	}

	return true;
}

/* The answer: `own` the cells of the static's box that hold the room's own
 * group's art (0 for a room not drawn HD) of `boxCells`; `sure` the
 * static's vertices the carrier's art drawn in every frame that lists the
 * room holds, and `found[0..count)` those each other group's art holds, of
 * `verts`. `holds[k]` comes back 1 for every other group that holds the
 * static when the answer is HD_STATIC_GROUPS, 0 otherwise. */
inline int HDStaticAnswer(int own, int boxCells, int sure, const int* found,
	int count, int verts, unsigned char* holds)
{
	for (int k = 0; k < count; k++)
		holds[k] = 0;

	if (HDStaticCellsHold(own, boxCells) || HDStaticOtherHolds(sure, verts))
		return HD_STATIC_ART;

	int held = 0;

	for (int k = 0; k < count; k++)
		if (HDStaticOtherHolds(found[k], verts))
		{
			holds[k] = 1;
			held++;
		}

	return held ? HD_STATIC_GROUPS : HD_STATIC_CLASSIC;
}

/* A flat static: its box has no extent along one axis - a pane, a plank, a
 * square laid on a wall. Its vertices fall into the cells of whatever art it
 * lies on or stands in front of, so another group's art is not asked for it
 * (HDRenderStaticArt); the room's own group and the carrier, which draw the
 * very surface it lies on, are. Measured: RICH2's room 6 has two panes above
 * the consoles of room 11, whose art holds all twelve of their corners at
 * 256 units and shows no pane; RICH3's room 26 a triangle that stands clear
 * of the art that holds two of its three corners. */
inline bool HDStaticFlat(const float* mins, const float* maxs)
{
	for (int axis = 0; axis < 3; axis++)
		if (maxs[axis] - mins[axis] < 1.0f)
			return true;

	return false;
}

/* How far a shatterable static's box and a piece's must overlap, and how
 * close a pane's plane must stand to its piece, in units. Counted with
 * RoomProbe -smashcensus over the fifteen levels: a piece that belongs to a
 * static overlaps it by 38 units or more on every axis the static has an
 * extent on, and a pane's plane stands 0 or 1 unit off its glass (ANDREA1
 * rooms 198 and 201: the glass at 966..967, the pane at 968); the statics
 * of one row touch or overlap their neighbour's piece by 0 to 2 units at
 * the shared edge (JOBY2 room 57, RICH3 rooms 120, 169, 170 and 171). 16
 * lies inside that gap. */
#define HD_STATIC_PIECE_EDGE	16.0f

/* Whether a destructible piece of the art is the piece of a shatterable
 * static (slot 50 to 57), the static's box - local to its room - moved into
 * the piece's group frame by the difference of the two rooms' origins (`dx`,
 * `dz`: the static's room's origin less the piece's). Along an axis on which
 * the static's box has an extent the two overlap by more than
 * HD_STATIC_PIECE_EDGE; along one on which it has none - a pane, flat by
 * HDStaticFlat's measure - its plane stands within HD_STATIC_PIECE_EDGE of
 * the piece. Without the slack a pane's glass a unit behind it stays after
 * the shot; with a mere touch a static takes its neighbour's piece as well,
 * and the last of them to be written decides it (UpdateDestructibles). Any
 * group's piece: a piece is art standing at the static's place, and once
 * the static is shot neither may be drawn. */
inline bool HDStaticPiecePaired(const float* mins, const float* maxs, float dx,
	float dz, const float* pieceMins, const float* pieceMaxs)
{
	const float lo[3] = { mins[0] + dx, mins[1], mins[2] + dz };
	const float hi[3] = { maxs[0] + dx, maxs[1], maxs[2] + dz };

	for (int axis = 0; axis < 3; axis++)
	{
		const float from =
			lo[axis] > pieceMins[axis] ? lo[axis] : pieceMins[axis];
		const float to =
			hi[axis] < pieceMaxs[axis] ? hi[axis] : pieceMaxs[axis];

		if (maxs[axis] - mins[axis] < 1.0f)
		{
			if (from - to > HD_STATIC_PIECE_EDGE)
				return false;
		}
		else if (to - from <= HD_STATIC_PIECE_EDGE)
			return false;
	}

	return true;
}

/* The statics the engine takes out of the classic list, slots 50 to 59: the
 * savegame keeps Flags bit 0 for exactly these ten (savegame.cpp) and
 * ShatterSounds holds ten per level. A shot takes 50
 * to 57 (control.cpp), a blow of the gladiator or of the Roman god any of
 * the ten, and an XRAY_CONTROLLER with OCB 333 every static at its own
 * place (objects.cpp): ANDREA2's window onto the hall, slot 59 in rooms 65
 * and 132, goes that way, from the same heavy trigger as flip 8. The
 * fifteen levels hold 16 statics of slots 58 and 59: ANDREA2 2, RICH1 2,
 * RICH2 12. */
#define HD_STATIC_SHATTER_FIRST	50
#define HD_STATIC_SHATTER_LAST	59

inline bool HDStaticShatterSlot(int slot)
{
	return slot >= HD_STATIC_SHATTER_FIRST && slot <= HD_STATIC_SHATTER_LAST;
}

/* Whether a static may take a piece of group `pieceGroup`, `twin` being the
 * flipmap twin of the static's room (-1 for none). FlipMap puts the twin in
 * the room's place, never beside it, so the twin's art is the other state's.
 * ANDREA2's window stands in both rooms 65 and 132, each state with its own
 * grid at the same place: a static that took the other state's grid as well
 * would leave both grids to whichever static UpdateDestructibles writes
 * last. Any other group's piece may pair: art standing at the place. */
inline bool HDStaticPieceInState(int twin, int pieceGroup)
{
	return twin < 0 || pieceGroup != twin;
}

/* The room that holds the MESH_INFO list room `buildRoom` had when the level
 * was built, out of the group table `roomGroup` of `count` rooms: FlipMap
 * swaps two rooms' records, their lists with them, and HDFlipRoom swaps
 * the table with them, so the list stands where the group stands. The room
 * itself until a flip moves it; -1 when no room holds it. */
inline int HDStaticListRoom(const short* roomGroup, int count, int buildRoom)
{
	if (buildRoom < 0)
		return -1;

	if (buildRoom < count && roomGroup[buildRoom] == buildRoom)
		return buildRoom;

	for (int r = 0; r < count; r++)
		if (roomGroup[r] == buildRoom)
			return r;

	return -1;
}

/* Whether a frame draws a static the classic way, from its answer and from
 * whether the frame lists a group that holds it. */
inline bool HDStaticDrawClassic(int answer, bool holderListed)
{
	if (answer == HD_STATIC_ART)
		return false;

	if (answer == HD_STATIC_GROUPS)
		return !holderListed;

	return true;
}

/* The piece of a static other groups hold (hd_render.cpp, MakeStaticPiece):
 * the triangles of the holder that holds the most of its vertices that make
 * the object, drawn on their own in a frame that lists the static's room and
 * none of its holders, with that group's matrix and lighting, inside the
 * room's window - so the object is the art at every angle and not the
 * classic static whenever the list happens to leave its holders out.
 *
 * The build notes every triangle of a group's overhang of no intrusion piece
 * and of its stand ranges whose centroid lies within the reach of the box of
 * a static of another room: 1024 units, one floor square, past the 530 units
 * the leaf cards of ANDREA2's palm crowns reach beyond the classic crown's
 * box (RoomProbe's build, the cards' own corners). A triangle belongs to a
 * static's box within the slack: 64 units, twice the 32 by which the art of
 * a palm trunk stands past the classic trunk's box, so the side triangles of
 * a trunk whose centroids lie just outside it are the trunk's. A piece is
 * the object alone only while no run of triangles it reaches into goes on,
 * outside every static's box, past 768 units - three quarters of the reach,
 * past the leaf cards' 530 - so a static the art models as part of a larger
 * structure keeps the classic fallback. */
#define HD_STATIC_PIECE_REACH	1024.0f
#define HD_STATIC_PIECE_SLACK	64.0f
#define HD_STATIC_PIECE_OPEN	768.0f

// Where a frame takes a static from (HDStaticDrawSource).
#define HD_STATIC_FROM_CLASSIC	0
#define HD_STATIC_FROM_ART	1
#define HD_STATIC_FROM_PIECE	2

/* The one source per frame of a static: its answer, whether the frame lists a
 * group that holds it, and whether its piece is drawn in this frame - made,
 * and its room's window and its group's room in hand. A static the art holds
 * for good, or one a listed holder draws, is the art; one held by groups none
 * of which is listed is its piece where that is drawn, and the classic static
 * otherwise, so the object is never lost; every other static is classic. */
inline int HDStaticDrawSource(int answer, bool holderListed, bool pieceDrawn)
{
	if (answer == HD_STATIC_ART)
		return HD_STATIC_FROM_ART;

	if (answer == HD_STATIC_GROUPS)
		return holderListed ? HD_STATIC_FROM_ART :
			pieceDrawn ? HD_STATIC_FROM_PIECE : HD_STATIC_FROM_CLASSIC;

	return HD_STATIC_FROM_CLASSIC;
}

/* The art copies (hd_artcopy.h, the rule; HDRenderLevelBegin puts them into
 * the .TRG as it is read): what the engine hands the build and what it
 * reads back, plain data, so that a file that cannot read the .TRG's
 * header can say what was copied.
 *
 * One static mesh of the level: its room and its index in the room's list,
 * its slot, MESH_INFO's place and y_rot, its own mesh's vertices where they
 * stand (world units, `vertCount` of them from `firstVert` in the list
 * handed beside) and its box as hd_level.cpp's StaticBox makes it, moved to
 * world x and z. */
struct HDArtCopyStatic
{
	int room, mesh, slot;
	int x, y, z;
	int rot;
	int firstVert, vertCount;
	float mins[3], maxs[3];
};

// How far round the target's box the group's own light is averaged (the
// tools' light measure, HDArtCopy::around).
#define HD_ARTCOPY_AROUND	512.0f

// One copy: the target and the donor (indices into the statics), the group,
// the motion and what was taken.
struct HDArtCopy
{
	int target, donor, group;
	int dx, dy, dz;		// the donor's place to the target's, world units
	int turn;			// quarter turns, 0 to 3
	int inside;			// the group's triangles wholly inside the grown box
	int crossing;		// with a corner inside it and one outside
	int walls;			// of the rest, lying whole on a classic face
	int standing;		// of the rest, whose image lies on art already there
	int tris;			// copied
	int before, after;	// the target's points on the group's art
	int verts;			// of its mesh vertices
	int firstTri;		// into HDArtCopyPlan::tris, `tris` of them
	int faces;			// classic faces beside the target given their art
	int faceTris;		// of `tris`, the faces' (HDArtCopyMake)
	/* The means of the two colours of the copied corners, and of the
	 * group's own corners round the target's place (its box grown by
	 * HD_ARTCOPY_AROUND), for the light. */
	float light[2][3];
	float around[2][3];
	int aroundCorners;
};

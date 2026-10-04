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
 *   - whether one group's art covers the room's faces completely
 *     (HDCoverFaces and HDCoverRoomCovered) - then the room is drawn as that
 *     group's art, its carrier; otherwise it stays classic and the art on
 *     its faces is left out while it is listed;
 *   - which of them a frame draws (HDStandDraw).
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
/* The spacing of the samples a face is tested at: a quarter of a sector, so
 * a 1024-unit wall is asked at a few hundred points and the whole game's
 * classic rooms take TrgProbe about six seconds. */
#define HD_COVER_STEP		64.0
/* Barycentric slack of the segment test, so that the seam between two art
 * triangles is not a gap. */
#define HD_COVER_SLACK		1e-3
/* The overlap an art triangle needs with a face, in units of the face's
 * plane, before it counts as standing on it - so that a triangle that only
 * touches a face's edge belongs to the face next door. */
#define HD_COVER_TOUCH		1e-3

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

/* The threshold: a room is drawn as the art only when it has a face and one
 * group's art covers every sample of every face. One bare sample keeps it
 * classic - a strict rule, because a room drawn as art that does not cover
 * it shows the void where the art stops (RICH2's room 16 has no art under
 * its 14 ceiling faces).
 *
 * One group and not the union of several, because two groups that both
 * cover a face are two copies of one surface: ANDREA2's group 127 carries a
 * second copy of 36 of room 124's 46 faces beside group 121's, and the two
 * drawn together fight as the classic faces did. So the covering group - the
 * carrier - is the one the room is drawn as, and the stand art of every other
 * group is left out while the room is listed. In the game the two rules give
 * the same verdicts: both covered rooms have a single group that covers
 * them alone (TrgProbe -classicart, `art=... alone=covered`). */
inline bool HDCoverRoomCovered(int faces, int facesCovered)
{
	return faces > 0 && facesCovered >= faces;
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
 *
 * While the room is listed its place comes from exactly one source: a
 * covered room is drawn as its carrier's art - all of the carrier's stand
 * ranges, the relief standing in the room's box on no face included, with
 * the carrier's group when that is listed and in the room's window when it
 * is not - and every other group's art that lies on its faces whole is left
 * out; a room no group covers is drawn classic and all the art that lies on
 * its faces whole is left out. Art that only grazes the room's faces, and
 * every other group's art in its box, is drawn with its group whatever the
 * room does - leaving it out would cut it where it leaves the faces. Where
 * such art reaches onto the faces of a listed room that stays classic, the
 * classic faces and the art lie in one place (RICH2's room 16 and group 14's
 * stairwell walls), so the art is drawn pushed back: the classic face wins
 * every pixel the two share, and the art beyond the faces stays. A room
 * that is not listed draws nothing of its own, and the art on its faces is
 * drawn with its group as everywhere else. */
struct HDStandAnswer
{
	bool classic;
	bool withArt;
	bool inHolder;
	bool behind;
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
	return out;
}

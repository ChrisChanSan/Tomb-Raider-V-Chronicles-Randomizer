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

/* The pieces of a broken static in HD: the rules, as pure functions.
 *
 * A shatterable static (slots 50 to 59) breaks into pieces in
 * ShatterObject (game\debris.cpp): every face of its classic mesh, up to
 * the caller's count, becomes one flying triangle with the face's classic
 * texture. In HD the static itself is drawn by a destructible part of the
 * room art (hd_cover.h, HDStaticPiecePaired), so the classic pieces flew
 * off an HD window in the classic page's colours. The rule here gives each
 * piece the look of the art it broke out of, and nothing else of it:
 *
 *   - a triangle of the static's HD part goes with the classic face it
 *     lies on: the face nearest to its centroid (HDDebrisFaceOf). No reach
 *     is set - the part is the static's own art by its pairing, and every
 *     one of its triangles is at some face;
 *   - faces that stand at one place, corner for corner (a pane's two
 *     sides), make one place: its triangles are dealt among those faces by
 *     a hash of the triangle's number, so each piece of the place carries
 *     a share and none is drawn twice (HDDebrisDeal);
 *   - a face that became a piece flies its triangles; a face the break's
 *     count left out takes them with it, as the classic face goes;
 *   - a piece whose face holds no triangle of the art draws nothing in HD:
 *     the art at that place went to the neighbouring faces, and an HD
 *     break is the art that the break hides;
 *   - the piece keeps its place, its flight, its life and its sound - the
 *     classic debris array is not touched - and the triangles ride on its
 *     matrix, offset from its birth place as they stood in the art.
 *
 * The blend is the part's own, read as the room draw reads it
 * (hd_render.cpp): the opaque run solid, the alpha run of material 14
 * glass at param[1] (MaterialBlend), any other alpha run cut at 0.5, the
 * additive run a glow at weight 1 (MaterialAddWeight compares the whole
 * material number, and a destructible part's carries bit 15).
 *
 * Engine-free: the engine (hd_debris.cpp) and RoomProbe -debris use the
 * same functions, and ModernTest checks them. */

#include <math.h>

/* One classic face of the broken mesh in world units, as ShatterObject
 * turns it: three or four corners; none for a face whose corner lies past
 * the 256 vertices the break reads, which no triangle can take. */
struct HDDebrisFace
{
	float corner[4][3];
	int corners;
};

// The part's run a triangle lies in, as the room draw's passes name them.
enum
{
	HD_DEBRIS_SOLID = 0,	// the opaque run
	HD_DEBRIS_CUTOUT,	// an alpha run that is no glass: cut at 0.5
	HD_DEBRIS_GLASS,	// material 14's alpha run: see-through
	HD_DEBRIS_GLOW,		// the additive run
	HD_DEBRIS_KINDS
};

/* The distance from `p` to the triangle a, b, c: the closest point on the
 * triangle by its Voronoi regions (vertex, edge, face), the textbook
 * construction, then the length. */
inline float HDDebrisPointTriangle(const float* p, const float* a,
	const float* b, const float* c)
{
	float ab[3], ac[3], ap[3], q[3];

	for (int i = 0; i < 3; i++)
	{
		ab[i] = b[i] - a[i];
		ac[i] = c[i] - a[i];
		ap[i] = p[i] - a[i];
	}

	const float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
	const float d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
	float s = 0.0f, t = 0.0f;

	if (d1 <= 0.0f && d2 <= 0.0f)
	{
		s = 0.0f;
		t = 0.0f;
	}
	else
	{
		float bp[3], cp[3];

		for (int i = 0; i < 3; i++)
		{
			bp[i] = p[i] - b[i];
			cp[i] = p[i] - c[i];
		}

		const float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
		const float d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
		const float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
		const float d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
		const float vc = d1 * d4 - d3 * d2;
		const float vb = d5 * d2 - d1 * d6;
		const float va = d3 * d6 - d5 * d4;

		if (d3 >= 0.0f && d4 <= d3)
			s = 1.0f;
		else if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
			s = d1 / (d1 - d3);
		else if (d6 >= 0.0f && d5 <= d6)
			t = 1.0f;
		else if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
			t = d2 / (d2 - d6);
		else if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
		{
			const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));

			s = 1.0f - w;
			t = w;
		}
		else
		{
			const float den = 1.0f / (va + vb + vc);

			s = vb * den;
			t = vc * den;
		}
	}

	for (int i = 0; i < 3; i++)
		q[i] = a[i] + s * ab[i] + t * ac[i] - p[i];

	return sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

/* A face's distance to `p`: a quad as the two triangles 0 1 2 and 0 2 3,
 * the way the classic draw splits it; a face with no corners is never
 * nearest. */
inline float HDDebrisFaceDistance(const HDDebrisFace& face, const float* p)
{
	if (face.corners < 3)
		return 3.0e38f;

	float d = HDDebrisPointTriangle(p, face.corner[0], face.corner[1],
		face.corner[2]);

	if (face.corners == 4)
	{
		const float e = HDDebrisPointTriangle(p, face.corner[0],
			face.corner[2], face.corner[3]);

		if (e < d)
			d = e;
	}

	return d;
}

/* Whether two faces stand at one place: as many corners, and every corner
 * of one is a corner of the other, in any order. */
inline bool HDDebrisSamePlace(const HDDebrisFace& a, const HDDebrisFace& b)
{
	if (a.corners != b.corners || a.corners < 3)
		return false;

	for (int i = 0; i < a.corners; i++)
	{
		bool found = false;

		for (int k = 0; k < b.corners && !found; k++)
			found = a.corner[i][0] == b.corner[k][0] &&
				a.corner[i][1] == b.corner[k][1] &&
				a.corner[i][2] == b.corner[k][2];

		if (!found)
			return false;
	}

	return true;
}

/* A well mixed 32-bit hash: the multiply-xorshift finaliser of a 32-bit
 * integer hash, a textbook construction, the one HDCardHash ends in
 * (hd_splash_math.h). The deal below is a hash of the triangle's number
 * and never a draw of the engine's random generator, which the break must
 * leave as it was. */
inline unsigned int HDDebrisHash(unsigned int x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// Which of a place's `count` faces takes triangle number `triangle`.
inline int HDDebrisDeal(unsigned int triangle, int count)
{
	return count > 1 ? (int)(HDDebrisHash(triangle) % (unsigned int)count) :
		0;
}

/* A face's box, mins then maxs, for the cut below; an empty box (mins over
 * maxs) for a face with no corners. */
inline void HDDebrisFaceBox(const HDDebrisFace& face, float* box)
{
	for (int a = 0; a < 3; a++)
	{
		box[a] = 3.0e38f;
		box[a + 3] = -3.0e38f;
	}

	for (int k = 0; k < face.corners; k++)
		for (int a = 0; a < 3; a++)
		{
			if (face.corner[k][a] < box[a])
				box[a] = face.corner[k][a];

			if (face.corner[k][a] > box[a + 3])
				box[a + 3] = face.corner[k][a];
		}
}

/* The distance from `p` to a box: never more than the distance to anything
 * inside it, so a face whose box is no nearer than the best face so far
 * cannot be nearer either and need not be measured. */
inline float HDDebrisBoxDistance(const float* box, const float* p)
{
	float d = 0.0f;

	for (int a = 0; a < 3; a++)
	{
		const float e = p[a] < box[a] ? box[a] - p[a] :
			p[a] > box[a + 3] ? p[a] - box[a + 3] : 0.0f;

		d += e * e;
	}

	return sqrtf(d);
}

/* Every face's place: the number of the first face that stands at the same
 * place (HDDebrisSamePlace), itself for a face alone there. */
inline void HDDebrisPlaces(const HDDebrisFace* faces, int count, int* place)
{
	for (int i = 0; i < count; i++)
	{
		place[i] = i;

		for (int k = 0; k < i; k++)
			if (place[k] == k && HDDebrisSamePlace(faces[i], faces[k]))
			{
				place[i] = k;
				break;
			}
	}
}

/* The face a triangle of the art goes with, its centroid `c`, its number
 * in the part `triangle`: the nearest face (the first of equal distances,
 * in the classic order), then the place's share dealt by the hash. -1 when
 * no face has corners. `boxes` (HDDebrisFaceBox, six floats a face) and
 * `place` (HDDebrisPlaces) may be null and are then worked out here; with
 * the boxes a face whose box is no nearer than the best so far is skipped,
 * which cannot change the answer - only its cost. */
inline int HDDebrisFaceOf(const HDDebrisFace* faces, int count,
	const float* c, unsigned int triangle, const float* boxes = 0,
	const int* place = 0)
{
	int best = -1;
	float nearest = 3.0e38f;

	for (int i = 0; i < count; i++)
	{
		if (boxes && faces[i].corners >= 3 &&
			HDDebrisBoxDistance(boxes + i * 6, c) >= nearest)
			continue;

		const float d = HDDebrisFaceDistance(faces[i], c);

		if (d < nearest)
		{
			nearest = d;
			best = i;
		}
	}

	if (best < 0)
		return -1;

	// The faces at the best face's place, in the classic order.
	int members = 0;

	for (int i = 0; i < count; i++)
		if (place ? place[i] == place[best] :
			i == best || HDDebrisSamePlace(faces[i], faces[best]))
			members++;

	int pick = HDDebrisDeal(triangle, members);

	for (int i = 0; i < count; i++)
		if (place ? place[i] == place[best] :
			i == best || HDDebrisSamePlace(faces[i], faces[best]))
		{
			if (!pick)
				return i;

			pick--;
		}

	return best;
}

/* The kind of a triangle of a destructible part, out of its run (`pass`
 * 0 opaque, 1 alpha, 2 additive) and its material, whose low byte is the
 * base material under the destructible bits. */
inline int HDDebrisKindOf(unsigned int material, int pass)
{
	if (pass == 2)
		return HD_DEBRIS_GLOW;

	if (pass == 1)
		return (material & 0xFFu) == 14u ? HD_DEBRIS_GLASS : HD_DEBRIS_CUTOUT;

	return HD_DEBRIS_SOLID;
}

/* The see-through weight of a glass triangle - material 14's param[1], 1
 * when it lies outside (0, 1], as MaterialBlend reads it - and 1 for every
 * other kind, whose alpha output nothing looks at. */
inline float HDDebrisSeeThrough(int kind, const float* param)
{
	if (kind != HD_DEBRIS_GLASS)
		return 1.0f;

	const float opacity = param[1];

	return opacity > 0.0f && opacity <= 1.0f ? opacity : 1.0f;
}

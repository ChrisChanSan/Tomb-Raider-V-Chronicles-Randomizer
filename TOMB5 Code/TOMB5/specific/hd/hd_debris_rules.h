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
 * Engine-free: the engine (hd_debris.h, whose definitions hd_level.cpp
 * holds) and RoomProbe -debris use the same functions, and ModernTest
 * checks them. */

#include <math.h>

/* One classic face of the broken mesh in world units, as ShatterObject
 * turns it: three or four corners; none for a face whose corner lies past
 * the 256 vertices the break reads, which no triangle can take. */
struct HDDebrisFace
{
	float corner[4][3];
	int corners;
};

/* Face `f` of a breaking mesh in the order ShatterObject walks them: the
 * `ngt3` triangles of `gt3` (five shorts each, the corners first), then
 * the quads of `gt4` (six each). Every corner is the turned vertex it
 * names - three shorts of `rotated`, offsets from the static's place -
 * at the place x, y, z. A corner past the `vertices` turned, or past the
 * 256 the break turns at most, leaves the face with no corner. */
inline void HDDebrisMeshFace(const short* gt3, int ngt3, const short* gt4,
	int f, const short* rotated, int vertices, long x, long y, long z,
	HDDebrisFace* out)
{
	const bool tri = f < ngt3;
	const short* face = tri ? gt3 + f * 5 : gt4 + (f - ngt3) * 6;

	out->corners = tri ? 3 : 4;

	for (int k = 0; k < out->corners; k++)
	{
		const int v = (unsigned short)face[k];

		if (v >= vertices || v >= 256)
		{
			out->corners = 0;
			return;
		}

		out->corner[k][0] = (float)(rotated[v * 3] + x);
		out->corner[k][1] = (float)(rotated[v * 3 + 1] + y);
		out->corner[k][2] = (float)(rotated[v * 3 + 2] + z);
	}
}

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

/* ---------------------------------------------------- the glass's look --
 *
 * The art's glass and the classic's. The classic draws a pane as a face it
 * adds, ONE / ONE (a room face of draw type 2, a mesh face with effects bit
 * 0, polyinsert.cpp), and two draw calls of the room art are drawn the same
 * way (hd_render.cpp): the glass half of a see-through part whose page
 * paints its frame on clear texels (GlassSplits) and the additive run of a
 * material 14 part whose page has no clear texel (AddGlass). Both pages are
 * told apart by the two numbers below, read here for the room draw and for
 * the pieces of a broken static alike.
 *
 * Their added light is the art's own colour. ANDREA2's window panes add
 * about 16, 17, 16 levels - grey, the art's warm vertex light on its blue
 * texels - where the classic's panes add 11.4, 27.5, 38.1, blue; RICH2's
 * bridge adds 18, 16, 16 where the classic's adds green (measured over
 * the fifteen levels' classic files and their art). So such a draw call
 * takes the hue of the classic glass at its place: every classic face the
 * classic adds whose centre lies inside the box of one of its triangles,
 * grown by HD_GLASS_PLACE_MARGIN; their added colour - the texel times the
 * face's light - weighted by their areas, over its own luma, is a direction
 * whose luma is 1 (HDGlassTintOf). The room shader puts the luma of the
 * art's add on that direction (hd_room.hlsl, gTint), so the strength stays
 * the art's and the hue becomes the classic's. Where the classic adds
 * nothing at the place - the lanterns, the glass on a table - the art keeps
 * its colour.
 *
 * And the pieces of a broken window show. The classic's flying panes add
 * their page at the static's light, a band of 13.1 to 46.0 levels from the
 * tenth to the nine tenths of the texels at ANDREA2's window, its middle
 * 20.4; the art's panes add about 16 to 18, near that middle. A piece of
 * glass takes the pane's hue and is raised by the band's top over its
 * middle (HDGlassShardGain), so that it stands near the top of the
 * classic's band and not in its middle. */
#define HD_GLASS_CLEAR_BELOW	16	// a clear texel, of 255
#define HD_GLASS_FRAME_PERMILLE	500	// more than half the page below 0.5
#define HD_GLASS_PLACE_MARGIN	32.0f	// units, the census's margin

/* A page whose frame is painted on clear texels: a texel under 16 of 255
 * at all, and more than half the page below the alpha test's reference -
 * the whole page's numbers (HDPageCacheSlotAlphaMin and
 * HDPageCacheSlotCutoutPermille). */
inline bool HDGlassFramePage(int alphaMin, int cutoutPermille)
{
	return alphaMin < HD_GLASS_CLEAR_BELOW &&
		cutoutPermille > HD_GLASS_FRAME_PERMILLE;
}

// A page that is glass in every texel: no texel under 16 of 255.
inline bool HDGlassWholePage(int alphaMin)
{
	return alphaMin >= HD_GLASS_CLEAR_BELOW;
}

// The luma of a colour, the Rec. 601 weights every band here is read in.
inline float HDGlassLuma(const float* rgb)
{
	return 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
}

/* The classic's light split (drawroom.cpp, polyinsert.cpp): a colour up to
 * 128 doubled into the diffuse, above it the diffuse pinned at 255 and the
 * rest specular, which the classic switches off for what it adds. */
inline float HDGlassSplit(float v)
{
	return v <= 128.0f ? v * 2.0f : 255.0f;
}

/* One classic face the classic adds, for the hue at its place: its centre
 * in world units, its area in square units and its added colour - the mean
 * over its texels of the texel times the face's light, 0 to 255. */
struct HDGlassFace
{
	float centre[3];
	float area;
	float colour[3];
};

/* Whether a classic face's centre lies at the place of a triangle of the
 * art: inside its box (mins then maxs) grown by HD_GLASS_PLACE_MARGIN. */
inline bool HDGlassAtPlace(const float* box, const float* centre)
{
	for (int a = 0; a < 3; a++)
		if (centre[a] < box[a] - HD_GLASS_PLACE_MARGIN ||
			centre[a] > box[a + 3] + HD_GLASS_PLACE_MARGIN)
			return false;

	return true;
}

/* The hue out of the faces' added colours summed by their areas (`sum`):
 * the colour over its own luma, a direction whose luma is 1, and w 1. All
 * zero where the faces add nothing - no face, or every texel black. */
inline void HDGlassTintOf(const float* sum, float* out)
{
	const float luma = HDGlassLuma(sum);

	for (int i = 0; i < 4; i++)
		out[i] = 0.0f;

	if (!(luma > 1.0e-6f))
		return;

	for (int i = 0; i < 3; i++)
		out[i] = sum[i] / luma;

	out[3] = 1.0f;
}

/* Whether the texel (tx, ty) of a page `w` by `h` lies on a face whose
 * `corners` corners stand at `uv` (0 to 1): its centre inside the polygon
 * or on an edge, every cross product of one sign or zero. */
inline bool HDGlassTexelIn(const float (*uv)[2], int corners, int tx,
	int ty, int w, int h)
{
	const float px = ((float)tx + 0.5f) / (float)w;
	const float py = ((float)ty + 0.5f) / (float)h;
	int sign = 0;

	for (int k = 0; k < corners; k++)
	{
		const float* a = uv[k];
		const float* b = uv[(k + 1) % corners];
		const float cr = (b[0] - a[0]) * (py - a[1]) -
			(px - a[0]) * (b[1] - a[1]);
		const int s = cr > 0.0f ? 1 : cr < 0.0f ? -1 : 0;

		if (s && sign && s != sign)
			return false;

		if (s)
			sign = s;
	}

	return true;
}

/* Every texel of a face in a page `w` by `h`, handed to `visit` as its
 * column and row: the face's box in texels walked, each texel tested
 * (HDGlassTexelIn). Returns the texels visited. */
template <class Visit>
inline long HDGlassFaceTexels(const float (*uv)[2], int corners, int w,
	int h, Visit visit)
{
	float u0 = 1.0f, u1 = 0.0f, v0 = 1.0f, v1 = 0.0f;
	long n = 0;

	for (int k = 0; k < corners; k++)
	{
		u0 = uv[k][0] < u0 ? uv[k][0] : u0;
		u1 = uv[k][0] > u1 ? uv[k][0] : u1;
		v0 = uv[k][1] < v0 ? uv[k][1] : v0;
		v1 = uv[k][1] > v1 ? uv[k][1] : v1;
	}

	for (int ty = (int)(v0 * (float)h); ty <= (int)(v1 * (float)h) &&
		ty < h; ty++)
		for (int tx = (int)(u0 * (float)w); tx <= (int)(u1 * (float)w) &&
			tx < w; tx++)
			if (tx >= 0 && ty >= 0 &&
				HDGlassTexelIn(uv, corners, tx, ty, w, h))
			{
				visit(tx, ty);
				n++;
			}

	return n;
}

/* A classic face's added colour on its page: the mean over the texels it
 * covers (HDGlassFaceTexels) of the texel times `light` (0 to 255 a
 * channel, the classic's split light), out of `page` - four bytes a texel,
 * blue first, `pitch` bytes a row, `w` by `h`. Each texel's added luma,
 * in whole levels, is counted into `hist` (256 bins) where one is given.
 * Returns the texels; `colour` is zero for a face that covers none. */
inline long HDGlassFaceColour(const unsigned char* page, int pitch, int w,
	int h, const float (*uv)[2], int corners, const float* light,
	float* colour, unsigned int* hist)
{
	double sum[3] = { 0.0, 0.0, 0.0 };
	const long n = HDGlassFaceTexels(uv, corners, w, h,
		[&](int tx, int ty)
		{
			const unsigned char* t = page + (long)ty * pitch + tx * 4;
			const float add[3] =
			{
				(float)t[2] * light[0] / 255.0f,
				(float)t[1] * light[1] / 255.0f,
				(float)t[0] * light[2] / 255.0f
			};

			for (int i = 0; i < 3; i++)
				sum[i] += add[i];

			if (hist)
			{
				const int level = (int)HDGlassLuma(add);

				hist[level < 0 ? 0 : level > 255 ? 255 : level]++;
			}
		});

	for (int i = 0; i < 3; i++)
		colour[i] = n > 0 ? (float)(sum[i] / (double)n) : 0.0f;

	return n;
}

// A face's area: its corners as a fan of triangles from the first.
inline float HDGlassFaceArea(const float (*corner)[3], int corners)
{
	float area = 0.0f;

	for (int k = 1; k + 1 < corners; k++)
	{
		float e[2][3], c[3];

		for (int a = 0; a < 3; a++)
		{
			e[0][a] = corner[k][a] - corner[0][a];
			e[1][a] = corner[k + 1][a] - corner[0][a];
		}

		c[0] = e[0][1] * e[1][2] - e[0][2] * e[1][1];
		c[1] = e[0][2] * e[1][0] - e[0][0] * e[1][2];
		c[2] = e[0][0] * e[1][1] - e[0][1] * e[1][0];
		area += 0.5f * sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
	}

	return area;
}

/* The level of a histogram of added luma, one bin a whole level of 255,
 * at the share `q` of the counted texels: the bin that holds the texel at
 * place q times (count - 1) in their sorted order. 0 for an empty
 * histogram. */
inline float HDGlassBand(const unsigned int* hist, int bins, float q)
{
	double total = 0.0;

	for (int i = 0; i < bins; i++)
		total += hist[i];

	if (total <= 0.0)
		return 0.0f;

	const double at = floor((double)q * (total - 1.0));
	double seen = 0.0;

	for (int i = 0; i < bins; i++)
	{
		seen += hist[i];

		if (seen > at)
			return (float)i;
	}

	return (float)(bins - 1);
}

/* How much a piece of glass is raised: the classic band's top over its
 * middle - the nine tenths and the half of the added luma over the classic
 * glass's texels (HDGlassBand) - and 1 where the band has no middle or no
 * top above it. */
inline float HDGlassShardGain(float half, float top)
{
	return half > 0.0f && top > half ? top / half : 1.0f;
}

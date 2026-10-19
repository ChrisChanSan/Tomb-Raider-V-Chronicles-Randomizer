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

#include <limits.h>
#include <math.h>

#include <vector>

/* The marble floors' places (hd_render.cpp, "the marble floors"): which
 * floor heights a frame mirrors and how strongly. Nothing here draws or
 * reads the device, so ModernTest checks the rules without one.
 *
 * A plane's score is the share of the screen its faces show, seen past
 * what stands in front of them where the last frame counted it (an
 * occlusion count of its faces against the frame's depth), else the share
 * they cover with nothing in front (HDMarbleCoverage). A floor hidden
 * behind a wall covers much of the screen and shows none of it.
 *
 * The places. At most `cap` planes are mirrored, each in a place that
 * holds its plane and the strength its mirror is laid at (its gain, 0 to
 * 1). A plane in a place keeps it while the empty places take every plane
 * waiting outside, or while it scores at least HD_MARBLE_HOLD of the best
 * plane outside, and its gain then rises by a step a frame to 1; a plane
 * that does neither fades out by the same step and leaves the place at 0,
 * which the best plane outside then takes, rising from one step. With
 * places for every plane a frame shows (HD_MARBLE_PLANES_MOST) no plane
 * fades out while it shows, and a plane coming into view fades in once.
 * A plane that shows nothing leaves at once - there is nothing to fade.
 * Places all empty after that take the best planes at full strength: the
 * first frame of a level, or of a view none of whose last planes shows
 * any more, shows its mirror whole. */

// One place of the frame's mirror: its floor height, INT_MIN when empty.
struct HDMarbleSlot
{
	int y;
	float gain;
};

/* A plane holds its place while it scores this much of the best plane
 * outside the places. */
#define HD_MARBLE_HOLD	0.5f

/* The share of the screen a count of pixels is, in HDMarbleCoverage's
 * units (the screen's -1..1 square, which is 4 whole). */
inline float HDMarbleSeenCover(int pixels, int width, int height)
{
	return width > 0 && height > 0 ?
		4.0f * (float)pixels / ((float)width * (float)height) : 0.0f;
}

/* A plane's score: the share of the screen its last count saw, `pixels`
 * of a `width` by `height` view asked `age` frames ago, while that count
 * is at most `oldest` frames old; else its `cover`, with nothing in front.
 * `pixels` under 0 is no count. A count the GPU has not answered in that
 * time says nothing of the view the frame now has. */
inline float HDMarbleScore(int pixels, int age, int oldest, float cover,
	int width, int height)
{
	return pixels >= 0 && age >= 0 && age <= oldest ?
		HDMarbleSeenCover(pixels, width, height) : cover;
}

/* The gain a place moves by in a frame: the frame's time `ms` over the
 * time a whole fade takes, `whole`; `ms` is taken at most `longest`, so a
 * frame after a halt (a menu, a load) moves a fade by no more than one at
 * that pace. At least 0, at most 1; 1 where `whole` is 0 or less. */
inline float HDMarbleFadeStep(double ms, double whole, double longest)
{
	if (whole <= 0.0)
		return 1.0f;

	const double t = ms < 0.0 ? 0.0 : ms > longest ? longest : ms;
	const double s = t / whole;

	return s > 1.0 ? 1.0f : (float)s;
}

/* The least a plane shows to be mirrored: a score (HDMarbleScore's share
 * of the screen, the -1..1 square being 4) under `least` counts as nothing
 * shown, so the plane takes no place and costs no mirrored draw. Planes
 * showing a sliver - the edge of a floor beside a door, a step's bevel seen
 * across a hall - each cost a whole mirrored draw of the rooms for a few
 * pixels of a faint reflection. HD_MARBLE_LEAST is a thousandth of the
 * screen: in RICH2's lobby and gallery, where a view shows up to 21
 * planes, it leaves 7 to 8 of them, takes the mirrored draw calls from
 * 2,414 to 879, and changes 3,643 to 6,351 of 3,686,400 pixels by more
 * than 8 of 255 (RoomProbe -marbleleast at 2,560 x 1,440). */
#define HD_MARBLE_LEAST	0.004f

inline float HDMarbleShown(float score, float least)
{
	return score >= least ? score : 0.0f;
}

// The score of height y among `count` (y, score) pairs; 0 when absent.
inline float HDMarbleScoreOf(const int* ys, const float* scores, int count,
	int y)
{
	for (int i = 0; i < count; i++)
		if (ys[i] == y)
			return scores[i];

	return 0.0f;
}

/* One frame's step of the places. `ys` and `scores` are the frame's planes
 * (any order), `step` the gain a frame (HDMarbleFadeStep; 1 fills and
 * empties at once), `hold` HD_MARBLE_HOLD. Returns how many places hold a
 * plane. */
inline int HDMarbleSlotsStep(HDMarbleSlot* slots, int cap, const int* ys,
	const float* scores, int count, float step, float hold)
{
	// What shows nothing leaves at once.
	for (int s = 0; s < cap; s++)
		if (slots[s].y != INT_MIN &&
			HDMarbleScoreOf(ys, scores, count, slots[s].y) <= 0.0f)
		{
			slots[s].y = INT_MIN;
			slots[s].gain = 0.0f;
		}

	bool fresh = true;

	for (int s = 0; s < cap; s++)
		fresh = fresh && slots[s].y == INT_MIN;

	// The best plane outside the places, before any place changes.
	const auto placed = [&](int y)
	{
		for (int s = 0; s < cap; s++)
			if (slots[s].y == y)
				return true;

		return false;
	};
	float best = 0.0f;
	int empty = 0, waiting = 0;

	for (int i = 0; i < count; i++)
		if (!placed(ys[i]) && scores[i] > 0.0f)
		{
			waiting++;
			best = scores[i] > best ? scores[i] : best;
		}

	for (int s = 0; s < cap; s++)
		empty += slots[s].y == INT_MIN ? 1 : 0;

	/* Where the empty places take every plane waiting, no plane need give
	 * its place up: none fades out while it shows. */
	const bool room = waiting <= empty;

	for (int s = 0; s < cap; s++)
	{
		if (slots[s].y == INT_MIN)
			continue;

		const float own = HDMarbleScoreOf(ys, scores, count, slots[s].y);

		if (room || own >= hold * best)
			slots[s].gain = slots[s].gain + step >= 1.0f ? 1.0f :
				slots[s].gain + step;
		else if (slots[s].gain - step <= 0.0f)
		{
			slots[s].y = INT_MIN;
			slots[s].gain = 0.0f;
		}
		else
			slots[s].gain -= step;
	}

	/* Empty places take the best planes outside, highest score first, the
	 * higher floor first between two alike (y runs down). */
	for (int s = 0; s < cap; s++)
	{
		if (slots[s].y != INT_MIN)
			continue;

		int pick = -1;

		for (int i = 0; i < count; i++)
			if (scores[i] > 0.0f && !placed(ys[i]) && (pick < 0 ||
				scores[i] > scores[pick] || (scores[i] == scores[pick] &&
				ys[i] < ys[pick])))
				pick = i;

		if (pick < 0)
			break;

		slots[s].y = ys[pick];
		slots[s].gain = fresh || step >= 1.0f ? 1.0f : step;
	}

	int used = 0;

	for (int s = 0; s < cap; s++)
		used += slots[s].y != INT_MIN ? 1 : 0;

	return used;
}

/* The rooms over (hd_render.cpp, marble_replay_over): which rooms the
 * frame did not draw go into the mirrors, each with the drawn room it is
 * found from (`via`, whose matrix and light it borrows). `up[g]` are the
 * rooms group g's ceiling portals open into, `side[g]` the rooms its
 * other portals open into, both by room number; `table` names the group
 * that draws each room (`rooms` long; 0 is room n drawn by group n);
 * `drawn[g]` is 1 for a group the frame drew. `depth` 1 takes the rooms
 * over the drawn rooms; 2 the rooms over the undrawn rooms beside them
 * as well; 3 the rooms over those rooms as well. Each group once, the
 * first way found. */
struct HDMarbleOverRoom
{
	int group;
	int via;
};

inline int HDMarbleGroupOf(int room, const short* table, int rooms,
	int groups)
{
	const int g = room < 0 ? -1 : !table ? room :
		room < rooms ? table[room] : -1;

	return g >= 0 && g < groups ? g : -1;
}

inline void HDMarbleOverRooms(const std::vector<std::vector<int> >& up,
	const std::vector<std::vector<int> >& side, const short* table,
	int rooms, const std::vector<char>& drawn, int depth,
	std::vector<HDMarbleOverRoom>& out)
{
	const int groups = (int)drawn.size();
	std::vector<char> taken(drawn.begin(), drawn.end());

	out.clear();

	const auto take = [&](int room, int via)
	{
		const int g = HDMarbleGroupOf(room, table, rooms, groups);

		if (g < 0 || taken[(size_t)g])
			return;

		taken[(size_t)g] = 1;
		out.push_back({ g, via });
	};

	for (int g = 0; depth >= 1 && g < groups && g < (int)up.size(); g++)
		if (drawn[(size_t)g])
			for (int r : up[(size_t)g])
				take(r, g);

	for (int g = 0; depth >= 2 && g < groups && g < (int)side.size(); g++)
		if (drawn[(size_t)g])
			for (int s : side[(size_t)g])
			{
				const int n = HDMarbleGroupOf(s, table, rooms, groups);

				if (n >= 0 && !drawn[(size_t)n] && n < (int)up.size())
					for (int r : up[(size_t)n])
						take(r, g);
			}

	const size_t found = out.size();

	for (size_t i = 0; depth >= 3 && i < found; i++)
	{
		const HDMarbleOverRoom o = out[i];

		if (o.group < (int)up.size())
			for (int r : up[(size_t)o.group])
				take(r, o.via);
	}
}

/* A sloped face's plane (hd_render.cpp, marble_build): the normal of the
 * face `p` (nine floats, the group's own x and z, the world's y) turned up
 * (y negative) and of length 1, and its distance in world space, the
 * group's origin (ox, oz) added. False for a face of no area. */
inline bool HDMarbleTiltPlane(const float* p, float ox, float oz,
	float* out)
{
	const double ux = (double)p[3] - p[0], uy = (double)p[4] - p[1];
	const double uz = (double)p[5] - p[2], vx = (double)p[6] - p[0];
	const double vy = (double)p[7] - p[1], vz = (double)p[8] - p[2];
	double n[3] = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
	const double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

	if (len <= 0.0)
		return false;

	const double s = n[1] > 0.0 ? -len : len;

	for (int k = 0; k < 3; k++)
	{
		n[k] /= s;
		out[k] = (float)n[k];
	}

	out[3] = (float)(n[0] * ((double)p[0] + ox) + n[1] * p[1] +
		n[2] * ((double)p[2] + oz));
	return true;
}

/* Whether two sloped planes (HDMarbleTiltPlane's four floats) are one: the
 * normals' parts within `same` and the distances within `join` units. */
inline bool HDMarbleTiltSame(const float* a, const float* b, float same,
	float join)
{
	return fabsf(a[0] - b[0]) <= same && fabsf(a[1] - b[1]) <= same &&
		fabsf(a[2] - b[2]) <= same && fabsf(a[3] - b[3]) <= join;
}

/* How many planes a frame mirrors at most: the registry's
 * hd_marble_ceiling, held to 1 to HD_MARBLE_PLANES_MOST. The ceiling
 * stands far over what a frame shows (RoomProbe's census eyes, four views
 * from every room with a mirror floor of the fifteen levels, find at most
 * 39 planes in view at once and 21 of them showing): every plane that
 * shows is mirrored, each at its own height (HDMarbleShown bounds what a
 * sliver costs), and the planes past the ceiling, should a frame ever
 * show more, are the ones covering the least (HDMarblePlanesPick). */
#define HD_MARBLE_PLANES_MOST	64

inline int HDMarblePlanesCap(int planes)
{
	return planes < 1 ? 1 : planes > HD_MARBLE_PLANES_MOST ?
		HD_MARBLE_PLANES_MOST : planes;
}

/* A leaning floor of the mirror's pages: a triangle facing up whose
 * normal leans further than the flat face's (HD_MARBLE_FLAT,
 * hd_splash_math.h) and its height is still over HD_MARBLE_LEAN of its
 * length - 60 degrees off the level. The fifteen levels' faces of the
 * mirror's pages that face up (RoomProbe -marblesteep) lean at most 56.3
 * degrees as floors (ramps, the corridors going up, a pool's surround)
 * and 85 and more as walls, and none between: the line lies in that gap.
 * `nyVertex` is the corners' own normals' height (up being negative), the
 * side as the flat face takes it. */
#define HD_MARBLE_LEAN	0.5f

inline bool HDMarbleLeanFace(const float* a, const float* b, const float* c,
	float nyVertex, float flat)
{
	const double ux = (double)b[0] - a[0], uy = (double)b[1] - a[1];
	const double uz = (double)b[2] - a[2];
	const double vx = (double)c[0] - a[0], vy = (double)c[1] - a[1];
	const double vz = (double)c[2] - a[2];
	const double nx = uy * vz - uz * vy;
	const double ny = uz * vx - ux * vz;
	const double nz = ux * vy - uy * vx;
	const double len = sqrt(nx * nx + ny * ny + nz * nz);

	return len > 1e-6 && nyVertex < 0.0f && fabs(ny) > HD_MARBLE_LEAN * len &&
		fabs(ny) <= flat * len;
}

/* The scissor of one plane's mirrored draw, in the target's pixels: the
 * plane's faces cover the frame's pixels x0..x1, y0..y1 (whole, the last
 * included) of a viewport at (vx, vy), `vw` wide; the target holds the
 * frame turned left to right (pixel x at 2 vx + vw - x) at `rows` rows a
 * row, and the layer reads it there (MarblePS). `margin` pixels of the
 * frame more on every side keep the bilinear read's neighbours; the
 * rectangle (right and bottom past its last pixel) is held to the target,
 * `tw` by `th`. False when nothing of it lies in the target. */
inline bool HDMarbleTargetRect(int x0, int y0, int x1, int y1, int vx,
	int vw, int rows, int margin, int tw, int th, int* out)
{
	const int left = 2 * vx + vw - (x1 + margin);
	const int right = 2 * vx + vw - (x0 - margin) + 1;
	const int top = (y0 - margin) * rows;
	const int bottom = (y1 + margin + 1) * rows;

	out[0] = left < 0 ? 0 : left;
	out[1] = top < 0 ? 0 : top;
	out[2] = right > tw ? tw : right;
	out[3] = bottom > th ? th : bottom;
	return out[0] < out[2] && out[1] < out[3];
}

/* Whether a box can show nothing in one plane's mirror: its eight corners
 * `h` in clip space (four floats each, the mirrored view's) all lie past
 * one side of the view, or all under the mirror's clip plane `clip` (clip
 * space), or - all of them in front of the eye - their box on the target,
 * the view's `vp` (x, y, width, height in the target's pixels), misses the
 * scissor `rect` (left, top, right, bottom; null for none). */
inline bool HDMarbleBoxOut(const float* h, const float* clip,
	const float* vp, const int* rect)
{
	int outside[7] = { 0, 0, 0, 0, 0, 0, 0 };
	bool front = true;
	float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };

	for (int c = 0; c < 8; c++)
	{
		const float* p = h + c * 4;

		outside[0] += p[0] < -p[3] ? 1 : 0;
		outside[1] += p[0] > p[3] ? 1 : 0;
		outside[2] += p[1] < -p[3] ? 1 : 0;
		outside[3] += p[1] > p[3] ? 1 : 0;
		outside[4] += p[2] < 0.0f ? 1 : 0;
		outside[5] += p[2] > p[3] ? 1 : 0;
		outside[6] += clip[0] * p[0] + clip[1] * p[1] + clip[2] * p[2] +
			clip[3] * p[3] < 0.0f ? 1 : 0;

		if (p[3] <= 1e-3f)
		{
			front = false;
			continue;
		}

		const float sx = vp[0] + (p[0] / p[3] + 1.0f) * 0.5f * vp[2];
		const float sy = vp[1] + (1.0f - p[1] / p[3]) * 0.5f * vp[3];

		lo[0] = sx < lo[0] ? sx : lo[0];
		lo[1] = sy < lo[1] ? sy : lo[1];
		hi[0] = sx > hi[0] ? sx : hi[0];
		hi[1] = sy > hi[1] ? sy : hi[1];
	}

	for (int s = 0; s < 7; s++)
		if (outside[s] == 8)
			return true;

	return front && rect && (hi[0] < (float)rect[0] - 1.0f ||
		lo[0] > (float)rect[2] + 1.0f || hi[1] < (float)rect[1] - 1.0f ||
		lo[1] > (float)rect[3] + 1.0f);
}

/* Whether a sloped face lies in a sloped plane found already: each of its
 * corners `p` (nine floats, the group's own x and z, the world's y; the
 * group's origin ox, oz added) within `within` units of the plane `t`
 * (HDMarbleTiltPlane's four floats) - the rule a level face keeps to its
 * floor height (marble_build: corners within the mirror clip's offset).
 * A slope tessellated into faces of slightly different lean (a ramp's
 * rounded corner, a pool's sloped surround) so shares one plane where its
 * faces lie within the offset of it, where the normals' rule
 * (HDMarbleTiltSame) made a plane of nearly every face. */
inline bool HDMarbleTiltFits(const float* t, const float* p, float ox,
	float oz, float within)
{
	for (int c = 0; c < 3; c++)
	{
		const double d = (double)t[0] * ((double)p[c * 3] + ox) +
			(double)t[1] * p[c * 3 + 1] + (double)t[2] * ((double)p[c * 3 + 2] +
			oz) - t[3];

		if (d > within || d < -within)
			return false;
	}

	return true;
}

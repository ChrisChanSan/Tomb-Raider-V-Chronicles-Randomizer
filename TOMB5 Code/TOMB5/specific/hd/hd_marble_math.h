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
 * 1). A plane in a place keeps it while it scores at least HD_MARBLE_HOLD
 * of the best plane outside the places, and its gain then rises by a step a
 * frame to 1; a plane that no longer does fades out by the same step and
 * leaves the place at 0, which the best plane outside then takes, rising
 * from one step. A plane that shows nothing leaves at once - there is
 * nothing to fade. Places all empty after that take the best planes at
 * full strength: the first frame of a level, or of a view none of whose
 * last planes shows any more, shows its mirror whole. */

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

	for (int i = 0; i < count; i++)
		if (!placed(ys[i]) && scores[i] > best)
			best = scores[i];

	for (int s = 0; s < cap; s++)
	{
		if (slots[s].y == INT_MIN)
			continue;

		const float own = HDMarbleScoreOf(ys, scores, count, slots[s].y);

		if (own >= hold * best)
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

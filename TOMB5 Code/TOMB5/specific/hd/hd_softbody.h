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

#include <math.h>

/* The soft motion of the chest of the HD Lara: the arithmetic.
 *
 * Three pieces, each a pure function of its arguments: the REGION, two
 * spheres in the body model's bind space found once per outfit out of its
 * own vertices; the MOTION, one point mass per side on a damped spring whose
 * anchor the torso carries through the world; and the CONSTANTS, the four
 * vertex registers one draw hands the model shader (hd_model.hlsl, gSoft),
 * which moves every vertex by its mask times its torso weight times the
 * side's offset.
 *
 * This header includes no engine header on purpose, like hd_objlight_dark.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp includes it and asserts on every
 * function below without a device or a level, and ModelProbe draws its test
 * offsets through the same constants. hd_lara.cpp finds the region,
 * hd_laradraw.cpp runs the motion and fills the constants. */

/* ------------------------------------------------------------ the table -- */

/* Every number that decides how the motion looks, in one place. The feel is
 * not measured anywhere; these are a starting point to be tuned against the
 * game.
 *
 *   HZ          the spring's natural frequency. Higher is stiffer: a quicker,
 *               smaller swing. Lower is a slower, heavier sway.
 *   DAMPING     the damping ratio, 0..1. 0.3 lets a swing die within two or
 *               three cycles; lower swings longer, towards 1 it settles
 *               without overshoot.
 *   GAIN        how much of the spring's own travel, in world units, reaches
 *               the picture. The spring is driven by the torso's real
 *               accelerations - a landing changes her speed by some 3000
 *               units a second - so its travel is far larger than a chest
 *               moves; this is the overall size of the motion.
 *   BOUND       the largest offset drawn, as a part of the region's radius.
 *               The output approaches it smoothly (a tanh) and never passes
 *               it: the ceiling of a hard landing.
 *   FRONTBACK   how much of the offset along her front-back axis is kept.
 *               A chest shears and bobs far more than it is pushed in or
 *               out; 1 keeps all of it, 0 none.
 *   SUBTLE, NORMAL, STRONG  the option's three strengths. Each scales GAIN
 *               and BOUND together and nothing else, so the timing of the
 *               swing is the same at all three. */
#define HD_SOFT_HZ		3.0f
#define HD_SOFT_DAMPING		0.3f
#define HD_SOFT_GAIN		0.1f
#define HD_SOFT_BOUND		0.3f
#define HD_SOFT_FRONTBACK	0.35f
#define HD_SOFT_SUBTLE		0.5f
#define HD_SOFT_NORMAL		1.0f
#define HD_SOFT_STRONG		1.5f

/* The machinery under the table: these keep the motion honest and are not a
 * matter of taste.
 *
 *   STEP        the fixed sub-step, seconds. The spring is advanced in whole
 *               steps of this length whatever the frame rate; the rest of a
 *               frame's time is carried to the next frame.
 *   GAP         a frame further than this from the last one, seconds, resets
 *               the state to rest instead of stepping across the gap: a
 *               hitch, a load, a return from a long pause.
 *   SPEED       the fastest the anchor can travel, world units a second, and
 *   SLACK       a margin on top of it, units. An anchor that moves further
 *               between two frames has been put somewhere - a teleport, a
 *               cutscene's first or last frame - and the state is reset
 *               instead of kicked. 9000 a second is 300 units a game tick,
 *               twice the speed at which a fall kills her.
 *   REST_X, REST_U  below these, units and units a second, the spring's
 *               travel and speed snap to exactly zero: a Lara at rest is
 *               drawn with offsets of exactly zero.
 *   STATE       the spring's own travel is held within this many times the
 *               bound divided by the gain, and its speed within that times
 *               the natural frequency, so that no input can grow the state
 *               without limit. */
#define HD_SOFT_STEP		(1.0f / 480.0f)
#define HD_SOFT_GAP		0.25f
#define HD_SOFT_SPEED		9000.0f
#define HD_SOFT_SLACK		128.0f
#define HD_SOFT_REST_X		0.05f
#define HD_SOFT_REST_U		0.5f
#define HD_SOFT_STATE		4.0f

/* The option's values: 0 off, 1 subtle, 2 normal, 3 strong. */
#define HD_SOFT_OFF		0
#define HD_SOFT_LEVELS		4

/* How much of GAIN and BOUND a strength gets; 0 for off and for anything out
 * of range. */
inline float HDSoftStrengthScale(int strength)
{
	switch (strength)
	{
	case 1: return HD_SOFT_SUBTLE;
	case 2: return HD_SOFT_NORMAL;
	case 3: return HD_SOFT_STRONG;
	default: return 0.0f;
	}
}

/* ----------------------------------------------------------- the region -- */

/* What the region finder decides on, from the body model's own data. The
 * torso's height is the skeleton's, hips joint to head joint (247.3 units on
 * ANDREA1's Lara); the half width is the torso's across the band.
 *
 *   TORSO       a vertex is the torso's when its weight on the torso node
 *               reaches this, of 1.
 *   BAND_LO, BAND_HI  the band the apexes are looked for in, as parts of
 *               the height above the hips joint. Over the 105 wardrobe
 *               bodies the finder accepts (ModelProbe -lara, 122 scanned) the
 *               apexes stand between 0.51 and 0.62 of it; the diving suit's
 *               helmet rim tops the band and is refused at its edge.
 *   MID         the middle strip left out of the search, as a part of the
 *               half width: the breastbone.
 *   OUTER       and the outer edge, likewise: the armpit and the shoulder.
 *   CAP         how far behind the frontmost vertex, as a part of the half
 *               width, a vertex still counts as the apex's cap.
 *   EDGE        a frontmost vertex this part of the height from the band's
 *               edge is no peak.
 *   MIRROR      the two apexes are mirror images when they agree to within
 *               this part of their distance apart, in height, in distance
 *               from the centre plane and in how far forward they stand,
 *   MIRROR_ABS  or to within this many units.
 *   SETBACK     the centre stands this part of the radius behind its apex.
 *   PROMINENCE  each apex stands this part of the radius in front of the
 *               torso one radius below it, or it does not stand out.
 *   INSIDE      fewest torso-weighted vertices inside a region.
 *   RADIUS_LO, RADIUS_HI  the radius as a part of the height.
 *   MIN_TORSO   fewest torso vertices for a torso at all.
 *   FEET        fewest vertices below the ankles to tell front from back. */
#define HD_SOFT_TORSO		0.5f
#define HD_SOFT_BAND_LO		0.45f
#define HD_SOFT_BAND_HI		0.75f
#define HD_SOFT_MID		0.10f
#define HD_SOFT_OUTER		0.85f
#define HD_SOFT_CAP		0.12f
#define HD_SOFT_EDGE		0.02f
#define HD_SOFT_MIRROR		0.15f
#define HD_SOFT_MIRROR_ABS	2.0f
#define HD_SOFT_SETBACK		0.5f
#define HD_SOFT_PROMINENCE	0.08f
#define HD_SOFT_INSIDE		30
#define HD_SOFT_RADIUS_LO	0.07f
#define HD_SOFT_RADIUS_HI	0.25f
#define HD_SOFT_MIN_TORSO	100
#define HD_SOFT_FEET		8

/* One vertex of the body as the finder sees it: the bind position, its
 * weight on the torso node (0..1), and which lower leg it belongs to - 1 for
 * a vertex whose heaviest node is the left shin or foot, 2 for the right, 0
 * for anything else. */
struct HDSoftVertex
{
	float x, y, z;
	float torso;
	int leg;
};

/* The joints the finder reads the body's axes from, in the same bind space:
 * the hips, the head, the two thighs, the torso and the two ankles (the
 * joints of the two foot nodes). */
struct HDSoftSkeleton
{
	float hips[3];
	float head[3];
	float leftThigh[3];
	float rightThigh[3];
	float torso[3];
	float leftAnkle[3];
	float rightAnkle[3];
};

/* The two sides are numbered 0 for her left and 1 for her right. */
struct HDSoftRegion
{
	int ok;			// 1 when every check passed
	const char* reason;	// "ok", or which check refused it
	float up[3];		// unit axes in bind space, out of the joints
	float side[3];		// towards her right
	float fwd[3];		// towards her front
	float apex[2][3];
	float centre[2][3];
	float radius;
	float height;		// the torso's height along `up`
	float width;		// its half width across the band
	float prominence;	// the lesser apex's lead over the middle strip
	int torsoVerts;		// vertices with a torso weight of HD_SOFT_TORSO or more
	int inside[2];		// torso-weighted vertices with a mask above 0
};

inline float hd_soft_dot(const float* a, const float* b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float hd_soft_len(const float* a)
{
	return sqrtf(hd_soft_dot(a, a));
}

/* The mask: 1 at the centre, 0 at the radius and beyond, and smooth between -
 * a smoothstep of 1 - distance / radius. The model shader carries the same
 * formula line for line (hd_model.hlsl, ModelVS); this copy is what the
 * finder counts with and what ModernTest holds the formula to. Distance over
 * radius against the mask:
 *
 *   0  1      0.25  0.84375      0.5  0.5      0.75  0.15625      1 and over  0
 *
 * An inverse radius of 0 gives 1 everywhere, which is why every caller sends
 * offsets of zero with a region of zero. */
inline float HDSoftMask(const float* p, const float* centre, float invRadius)
{
	const float d[3] = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
	float t = 1.0f - hd_soft_len(d) * invRadius;

	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

	return t * t * (3.0f - 2.0f * t);
}

inline void hd_soft_refuse(HDSoftRegion* out, const char* why)
{
	out->ok = 0;
	out->reason = why;
}

/* The finder. Every step reads the data, nothing is assumed about which
 * model axis is which:
 *
 *   up      from the hips joint to the head joint;
 *   side    from the left thigh joint to the right, square to `up`;
 *   front   whichever way along their cross product the feet point: the
 *           mean of the lower-leg vertices below the ankle joints lies in
 *           front of the ankles (toes, or a flipper);
 *   apex    per side, the frontmost torso vertex in the band, between the
 *           middle strip and the outer edge - its height and depth, and
 *           across, the middle of the cap it stands on;
 *   radius  half the apexes' distance apart across the body, so that the two
 *           regions meet in the middle;
 *   centre  the apex set back by HD_SOFT_SETBACK of the radius.
 *
 * Refusals, in the order they are checked: no_axes, no_front, no_torso,
 * band_empty, band_edge (a frontmost vertex at the band's edge), flat
 * (neither apex stands out of the torso below it), asymmetric (one side
 * alone stands out, or the two are not mirror images), radius, sparse. */
inline void HDSoftFindRegion(const HDSoftVertex* v, int count,
	const HDSoftSkeleton* sk, HDSoftRegion* out)
{
	*out = HDSoftRegion();
	out->reason = "ok";

	if (!v || count <= 0 || !sk)
	{
		hd_soft_refuse(out, "no_torso");
		return;
	}

	float up[3] = { sk->head[0] - sk->hips[0], sk->head[1] - sk->hips[1],
		sk->head[2] - sk->hips[2] };
	float upLen = hd_soft_len(up);

	if (!(upLen >= 1.0f))
	{
		hd_soft_refuse(out, "no_axes");
		return;
	}

	for (int i = 0; i < 3; i++)
		up[i] /= upLen;

	float side[3] = { sk->rightThigh[0] - sk->leftThigh[0],
		sk->rightThigh[1] - sk->leftThigh[1], sk->rightThigh[2] - sk->leftThigh[2] };
	const float along = hd_soft_dot(side, up);

	for (int i = 0; i < 3; i++)
		side[i] -= along * up[i];

	const float sideLen = hd_soft_len(side);

	if (!(sideLen >= 1.0f))
	{
		hd_soft_refuse(out, "no_axes");
		return;
	}

	for (int i = 0; i < 3; i++)
		side[i] /= sideLen;

	float fwd[3] =
	{
		up[1] * side[2] - up[2] * side[1],
		up[2] * side[0] - up[0] * side[2],
		up[0] * side[1] - up[1] * side[0]
	};

	/* Front from the feet. */
	double lead = 0.0;
	int feet = 0;

	for (int i = 0; i < count; i++)
	{
		if (v[i].leg != 1 && v[i].leg != 2)
			continue;

		const float* ankle = v[i].leg == 1 ? sk->leftAnkle : sk->rightAnkle;
		const float rel[3] = { v[i].x - ankle[0], v[i].y - ankle[1], v[i].z - ankle[2] };

		if (hd_soft_dot(rel, up) > 0.0f)
			continue;

		lead += hd_soft_dot(rel, fwd);
		feet++;
	}

	if (feet < HD_SOFT_FEET || !(fabs(lead / feet) >= 1.0))
	{
		hd_soft_refuse(out, "no_front");
		return;
	}

	if (lead < 0.0)
		for (int i = 0; i < 3; i++)
			fwd[i] = -fwd[i];

	for (int i = 0; i < 3; i++)
	{
		out->up[i] = up[i];
		out->side[i] = side[i];
		out->fwd[i] = fwd[i];
	}

	int torso = 0;

	for (int i = 0; i < count; i++)
		if (v[i].torso >= HD_SOFT_TORSO)
			torso++;

	out->torsoVerts = torso;

	if (torso < HD_SOFT_MIN_TORSO)
	{
		hd_soft_refuse(out, "no_torso");
		return;
	}

	/* The torso's height is the skeleton's, from the hips joint to the head
	 * joint, and not the extent of its vertices: a coat's skirt, a collar or
	 * a helmet carried on the torso node would stretch the latter and slide
	 * the band off the chest. */
	const float height = upLen;
	const float bandLo = HD_SOFT_BAND_LO * height;
	const float bandHi = HD_SOFT_BAND_HI * height;

	out->height = height;

	/* Every coordinate from here on is taken about the torso joint: s across,
	 * f forward, h up. The half width is the band's own. */
	float halfWidth = 0.0f;

	for (int i = 0; i < count; i++)
	{
		if (!(v[i].torso >= HD_SOFT_TORSO))
			continue;

		const float rel[3] = { v[i].x - sk->torso[0], v[i].y - sk->torso[1],
			v[i].z - sk->torso[2] };
		const float relHips[3] = { v[i].x - sk->hips[0], v[i].y - sk->hips[1],
			v[i].z - sk->hips[2] };
		const float h = hd_soft_dot(relHips, up);

		if (h < bandLo || h > bandHi)
			continue;

		const float s = fabsf(hd_soft_dot(rel, side));

		if (s > halfWidth)
			halfWidth = s;
	}

	out->width = halfWidth;

	int found[2] = { -1, -1 };
	float best[2] = { 0.0f, 0.0f };
	float bestCentred[2] = { 0.0f, 0.0f };

	for (int i = 0; i < count; i++)
	{
		if (!(v[i].torso >= HD_SOFT_TORSO))
			continue;

		const float rel[3] = { v[i].x - sk->torso[0], v[i].y - sk->torso[1],
			v[i].z - sk->torso[2] };
		const float relHips[3] = { v[i].x - sk->hips[0], v[i].y - sk->hips[1],
			v[i].z - sk->hips[2] };
		const float h = hd_soft_dot(relHips, up);

		if (h < bandLo || h > bandHi)
			continue;

		const float s = hd_soft_dot(rel, side);
		const float a = fabsf(s);

		if (a < HD_SOFT_MID * halfWidth || a > HD_SOFT_OUTER * halfWidth)
			continue;

		const int at = s < 0.0f ? 0 : 1;
		const float f = hd_soft_dot(rel, fwd);
		const float centred = fabsf(h - 0.5f * (bandLo + bandHi));

		/* A tie goes to the vertex nearer the band's middle, so that a front
		 * that is the same all the way up is judged flat and not cut off at
		 * the band's edge. */
		if (found[at] < 0 || f > best[at] || (f == best[at] && centred < bestCentred[at]))
		{
			found[at] = i;
			best[at] = f;
			bestCentred[at] = centred;
		}
	}

	if (found[0] < 0 || found[1] < 0)
	{
		hd_soft_refuse(out, "band_empty");
		return;
	}

	/* A peak lies inside the band. A frontmost vertex on the band's edge is
	 * the band cutting through a surface that keeps coming forward beyond
	 * it - a collar, a helmet's rim, a front that leans - and not a chest. */
	for (int k = 0; k < 2; k++)
	{
		const HDSoftVertex& a = v[found[k]];
		const float relHips[3] = { a.x - sk->hips[0], a.y - sk->hips[1], a.z - sk->hips[2] };
		const float h = hd_soft_dot(relHips, up);

		if (h < bandLo + HD_SOFT_EDGE * height || h > bandHi - HD_SOFT_EDGE * height)
		{
			hd_soft_refuse(out, "band_edge");
			return;
		}
	}

	/* Across, the apex is the middle of the cap the frontmost vertex stands
	 * on: every vertex of the same search within HD_SOFT_CAP of the half
	 * width behind it, averaged; its height and depth are the frontmost
	 * vertex's own. A single vertex can be a seam, a zip or a fold at the
	 * cap's inner edge, and the radius is taken from the apexes' distance
	 * apart, so across is the coordinate a stray vertex would spoil. */
	double capS[2] = { 0.0, 0.0 };
	int capCount[2] = { 0, 0 };

	for (int i = 0; i < count; i++)
	{
		if (!(v[i].torso >= HD_SOFT_TORSO))
			continue;

		const float rel[3] = { v[i].x - sk->torso[0], v[i].y - sk->torso[1],
			v[i].z - sk->torso[2] };
		const float relHips[3] = { v[i].x - sk->hips[0], v[i].y - sk->hips[1],
			v[i].z - sk->hips[2] };
		const float hb = hd_soft_dot(relHips, up);

		if (hb < bandLo || hb > bandHi)
			continue;

		const float s = hd_soft_dot(rel, side);
		const float a = fabsf(s);

		if (a < HD_SOFT_MID * halfWidth || a > HD_SOFT_OUTER * halfWidth)
			continue;

		const int at = s < 0.0f ? 0 : 1;

		if (hd_soft_dot(rel, fwd) < best[at] - HD_SOFT_CAP * halfWidth)
			continue;

		capS[at] += s;
		capCount[at]++;
	}

	float s2[2], h2[2], f2[2];

	for (int k = 0; k < 2; k++)
	{
		const HDSoftVertex& a = v[found[k]];
		const float rel[3] = { a.x - sk->torso[0], a.y - sk->torso[1], a.z - sk->torso[2] };

		s2[k] = (float)(capS[k] / capCount[k]);
		h2[k] = hd_soft_dot(rel, up);
		f2[k] = best[k];

		for (int i = 0; i < 3; i++)
			out->apex[k][i] = sk->torso[i] + s2[k] * side[i] + h2[k] * up[i] +
				f2[k] * fwd[i];
	}

	const float apart = s2[1] - s2[0];
	const float radius = 0.5f * apart;

	out->radius = radius;

	/* A chest stands out of the torso under it; a flat front, or one that
	 * keeps coming forward lower down, does not. Per side, the front of the
	 * torso one radius below the apex, in a column half a radius wide about
	 * it, and the apex's lead over it; no vertex there leaves the apex
	 * nothing to stand out of. Neither side standing out is a flat front, one
	 * side alone is a one-sided one. Below and not above: a chest's upper
	 * slope runs flat into the collarbones, its lower curve turns back. */
	const float window = radius > 1.0f ? radius : 1.0f;
	bool stands[2];
	float lead2[2];

	for (int k = 0; k < 2; k++)
	{
		bool any = false;
		float front = 0.0f;

		for (int i = 0; i < count; i++)
		{
			if (!(v[i].torso >= HD_SOFT_TORSO))
				continue;

			const float rel[3] = { v[i].x - sk->torso[0], v[i].y - sk->torso[1],
				v[i].z - sk->torso[2] };
			const float dh = h2[k] - hd_soft_dot(rel, up);

			if (fabsf(hd_soft_dot(rel, side) - s2[k]) > 0.5f * window ||
				dh < 0.75f * window || dh > 1.25f * window)
				continue;

			const float f = hd_soft_dot(rel, fwd);

			if (!any || f > front)
				front = f;

			any = true;
		}

		lead2[k] = any ? f2[k] - front : 0.0f;
		stands[k] = lead2[k] > 0.0f && lead2[k] >= HD_SOFT_PROMINENCE * radius;
	}

	out->prominence = lead2[0] < lead2[1] ? lead2[0] : lead2[1];

	if (!stands[0] && !stands[1])
	{
		hd_soft_refuse(out, "flat");
		return;
	}

	const float tolerance = HD_SOFT_MIRROR * apart > HD_SOFT_MIRROR_ABS
		? HD_SOFT_MIRROR * apart : HD_SOFT_MIRROR_ABS;

	if (!stands[0] || !stands[1] ||
		!(fabsf(h2[0] - h2[1]) <= tolerance) ||
		!(fabsf(-s2[0] - s2[1]) <= tolerance) ||
		!(fabsf(f2[0] - f2[1]) <= tolerance))
	{
		hd_soft_refuse(out, "asymmetric");
		return;
	}

	if (!(radius >= HD_SOFT_RADIUS_LO * height) || !(radius <= HD_SOFT_RADIUS_HI * height))
	{
		hd_soft_refuse(out, "radius");
		return;
	}

	for (int k = 0; k < 2; k++)
		for (int i = 0; i < 3; i++)
			out->centre[k][i] = out->apex[k][i] - fwd[i] * HD_SOFT_SETBACK * radius;

	const float inv = 1.0f / radius;

	for (int i = 0; i < count; i++)
	{
		if (!(v[i].torso > 0.0f))
			continue;

		const float p[3] = { v[i].x, v[i].y, v[i].z };

		for (int k = 0; k < 2; k++)
			if (HDSoftMask(p, out->centre[k], inv) > 0.0f)
				out->inside[k]++;
	}

	if (out->inside[0] < HD_SOFT_INSIDE || out->inside[1] < HD_SOFT_INSIDE)
	{
		hd_soft_refuse(out, "sparse");
		return;
	}

	out->ok = 1;
}

/* ----------------------------------------------------------- the motion -- */

/* One side: the spring's travel x, the point minus its anchor in world units,
 * and its speed u. */
struct HDSoftSide
{
	float x[3];
	float u[3];
};

/* Both sides and what the motion needs to remember between frames. `primed`
 * is how much of the anchor's past is known: 0 nothing, 1 its position, 2 its
 * speed as well. The counters are for the log and are read and cleared by
 * its caller. */
struct HDSoftSim
{
	HDSoftSide side[2];
	float anchor[2][3];
	float speed[2][3];
	int primed;
	double carry;		// time not yet stepped, in sub-steps

	int resets;
	int steps;
	int clamped;
	float peak;
};

inline void HDSoftReset(HDSoftSim* s)
{
	for (int k = 0; k < 2; k++)
		for (int i = 0; i < 3; i++)
		{
			s->side[k].x[i] = 0.0f;
			s->side[k].u[i] = 0.0f;
			s->anchor[k][i] = 0.0f;
			s->speed[k][i] = 0.0f;
		}

	s->primed = 0;
	s->carry = 0.0;
}

inline void HDSoftClear(HDSoftSim* s)
{
	HDSoftReset(s);
	s->resets = 0;
	s->steps = 0;
	s->clamped = 0;
	s->peak = 0.0f;
}

/* The exact step of an underdamped oscillator x'' + 2 z w x' + w^2 x = 0 over
 * a time h, as a 2 x 2 matrix on (x, u):
 *
 *   x(h) = e (c + z w / wd s) x + e s / wd u
 *   u(h) = -e w^2 / wd s x       + e (c - z w / wd s) u
 *
 * with wd = w sqrt(1 - z^2), e = exp(-z w h), c = cos(wd h), s = sin(wd h).
 * Exact for any h, so the sub-step's length changes nothing but where the
 * frame's remainder is cut. The damping ratio is held below 1, where the
 * formula holds. */
struct HDSoftStep
{
	float a, b, c, d;
};

inline HDSoftStep HDSoftPropagator(float hz, float damping, float h)
{
	const double w = 2.0 * 3.14159265358979323846 * (hz > 0.01f ? hz : 0.01f);
	const double z = damping < 0.0f ? 0.0 : (damping > 0.95f ? 0.95 : damping);
	const double wd = w * sqrt(1.0 - z * z);
	const double e = exp(-z * w * h);
	const double c = cos(wd * h);
	const double s = sin(wd * h);
	HDSoftStep m;

	m.a = (float)(e * (c + z * w / wd * s));
	m.b = (float)(e * s / wd);
	m.c = (float)(-e * w * w / wd * s);
	m.d = (float)(e * (c - z * w / wd * s));
	return m;
}

inline bool hd_soft_finite(float f)
{
	return f == f && f < 3.0e38f && f > -3.0e38f;
}

/* The offset a side draws, from its travel: GAIN times the travel, eased
 * into BOUND times the radius by a tanh so that it never passes it. Both are
 * scaled by the strength. True when the raw value passed the bound, which
 * is what the log counts as a frame at the bound. */
inline bool HDSoftOutput(const float* x, int strength, float radius, float* out)
{
	const float scale = HDSoftStrengthScale(strength);
	const float gain = HD_SOFT_GAIN * scale;
	const float bound = HD_SOFT_BOUND * scale * radius;
	const float y[3] = { gain * x[0], gain * x[1], gain * x[2] };
	const float m = hd_soft_len(y);

	if (!(m > 0.0f) || !(bound > 0.0f))
	{
		out[0] = out[1] = out[2] = 0.0f;
		return false;
	}

	const float k = bound * tanhf(m / bound) / m;

	out[0] = y[0] * k;
	out[1] = y[1] * k;
	out[2] = y[2] * k;
	return m > bound;
}

/* One frame of the motion.
 *
 * `anchor` is where the two region centres stand in the world on the frame
 * that is drawn, `dt` the game time since the last frame that advanced, in
 * seconds. The anchor's velocity over the frame is its travel over `dt`;
 * where that velocity differs from the previous frame's, the change is taken
 * out of the point's speed relative to the anchor - the point keeps its own
 * velocity while the anchor's changes - and the spring then runs freely in
 * whole sub-steps. An anchor that moves at a constant speed therefore drives
 * nothing, and the response depends on the path and not on how often it was
 * sampled.
 *
 *   dt <= 0          nothing advances: a frame drawn twice, or the game
 *                    standing still. The output is the state's.
 *   dt > GAP, a jump further than SPEED * dt + SLACK, or anything not finite
 *                    resets to rest and takes this frame as the new start.
 *   the first frame after a reset knows only the anchor's position, the
 *                    second its speed as well; the spring starts moving on
 *                    the third, so a reset never kicks.
 *
 * `out` is the two drawn offsets in world units. Returns true when the state
 * is at rest - both sides exactly zero. */
inline bool HDSoftAdvance(HDSoftSim* s, const float anchor[2][3], float dt,
	int strength, float radius, float out[2][3])
{
	bool reset = !hd_soft_finite(dt) || dt > HD_SOFT_GAP;

	for (int k = 0; k < 2 && !reset; k++)
		for (int i = 0; i < 3; i++)
			if (!hd_soft_finite(anchor[k][i]))
				reset = true;

	if (!reset && s->primed > 0 && dt > 0.0f)
		for (int k = 0; k < 2; k++)
		{
			const float d[3] = { anchor[k][0] - s->anchor[k][0],
				anchor[k][1] - s->anchor[k][1], anchor[k][2] - s->anchor[k][2] };

			if (hd_soft_len(d) > HD_SOFT_SPEED * dt + HD_SOFT_SLACK)
				reset = true;
		}

	if (reset || s->primed == 0)
	{
		if (reset)
			s->resets++;

		HDSoftReset(s);

		bool usable = true;

		for (int k = 0; k < 2; k++)
			for (int i = 0; i < 3; i++)
			{
				if (!hd_soft_finite(anchor[k][i]))
					usable = false;

				s->anchor[k][i] = anchor[k][i];
			}

		s->primed = usable ? 1 : 0;

		for (int k = 0; k < 2; k++)
			out[k][0] = out[k][1] = out[k][2] = 0.0f;

		return true;
	}

	if (dt > 0.0f)
	{
		for (int k = 0; k < 2; k++)
			for (int i = 0; i < 3; i++)
			{
				const float speed = (anchor[k][i] - s->anchor[k][i]) / dt;

				if (s->primed >= 2)
					s->side[k].u[i] -= speed - s->speed[k][i];

				s->speed[k][i] = speed;
				s->anchor[k][i] = anchor[k][i];
			}

		s->primed = 2;
		/* Counted in sub-steps and with a ten-thousandth of one to spare: a
		 * sixtieth of a second is eight sub-steps, and as floats it comes out
		 * a hair under eight, which would drop a step every frame and carry
		 * it to the next. */
		s->carry += (double)dt / (double)HD_SOFT_STEP;

		const HDSoftStep m = HDSoftPropagator(HD_SOFT_HZ, HD_SOFT_DAMPING, HD_SOFT_STEP);
		const int n = (int)floor(s->carry + 1.0e-4);

		s->carry -= n;

		if (s->carry < 0.0)
			s->carry = 0.0;

		for (int step = 0; step < n; step++)
			for (int k = 0; k < 2; k++)
				for (int i = 0; i < 3; i++)
				{
					const float x = s->side[k].x[i];
					const float u = s->side[k].u[i];

					s->side[k].x[i] = m.a * x + m.b * u;
					s->side[k].u[i] = m.c * x + m.d * u;
				}

		s->steps += n;

		/* The state's own bound, and a state that is no longer a number
		 * goes back to rest. */
		const float scale = HDSoftStrengthScale(strength);
		const float xMax = scale > 0.0f
			? HD_SOFT_STATE * HD_SOFT_BOUND * radius / HD_SOFT_GAIN : 0.0f;
		const float uMax = xMax * 2.0f * 3.14159265f * HD_SOFT_HZ;
		bool broken = false;

		for (int k = 0; k < 2; k++)
		{
			float* x = s->side[k].x;
			float* u = s->side[k].u;
			const float lx = hd_soft_len(x);
			const float lu = hd_soft_len(u);

			if (!hd_soft_finite(lx) || !hd_soft_finite(lu))
			{
				broken = true;
				continue;
			}

			if (lx > xMax)
				for (int i = 0; i < 3; i++)
					x[i] = lx > 0.0f ? x[i] * (xMax / lx) : 0.0f;

			if (lu > uMax)
				for (int i = 0; i < 3; i++)
					u[i] = lu > 0.0f ? u[i] * (uMax / lu) : 0.0f;
		}

		if (broken)
		{
			s->resets++;
			HDSoftReset(s);

			for (int k = 0; k < 2; k++)
				for (int i = 0; i < 3; i++)
					s->anchor[k][i] = anchor[k][i];

			s->primed = 1;
		}

		/* At rest is exactly at rest. Both sides at once, so that one side
		 * cannot hold the other's last hair of travel for ever. */
		bool still = true;

		for (int k = 0; k < 2; k++)
			if (!(hd_soft_len(s->side[k].x) < HD_SOFT_REST_X) ||
				!(hd_soft_len(s->side[k].u) < HD_SOFT_REST_U))
				still = false;

		if (still)
			for (int k = 0; k < 2; k++)
				for (int i = 0; i < 3; i++)
				{
					s->side[k].x[i] = 0.0f;
					s->side[k].u[i] = 0.0f;
				}
	}

	bool rest = true;
	bool clamped = false;

	for (int k = 0; k < 2; k++)
	{
		if (HDSoftOutput(s->side[k].x, strength, radius, out[k]))
			clamped = true;

		const float m = hd_soft_len(out[k]);

		if (m > s->peak)
			s->peak = m;

		for (int i = 0; i < 3; i++)
			if (s->side[k].x[i] != 0.0f || s->side[k].u[i] != 0.0f)
				rest = false;
	}

	if (clamped)
		s->clamped++;

	return rest;
}

/* ------------------------------------------------------------ the clock -- */

/* Which draw steps the motion, and by how much game time.
 *
 * One step per drawn frame: the first draw of Lara after HDSoftClockFrame
 * that is not a mirror's. A mirror's draw, and a second draw of the same
 * frame, step nothing and draw the state as it stands.
 *
 * The time is the game's own: a tick counter that advances once a logic tick
 * (30 a second) and stands still while the game does, plus the interpolation
 * factor the frame was drawn at, 0..1 past the tick before. `dt` is the
 * difference to the last frame that advanced, with the counter's wrap; a
 * frame that is not later than that one - a paused game drawn again - gives
 * 0 and does not move the clock. The first frame after HDSoftClockForget
 * gives 0 and starts it. */
struct HDSoftClock
{
	int stepped;
	int known;
	unsigned short tick;
	float alpha;
};

inline void HDSoftClockFrame(HDSoftClock* c)
{
	c->stepped = 0;
}

inline void HDSoftClockForget(HDSoftClock* c)
{
	c->known = 0;
}

inline bool HDSoftClockStep(HDSoftClock* c, bool mirror, unsigned short tick, float alpha,
	float* dt)
{
	*dt = 0.0f;

	if (mirror || c->stepped)
		return false;

	c->stepped = 1;

	if (c->known)
		*dt = ((float)(unsigned short)(tick - c->tick) + alpha - c->alpha) / 30.0f;

	if (!c->known || *dt > 0.0f)
	{
		c->tick = tick;
		c->alpha = alpha;
		c->known = 1;
	}

	if (*dt < 0.0f)
		*dt = 0.0f;

	return true;
}

/* --------------------------------------------------------- the constants -- */

/* Where a region's centre stands in the world. `joint` is the torso node's
 * matrix from the body's own origin, three rows of four - rotation and
 * translation, world = R p + t - and `origin` that origin in the world; the
 * centre is taken about the torso joint's bind position first, exactly as
 * the shader's bind fold takes every vertex. */
inline void HDSoftAnchor(const float* joint, const float* origin, const float* centre,
	const float* torsoBind, float* out)
{
	const float p[3] = { centre[0] - torsoBind[0], centre[1] - torsoBind[1],
		centre[2] - torsoBind[2] };

	for (int r = 0; r < 3; r++)
		out[r] = origin[r] + joint[r * 4 + 3] +
			joint[r * 4 + 0] * p[0] + joint[r * 4 + 1] * p[1] + joint[r * 4 + 2] * p[2];
}

/* A world offset into the torso's own axes (the transpose of the joint's
 * rotation), with its front-back part weakened by HD_SOFT_FRONTBACK. `fwd` is
 * the region's front in bind space, which is the torso's own frame at rest. */
inline void HDSoftToTorso(const float* joint, const float* fwd, const float* world,
	float* out)
{
	for (int c = 0; c < 3; c++)
		out[c] = joint[0 * 4 + c] * world[0] + joint[1 * 4 + c] * world[1] +
			joint[2 * 4 + c] * world[2];

	const float along = hd_soft_dot(out, fwd) * (1.0f - HD_SOFT_FRONTBACK);

	for (int i = 0; i < 3; i++)
		out[i] -= along * fwd[i];
}

/* The four vertex registers of one draw, c163 to c166 of hd_model.hlsl:
 *
 *   [0..3]    the left centre in bind space and 1 / radius
 *   [4..7]    the right centre and 1 / radius
 *   [8..11]   the left offset in VIEW space, and the torso node
 *   [12..15]  the right offset, and 0
 *
 * `torsoOffset` is each side's offset in the torso's own axes and `view` the
 * torso node's row-major model-to-view matrix of the very draw these are for:
 * its first three rows are where the torso's three axes point in that view,
 * so a mirror's reflected draw gets a reflected offset. A region that is not
 * ok, or no region, gives sixteen zeros, which the shader draws as nothing
 * moved. */
inline void HDSoftConstants(const HDSoftRegion* region, const float torsoOffset[2][3],
	const float* view, int torsoNode, float* out)
{
	for (int i = 0; i < 16; i++)
		out[i] = 0.0f;

	if (!region || !region->ok || !(region->radius > 0.0f) || !view)
		return;

	for (int k = 0; k < 2; k++)
	{
		for (int i = 0; i < 3; i++)
		{
			out[k * 4 + i] = region->centre[k][i];
			out[8 + k * 4 + i] = torsoOffset[k][0] * view[0 * 4 + i] +
				torsoOffset[k][1] * view[1 * 4 + i] + torsoOffset[k][2] * view[2 * 4 + i];
		}

		out[k * 4 + 3] = 1.0f / region->radius;
	}

	out[11] = (float)torsoNode;
}

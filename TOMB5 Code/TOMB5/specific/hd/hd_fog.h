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

/* The fog bulbs' body: the rule of the fog's own pass (hd_render.cpp, "the
 * fog bulbs' body"; hd_shader/hd_fog.hlsl), as pure functions, so that
 * ModernTest and RoomProbe -fogprofile hold the shader's arithmetic without
 * a device. Nothing here draws or holds state.
 *
 * A fog bulb is the level's own: a centre, a radius, a colour and a density
 * d (aRoomInit and aBuildFogBulbList, drawroom.cpp; d is 1 / den, about 1/72
 * in every level that has bulbs). The classic path adds, per room vertex,
 * the length of the eye ray inside the sphere times d - the chord - as 0..255
 * of the bulb's colour and takes the same off the surface's multiply: a
 * haze ON the surfaces behind the bulb, nothing over the sky and nothing in
 * the air between.
 *
 * Here the bulb is a body of fog of its own:
 *
 *   the density   d * HD_FOG_PEAK * (1 - rho^2), rho the distance from the
 *                 centre over the radius: the most at the centre, none at
 *                 the rim and no step there (the column through it falls
 *                 as (1 - b^2)^1.5 with the ray's distance b from the centre,
 *                 so it leaves the rim with zero slope). HD_FOG_PEAK is 1.5
 *                 because that makes the column through the centre 2 r d,
 *                 the classic chord's own most: the bulb holds no more fog
 *                 along its densest line than the classic draws there.
 *   the column    the density integrated along the eye ray from the eye, or
 *                 the sphere's near side, to the frame's depth or the far
 *                 side: a surface inside the bulb is veiled by the fog in
 *                 front of it alone, and the sky behind a bulb by all of it.
 *   the veil      tau = column / 255 (the classic's own scale: a column of
 *                 255 is the classic's whole veil) and a transmittance
 *                 T = exp(-tau). The pixel becomes dst * T + colour (1 - T):
 *                 for a thin veil that is the classic's dst (1 - tau) +
 *                 colour tau, and 1 - T never exceeds tau.
 *   the strength  the density times HDFogStrength, the bulb's own factor:
 *                 its densest line veils black by HD_FOG_TOP in its colour,
 *                 the top of the look's band (the pass, hd_render.cpp,
 *                 fog_take; the shader takes the density as it is given).
 *   the breath    the density times 1 - HD_FOG_BREATH * n, n a value noise
 *                 of hashed lattice points (HDFogHash) over cells of
 *                 HD_FOG_CELL radii, anchored in the world and rising by
 *                 HD_FOG_RISE cells a second on the game's clock - the fog
 *                 is not a smooth ball and only ever thins. The column is
 *                 taken in HD_FOG_STEPS equal pieces, each one exact for the
 *                 smooth density and weighted with the breath at its middle.
 *
 * Units: world units (y down), view space with the eye at the origin, the
 * ray dir * t with t the view depth, seconds. */

// The density's weight at the centre: the column through it is 2 r d.
#define HD_FOG_PEAK		1.5f
// The share of the density the breath may take away.
#define HD_FOG_BREATH		0.5f
// The breath's lattice cell as a share of the bulb's radius.
#define HD_FOG_CELL		0.5f
// The breath's rise, in cells per second of the game's clock.
#define HD_FOG_RISE		0.25f
// The pieces the column is taken in, each with its own breath.
#define HD_FOG_STEPS		4

/* The piece of the ray dir * t (t the view depth, eye at 0) inside the
 * sphere at c of radius `rad`, cut at the eye and at the surface's view
 * depth zs. Measured along the ray from the point nearest the centre, in
 * radii: s0..s1, and beta2 the squared distance of that point from the
 * centre in radii squared. False when the ray misses the sphere or the
 * piece is empty. */
inline bool HDFogSpan(const float dir[3], const float c[3], float rad,
	float zs, float* s0, float* s1, float* beta2)
{
	if (!(rad > 0.0f))
		return false;

	const float a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
	const float len = sqrtf(a > 1e-12f ? a : 1e-12f);
	const float m = (dir[0] * c[0] + dir[1] * c[1] + dir[2] * c[2]) / len;
	const float cc = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
	const float b2 = (cc - m * m) / (rad * rad);

	if (!(b2 < 1.0f))
		return false;

	const float edge = sqrtf(1.0f - (b2 > 0.0f ? b2 : 0.0f));
	const float eye = -m / rad;
	const float surface = (zs * len - m) / rad;
	const float lo = eye > -edge ? eye : -edge;
	const float hi = surface < edge ? surface : edge;

	if (!(hi > lo))
		return false;

	*s0 = lo;
	*s1 = hi;
	*beta2 = b2 > 0.0f ? b2 : 0.0f;
	return true;
}

/* The smooth density's integral over s0..s1 of the span, in radii, without
 * the weight and the radius: G(s1) - G(s0), G(s) = (1 - beta2) s - s^3 / 3.
 * Through the centre, whole, it is 4/3. */
inline float HDFogColumn(float beta2, float s0, float s1)
{
	const float k = 1.0f - beta2;

	return (k * s1 - s1 * s1 * s1 / 3.0f) - (k * s0 - s0 * s0 * s0 / 3.0f);
}

/* The veil of a column: tau for the bulb's density d and radius over a
 * span's HDFogColumn, in the classic's 0..255 units over 255. */
inline float HDFogTau(float d, float rad, float column)
{
	return d * HD_FOG_PEAK * rad * column / 255.0f;
}

// The light that passes a veil of tau.
inline float HDFogTransmit(float tau)
{
	return expf(-(tau > 0.0f ? tau : 0.0f));
}

/* The classic's chord intensity for the same ray through the bulb's centre,
 * whole: 2 r d, the most the classic draws for that bulb (0..255 before its
 * clamp). The fog's column through the centre is this, exactly. */
inline float HDFogClassicPeak(float d, float rad)
{
	return 2.0f * rad * d;
}

inline float HDFogFrac(float x)
{
	return x - floorf(x);
}

/* A lattice point's value in 0..1, a hash of its three whole coordinates -
 * the sky cloud's (hd_sky.hlsl, CloudHash): folded into 0..63 per axis,
 * then scrambled by two rounds in which each axis is multiplied by a linear
 * function of another and folded back into 0..1. Arithmetic only, as the
 * shader has it (ps_3_0 has no integers); the processor's float rounding
 * may land a different last bit than the card's. */
inline float HDFogHash(float x, float y, float z)
{
	float p[3] = { x - 64.0f * floorf(x / 64.0f), y - 64.0f * floorf(y / 64.0f),
		z - 64.0f * floorf(z / 64.0f) };
	float q[3];

	p[0] = HDFogFrac(p[0] * 0.1361f + 0.317f);
	p[1] = HDFogFrac(p[1] * 0.1187f + 0.317f);
	p[2] = HDFogFrac(p[2] * 0.1543f + 0.317f);

	for (int i = 0; i < 3; i++)
		q[i] = HDFogFrac(p[i] * (p[(i + 1) % 3] * 47.31f + 13.73f));

	for (int i = 0; i < 3; i++)
		p[i] = HDFogFrac(q[i] * (q[(i + 2) % 3] * 31.17f + 7.91f));

	return HDFogFrac(p[0] + p[1] * 1.618f + p[2] * 2.718f);
}

/* The breath's field at q (in cells): the eight hashed corners of q's cell,
 * blended with the smoothstep of q's place inside it. 0..1. */
inline float HDFogNoise(const float q[3])
{
	const float c[3] = { floorf(q[0]), floorf(q[1]), floorf(q[2]) };
	float f[3];

	for (int i = 0; i < 3; i++)
	{
		const float t = q[i] - c[i];

		f[i] = t * t * (3.0f - 2.0f * t);
	}

	float v[2][2][2];

	for (int k = 0; k < 2; k++)
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++)
				v[k][j][i] = HDFogHash(c[0] + i, c[1] + j, c[2] + k);

	float e[2][2];

	for (int k = 0; k < 2; k++)
		for (int j = 0; j < 2; j++)
			e[k][j] = v[k][j][0] + (v[k][j][1] - v[k][j][0]) * f[0];

	const float g0 = e[0][0] + (e[0][1] - e[0][0]) * f[1];
	const float g1 = e[1][0] + (e[1][1] - e[1][0]) * f[1];

	return g0 + (g1 - g0) * f[2];
}

/* The breath at a world point for a bulb of radius `rad` at the clock:
 * 1 - HD_FOG_BREATH * n, n the field over cells of HD_FOG_CELL radii that
 * rise (towards -y) by HD_FOG_RISE cells a second. */
inline float HDFogBreath(const float world[3], float rad, float clock)
{
	const float cell = HD_FOG_CELL * (rad > 1.0f ? rad : 1.0f);
	const float q[3] = { world[0] / cell, world[1] / cell + clock *
		HD_FOG_RISE, world[2] / cell };

	return 1.0f - HD_FOG_BREATH * HDFogNoise(q);
}

/* The sphere's rectangle on the screen, in normalised device coordinates
 * (x right, y up, -1..1), out = x0, y0, x1, y1, for a projection whose
 * scales are sx (_11) and sy (-_22, both positive) and a near plane at
 * znear. 0: the sphere is off the screen or behind the eye; 1: the
 * rectangle; 2: the whole screen (the sphere reaches the near plane). The
 * edges are the planes through the eye that touch the sphere, one pair per
 * axis, so the rectangle is the silhouette's own box. */
inline int HDFogBounds(const float c[3], float rad, float sx, float sy,
	float znear, float out[4])
{
	if (!(rad > 0.0f) || c[2] + rad <= znear)
		return 0;

	if (c[2] - rad <= znear)
	{
		out[0] = -1.0f;
		out[1] = -1.0f;
		out[2] = 1.0f;
		out[3] = 1.0f;
		return 2;
	}

	// One axis: the tangents of the two touching planes, over the depth.
	float lo[2], hi[2];
	const float axis[2] = { c[0], -c[1] };
	const float scale[2] = { sx, sy };

	for (int i = 0; i < 2; i++)
	{
		const float x = axis[i], z = c[2];
		const float l2 = x * x + z * z;
		const float t2 = l2 - rad * rad;
		const float t = sqrtf(t2 > 0.0f ? t2 : 0.0f);
		// The touching directions (x', z') in the plane, as tan = x' / z'.
		const float z1 = (z * t - x * rad) / l2, x1 = (x * t + z * rad) / l2;
		const float z2 = (z * t + x * rad) / l2, x2 = (x * t - z * rad) / l2;
		const float e1 = z1 > 1e-6f ? x1 / z1 * scale[i] : 1e9f;
		const float e2 = z2 > 1e-6f ? x2 / z2 * scale[i] : -1e9f;

		lo[i] = e1 < e2 ? e1 : e2;
		hi[i] = e1 < e2 ? e2 : e1;
	}

	if (hi[0] <= -1.0f || lo[0] >= 1.0f || hi[1] <= -1.0f || lo[1] >= 1.0f)
		return 0;

	out[0] = lo[0] < -1.0f ? -1.0f : lo[0];
	out[1] = lo[1] < -1.0f ? -1.0f : lo[1];
	out[2] = hi[0] > 1.0f ? 1.0f : hi[0];
	out[3] = hi[1] > 1.0f ? 1.0f : hi[1];
	return 1;
}

/* The whole rule for one pixel's ray on the processor, the shader's twin:
 * dir the ray (dir.z 1), c the bulb's centre (view), zs the surface's view
 * depth, eyeWorld the eye in the world and axes the world's three axes in
 * view space (rows), so a view point p is eyeWorld + (p.axes[0], p.axes[1],
 * p.axes[2]) in the world. breathOn 0 leaves the breath out. Returns tau. */
inline float HDFogRayTau(const float dir[3], const float c[3], float rad,
	float d, float zs, const float eyeWorld[3], const float axes[9],
	float clock, int breathOn)
{
	float s0 = 0, s1 = 0, b2 = 0;

	if (!HDFogSpan(dir, c, rad, zs, &s0, &s1, &b2))
		return 0.0f;

	const float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] +
		dir[2] * dir[2]);
	const float m = (dir[0] * c[0] + dir[1] * c[1] + dir[2] * c[2]) / len;
	float column = 0.0f;

	for (int k = 0; k < HD_FOG_STEPS; k++)
	{
		const float a = s0 + (s1 - s0) * (float)k / HD_FOG_STEPS;
		const float b = s0 + (s1 - s0) * (float)(k + 1) / HD_FOG_STEPS;
		float w = 1.0f;

		if (breathOn)
		{
			// The piece's middle, in view space, then in the world.
			const float along = m + 0.5f * (a + b) * rad;
			const float p[3] = { dir[0] / len * along, dir[1] / len * along,
				dir[2] / len * along };
			float world[3];

			for (int i = 0; i < 3; i++)
				world[i] = eyeWorld[i] + p[0] * axes[i * 3 + 0] +
					p[1] * axes[i * 3 + 1] + p[2] * axes[i * 3 + 2];

			w = HDFogBreath(world, rad, clock);
		}

		column += w * HDFogColumn(b2, a, b);
	}

	return HDFogTau(d, rad, column);
}

/* The density at a point rho radii from a bulb's centre (rho under 1),
 * without the breath: d * HD_FOG_PEAK * (1 - rho^2), as the pass
 * integrates it; 0 at the rim and outside. */
inline float HDFogDensity(float d, float rho)
{
	return rho < 1.0f ? d * HD_FOG_PEAK * (1.0f - rho * rho) : 0.0f;
}

/* A point's distance from a bulb's centre, in radii (both in the same
 * space). */
inline float HDFogRho(const float p[3], const float c[3], float rad)
{
	const float x = p[0] - c[0], y = p[1] - c[1], z = p[2] - c[2];

	return rad > 0.0f ? sqrtf(x * x + y * y + z * z) / rad : 2.0f;
}

/* The top of the look's band, in 0..1 of luma: the most a bulb's body and
 * the art's mist add over the picture under them. */
#define HD_FOG_TOP		(33.0f / 255.0f)
// The most a bulb's densest line may veil, whatever its colour.
#define HD_FOG_VEIL_MOST	0.5f
/* The art's mist's weight in the additive pass (HDFogArtWeight): the top
 * over the most the mist adds at 1 (94.3 of 255 at the 95th percentile of
 * its pixels, the brightest of the eyes measured), so it adds the top. */
#define HD_FOG_ART_WEIGHT	0.35f

/* The body's strength: the factor on the density that makes the veil of
 * the bulb's densest line - the column 2 r d through its centre - add
 * HD_FOG_TOP over black in the bulb's own colour (r, g, b 0..1): the veil
 * HD_FOG_TOP over the colour's luma, at most HD_FOG_VEIL_MOST, as
 * 1 - exp(-S 2 r d / 255). Above 1 for a bulb fainter than the top, below
 * for a brighter one; 1 where the bulb has no colour or no column. */
inline float HDFogStrength(float d, float rad, float r, float g, float b)
{
	const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
	const float peak = HDFogClassicPeak(d, rad) / 255.0f;

	if (!(luma > 0.0f) || !(peak > 0.0f))
		return 1.0f;

	float veil = HD_FOG_TOP / luma;

	if (veil > HD_FOG_VEIL_MOST)
		veil = HD_FOG_VEIL_MOST;

	return -logf(1.0f - veil) / peak;
}

/* The art's mist: an additive part of the flow material (`flow`) whose
 * page drifts on both axes (su, sv, page units a second). Every water flow
 * of the fifteen levels scrolls along one axis, its stream; the mist
 * drifts askew. */
inline bool HDFogArtMist(bool flow, float su, float sv)
{
	return flow && su != 0.0f && sv != 0.0f;
}

// The mist's additive weight, for its own weight `base` (1 for the flow).
inline float HDFogArtWeight(float base)
{
	return base * HD_FOG_ART_WEIGHT;
}

/* RoomProbe's numbers of an additive range (hd_render.cpp,
 * fog_add_weight): its group, its .TRG part (a book's, -1 for any other),
 * its material, its box's centre in the world and in view space and its
 * half sizes, the bulb of the bracket the centre lies in (-1 for none) and
 * its rho, the range's own weight and the weight drawn, its swing and its
 * scroll. */
struct HDFogArtPart
{
	int group, part, material, bulb;
	float world[3], view[3], half[3];
	float rho, base, weight, period, scroll[2];
};

/* level 0: the ranges the last bracket that asked the additive pass drew,
 * one entry per draw; 1: every additive range of the level, with its own
 * weight. Writes at most `max` into `out` (which may be 0) and returns
 * how many there are. Defined in hd_render.cpp. */
int HDRenderFogArt(HDFogArtPart* out, int max, int level);
// RoomProbe's -fogartweight: w for the art's mist, -1 the rule.
void HDRenderFogArtProbe(float weight);
// RoomProbe's -fogstrength: s for every bulb's body, -1 the rule.
void HDRenderFogStrengthProbe(float strength);

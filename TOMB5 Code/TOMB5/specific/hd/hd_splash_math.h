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

/* The arithmetic of the water's cards (hd_render.cpp, "the cards"): a dive
 * splash's rings, crown and drops, a fish tank's spray and the wet film on
 * the tank's floor, as functions of the time alone. Nothing here draws,
 * holds state or takes the game's random numbers: a card's own differences
 * come out of HDCardHash of its splash's or spark's seed and its index, so
 * the game's random stream is the classic one, a picture of RoomProbe at a
 * given time is the picture the game draws at that age, and ModernTest
 * checks the motion without a device.
 *
 * Units are world units (y down), seconds and the game's ticks of 1/30 s.
 * The classic numbers are the engine's own: UpdateSplashes and
 * S_DrawSplashes for the splash (game\effect2.cpp, specificfx.cpp),
 * TriggerFishtankSpray, ControlFishtank and UpdateSparks for the tank's
 * spray (game\tower.cpp, effect2.cpp), ControlFloorLasers and GetFishTank
 * for the floor's state (game\lasers.cpp). */

// A splash as SetupSplash takes it (SPLASH_SETUP, global\types.h).
struct HDSplashSetup
{
	float x, y, z;		// world; y the water's surface
	int innerRad, innerSize, innerRadVel, innerYVel;
	int middleRad, middleSize, middleRadVel, middleYVel;
	int outerRad, outerSize, outerRadVel;
};

/* A splash at one moment, in world units: each ring's outer edge as
 * S_DrawSplashes draws it (twice the struct's radius plus its size - the
 * draw shifts by W2V_SHIFT - 1), the inner and middle columns' height over
 * the surface (InnerY and MiddleY over 8, as the draw takes them), the
 * classic life over its first 62, and whether the splash still lives. */
struct HDSplashNow
{
	float innerR, middleR, outerR;
	float innerW, middleW;		// the rings' widths, twice their sizes
	float innerH, middleH;
	float life;
	int alive;
};

// The splash's struct, one tick of UpdateSplashes at a time.
struct HDSplashTick
{
	int innerRad, innerSize, innerRadVel, innerY, innerYVel;
	int middleRad, middleSize, middleRadVel, middleY, middleYVel;
	int outerRad, outerSize, outerRadVel;
	int life, alive;
};

inline HDSplashTick HDSplashStart(const HDSplashSetup& s)
{
	HDSplashTick t;

	t.innerRad = s.innerRad;
	t.innerSize = s.innerSize;
	t.innerRadVel = s.innerRadVel;
	t.innerYVel = s.innerYVel;
	t.innerY = s.innerYVel >> 2;
	t.middleRad = s.middleRad;
	t.middleSize = s.middleSize;
	t.middleRadVel = s.middleRadVel;
	t.middleYVel = s.middleYVel;
	t.middleY = s.middleYVel >> 2;
	t.outerRad = s.outerRad;
	t.outerSize = s.outerSize;
	t.outerRadVel = s.outerRadVel;
	t.life = 62;
	t.alive = 1;
	return t;
}

/* UpdateSplashes for one splash, line for line, in ints: the struct's
 * fields are shorts, and no value of the three callers leaves a short. */
inline void HDSplashStep(HDSplashTick& t)
{
	if (!t.alive)
		return;

	t.innerRad += t.innerRadVel >> 5;
	t.innerSize += t.innerRadVel >> 6;
	t.innerRadVel -= t.innerRadVel >> 6;
	t.middleRad += t.middleRadVel >> 5;
	t.middleSize += t.middleRadVel >> 6;
	t.middleRadVel -= t.middleRadVel >> 6;
	t.outerRad += t.outerRadVel >> 5;
	t.outerSize += t.outerRadVel >> 6;
	t.outerRadVel -= t.outerRadVel >> 6;
	t.innerY += t.innerYVel >> 4;
	t.innerYVel += 0x400;

	if (t.innerYVel > 0x4000)
		t.innerYVel = 0x4000;

	if (t.innerY < 0)
	{
		if (t.innerY < -0x7000)
			t.innerY = -0x7000;
	}
	else
	{
		t.innerY = 0;
		t.life -= 2;

		if (t.life <= 0)
			t.alive = 0;
	}

	t.middleY += t.middleYVel >> 4;
	t.middleYVel += 0x380;

	if (t.middleYVel > 0x4000)
		t.middleYVel = 0x4000;

	if (t.middleY < 0)
	{
		if (t.middleY < -0x7000)
			t.middleY = -0x7000;
	}
	else
		t.middleY = 0;
}

inline void HDSplashMeasure(const HDSplashTick& t, HDSplashNow& o)
{
	o.innerR = 2.0f * (float)(t.innerRad + t.innerSize);
	o.middleR = 2.0f * (float)(t.middleRad + t.middleSize);
	o.outerR = 2.0f * (float)(t.outerRad + t.outerSize);
	o.innerW = 2.0f * (float)t.innerSize;
	o.middleW = 2.0f * (float)t.middleSize;
	o.innerH = (float)(-t.innerY) / 8.0f;
	o.middleH = (float)(-t.middleY) / 8.0f;
	o.life = (float)t.life / 62.0f;
	o.alive = t.alive;
}

/* The splash `seconds` after SetupSplash: the classic struct stepped over
 * the whole ticks and drawn on linearly to the next one, so the HD form
 * runs as smoothly at sixty frames as at thirty. The classic life is at
 * most 31 ticks after the column fell back, so 600 ticks (20 s) bound the
 * loop with room to spare. */
inline HDSplashNow HDSplashAt(const HDSplashSetup& s, float seconds)
{
	HDSplashNow now = {};
	HDSplashTick a = HDSplashStart(s);

	if (seconds < 0.0f)
		return now;

	const float ticks = seconds * 30.0f;
	const int whole = ticks < 600.0f ? (int)ticks : 600;
	const float part = ticks < 600.0f ? ticks - (float)whole : 0.0f;

	for (int i = 0; i < whole && a.alive; i++)
		HDSplashStep(a);

	HDSplashTick b = a;

	HDSplashStep(b);

	HDSplashNow na, nb;

	HDSplashMeasure(a, na);
	HDSplashMeasure(b, nb);

	if (!na.alive)
		return na;

	if (!nb.alive)
	{
		nb = na;
		nb.life = 0.0f;
	}

	now.innerR = na.innerR + (nb.innerR - na.innerR) * part;
	now.middleR = na.middleR + (nb.middleR - na.middleR) * part;
	now.outerR = na.outerR + (nb.outerR - na.outerR) * part;
	now.innerW = na.innerW + (nb.innerW - na.innerW) * part;
	now.middleW = na.middleW + (nb.middleW - na.middleW) * part;
	now.innerH = na.innerH + (nb.innerH - na.innerH) * part;
	now.middleH = na.middleH + (nb.middleH - na.middleH) * part;
	now.life = na.life + (nb.life - na.life) * part;
	now.alive = 1;
	return now;
}

/* A well mixed 32-bit hash (the multiply-xorshift finaliser of a 32-bit
 * integer hash, a textbook construction) and a share of 0..1 from it. */
inline unsigned int HDCardHash(unsigned int a, unsigned int b)
{
	unsigned int h = a * 0x9E3779B9u ^ (b + 0x7F4A7C15u);

	h ^= h >> 16;
	h *= 0x7FEB352Du;
	h ^= h >> 15;
	h *= 0x846CA68Bu;
	h ^= h >> 16;
	return h;
}

inline float HDCardShare(unsigned int a, unsigned int b)
{
	return (float)(HDCardHash(a, b) & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

// The pages a card takes: the remaster's spray, its second spray, drops.
enum
{
	HD_CARD_SPRAY = 0,
	HD_CARD_SPRAY2 = 1,
	HD_CARD_DROPS = 2
};

/* One card: its middle in the world, its half width and half height,
 * its share (0..1, the alpha before the page and the round cut), the page
 * and the window of it (u, v the corner, size the window's side, 0..1 of
 * the page), and its turn on the screen. */
struct HDCard
{
	float x, y, z;
	float half, tall;
	float alpha;
	int page;
	float u, v, size;
	float turn;
};

/* A splash's cards (S_DrawSplashes's rings and spray, drawn ours):
 *
 *   the crown   kSplashCrownInner cards on the inner ring and
 *               kSplashCrownMiddle on the middle ring, each standing at its
 *               column's height of the moment (InnerY or MiddleY over 8)
 *               times 0.7..1.0 and read at its own time, 0.85..1.15 of the
 *               splash's, so the crown is ragged and not one cylinder;
 *               as wide as the inner and middle rings' widths together
 *               over two, 24 to 160 units, as tall as its column and at
 *               least half its width - once the column has fallen back a
 *               low haze on the water -, leaning up to 0.3 radians either
 *               way; its share the classic life, 1.0 on the inner ring
 *               and 0.8 on the middle
 *   the drops   kSplashDrops cards of the drops page, thrown from the inner
 *               ring at its first width with 0.5..1.1 of the inner column's
 *               first speed and 0.5..1.5 times the inner ring's speed
 *               outward, under the column's own gravity, gone when they
 *               are back at the surface; 24..48 units wide, twice as tall
 *
 * The column's first speed is its first step, InnerYVel over 16 over 8 a
 * tick, its gravity 0x400 over 16 over 8 = 8 units a tick squared (7200
 * units/s^2) - the splash's own numbers, so the drops land as its column
 * does. The counts, the shares and the widths are free choices, set by eye
 * on the round's RoomProbe pictures. Returns the cards written, at most
 * `max`. */
const int kSplashCrownInner = 12;
const int kSplashCrownMiddle = 16;
const int kSplashDrops = 24;
const int kSplashCards = kSplashCrownInner + kSplashCrownMiddle + kSplashDrops;

inline int HDSplashCards(const HDSplashSetup& s, unsigned int seed,
	float seconds, HDCard* out, int max)
{
	int n = 0;
	const HDSplashNow now = HDSplashAt(s, seconds);

	if (!now.alive || max <= 0)
		return 0;

	const float kTwoPi = 6.2831853f;

	for (int k = 0; k < kSplashCrownInner + kSplashCrownMiddle && n < max;
		k++)
	{
		const bool inner = k < kSplashCrownInner;
		const int ring = inner ? kSplashCrownInner : kSplashCrownMiddle;
		const int at = inner ? k : k - kSplashCrownInner;
		const float angle = kTwoPi * ((float)at +
			HDCardShare(seed, k * 7 + 1)) / (float)ring;
		const float pace = 0.85f + 0.3f * HDCardShare(seed, k * 7 + 2);
		const HDSplashNow mine = HDSplashAt(s, seconds * pace);
		const float reach = 0.7f + 0.3f * HDCardShare(seed, k * 7 + 3);
		const float r = inner ? now.innerR : now.middleR;
		const float h = (inner ? mine.innerH : mine.middleH) * reach;
		const float half = 0.5f * (now.innerW + now.middleW);
		HDCard& c = out[n++];

		c.x = s.x + r * sinf(angle);
		c.z = s.z + r * cosf(angle);
		c.y = s.y - h * 0.5f;
		c.half = half < 24.0f ? 24.0f : half > 160.0f ? 160.0f : half;
		c.tall = h * 0.5f > 0.5f * c.half ? h * 0.5f : 0.5f * c.half;
		c.alpha = (mine.alive ? mine.life : 0.0f) * (inner ? 1.0f : 0.8f);
		c.page = HDCardShare(seed, k * 7 + 4) < 0.5f ? HD_CARD_SPRAY :
			HD_CARD_SPRAY2;
		c.size = 0.5f;
		c.u = 0.5f * HDCardShare(seed, k * 7 + 5);
		c.v = 0.5f * HDCardShare(seed, k * 7 + 6);
		c.turn = 0.6f * (HDCardShare(seed, k * 7 + 7) - 0.5f);
	}

	const HDSplashNow first = HDSplashAt(s, 0.0f);
	const float up0 = (float)(-s.innerYVel) / 16.0f / 8.0f * 30.0f;
	const float out0 = (float)(s.innerRadVel >> 5) * 2.0f * 30.0f;
	const float g = 7200.0f;

	for (int k = 0; k < kSplashDrops && n < max; k++)
	{
		const unsigned int key = 1000u + (unsigned int)k * 5u;
		const float up = up0 * (0.5f + 0.6f * HDCardShare(seed, key));
		const float side = out0 * (0.5f + HDCardShare(seed, key + 1));
		const float angle = kTwoPi * HDCardShare(seed, key + 2);
		const float h = up * seconds - 0.5f * g * seconds * seconds;

		if (h < 0.0f && seconds > 0.0f)
			continue;

		const float r = first.innerR + side * seconds;
		HDCard& c = out[n++];

		c.x = s.x + r * sinf(angle);
		c.z = s.z + r * cosf(angle);
		c.y = s.y - h;
		c.half = 24.0f + 24.0f * HDCardShare(seed, key + 3);
		c.tall = 2.0f * c.half;
		c.alpha = 1.0f;
		c.page = HD_CARD_DROPS;
		c.size = 0.5f;
		c.u = 0.5f * HDCardShare(seed, key + 4);
		c.v = 0.5f * HDCardShare(seed, key + 2);
		c.turn = 0.0f;
	}

	return n;
}

/* The splash's two rings on the sheet: the classic middle and outer rings'
 * outer edges as their fronts, the strength (a ring's slope, as the sheet's
 * rings take it) 1.0 and 0.6 of the classic life - a free choice, set by
 * eye on the round's RoomProbe pictures: twice the 0.5 a wading Lara's ring
 * starts with, for the biggest thing that meets the water. out[0..3] the
 * outer ring (front, strength) then the middle's. False when the splash is
 * gone. */
inline bool HDSplashRings(const HDSplashSetup& s, float seconds, float* out)
{
	const HDSplashNow now = HDSplashAt(s, seconds);

	if (!now.alive)
		return false;

	out[0] = now.outerR;
	out[1] = 1.0f * now.life;
	out[2] = now.middleR;
	out[3] = 0.6f * now.life;
	return true;
}

/* A fish tank's spark, TriggerFishtankSpray's, as UpdateSparks moves it -
 * with this file's hash in the place of GetRandomControl:
 *
 *   life      14..17 ticks; the colour climbs over its first 4 ticks
 *             (ColFadeSpeed), holds, and falls over its last 8 (FadeToBlack)
 *   velocity  x, z -128..127 in 1/32 units a tick, losing 1/16 a tick
 *             (Friction 4); y from -(0..15), gravity -16..-31 a tick, held
 *             at MaxYvel -8..-15 times 32: the spark RISES, 8 to 15 units a
 *             tick at its fastest - the tank's mist
 *   place     the point -8..7 in x and z
 *   size      Size from dSize / 4 to dSize (64..95) over the life, drawn
 *             with Scalar 3 as a radius of Size << 3 >> 1 - 64..95 units
 *             growing to 256..380
 *
 * HDTankPuff gives the card at `seconds` after the spark was made; false
 * when it is gone. `c` the strength TriggerFishtankSpray took (0..64 after
 * its halving), the share kTankSprayShare times c over 64. The card
 * stands on the spark's point, its middle its half height over it: the
 * spark starts on the floor (joint 0 lies 1 unit over the item), and a
 * card round its point would lie half under the floor and keep the rest
 * within the soft edge's 64 units of it.
 *
 * kTankSprayShare 0.8, the dive splash's crown's (1.0 on its inner ring,
 * 0.8 on its middle one): the spray is the same water thrown up. Counted
 * at 1 s (RoomProbe -tank with and without -tankleave spray, 1920 x
 * 1080, the tank's water drawn in both): the spray of RICH2 room 122's
 * tank touches 31.4 % of the picture at +48 brightness and of room 23's,
 * whose points lie inside the 256 high step before its glass, 3.5 %. */
const float kTankSprayShare = 0.8f;

inline bool HDTankPuff(float x, float y, float z, int c, unsigned int seed,
	float seconds, HDCard* out)
{
	if (seconds < 0.0f || c <= 0)
		return false;

	const unsigned int h = HDCardHash(seed, 77u);
	const int life0 = 14 + (int)(h & 3);
	const int dSize = 64 + (int)((h >> 2) & 31);
	const int size0 = dSize >> 2;
	int yvel = -(int)((h >> 7) & 15);
	const int gravity = -16 - (int)((h >> 11) & 15);
	const int maxYvel = -8 - (int)((h >> 15) & 7);
	int xvel = (int)((h >> 18) & 255) - 128;
	int zvel = (int)(HDCardHash(seed, 78u) & 255) - 128;
	int px = (int)(HDCardHash(seed, 79u) & 15) - 8;
	int py = 0;
	int pz = (int)(HDCardHash(seed, 80u) & 15) - 8;
	const float ticks = seconds * 30.0f;
	const int whole = (int)ticks;

	if (whole >= life0)
		return false;

	int life = life0;
	float px1 = 0.0f, py1 = 0.0f, pz1 = 0.0f;

	for (int i = 0; i <= whole; i++)
	{
		if (i == whole)
		{
			px1 = (float)px;
			py1 = (float)py;
			pz1 = (float)pz;
		}

		life--;
		yvel += gravity;

		if (yvel < (maxYvel << 5))
			yvel = maxYvel << 5;

		xvel -= xvel >> 4;
		zvel -= zvel >> 4;
		px += xvel >> 5;
		py += yvel >> 5;
		pz += zvel >> 5;
	}

	// Between the tick before and the one after, linearly.
	const float part = ticks - (float)whole;
	const float age = (float)(life0 - life) - 1.0f + part;
	const float grow = age / (float)life0;
	float share;

	if (age < 4.0f)
		share = age / 4.0f;
	else if ((float)life0 - age < 8.0f)
		share = ((float)life0 - age) / 8.0f;
	else
		share = 1.0f;

	out->x = x + px1 + ((float)px - px1) * part;
	out->y = y + py1 + ((float)py - py1) * part;
	out->z = z + pz1 + ((float)pz - pz1) * part;
	out->half = 4.0f * ((float)size0 + (float)(dSize - size0) * grow);
	out->tall = out->half;
	out->y -= out->tall;
	out->alpha = kTankSprayShare * (float)c / 64.0f *
		(share > 0.0f ? share : 0.0f);
	out->page = (h >> 26) & 1 ? HD_CARD_SPRAY : HD_CARD_SPRAY2;
	out->size = 0.5f;
	out->u = 0.5f * HDCardShare(seed, 81u);
	out->v = 0.5f * HDCardShare(seed, 82u);
	out->turn = 6.2831853f * HDCardShare(seed, 83u);
	return true;
}

/* How wet the tank's floor is, 0..1 - the film's strength (hd_render.cpp,
 * "the tank's film"). `tank` 0 before the tank breaks, 1 while it sprays
 * (ControlFishtank runs), 2 once KillItem took it; `flags1` its
 * item_flags[1]; `laser` 1 while the floor lasers live and run, their
 * trigger_flags `count`.
 *
 *   spraying  (4096 - flags1) / 1024, at most 1: item_flags[1] starts at
 *             4096 (InitialiseFishtank) and loses a 64th a tick, so the
 *             film comes up over the first 19 ticks, 0.63 s, the water
 *             spreading as it pours out
 *   after     count / 64 while the lasers live: GetFishTank holds their
 *             trigger_flags at 64 while the tank runs, ControlFloorLasers
 *             counts it down one a tick and kills them at 0 - the film
 *             dries with the lasers' own countdown, 2.13 s
 *   else      0
 *
 * 1024 is a free choice, set by eye on the round's RoomProbe pictures. */
inline float HDFilmStrength(int tank, int flags1, int laser, int count)
{
	if (tank == 1)
	{
		const float s = (float)(4096 - flags1) / 1024.0f;

		return s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
	}

	if (tank == 2 && laser && count > 0 && count <= 64)
		return (float)count / 64.0f;

	return 0.0f;
}

/* The tank's own clock, for RoomProbe's -tank: item_flags[1] after `ticks`
 * of ControlFishtank (it loses flags1 >> 6 a tick while over 819) and
 * whether the tank still runs: 4096 falls to 819 or under after 105 steps
 * of the integer arithmetic (810), so the tank sprays 105 ticks, 3.5 s,
 * and is killed on the next. */
inline int HDTankFlagsAfter(int ticks, int* running)
{
	int f = 4096;
	int t = 0;

	for (; t < ticks && f > 819; t++)
		f -= f >> 6;

	if (running)
		*running = f > 819 ? 1 : 0;

	return f;
}

/* The ticks ControlFishtank has run when item_flags[1] reads `flags1`: the
 * first step count whose value is at or under it (0 for 4096, 105 for
 * 810), so a frame that sees only the item's field knows the tank's age. */
inline int HDTankTicksOf(int flags1)
{
	int f = 4096;
	int t = 0;

	for (; f > flags1 && t < 106; t++)
		f -= f >> 6;

	return t;
}

/* A fish tank's water as its classic mesh shapes it - the FISHTANK mesh
 * DrawScaledSpike scales in y by item_flags[1] / 4096 (game\traps.cpp) -
 * measured once at level load in the tank's own frame: x along the glass,
 * y down from the item's place, z out of the glass into the room. In
 * RICH2 that mesh is the water pouring out of the broken glass: a flat top
 * 1,276 over the floor behind the glass and a curved apron from the glass's
 * top edge down to the floor 866 units in front of it.
 *
 *   top, floor    the highest and the lowest vertex
 *   glassZ        the front of the top: the furthest z among the vertices
 *                 within kTankBand of the top - where the water leaves the
 *                 tank
 *   footZ         the furthest z of all: the apron's reach on the floor
 *   topX0..topX1  the top's width, footX0..footX1 the foot's (the vertices
 *                 within kTankBand of the floor)
 *
 * kTankBand, 32 units, takes RICH2's top vertices (y -1276 and -1277) and
 * its floor ones (y 6) and none of the apron's next rows (y -1239 under
 * the top, -382 over the floor) - a choice on that count. */
const float kTankBand = 32.0f;		// units

struct HDTankShape
{
	float top, floor;
	float glassZ, footZ;
	float topX0, topX1;
	float footX0, footX1;
	int ok;
};

// `xyz` the mesh's vertices, three floats each, in the tank's frame.
inline HDTankShape HDTankShapeOf(const float* xyz, int n)
{
	HDTankShape s = {};

	if (!xyz || n <= 0)
		return s;

	s.top = 1.0e9f;
	s.floor = -1.0e9f;
	s.footZ = -1.0e9f;

	for (int i = 0; i < n; i++)
	{
		const float y = xyz[i * 3 + 1];
		const float z = xyz[i * 3 + 2];

		s.top = y < s.top ? y : s.top;
		s.floor = y > s.floor ? y : s.floor;
		s.footZ = z > s.footZ ? z : s.footZ;
	}

	s.glassZ = -1.0e9f;
	s.topX0 = s.footX0 = 1.0e9f;
	s.topX1 = s.footX1 = -1.0e9f;

	for (int i = 0; i < n; i++)
	{
		const float x = xyz[i * 3];
		const float y = xyz[i * 3 + 1];
		const float z = xyz[i * 3 + 2];

		if (y <= s.top + kTankBand)
		{
			s.glassZ = z > s.glassZ ? z : s.glassZ;
			s.topX0 = x < s.topX0 ? x : s.topX0;
			s.topX1 = x > s.topX1 ? x : s.topX1;
		}

		if (y >= s.floor - kTankBand)
		{
			s.footX0 = x < s.footX0 ? x : s.footX0;
			s.footX1 = x > s.footX1 ? x : s.footX1;
		}
	}

	s.ok = s.floor - s.top > kTankBand && s.footZ > s.glassZ ? 1 : 0;
	return s;
}

/* The burst when the glass breaks: the tank's water thrown out of the
 * opened glass on the tank's first ticks, before its apron stands - cards
 * in the tank's frame (x across, y down from the item's place, z out),
 * the caller turns them into the world.
 *
 *   kTankBurstSpray spray cards and kTankBurstDrops drops leave the glass
 *   (z glassZ) at a height h0 over the floor spread evenly over the
 *   opening (0 .. top) and a place across the top's width, each at its own
 *   moment within the first kTankBurstTime; out of the glass at the speed
 *   water leaves an opening under a column of its own depth,
 *   sqrt(2 g (height - h0)) (Torricelli), times 0.6..1.0, and up or down
 *   by up to kTankBurstLift, under g; gone when it is back on the floor
 *   or after kTankBurstLife. A spray card widens from its size to 2.5
 *   times it as it flies and fades with the share of its flight left; a
 *   drop stays whole.
 *
 * g = 4300 units/s^2 is 9.81 m/s^2 at 2.3 mm a unit (Lara's 768 units for
 * 1.77 m). From RICH2's 1,276 the fastest leave at 3,300 units/s and land
 * at most 1,276 units out (2 sqrt(d h) at d = h): the apron's own reach.
 * The counts, the time (0.2 s, 6 ticks), the lift (400 units/s), the life
 * (1 s), the sizes and the shares are free choices, set by eye on the
 * round's RoomProbe pictures. Returns the cards written, at most `max`. */
const int kTankBurstSpray = 40;
const int kTankBurstDrops = 24;
const int kTankBurstCards = kTankBurstSpray + kTankBurstDrops;
const float kTankBurstTime = 0.2f;	// s
const float kTankBurstLift = 400.0f;	// units/s
const float kTankBurstLife = 1.0f;	// s
const float kTankG = 4300.0f;		// units/s^2

inline int HDTankBurst(const HDTankShape& sh, unsigned int seed,
	float seconds, HDCard* out, int max)
{
	int n = 0;

	if (!sh.ok || seconds < 0.0f ||
		seconds > kTankBurstTime + kTankBurstLife || max <= 0)
		return 0;

	const float height = sh.floor - sh.top;

	for (int k = 0; k < kTankBurstCards && n < max; k++)
	{
		const bool drop = k >= kTankBurstSpray;
		const unsigned int key = 2000u + (unsigned int)k * 7u;
		const float t0 = kTankBurstTime * HDCardShare(seed, key);
		const float t = seconds - t0;

		if (t < 0.0f || t > kTankBurstLife)
			continue;

		const float h0 = height * HDCardShare(seed, key + 1);
		const float across = HDCardShare(seed, key + 2);
		const float speed = sqrtf(2.0f * kTankG * (height - h0)) *
			(0.6f + 0.4f * HDCardShare(seed, key + 3));
		const float lift = kTankBurstLift *
			(2.0f * HDCardShare(seed, key + 4) - 1.0f);
		const float h = h0 + lift * t - 0.5f * kTankG * t * t;

		if (h < 0.0f)
			continue;

		// The flight's whole length, for the share of it left.
		const float land = (lift + sqrtf(lift * lift + 2.0f * kTankG * h0)) /
			kTankG;
		const float left = land > 0.0f ? 1.0f - t / land : 0.0f;
		HDCard& c = out[n++];

		c.x = sh.topX0 + (sh.topX1 - sh.topX0) * across;
		c.y = sh.floor - h;
		c.z = sh.glassZ + speed * t;
		c.size = 0.5f;
		c.u = 0.5f * HDCardShare(seed, key + 5);
		c.v = 0.5f * HDCardShare(seed, key + 6);

		if (drop)
		{
			c.half = 24.0f + 24.0f * HDCardShare(seed, key + 5);
			c.tall = 2.0f * c.half;
			c.alpha = 1.0f;
			c.page = HD_CARD_DROPS;
			c.turn = 0.0f;
		}
		else
		{
			const float grow = 1.0f + 1.5f * (1.0f - left);

			c.half = (96.0f + 96.0f * HDCardShare(seed, key + 6)) * grow;
			c.tall = c.half;
			c.alpha = 0.7f * (left > 0.0f ? left : 0.0f);
			c.page = HDCardShare(seed, key + 4) < 0.5f ? HD_CARD_SPRAY :
				HD_CARD_SPRAY2;
			c.turn = 6.2831853f * HDCardShare(seed, key + 3);
		}
	}

	return n;
}

/* The tank's water itself - the classic mesh's own shape drawn as this
 * water (hd_render.cpp, "the tank's water") - at `seconds` after the tank
 * broke:
 *
 *   scale  item_flags[1] / 4096, DrawScaledSpike's y scale, the tank's own
 *          ticks (HDTankFlagsAfter) run on linearly between them: 1.0 down
 *          to 0.198 at the 105th
 *   reach  how far the apron has come out of the glass: 0 to 1 over
 *          kTankDomeOut, 0.25 s - 866 units out in 0.25 s is 3,460 units/s,
 *          the speed water leaves the glass under its whole 1,276 (the
 *          burst's own, sqrt(2 g h) = 3,310)
 *   share  0 to 1 over kTankDomeIn and back to 0 over kTankDomeEnd's last
 *          kTankDomeFade: KillItem takes the tank on its 106th tick
 *          (3.53 s), and the classic mesh is gone in that frame - the water
 *          is gone half a second sooner instead of at once
 *
 * kTankDomeIn, 2 ticks, and kTankDomeFade, 0.5 s, are free choices. */
const float kTankDomeOut = 0.25f;		// s
const float kTankDomeIn = 2.0f / 30.0f;		// s
const float kTankDomeEnd = 106.0f / 30.0f;	// s
const float kTankDomeFade = 0.5f;		// s

struct HDTankDome
{
	float scale, reach, share;
	int alive;
};

inline HDTankDome HDTankDomeAt(float seconds)
{
	HDTankDome d = {};

	if (seconds < 0.0f || seconds >= kTankDomeEnd)
		return d;

	const float ticks = seconds * 30.0f;
	const int whole = (int)ticks;
	const float a = (float)HDTankFlagsAfter(whole, 0);
	const float b = (float)HDTankFlagsAfter(whole + 1, 0);
	const float in = seconds / kTankDomeIn;
	const float out = (kTankDomeEnd - seconds) / kTankDomeFade;
	const float reach = seconds / kTankDomeOut;

	d.scale = (a + (b - a) * (ticks - (float)whole)) / 4096.0f;
	d.reach = reach < 1.0f ? reach : 1.0f;
	d.share = (in < 1.0f ? in : 1.0f) * (out < 1.0f ? out : 1.0f);
	d.alive = 1;
	return d;
}

/* One vertex of the tank's mesh at that moment, in the tank's frame:
 * scaled in y as DrawScaledSpike scales it, the apron (the part out of the
 * glass, z over glassZ) drawn in towards the glass by its reach; and the
 * vertex's way down the water from the glass's top - its run out of the
 * glass plus its drop under the top, less than 0 on the top behind the
 * glass - for the page to run down the water. out[0..3] x, y, z, way. */
inline void HDTankDomePoint(const HDTankShape& sh, const HDTankDome& d,
	const float* in, float* out)
{
	const float y = in[1] * d.scale;
	const float z = in[2] > sh.glassZ ?
		sh.glassZ + (in[2] - sh.glassZ) * d.reach : in[2];

	out[0] = in[0];
	out[1] = y;
	out[2] = z;
	out[3] = (z - sh.glassZ) + (y - sh.top * d.scale);
}

/* How the water's surface is lit as a card: a share kTankDomeBody where the
 * eye looks straight at it and up to kTankDomeBody + kTankDomeRim where it
 * looks along it, (1 - |cos|)^2 between - the water's edge reads as a
 * thicker sheet, as a curved glassy surface does - times the dome's share;
 * the page (the remaster's spray, 3243) runs down the water at
 * kTankDomeFall, half the speed water reaches falling the dome's height
 * (sqrt(2 g 1,282) / 2 = 1,660 units/s), stretched along the way down -
 * one page every kTankDomeAcross across and every kTankDomeAlong down, so
 * its foam blotches draw out into the streaks of falling water. The
 * shares and the two lengths are free choices, set by eye on the round's
 * RoomProbe pictures: 0.3 and one page every 512 units both ways draw a
 * blotched cloud over the room. */
const float kTankDomeBody = 0.15f;
const float kTankDomeRim = 0.5f;
const float kTankDomeFall = 1660.0f;	// units/s
const float kTankDomeAcross = 384.0f;	// units
const float kTankDomeAlong = 2048.0f;	// units

inline float HDTankDomeShare(const HDTankDome& d, float cosine)
{
	const float c = cosine < 0.0f ? -cosine : cosine;
	const float edge = 1.0f - (c > 1.0f ? 1.0f : c);

	return d.share * (kTankDomeBody + kTankDomeRim * edge * edge);
}

/* The wet floor (hd_render.cpp, "the wet floor"): how far a breaking
 * tank's water has run over the floor at `seconds` after it broke, along
 * its way out of the glass, and how wet a point `way` along it is then.
 *
 *   the front  under the dome its apron's own reach (`reach`, the foot's
 *              distance from the glass, 866 units in RICH2), which it
 *              comes out to over kTankDomeOut; past the foot the edge of
 *              a shallow flood, at the speed a wave runs in water of the
 *              depth the tank's film stands at while it pours (8 + 16
 *              units, hd_render.cpp kFilmHair and kFilmRise):
 *              sqrt(g h) = sqrt(4300 x 24) = 321 units/s, 0.74 m/s
 *   the wet    0 ahead of the front and 1 kWetRamp behind it, linear
 *              between - the front's edge a quarter square wide
 *
 * kWetRagged pushes each point of the grid back along the way by up to
 * 384 units, out of a hash of its place (hd_render.cpp), so the front's
 * edge is ragged rather than a ring round the glass; 256 and 384 (a
 * quarter square and one and a half of the grid's 256-unit steps) are free
 * choices, set by eye on the round's RoomProbe pictures.
 *
 * kWetRadius: the front stops this far past the dome's foot, along the
 * way. The water that lands is the dome's, 3186 units wide at the floor
 * in RICH2 (the foot's width, HDTankShapeOf); it spreads past the foot by
 * half that width, 1593, taken up to the run's whole squares: 2048, two
 * squares. A free choice on that ground: in RICH2 it wets room 122 in
 * front of the glass, 45 and 121 as far as their floors run level, the
 * corridor 46 one square into its mouth, and room 23's lowered floor but
 * its far two squares (RoomProbe -tankwetmap). */
const float kWetSpread = 321.0f;	// units/s
const float kWetRamp = 256.0f;		// units
const float kWetRagged = 384.0f;	// units
const float kWetRadius = 2048.0f;	// units past the dome's foot

inline float HDWetFront(float seconds, float reach)
{
	if (seconds <= 0.0f)
		return -1.0e9f;

	if (seconds < kTankDomeOut)
		return reach * seconds / kTankDomeOut;

	return reach + kWetSpread * (seconds - kTankDomeOut);
}

/* A ring's strength on the sheets (hd_render.cpp, "the rings"): `slope`
 * times its first size over 128, and `body` times more for a ring that
 * starts over 128 (a floating body's), times the square of `left`, the
 * share of its life left. */
inline float HDRingStrength(float slope, float body, float size, float left)
{
	const float first = size / 128.0f;

	return slope * first * (first > 1.0f ? body : 1.0f) * left * left;
}

inline float HDWetAt(float front, float way)
{
	const float w = (front - way) / kWetRamp;

	return w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
}

/* The sloping floors under a wet floor (hd_render.cpp, "the sloping
 * floors"). HDWetSlope: whether a floor entry's function (the type word's
 * low five bits) is one the water runs over - a TILT (2), a SPLIT1/2 (7,
 * 8) or a NOCOLF (11 to 14). HDWetHeight: a floor's height at px, pz (0 to
 * 1024) inside its square, out of its corners' heights c[4] - x0z0, x1z0,
 * x0z1, x1z1 - on the plane of the triangle the point lies in, as the
 * engine's GetHeight divides a split: functions 7, 11 and 12 along the
 * diagonal from corner 1 to corner 2, 8, 13 and 14 from corner 0 to
 * corner 3; a tilt and a flat floor are one plane. HDWetPasses: whether
 * water crosses an edge whose corners stand at a0, a1 on the side it
 * comes from and b0, b1 on the other: at each corner level or lower by
 * `step` at most (y counts down). kWetSlopeRise: how far a point of a
 * slope may stand over the edge the water came in by and still be wet,
 * the corners' rounding to whole units. HDWetCrease: whether a split's
 * crease runs from corner 1 to corner 2 (7, 11, 12) and not from corner 0
 * to corner 3. HDWetCell: the triangles of one 256-unit cell of the wet
 * layer, whose grid points a (x0z0), b (x1z0), d (x0z1) and e (x1z1) are
 * numbered or -1 where dry; the cell is halved along the diagonal its
 * square's crease follows, so no triangle crosses the crease and a
 * split's wet half is covered whole, and a half is drawn where its three
 * points are wet. It writes up to six numbers to `tri` and returns how
 * many. */
const int kWetSlopeRise = 8;		// units

inline bool HDWetSlope(int fn)
{
	return fn == 2 || fn == 7 || fn == 8 || (fn >= 11 && fn <= 14);
}

inline bool HDWetCrease(int fn)
{
	return fn == 7 || fn == 11 || fn == 12;
}

inline int HDWetCell(int fn, int a, int b, int d, int e, int* tri)
{
	const bool cross = HDWetCrease(fn);
	const int half[6] =
	{
		a, b, cross ? d : e,
		cross ? b : a, e, d
	};
	int n = 0;

	for (int h = 0; h < 6; h += 3)
		if (half[h] >= 0 && half[h + 1] >= 0 && half[h + 2] >= 0)
			for (int k = 0; k < 3; k++)
				tri[n++] = half[h + k];

	return n;
}

inline float HDWetHeight(int fn, const int* c, float px, float pz)
{
	const float u = px / 1024.0f, v = pz / 1024.0f;
	const bool fam1 = HDWetCrease(fn);

	if (fn == 2 || !fn)
		return (float)c[0] + (float)(c[1] - c[0]) * u +
			(float)(c[2] - c[0]) * v;

	if (fam1)
		return px > 1024.0f - pz ?
			(float)c[3] + (float)(c[2] - c[3]) * (1.0f - u) +
			(float)(c[1] - c[3]) * (1.0f - v) :
			(float)c[0] + (float)(c[1] - c[0]) * u +
			(float)(c[2] - c[0]) * v;

	return px > pz ?
		(float)c[0] + (float)(c[1] - c[0]) * u + (float)(c[3] - c[1]) * v :
		(float)c[0] + (float)(c[2] - c[0]) * v + (float)(c[3] - c[2]) * u;
}

inline bool HDWetPasses(int a0, int a1, int b0, int b1, int step)
{
	return b0 >= a0 && b0 - a0 <= step && b1 >= a1 && b1 - a1 <= step;
}

/* A plane carried through a matrix (hd_render.cpp, "the models in the
 * mirror"): for row vectors p' = p T, the plane c with c.p' = w.p for every
 * p, c = T^-1 w - the wet squares' bounds from the world into view space and
 * on into the mirrored projection's clip space, where the device takes a
 * clip plane while a vertex shader draws. False for a singular T. */
inline bool HDWetPlaneTo(const float* T, const float* w, float* c)
{
	double a[4][8];

	for (int r = 0; r < 4; r++)
		for (int k = 0; k < 8; k++)
			a[r][k] = k < 4 ? (double)T[r * 4 + k] : (k - 4 == r ? 1.0 : 0.0);

	for (int col = 0; col < 4; col++)
	{
		int best = col;

		for (int r = col + 1; r < 4; r++)
			if ((a[r][col] < 0 ? -a[r][col] : a[r][col]) >
				(a[best][col] < 0 ? -a[best][col] : a[best][col]))
				best = r;

		if (a[best][col] == 0.0)
			return false;

		for (int k = 0; k < 8; k++)
		{
			const double t = a[col][k];

			a[col][k] = a[best][k];
			a[best][k] = t;
		}

		const double d = a[col][col];

		for (int k = 0; k < 8; k++)
			a[col][k] /= d;

		for (int r = 0; r < 4; r++)
			if (r != col && a[r][col] != 0.0)
			{
				const double f = a[r][col];

				for (int k = 0; k < 8; k++)
					a[r][k] -= f * a[col][k];
			}
	}

	for (int r = 0; r < 4; r++)
	{
		double s = 0.0;

		for (int k = 0; k < 4; k++)
			s += a[r][4 + k] * (double)w[k];

		c[r] = (float)s;
	}

	return true;
}

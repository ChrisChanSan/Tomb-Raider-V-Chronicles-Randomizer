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
 *               over two, at least 24 units, and as tall as its column,
 *               leaning up to 0.3 radians either way; its share the
 *               classic life, 1.0 on the inner ring and 0.8 on the middle
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
		c.half = half > 24.0f ? half : 24.0f;
		c.tall = h * 0.5f > c.half ? h * 0.5f : c.half;
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
 * its halving), the share 0.35 times c over 64 - 0.35 a free choice, set by
 * eye on the round's RoomProbe pictures. */
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
	out->alpha = 0.35f * (float)c / 64.0f * (share > 0.0f ? share : 0.0f);
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

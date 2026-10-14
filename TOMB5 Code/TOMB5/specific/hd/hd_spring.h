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

/* The springs: a jet of water that rises from the floor of a pool to its
 * sheet - its rings on the sheet, a few small bubbles in the jet and the
 * rising current, a column that bends what stands behind it. This file
 * holds the rules alone: the table, the hashed clock, the bubbles' path
 * and the file row's numbers, without the engine and without Direct3D,
 * so the engine, RoomProbe and ModernTest share them. hd_spring.cpp
 * builds a level's springs, rings the sheets and draws them. This
 * project's own design; every random choice is a hash of the
 * spring's or the body's numbers, never a draw of a generator. */
#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

enum
{
	HD_SPRING_TABLE,	// kHDSprings, compiled in
	HD_SPRING_FILE,		// a `spring` row of hd_water.txt
	HD_SPRING_RULE		// the placement rule (HDSpringRulePick)
};

// One spring of a level.
struct HDSpring
{
	char stem[16];
	int room;		// the room its row names, -1 for the rule's
	float x, z;		// world, the jet's place
	int floorY;		// world y of the floor it rises from
	int surfaceY;		// world y of the sheet over it
	float strength;		// 0..1
	float period;		// seconds from one of its rings to the next
	int body;		// its water body (hd_render.h), -1 for none
	int how;		// HD_SPRING_TABLE, _FILE or _RULE
	int rings;		// 1 rings of its own, 0 a ripple row rings there
	unsigned seed;		// the hash every choice of it starts from
};

/* The compiled table. ANDREA2 room 194's fountain basin over water room
 * 120: the middle of its six water squares (x 54,272 to 57,344, z 31,744
 * to 33,792), the floor 2,304 under the surface at 256 under all six
 * (RoomProbe -life LIFESQUARES on the classic level), a ring every 0.7
 * seconds - the period tuned in the game with a ripple row of
 * hd_water.txt. A `spring` row of the file for the same level and room
 * changes the entry (HDSpringFileApply). */
struct HDSpringRow
{
	const char* stem;
	int room;
	float x, z;
	int floorY;
	float strength, period;
};

static const HDSpringRow kHDSprings[] =
{
	{ "ANDREA2", 194, 55808.0f, 32768.0f, 2560, 1.0f, 0.7f },
};

/* The rings. A ring of size 80 at strength 1, the size tuned with the
 * same row - a jet's landing, smaller than a wading Lara's 112 to 127
 * (game\effects.cpp, WadeSplash) - and in step with the strength under it
 * (a ring's slope is its size over 128, hd_render.cpp "the rings"), never
 * under 16. A spring of the rule rings every 0.7 s over its strength: a
 * weaker jet lands less often. Each ring starts up to a fifth of a period
 * early or late, a hash of the spring and the ring's count, so the train
 * does not tick. Only springs within kHDSpringRingReach of the eye ring
 * the sheets: the drips' reach (hd_life_rules.h, kHDLifeDripReach), so
 * far springs do not crowd the 64 rings the sheets keep. Sizes, share and
 * reach free choices, set by eye on the round's RoomProbe pictures. */
static const float kHDSpringRingSize = 80.0f;
static const float kHDSpringRingLeast = 16.0f;
static const float kHDSpringPeriod = 0.7f;
static const float kHDSpringJitter = 0.4f;	// the whole span, periods
static const float kHDSpringRingReach = 6144.0f;

/* The bubbles. Three a second at strength 1 (two to four was asked for:
 * three the middle), each born at its own hashed moment within its
 * slot of a third of a second, over the jet's core: a hashed place within
 * kHDSpringBubbleSpread of the jet's line, which widens with the column.
 * They rise at 200 to 300 units a second, a hash per bubble: a few
 * millimetres of air rise at about 0.25 m/s through still water (110
 * units at 2.3 mm a unit), and the jet carries them faster. The classic
 * bubble (game\tomb4fx.cpp, UpdateBubbles) speeds up a tick at a time
 * without a cap - 883 units a second after the basin's 2,304 - which a
 * bubble in a steady jet does not. They sway round their line with the
 * classic bubble's form: about 20 units across one way and 7 the other
 * at a period of 1.42 s there (y_rot 6 a tick), here 8 to 20 and a
 * third of it the other way at 1.0 to 1.8 s, a hash each. Their radius
 * is 5 to 12 units, the classic breath bubble's drawn size over its rise
 * (DrawBubbles: half of half its size, 4 to 7.5 at birth, growing). One
 * in three, by hash, leaves a small ring where it reaches the sheet, of
 * the classic bubble's size there (SetupRipple, 48 to 63, flags 2). Rate,
 * speeds, sway and share free choices, set by eye on the round's RoomProbe
 * pictures of ANDREA2's fountain. */
static const float kHDSpringRate = 3.0f;	// a second, at strength 1
static const float kHDSpringRiseMin = 200.0f;
static const float kHDSpringRiseSpan = 100.0f;
static const float kHDSpringSwayMin = 8.0f;
static const float kHDSpringSwaySpan = 12.0f;
static const float kHDSpringSwayPeriod = 1.0f;
static const float kHDSpringSwayPeriodSpan = 0.8f;
static const float kHDSpringBubbleMin = 5.0f;
static const float kHDSpringBubbleSpan = 7.0f;
static const float kHDSpringBubbleSpread = 0.4f;	// of the half-width
static const float kHDSpringBubbleIn = 0.25f;	// seconds to full
static const int kHDSpringPopEvery = 3;
static const int kHDSpringPopSize = 48;

/* The rising current: a column from the floor to the sheet, half as wide
 * at the floor as 32 units and 96 for each step of strength (128 at
 * strength 1), widening by a tenth of its height as a round jet does (its
 * half-width grows about a tenth of the way it has run), its bend strongest
 * on its line and gone at its edge, falling with height as a jet's speed
 * falls, as kHDSpringNear over kHDSpringNear plus the height. The bend is
 * at most kHDSpringBend units of view space at strength 1, its pattern
 * rising at kHDSpringJet units a second. Seen from far the column is
 * thin: it fades out while its half-width at mid height falls from 16 to
 * 6 pixels, where a bend of a few pixels cannot be read. Free choices,
 * set by eye on the round's RoomProbe pictures of the fountain. */
static const float kHDSpringCore = 32.0f;
static const float kHDSpringCoreGain = 96.0f;
static const float kHDSpringSpread = 0.1f;
static const float kHDSpringNear = 1200.0f;
static const float kHDSpringBend = 32.0f;
static const float kHDSpringJet = 400.0f;
static const float kHDSpringFadeFrom = 16.0f;	// pixels, fully drawn
static const float kHDSpringFadeTo = 6.0f;	// pixels, gone

/* The life's mixer (hd_life_rules.h, HDLifeHash), 0 .. 32767: two numbers
 * in, a spread value out. */
inline unsigned HDSpringHash(unsigned a, unsigned b)
{
	unsigned s = a * 0x41C64E6Du + 12345u;

	s ^= b * 0x9E3779B1u;
	s = s * 0x41C64E6Du + 12345u;
	s = s * 0x41C64E6Du + 12345u;
	return (s >> 10) & 0x7FFF;
}

// A hash as 0..1: a number of the spring's, a count, a salt per choice.
inline float HDSpringUnit(unsigned seed, int count, unsigned salt)
{
	return (float)HDSpringHash(seed ^ (salt * 0x2545F491u),
		(unsigned)count) / 32767.0f;
}

// A level's name as a number, its letters case-blind.
inline unsigned HDSpringStemHash(const char* stem)
{
	unsigned h = 2166136261u;

	for (const char* c = stem; c && *c; c++)
	{
		const unsigned char ch = (unsigned char)(*c >= 'a' && *c <= 'z' ?
			*c - 32 : *c);

		h = (h ^ ch) * 16777619u;
	}

	return h;
}

// A spring's seed: its level and its place.
inline unsigned HDSpringSeed(const char* stem, float x, float z)
{
	return HDSpringHash(HDSpringStemHash(stem),
		(unsigned)(int)x * 31u + (unsigned)(int)z);
}

// Its ring's size and its period out of its strength.
inline float HDSpringRingSize(float strength)
{
	const float s = kHDSpringRingSize * strength;

	return s > kHDSpringRingLeast ? s : kHDSpringRingLeast;
}

inline float HDSpringRulePeriod(float strength)
{
	return kHDSpringPeriod / (strength > 0.1f ? strength : 0.1f);
}

// When the spring's ring number `j` starts, seconds on the level's clock.
inline double HDSpringRingBorn(const HDSpring& s, int j)
{
	return ((double)j + kHDSpringJitter * (HDSpringUnit(s.seed, j, 1u) -
		0.5)) * (double)s.period;
}

// The bubbles a second, and when bubble number `k` is born.
inline float HDSpringBubbleRate(const HDSpring& s)
{
	return kHDSpringRate * s.strength;
}

inline double HDSpringBubbleBorn(const HDSpring& s, int k)
{
	const float rate = HDSpringBubbleRate(s);

	return rate > 0.0f ? ((double)k + HDSpringUnit(s.seed, k, 2u)) /
		(double)rate : 1.0e30;
}

// The column's half-width at `h` units over the floor.
inline float HDSpringHalfWidth(const HDSpring& s, float h)
{
	return kHDSpringCore + kHDSpringCoreGain * s.strength +
		kHDSpringSpread * h;
}

// A bubble at a moment: world place, radius and its share 0..1.
struct HDSpringBubbleAt
{
	float x, y, z;
	float radius;
	float share;
	float top;	// seconds from its birth to the sheet
};

/* Bubble `k` at clock `t`: false before its birth and after it reached
 * the sheet. Its line starts within kHDSpringBubbleSpread of the jet's
 * half-width at the floor and widens with the column. */
inline bool HDSpringBubble(const HDSpring& s, int k, double t,
	HDSpringBubbleAt* out)
{
	const double born = HDSpringBubbleBorn(s, k);
	const float depth = (float)(s.floorY - s.surfaceY);
	const float speed = kHDSpringRiseMin + kHDSpringRiseSpan *
		HDSpringUnit(s.seed, k, 3u);
	const float age = (float)(t - born);

	out->top = depth / speed;

	if (age < 0.0f || depth <= 0.0f || age >= out->top)
		return false;

	const float h = speed * age;
	const float way = 6.2831853f * HDSpringUnit(s.seed, k, 4u);
	const float off = kHDSpringBubbleSpread * HDSpringUnit(s.seed, k, 5u) *
		HDSpringHalfWidth(s, h);
	const float sway = kHDSpringSwayMin + kHDSpringSwaySpan *
		HDSpringUnit(s.seed, k, 6u);
	const float period = kHDSpringSwayPeriod + kHDSpringSwayPeriodSpan *
		HDSpringUnit(s.seed, k, 7u);
	const float phase = 6.2831853f * (age / period +
		HDSpringUnit(s.seed, k, 8u));
	const float c = cosf(way), n = sinf(way);
	const float a = sway * sinf(phase), b = sway / 3.0f * cosf(phase);

	out->x = s.x + c * off + a * c - b * n;
	out->z = s.z + n * off + a * n + b * c;
	out->y = (float)s.floorY - h;
	out->radius = kHDSpringBubbleMin + kHDSpringBubbleSpan *
		HDSpringUnit(s.seed, k, 9u);
	out->share = age < kHDSpringBubbleIn ? age / kHDSpringBubbleIn : 1.0f;
	return true;
}

// Whether bubble `k` leaves a ring at the sheet, and that ring's size.
inline bool HDSpringBubblePops(const HDSpring& s, int k, float* size)
{
	const unsigned h = HDSpringHash(s.seed ^ 0x5bd1e995u, (unsigned)k);

	*size = (float)(kHDSpringPopSize + (int)(h & 15u));
	return (h >> 4) % (unsigned)kHDSpringPopEvery == 0;
}

/* A `spring` key of hd_water.txt: 'spring <x> <z> <strength> [<period>]'
 * after the row's level, room and class. tok[at] is the key; the numbers
 * follow. x and z anywhere in a level, the strength 0 to 1 (0 takes the
 * spring away), the period 0.1 to 60 seconds - without it, 0 into
 * out[3], the table's or the rule's. Returns the tokens taken, the key
 * among them, or -1 for a key that cannot be read. */
inline int HDSpringParseKey(char* const* tok, int n, int at, float* out)
{
	if (at + 3 >= n)
		return -1;

	static const double lo[4] = { -1.0e7, -1.0e7, 0.0, 0.1 };
	static const double hi[4] = { 1.0e7, 1.0e7, 1.0, 60.0 };
	int count = 3;

	out[3] = 0.0f;

	if (at + 4 < n)
	{
		char* end = 0;

		strtod(tok[at + 4], &end);
		count = (*end == 0 && end != tok[at + 4]) ? 4 : 3;
	}

	for (int k = 0; k < count; k++)
	{
		char* end = 0;
		const double v = strtod(tok[at + 1 + k], &end);

		if (*end || end == tok[at + 1 + k] || !(v >= lo[k] && v <= hi[k]))
			return -1;

		out[k] = (float)v;
	}

	return count + 1;
}

/* What a row of the file does to the level's springs: one for the level
 * and room of a spring already there changes its place, strength and
 * period (a period of 0 keeps it) and a strength of 0 takes it away; any
 * other adds a spring. The caller finds the floor and the sheet after. */
inline void HDSpringFileApply(std::vector<HDSpring>& springs, const char* stem,
	int room, const float* key)
{
	for (size_t i = 0; i < springs.size(); i++)
	{
		HDSpring& s = springs[i];

		if (s.room != room || s.how == HD_SPRING_RULE)
			continue;

		if (key[2] <= 0.0f)
		{
			springs.erase(springs.begin() + (std::ptrdiff_t)i);
			return;
		}

		s.x = key[0];
		s.z = key[1];
		s.strength = key[2];
		s.period = key[3] > 0.0f ? key[3] : s.period;
		s.floorY = 0;
		s.how = HD_SPRING_FILE;
		s.seed = HDSpringSeed(stem, s.x, s.z);
		return;
	}

	if (key[2] <= 0.0f)
		return;

	HDSpring s = {};

	strncpy(s.stem, stem, sizeof(s.stem) - 1);
	s.room = room;
	s.x = key[0];
	s.z = key[1];
	s.strength = key[2];
	s.period = key[3] > 0.0f ? key[3] : HDSpringRulePeriod(key[2]);
	s.body = -1;
	s.how = HD_SPRING_FILE;
	s.rings = 1;
	s.seed = HDSpringSeed(stem, s.x, s.z);
	springs.push_back(s);
}

/* hd_spring.cpp. HDSpringBuild lays the level's springs onto its water
 * after the sheets are built; HDSpringRings hands their rings to the
 * sheets once a frame before they draw (the eye's world place picks the
 * springs near enough); HDSpringFrameDraw draws the columns and the
 * bubbles after the sheets. HDSpringAdd is RoomProbe's -spring,
 * HDSpringTiming and HDSpringDrawn its numbers, HDSpringParts its
 * -springparts; HDSpringLogLine writes the SPRINGLOG lines. The engine
 * calls the five after them (HDLevelBuild, HDLevelUnload, FeedWaterRipples,
 * DrawWaterCards, HDLevelDeviceLost). */
void HDSpringClear();
void HDSpringBuild(const char* stem);
bool HDSpringAdd(const char* stem, float x, float z, float strength);
int HDSpringCount();
const HDSpring* HDSpringAt(int i);
void HDSpringRings(float eyeX, float eyeY, float eyeZ);
void HDSpringFrameDraw(const float* toFrame);
void HDSpringTiming(int on, double* ms);
void HDSpringDrawn(int* columns, int* bubbles);
void HDSpringParts(int mask);
bool HDSpringLogLine(int at, char* out, size_t size);
void HDSpringRelease();

void HDSpringLevelBegin(const char* stem);
void HDSpringLevelEnd();
void HDSpringFeed();
void HDSpringDraw(const float* toFrame);
void HDSpringDeviceLost();

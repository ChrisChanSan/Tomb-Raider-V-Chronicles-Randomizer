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
 * sheet - its rings on the sheet, a few small bubbles in the jet, the
 * rising current, a column that bends what stands behind it, and the crack
 * in the floor it rises out of. This file holds the rules alone: the table,
 * the hashed clock, the bubbles' path, the crack's shape, the file row's
 * numbers and the placement rule, without the engine and
 * without Direct3D, so the engine, RoomProbe and ModernTest share them.
 * hd_spring.cpp builds a level's springs, rings the sheets and draws
 * them. This project's own design; every random choice is a hash of the
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
 * cells 24 units across and 96 along the line (hd_spring.cpp), rising at
 * kHDSpringJet units a second: streaks that rise. Seen from far the column
 * is thin: it fades out while its half-width at mid height falls from 16
 * to 6 pixels, where a bend of a few pixels cannot be read. Free choices,
 * set by eye on RoomProbe pictures of the fountain from under the water
 * (960 x 540, the column alone, pixels changed by 8 levels or more at 1
 * and 2 s): a bend of 32 with square cells of 48 and the weight taken on
 * the bend and on the share both, 276 to 370, not told from the water
 * without it; 96 with the streaks, 3,919 to 4,586, a band that wavers;
 * 128 with cells of 32 by 128, 5,236 to 5,718, the wall smeared. */
static const float kHDSpringCore = 32.0f;
static const float kHDSpringCoreGain = 96.0f;
static const float kHDSpringSpread = 0.1f;
static const float kHDSpringNear = 1200.0f;
static const float kHDSpringBend = 96.0f;
static const float kHDSpringJet = 400.0f;
static const float kHDSpringFadeFrom = 16.0f;	// pixels, fully drawn
static const float kHDSpringFadeTo = 6.0f;	// pixels, gone

/* The placement rule ("sporadically under water where it fits"). A
 * candidate square draws a sheet, has 768 units of water or more under it
 * (the life's deep water, hd_life_rules.h kHDLifeDeep), lies three squares
 * or more from every opening of the water (a portal through a wall that
 * reaches into the water and leads out of the body, HDSpringOpening) and
 * from Lara's start, and under no static. A
 * body that is no river, has no current and holds kHDSpringSquares such
 * squares or more is one in kHDSpringEvery times the key's value
 * (HDSpringRulePick) to hold one spring, at a hashed candidate square's
 * middle, its strength 0.5 to 1 by hash. Six squares: a body the life
 * gives a school of fish (hd_life_rules.h, kHDLifeDeepSquares). One in
 * two: in the fifteen levels five bodies hold six candidates, and the
 * hash picks three of them in three levels at one in two, one at one in
 * three (RoomProbe -springs on the classic levels) - a spring found now
 * and then, not one per pond. */
static const int kHDSpringDeep = 768;
static const float kHDSpringClear = 3072.0f;	// three squares
static const int kHDSpringSquares = 6;
static const int kHDSpringEvery = 2;

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

/* The column's noise table (hd_spring.hlsl, SpringNoise): 64 by 64
 * texels, red and green each the mixer of the texel's number and a salt of
 * its own, 0..255. Over the table the two channels measure a mean of 127.0
 * and 128.1, a spread of 73.9 and 73.6 (a uniform byte 73.9) and a
 * correlation of neighbours and of the two channels under 0.03. */
static const int kHDSpringNoiseSide = 64;
static const unsigned kHDSpringNoiseSalt[2] = { 0x5A17u, 0xC0DEu };

inline unsigned char HDSpringNoiseTexel(int x, int y, int channel)
{
	return (unsigned char)(HDSpringHash((unsigned)(y * kHDSpringNoiseSide +
		x), kHDSpringNoiseSalt[channel & 1]) >> 7);
}

// A hash as 0..1: a number of the spring's, a count, a salt per choice.
inline float HDSpringUnit(unsigned seed, int count, unsigned salt)
{
	return (float)HDSpringHash(seed ^ (salt * 0x2545F491u),
		(unsigned)count) / 32767.0f;
}

// A level's name as a number, its letters case-blind (FNV-1a, 32 bits).
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

/* The column in a frame, its foot `b` and its top `t` in view space, z the
 * depth in front of the eye. The part drawn is the part more than
 * kHDSpringFront in front of the eye - the draw's vertices divide by their
 * depth -: `lo` and `hi` are the shares of the way from the foot to the
 * top where it begins and ends, 0 and 1 while both ends are in front; false
 * when no part is. An eye beside the jet that looks up or down it keeps the
 * part in front; the rows behind it are cut. */
static const float kHDSpringFront = 1.0f;

inline bool HDSpringColumnCut(const float* b, const float* t, float* lo,
	float* hi)
{
	*lo = 0.0f;
	*hi = 1.0f;

	if (b[2] > kHDSpringFront && t[2] > kHDSpringFront)
		return true;

	if (b[2] <= kHDSpringFront && t[2] <= kHDSpringFront)
		return false;

	const float at = (kHDSpringFront - b[2]) / (t[2] - b[2]);

	if (b[2] <= kHDSpringFront)
		*lo = at;
	else
		*hi = at;

	return *hi > *lo;
}

/* The drawn part's ends `p0`, `p1` and its middle `mid` between the shares
 * `lo` and `hi` of the way from `b` to `t`; the whole column's own ends
 * where it is not cut. */
inline void HDSpringColumnPart(const float* b, const float* t, float lo,
	float hi, float* p0, float* p1, float* mid)
{
	for (int k = 0; k < 3; k++)
	{
		p0[k] = lo > 0.0f ? b[k] + (t[k] - b[k]) * lo : b[k];
		p1[k] = hi < 1.0f ? b[k] + (t[k] - b[k]) * hi : t[k];
		mid[k] = (p0[k] + p1[k]) * 0.5f;
	}
}

/* The column's weight: its strength, faded as the eye looks along its line
 * `ax` - the sine of the angle between the line and the way to the drawn
 * part's middle `mid`, from 0.2 (gone) to 0.5 (whole), where the quad
 * turns edge-on - and as its half-width there, `half` units, falls from
 * kHDSpringFadeFrom to kHDSpringFadeTo pixels at the focal length `focal`
 * (pixels). `side` takes ax x mid, the quad's way across, and `length` its
 * length. */
inline float HDSpringColumnWeight(float strength, const float* ax,
	const float* mid, float half, float focal, float* side, float* length)
{
	side[0] = ax[1] * mid[2] - ax[2] * mid[1];
	side[1] = ax[2] * mid[0] - ax[0] * mid[2];
	side[2] = ax[0] * mid[1] - ax[1] * mid[0];

	const float sl = sqrtf(side[0] * side[0] + side[1] * side[1] +
		side[2] * side[2]);
	const float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
	const float ml = sqrtf(mid[0] * mid[0] + mid[1] * mid[1] +
		mid[2] * mid[2]);
	const float sine = al > 0.0f && ml > 0.0f ? sl / (al * ml) : 0.0f;
	const float across = half * focal / mid[2];

	*length = sl;
	return strength * fminf(fmaxf((sine - 0.2f) / 0.3f, 0.0f), 1.0f) *
		fminf(fmaxf((across - kHDSpringFadeTo) /
		(kHDSpringFadeFrom - kHDSpringFadeTo), 0.0f), 1.0f);
}

/* Whether the drawn part from `p0` to `p1` (view space, both in front),
 * `h0` and `h1` units wide to each side there, reaches the screen: every
 * corner of its quad lies within its half-width of an end, so the box of
 * the ends, each widened by its half-width at its nearest depth and its
 * farthest, through the projection (x' = proj[0] x / z + proj[8], y' =
 * proj[5] y / z + proj[9], row-major as D3DMATRIX) against -1..1. An end
 * nearer than its half-width may stand round the eye: then it does. */
inline bool HDSpringColumnOnScreen(const float* p0, const float* p1,
	float h0, float h1, const float* proj)
{
	const float* p[2] = { p0, p1 };
	const float h[2] = { h0, h1 };
	float lo[2] = { 1.0e30f, 1.0e30f }, hi[2] = { -1.0e30f, -1.0e30f };

	for (int e = 0; e < 2; e++)
	{
		if (p[e][2] - h[e] <= kHDSpringFront)
			return true;

		for (int c = 0; c < 8; c++)
		{
			const float z = p[e][2] + (c & 1 ? h[e] : -h[e]);
			const float x = p[e][0] + (c & 2 ? h[e] : -h[e]);
			const float y = p[e][1] + (c & 4 ? h[e] : -h[e]);
			const float sx = proj[0] * x / z + proj[8];
			const float sy = proj[5] * y / z + proj[9];

			lo[0] = fminf(lo[0], sx);
			hi[0] = fmaxf(hi[0], sx);
			lo[1] = fminf(lo[1], sy);
			hi[1] = fmaxf(hi[1], sy);
		}
	}

	return !(hi[0] < -1.0f || lo[0] > 1.0f || hi[1] < -1.0f || lo[1] > 1.0f);
}

/* Whether spring `s`'s column, `depth` units from its foot `b` to its top
 * `t` (view space), is drawn in a frame of focal length `focal` and
 * projection `proj` and reaches its screen: a part in front, a weight over
 * 0 and a way across - the numbers the draw takes (hd_spring.cpp,
 * HDSpringFrameDraw). The column bends the frame's copy, so a frame it
 * stands in takes one (HDSpringFrameCopy) whether or not a sheet of the
 * frame asks for it. */
inline bool HDSpringColumnInFrame(const HDSpring& s, float depth,
	const float* b, const float* t, float focal, const float* proj)
{
	float lo, hi, p0[3], p1[3], mid[3], side[3], sl;

	if (depth <= 0.0f || !HDSpringColumnCut(b, t, &lo, &hi))
		return false;

	HDSpringColumnPart(b, t, lo, hi, p0, p1, mid);

	const float ax[3] = { t[0] - b[0], t[1] - b[1], t[2] - b[2] };
	const float weight = HDSpringColumnWeight(s.strength, ax, mid,
		HDSpringHalfWidth(s, depth * (lo + hi) * 0.5f), focal, side, &sl);

	return weight > 0.0f && sl > 0.0f && HDSpringColumnOnScreen(p0, p1,
		HDSpringHalfWidth(s, depth * lo), HDSpringHalfWidth(s, depth * hi),
		proj);
}

/* The crack at the spring's foot: a jagged slit in the floor the jet rises
 * out of, its walls the floor's own page carried over the lip and down,
 * its bottom black. Every choice is a hash of the spring's seed - its level
 * and its place -, the same every time:
 *
 *   the main line   256 to 512 units long, turned any way, bowed by up to
 *                   8 % of its length to one side and jagged by up to 4 %
 *                   at each of its 14 steps
 *   its width       32 to 96 units at the widest; each side's half at a
 *                   step 0.6 to 1 of the profile sin(pi t)^0.6, the two
 *                   sides apart, so the rim is torn and the slit closes to
 *                   a point at both ends
 *   its branches    two to four fine side cracks a third of the main line
 *                   long (0.8 to 1.2 of that), rooted at a step between a
 *                   fifth and four fifths of the way, the sides taking
 *                   turns, leaning 20 to 45 degrees off square to the line
 *                   (one lean a side, so a side's branches never cross), a
 *                   third of the side's half wide at the root and closing
 *                   to a point in four steps
 *   its depth       64 to 128 units under the floor
 *
 * The strength scales all of it from half (strength 0) to whole: a weaker
 * jet opens less ground. A crack that does not lie on its body's deep
 * squares (kHDSpringDeep) is made again at 0.8, 0.64 and 0.5 of its size
 * (HDSpringCrackChoose). The sizes are free choices against the jet: the
 * column is 64 units across at the floor at strength 0 and 256 at 1
 * (HDSpringHalfWidth), the slit one to two jets long and narrower than the
 * jet, set by eye on RoomProbe pictures of ANDREA2's fountain. */
static const float kHDSpringCrackLength = 256.0f;
static const float kHDSpringCrackLengthSpan = 256.0f;
static const float kHDSpringCrackWidth = 32.0f;
static const float kHDSpringCrackWidthSpan = 64.0f;
static const float kHDSpringCrackDepth = 64.0f;
static const float kHDSpringCrackDepthSpan = 64.0f;
static const float kHDSpringCrackBow = 0.08f;	// of the length
static const float kHDSpringCrackJag = 0.04f;	// of the length
static const float kHDSpringCrackTear = 0.6f;	// a side's least share
static const float kHDSpringCrackSlide = 0.3f;	// of a step, along it
static const float kHDSpringCrackLeanMin = 20.0f;	// degrees
static const float kHDSpringCrackLeanSpan = 25.0f;
static const float kHDSpringCrackRoot = 1.0f / 3.0f;	// of the side's half
static const int kHDSpringCrackSteps = 14;
static const int kHDSpringCrackBranchSteps = 4;
static const int kHDSpringCrackBranchesMin = 2;
static const int kHDSpringCrackBranchesMax = 4;
static const int kHDSpringCrackRounds = 16;
// The outline: the slit's two sides, and each branch in place of one point.
static const int kHDSpringCrackMax = 2 * kHDSpringCrackSteps +
	kHDSpringCrackBranchesMax * 2 * kHDSpringCrackBranchSteps;

struct HDSpringCrackShape
{
	float fit;		// the share of its size it was made at
	float length, width, depth;
	float dirX, dirZ;	// the main line's way
	float sideX, sideZ;	// its left, square to it
	int branches;
	int round;		// the round of the branches' hash taken
	float branchLength[kHDSpringCrackBranchesMax];
	int branchAt[kHDSpringCrackBranchesMax];	// the step it roots at
	int branchSide[kHDSpringCrackBranchesMax];	// 1 left, -1 right
	int points;		// of the outline, world x and z
	float x[kHDSpringCrackMax], z[kHDSpringCrackMax];
	// The main line's steps and each side's half-width there.
	float midX[kHDSpringCrackSteps + 1], midZ[kHDSpringCrackSteps + 1];
	float left[kHDSpringCrackSteps + 1], right[kHDSpringCrackSteps + 1];
	float bottomY;		// world y of its bottom, its build's (hd_spring.cpp)
};

/* Whether two edges of the outline that do not meet cross: a pair of
 * edges whose ends lie on both sides of each other. */
inline bool HDSpringCrackCrosses(const HDSpringCrackShape& c)
{
	const int n = c.points;

	for (int i = 0; i < n; i++)
		for (int j = i + 2; j < n; j++)
		{
			if (i == 0 && j == n - 1)
				continue;

			const double ax = c.x[i], az = c.z[i];
			const double bx = c.x[(i + 1) % n], bz = c.z[(i + 1) % n];
			const double cx = c.x[j], cz = c.z[j];
			const double dx = c.x[(j + 1) % n], dz = c.z[(j + 1) % n];
			const double d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
			const double d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
			const double d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
			const double d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);

			if ((d1 > 0.0) != (d2 > 0.0) && (d3 > 0.0) != (d4 > 0.0))
				return true;
		}

	return false;
}

// A hash of the crack's own as 0..1: a field of it and a count in it.
inline float HDSpringCrackUnit(unsigned seed, int count, unsigned field)
{
	return HDSpringUnit(seed, count, 20u + field);
}

/* The crack's outline at `fit` of its size: the slit's left side from its
 * first tip to its last, then its right side back, each branch a spike in
 * place of the side's point at its root - out along one edge, the tip,
 * back along the other - so the outline is one simple polygon. */
inline void HDSpringCrackMake(const HDSpring& s, float fit,
	HDSpringCrackShape* out)
{
	HDSpringCrackShape& c = *out;
	const unsigned seed = s.seed;
	const int n = kHDSpringCrackSteps, k = kHDSpringCrackBranchSteps;
	const float pi = 3.14159265f;
	const float scale = (0.5f + 0.5f * s.strength) * fit;
	const float turn = 2.0f * pi * HDSpringCrackUnit(seed, 0, 0u);

	memset(&c, 0, sizeof(c));
	c.fit = fit;
	c.length = (kHDSpringCrackLength + kHDSpringCrackLengthSpan *
		HDSpringCrackUnit(seed, 1, 0u)) * scale;
	c.width = (kHDSpringCrackWidth + kHDSpringCrackWidthSpan *
		HDSpringCrackUnit(seed, 2, 0u)) * scale;
	c.depth = (kHDSpringCrackDepth + kHDSpringCrackDepthSpan *
		HDSpringCrackUnit(seed, 3, 0u)) * scale;
	c.dirX = cosf(turn);
	c.dirZ = sinf(turn);
	c.sideX = -c.dirZ;
	c.sideZ = c.dirX;

	const float bow = kHDSpringCrackBow * c.length *
		(2.0f * HDSpringCrackUnit(seed, 4, 0u) - 1.0f);
	const float step = c.length / (float)n;
	float lx[kHDSpringCrackSteps + 1], lz[kHDSpringCrackSteps + 1];
	float rx[kHDSpringCrackSteps + 1], rz[kHDSpringCrackSteps + 1];

	for (int i = 0; i <= n; i++)
	{
		const float t = (float)i / (float)n;
		const bool end = i == 0 || i == n;
		const float jag = end ? 0.0f : kHDSpringCrackJag * c.length *
			(2.0f * HDSpringCrackUnit(seed, i, 1u) - 1.0f);
		const float lat = bow * sinf(pi * t) + jag;
		const float along = (t - 0.5f) * c.length;
		const float profile = end ? 0.0f : 0.5f * c.width *
			powf(sinf(pi * t), 0.6f);

		c.midX[i] = s.x + c.dirX * along + c.sideX * lat;
		c.midZ[i] = s.z + c.dirZ * along + c.sideZ * lat;
		c.left[i] = profile * (kHDSpringCrackTear + (1.0f -
			kHDSpringCrackTear) * HDSpringCrackUnit(seed, i, 2u));
		c.right[i] = profile * (kHDSpringCrackTear + (1.0f -
			kHDSpringCrackTear) * HDSpringCrackUnit(seed, i, 3u));

		const float slideL = end ? 0.0f : kHDSpringCrackSlide * step *
			(2.0f * HDSpringCrackUnit(seed, i, 4u) - 1.0f);
		const float slideR = end ? 0.0f : kHDSpringCrackSlide * step *
			(2.0f * HDSpringCrackUnit(seed, i, 5u) - 1.0f);

		lx[i] = c.midX[i] + c.sideX * c.left[i] + c.dirX * slideL;
		lz[i] = c.midZ[i] + c.sideZ * c.left[i] + c.dirZ * slideL;
		rx[i] = c.midX[i] - c.sideX * c.right[i] + c.dirX * slideR;
		rz[i] = c.midZ[i] - c.sideZ * c.right[i] + c.dirZ * slideR;
	}

	/* The branches, drawn by the hash; where their spikes would cross the
	 * rim or each other the branches alone are drawn again with the next
	 * round of the hash (`round` in every branch hash), up to
	 * kHDSpringCrackRounds times - the same spring takes the same round
	 * every time. 16.5 % of 4,000 cracks of any spring cross in the first
	 * round (ModernTest, the crack's group); sixteen rounds leave none. */
	const int count = kHDSpringCrackBranchesMin + (int)(HDSpringHash(seed ^
		0x2C1B3C6Du, 7u) % (unsigned)(kHDSpringCrackBranchesMax -
		kHDSpringCrackBranchesMin + 1));
	const int first = (int)(HDSpringHash(seed ^ 0x2C1B3C6Du, 8u) & 1u);

	c.branches = count;

	for (int round = 0; round < kHDSpringCrackRounds; round++)
	{
		float lean[2], ahead[2];

		for (int side = 0; side < 2; side++)
		{
			lean[side] = (kHDSpringCrackLeanMin + kHDSpringCrackLeanSpan *
				HDSpringCrackUnit(seed, side + 2 * round, 6u)) * pi / 180.0f;
			ahead[side] = HDSpringCrackUnit(seed, side + 2 * round, 7u) < 0.5f ?
				1.0f : -1.0f;
		}

		for (int b = 0; b < count; b++)
		{
			const int h = b + 8 * round;
			const float t = 0.2f + 0.6f * ((float)b + 0.2f + 0.6f *
				HDSpringCrackUnit(seed, h, 8u)) / (float)count;
			int at = (int)(t * (float)n + 0.5f);

			at = at < 2 ? 2 : at > n - 2 ? n - 2 : at;
			c.branchAt[b] = at;
			c.branchSide[b] = ((b + first) & 1) ? 1 : -1;
			c.branchLength[b] = c.length / 3.0f * (0.8f + 0.4f *
				HDSpringCrackUnit(seed, h, 10u));
		}

		// The outline, point by point.
		int m = 0;

		auto put = [&](float x, float z)
		{
			if (m < kHDSpringCrackMax)
			{
				c.x[m] = x;
				c.z[m] = z;
				m++;
			}
		};

		auto spike = [&](int b, float x, float z)
		{
			const int h = b + 8 * round;
			const int sd = c.branchSide[b];
			const int si = sd > 0 ? 0 : 1;
			const float a = lean[si] + (HDSpringCrackUnit(seed, h, 9u) - 0.5f) *
				10.0f * pi / 180.0f;
			const float ox = c.sideX * (float)sd, oz = c.sideZ * (float)sd;
			const float bx = ox * cosf(a) + c.dirX * sinf(a) * ahead[si];
			const float bz = oz * cosf(a) + c.dirZ * sinf(a) * ahead[si];
			const float px = -bz, pz = bx;
			const float len = c.branchLength[b];
			const float root = kHDSpringCrackRoot * (sd > 0 ?
				c.left[c.branchAt[b]] : c.right[c.branchAt[b]]);
			// The side is walked forward on the left, back on the right.
			const float wx = c.dirX * (float)sd, wz = c.dirZ * (float)sd;
			const float e = (px * wx + pz * wz) < 0.0f ? 1.0f : -1.0f;
			float qx[kHDSpringCrackBranchSteps + 1];
			float qz[kHDSpringCrackBranchSteps + 1];
			float hw[kHDSpringCrackBranchSteps + 1];

			for (int j = 0; j <= k; j++)
			{
				const float jag = j > 0 && j < k ? 0.06f * len * (2.0f *
					HDSpringCrackUnit(seed, h * 8 + j, 11u) - 1.0f) : 0.0f;

				qx[j] = x + bx * len * (float)j / (float)k + px * jag;
				qz[j] = z + bz * len * (float)j / (float)k + pz * jag;
				hw[j] = root * (1.0f - (float)j / (float)k);
			}

			for (int j = 0; j < k; j++)
				put(qx[j] + e * px * hw[j], qz[j] + e * pz * hw[j]);

			put(qx[k], qz[k]);

			for (int j = k - 1; j >= 0; j--)
				put(qx[j] - e * px * hw[j], qz[j] - e * pz * hw[j]);
		};

		auto branch_at = [&](int i, int sd)
		{
			for (int b = 0; b < count; b++)
				if (c.branchAt[b] == i && c.branchSide[b] == sd)
					return b;

			return -1;
		};

		put(c.midX[0], c.midZ[0]);

		for (int i = 1; i < n; i++)
		{
			const int b = branch_at(i, 1);

			if (b >= 0)
				spike(b, lx[i], lz[i]);
			else
				put(lx[i], lz[i]);
		}

		put(c.midX[n], c.midZ[n]);

		for (int i = n - 1; i >= 1; i--)
		{
			const int b = branch_at(i, -1);

			if (b >= 0)
				spike(b, rx[i], rz[i]);
			else
				put(rx[i], rz[i]);
		}

		c.points = m;
		c.round = round;

		if (!HDSpringCrackCrosses(c))
			break;
	}
}

// Whether world x, z lies inside the outline (the even-odd rule).
inline bool HDSpringCrackHolds(const HDSpringCrackShape& c, float x, float z)
{
	bool in = false;

	for (int i = 0, j = c.points - 1; i < c.points; j = i++)
		if ((c.z[i] > z) != (c.z[j] > z) && x < c.x[j] + (c.x[i] - c.x[j]) *
			(z - c.z[j]) / (c.z[i] - c.z[j]))
			in = !in;

	return in;
}

/* The floor under a crack's outline (HDSpringCrackFloorFit): the plane
 * that fits its points' heights best and the highest of them; y down. */
struct HDSpringCrackFloor
{
	float y;		// the plane's height at the points' mean x, z
	float slopeX, slopeZ;	// its change of y a unit along world x, z
	float x, z;		// the points' mean
	float top;		// the points' least y: the highest of them
};

/* The floor of n points at world x, z with heights y: the plane of least
 * squares about their mean. Points on one line span no plane: then the
 * flat floor at their mean height. */
inline HDSpringCrackFloor HDSpringCrackFloorFit(const float* x,
	const float* z, const float* y, int n)
{
	HDSpringCrackFloor f = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	double mx = 0.0, mz = 0.0, my = 0.0;

	if (n < 1)
		return f;

	f.top = y[0];

	for (int i = 0; i < n; i++)
	{
		mx += x[i];
		mz += z[i];
		my += y[i];
		f.top = y[i] < f.top ? y[i] : f.top;
	}

	mx /= n;
	mz /= n;
	my /= n;

	double xx = 0.0, xz = 0.0, zz = 0.0, xy = 0.0, zy = 0.0;

	for (int i = 0; i < n; i++)
	{
		const double dx = x[i] - mx, dz = z[i] - mz, dy = y[i] - my;

		xx += dx * dx;
		xz += dx * dz;
		zz += dz * dz;
		xy += dx * dy;
		zy += dz * dy;
	}

	const double det = xx * zz - xz * xz;

	f.y = (float)my;
	f.x = (float)mx;
	f.z = (float)mz;

	if (det > 1.0e-6 * xx * zz && det > 0.0)
	{
		f.slopeX = (float)((xy * zz - zy * xz) / det);
		f.slopeZ = (float)((zy * xx - xy * xz) / det);
	}

	return f;
}

/* Whether an eye at world x, y, z (y down) looks into a crack on `floor`
 * whose bottom lies at bottomY, each test by `lift`: the eye over the
 * bottom, and over the highest point of the outline or else over the
 * floor's plane at the eye's own column. On a slope the outline's heights
 * part, and an eye under its highest point still looks into the slit from
 * the side, the walls hiding the bottom it cannot see. Under the bottom
 * the hole would stand black on the bottom's plane in front of the walls
 * (HolePS meets that plane before the floor); behind the floor's plane
 * the eye looks at the back of the floor the crack opens. The plane only
 * widens the test of the highest point: away from the outline it is a
 * guess at the floor, so an eye over the whole outline keeps its crack. */
inline bool HDSpringCrackSeen(const float* eye,
	const HDSpringCrackFloor& floor, float bottomY, float lift)
{
	if (eye[1] >= bottomY - lift)
		return false;

	if (eye[1] < floor.top - lift)
		return true;

	const float y = floor.y + floor.slopeX * (eye[0] - floor.x) +
		floor.slopeZ * (eye[2] - floor.z);

	return eye[1] < y - lift;
}

/* Where bubble `k` is born in the crack: on the main line between a
 * quarter and three quarters of its way, and across it at most half the
 * side's half-width there - inside the slit whatever the tear. */
inline void HDSpringCrackBirth(const HDSpringCrackShape& c, unsigned seed,
	int k, float* x, float* z)
{
	const float at = (0.25f + 0.5f * HDSpringUnit(seed, k, 40u)) *
		(float)kHDSpringCrackSteps;
	const int i = (int)at < kHDSpringCrackSteps - 1 ? (int)at :
		kHDSpringCrackSteps - 1;
	const float f = at - (float)i;
	const float across = 2.0f * HDSpringUnit(seed, k, 41u) - 1.0f;
	const float half = across > 0.0f ? c.left[i] + f * (c.left[i + 1] -
		c.left[i]) : c.right[i] + f * (c.right[i + 1] - c.right[i]);
	const float off = 0.5f * across * half;

	*x = c.midX[i] + f * (c.midX[i + 1] - c.midX[i]) + c.sideX * off;
	*z = c.midZ[i] + f * (c.midZ[i + 1] - c.midZ[i]) + c.sideZ * off;
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
 * half-width at the floor and widens with the column. With a crack it is
 * born on the crack's bottom at its own place in the slit
 * (HDSpringCrackBirth), and its spread and sway grow from nothing there to
 * whole at the floor, so it rises out of the slit and not through its
 * walls. */
inline bool HDSpringBubble(const HDSpring& s, int k, double t,
	HDSpringBubbleAt* out, const HDSpringCrackShape* crack = 0)
{
	const double born = HDSpringBubbleBorn(s, k);
	const float foot = crack ? crack->bottomY : (float)s.floorY;
	const float depth = foot - (float)s.surfaceY;
	const float speed = kHDSpringRiseMin + kHDSpringRiseSpan *
		HDSpringUnit(s.seed, k, 3u);
	const float age = (float)(t - born);

	out->top = depth / speed;

	if (age < 0.0f || depth <= 0.0f || age >= out->top)
		return false;

	const float h = speed * age;
	const float grow = crack && h < crack->depth ? h / crack->depth : 1.0f;
	const float way = 6.2831853f * HDSpringUnit(s.seed, k, 4u);
	const float off = kHDSpringBubbleSpread * HDSpringUnit(s.seed, k, 5u) *
		HDSpringHalfWidth(s, h) * grow;
	const float sway = kHDSpringSwayMin + kHDSpringSwaySpan *
		HDSpringUnit(s.seed, k, 6u);
	const float period = kHDSpringSwayPeriod + kHDSpringSwayPeriodSpan *
		HDSpringUnit(s.seed, k, 7u);
	const float phase = 6.2831853f * (age / period +
		HDSpringUnit(s.seed, k, 8u));
	const float c = cosf(way), n = sinf(way);
	const float a = sway * sinf(phase) * grow;
	const float b = sway / 3.0f * cosf(phase) * grow;
	float x = s.x, z = s.z;

	if (crack)
		HDSpringCrackBirth(*crack, s.seed, k, &x, &z);

	out->x = x + c * off + a * c - b * n;
	out->z = z + n * off + a * n + b * c;
	out->y = foot - h;
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

// The placement rule's view of a body and its squares.
struct HDSpringSquare
{
	int x, z;	// world, the square's corner
	int depth;	// down to the floor under it
	int sheet;	// 1 when it draws a sheet
};

struct HDSpringBody
{
	int index;
	int surfaceY;
	int river;	// 1 for a river or a body with a current
	int taken;	// 1 when a spring of the table or the file is in it
	std::vector<HDSpringSquare> squares;
};

/* What a candidate keeps clear of: a portal through a wall (its box in
 * world x, z and the height it spans, which must reach into the body's
 * water), Lara's start and a static (a point, at any height). */
enum
{
	HD_SPRING_PORTAL,
	HD_SPRING_START,
	HD_SPRING_STATIC
};

struct HDSpringBlock
{
	int kind;
	float x0, z0, x1, z1;
	float y0, y1;		// world, the portal's span; any for the rest
};

// How far the middle of a square lies from a block's box, x and z.
inline float HDSpringGap(const HDSpringSquare& q, const HDSpringBlock& b)
{
	const float mx = (float)q.x + 512.0f, mz = (float)q.z + 512.0f;
	const float dx = mx < b.x0 ? b.x0 - mx : mx > b.x1 ? mx - b.x1 : 0.0f;
	const float dz = mz < b.z0 ? b.z0 - mz : mz > b.z1 ? mz - b.z1 : 0.0f;

	return sqrtf(dx * dx + dz * dz);
}

// Whether a square of the body holds world x, z.
inline bool HDSpringHolds(const HDSpringBody& body, float x, float z)
{
	for (const HDSpringSquare& q : body.squares)
		if (x >= (float)q.x && x < (float)(q.x + 1024) && z >= (float)q.z &&
			z < (float)(q.z + 1024))
			return true;

	return false;
}

/* Whether a portal is an opening of the body's water: a portal through a
 * wall stands in a plane of x or of z; where the body's squares lie on
 * both sides of its middle, it only parts two rooms of one water (JOBY2's
 * sea runs on over rooms 42 and 76) and is no way in or out. */
inline bool HDSpringOpening(const HDSpringBody& body, const HDSpringBlock& b)
{
	const float mx = 0.5f * (b.x0 + b.x1), mz = 0.5f * (b.z0 + b.z1);
	const bool alongZ = b.x1 - b.x0 < 1.0f;
	const float dx = alongZ ? 512.0f : 0.0f, dz = alongZ ? 0.0f : 512.0f;

	return !(HDSpringHolds(body, mx - dx, mz - dz) &&
		HDSpringHolds(body, mx + dx, mz + dz));
}

// Whether a square of a body is a candidate (see kHDSpringDeep).
inline bool HDSpringCandidate(const HDSpringBody& body,
	const HDSpringSquare& q, const std::vector<HDSpringBlock>& blocks)
{
	if (!q.sheet || q.depth < kHDSpringDeep)
		return false;

	for (const HDSpringBlock& b : blocks)
	{
		if (b.kind == HD_SPRING_STATIC)
		{
			if (b.x0 >= (float)q.x && b.x0 < (float)(q.x + 1024) &&
				b.z0 >= (float)q.z && b.z0 < (float)(q.z + 1024))
				return false;

			continue;
		}

		/* A portal counts where its span reaches into this body's water
		 * and it is an opening of it. */
		if (b.kind == HD_SPRING_PORTAL && (b.y1 <= (float)body.surfaceY ||
			b.y0 >= (float)(body.surfaceY + q.depth) ||
			!HDSpringOpening(body, b)))
			continue;

		if (HDSpringGap(q, b) < kHDSpringClear)
			return false;
	}

	return true;
}

// A body's candidates, their indices into its squares.
inline int HDSpringCandidates(const HDSpringBody& body,
	const std::vector<HDSpringBlock>& blocks, std::vector<int>* out)
{
	int n = 0;

	for (size_t i = 0; i < body.squares.size(); i++)
		if (HDSpringCandidate(body, body.squares[i], blocks))
		{
			if (out)
				out->push_back((int)i);

			n++;
		}

	return n;
}

// Whether world x, z lies on a square of the body with deep water under it.
inline bool HDSpringOnDeep(const std::vector<HDSpringSquare>& squares,
	float x, float z)
{
	for (const HDSpringSquare& q : squares)
		if (q.depth >= kHDSpringDeep && x >= (float)q.x &&
			x < (float)(q.x + 1024) && z >= (float)q.z &&
			z < (float)(q.z + 1024))
			return true;

	return false;
}

/* Whether a crack lies on its body's deep squares: every point of its
 * outline and the middle of every edge. */
inline bool HDSpringCrackFits(const HDSpringCrackShape& c,
	const std::vector<HDSpringSquare>& squares)
{
	for (int i = 0; i < c.points; i++)
	{
		const int j = (i + 1) % c.points;

		if (!HDSpringOnDeep(squares, c.x[i], c.z[i]) ||
			!HDSpringOnDeep(squares, 0.5f * (c.x[i] + c.x[j]),
			0.5f * (c.z[i] + c.z[j])))
			return false;
	}

	return c.points > 2;
}

// The crack at the first size that fits its body; false when none does.
inline bool HDSpringCrackChoose(const HDSpring& s,
	const std::vector<HDSpringSquare>& squares, HDSpringCrackShape* out)
{
	static const float kFit[4] = { 1.0f, 0.8f, 0.64f, 0.5f };

	for (int i = 0; i < 4; i++)
	{
		HDSpringCrackMake(s, kFit[i], out);

		if (HDSpringCrackFits(*out, squares))
			return true;
	}

	return false;
}

/* The body's first square - the least z, then the least x -, whatever
 * order the squares come in. */
inline unsigned HDSpringBodyKey(const HDSpringBody& body)
{
	int bx = 0, bz = 0;
	bool any = false;

	for (const HDSpringSquare& q : body.squares)
		if (!any || q.z < bz || (q.z == bz && q.x < bx))
		{
			bx = q.x;
			bz = q.z;
			any = true;
		}

	return (unsigned)(bx >> 10) * 65537u + (unsigned)(bz >> 10);
}

/* The rule for one body with `candidates` candidate squares: whether it
 * holds a spring, which candidate (0 .. candidates - 1) and how strong.
 * `thin` is the key's value: 0 places none, n one body in
 * kHDSpringEvery times n. The hash of the level and of the body's first
 * square decides; the same level gives the same springs every time. */
inline bool HDSpringRulePick(const char* stem, unsigned bodyKey,
	int candidates, int river, int taken, int thin, int* pick,
	float* strength)
{
	if (thin <= 0 || river || taken || candidates < kHDSpringSquares)
		return false;

	const unsigned h = HDSpringHash(HDSpringStemHash(stem), bodyKey);

	if (h % (unsigned)(kHDSpringEvery * thin) != 0)
		return false;

	*pick = (int)(HDSpringHash(h, 2u) % (unsigned)candidates);
	*strength = 0.5f + 0.5f * (float)HDSpringHash(h, 3u) / 32767.0f;
	return true;
}

/* hd_spring.cpp. HDSpringBuild lays the level's springs onto its water
 * after the sheets are built, the rule's with the level's blocks and the
 * key's value `thin` (HDSpringRulePick); HDSpringRuleLine lists what the
 * rule saw in each body, for RoomProbe -springs; HDSpringRings hands
 * their rings to the sheets once a frame before they draw (the eye's
 * world place picks the springs near enough); HDSpringFrameDraw draws the
 * columns and the bubbles after the sheets; HDSpringFrameCopy takes the
 * frame's copy, where the sheets' is taken, for a column in the frame
 * (HDSpringColumnInFrame). HDSpringAdd is RoomProbe's -spring,
 * HDSpringTiming and HDSpringDrawn its numbers, HDSpringParts its
 * -springparts; HDSpringLogLine writes the SPRINGLOG lines. The engine
 * calls the six after them (HDLevelBuild, HDLevelUnload, FeedWaterRipples,
 * DrawWaterCards, HDFrameDrawGlow, HDLevelDeviceLost).
 *
 * HDSpringCrackBuild gives each spring its crack once the springs are laid
 * (`on` 0 builds none: the key hd_springcrack), reading the floor under it
 * out of the level's .TRG `trg` within the rooms `rooms` name;
 * HDSpringCrackAt is a spring's crack (0 for none), HDSpringCrackLine
 * RoomProbe's listing of its outline, HDSpringCrackFrameDraw draws the
 * cracks in view at the start of the blend phase, before the frame's copy
 * and the sheets, those whose floor's room `drawn` says the frame drew (0
 * asks nothing), and HDSpringCrackNumbers says what the last draw drew and
 * its GPU time (HDSpringTiming). The engine calls HDSpringCrackDraw. */
struct HDSpringRoom
{
	int room;
	float x0, z0;		// world, its origin and its box's least corner
	float x1, z1;
};

void HDSpringCrackBuild(const char* trg, const std::vector<HDSpringRoom>& rooms,
	int on);
const HDSpringCrackShape* HDSpringCrackAt(int i);
bool HDSpringCrackLine(int i, int at, char* out, size_t size);
void HDSpringCrackFrameDraw(const float* toFrame, bool (*drawn)(int room));
void HDSpringCrackNumbers(int* drawn, double* ms);
void HDSpringCrackDraw();

void HDSpringClear();
void HDSpringBuild(const char* stem, const std::vector<HDSpringBlock>& blocks,
	int thin);
bool HDSpringRuleLine(int at, char* out, size_t size);
bool HDSpringAdd(const char* stem, float x, float z, float strength);
int HDSpringCount();
const HDSpring* HDSpringAt(int i);
void HDSpringRings(float eyeX, float eyeY, float eyeZ);
void HDSpringFrameDraw(const float* toFrame);
bool HDSpringFrameCopy(const float* toFrame);
void HDSpringTiming(int on, double* ms);
void HDSpringDrawn(int* columns, int* bubbles);
void HDSpringParts(int mask);
bool HDSpringLogLine(int at, char* out, size_t size);
void HDSpringRelease();

void HDSpringLevelBegin(const char* stem);
void HDSpringLevelEnd();
void HDSpringFeed();
void HDSpringDraw(const float* toFrame);
void HDSpringCopy();
void HDSpringDeviceLost();

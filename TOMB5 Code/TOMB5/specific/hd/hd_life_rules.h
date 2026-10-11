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

/* The water's life: the rules, as pure functions.
 *
 * Small things float on the water sheets and a few fish swim under them
 * (hd_life.cpp draws them; RoomProbe -life draws the same in its pictures;
 * ModernTest checks the rules). This header holds the tables and every rule
 * that moves them, and includes no engine header: the drift step, the stop
 * at a bank, the spawn upstream, the flee from Lara, the bob, the matrix a
 * model is drawn with. The design and its numbers are this project's own;
 * the models are the remaster's own files, found by name at level load.
 */

#include <math.h>
#include <stdio.h>
#include <vector>

enum
{
	HD_LIFE_BOTTLE = 0,
	HD_LIFE_RAG,
	HD_LIFE_LEAF,
	HD_LIFE_BARREL,
	HD_LIFE_FISH,
	HD_LIFE_KINDS
};

/* One kind. `model` is the .TRM stem the model resolver looks for (5\ITEM,
 * then 4\ITEM). `draft` is the share of the model's height under the
 * surface; `rest` the quarter turns about z that lay it into its floating
 * pose (a bottle and a barrel on their side, the cloth flat as built);
 * `forward` the angle in degrees of the way the model faces, from +z
 * toward +x, for a fish; `most` how many of it one body takes, 0 for no
 * limit. The drafts are free choices, set by eye on the round's RoomProbe
 * pictures: glass with air in it half under, soaked cloth low, wood a
 * little high; so are two barrels a body at most (one is 326 units
 * across, ANIMATING3_ANDY1's box). The fish's head points toward +x:
 * ModelProbe -name FISH2 seen from the side and from above (-yaw 0 with
 * -pitch 0 and 90, +x to the right) shows the eye on the +x end and the
 * forked tail on the other. */
struct HDLifeKind
{
	const char* name;	// the LIFELOG's word
	const char* model;
	float draft;
	int rest;
	float forward;
	int most;
};

static const HDLifeKind kHDLifeKinds[HD_LIFE_KINDS] =
{
	{ "bottle", "BOTTLE", 0.50f, 1, 0.0f, 0 },
	{ "rag", "WET_CLOTH", 0.70f, 0, 0.0f, 0 },
	{ "leaf", "CLOTH_RICH2_RICH1", 0.70f, 0, 0.0f, 0 },
	{ "barrel", "ANIMATING3_ANDY1", 0.45f, 1, 0.0f, 2 },
	{ "fish", "FISH2", 0.0f, 0, 90.0f, 0 },
};

/* One level's life: which bodies take floats, the kinds' weights, whether
 * the bodies take a school and drips. A body takes floats when the row
 * says `rivers` and it is a river of at least kHDLifeRiverMin squares, or
 * when it is of the class the row allows (`channelOnly`: channel alone)
 * and has at least `minSquares` sheet squares (HDLifeTakes). The sets
 * follow the levels' own water: ANDREA2's sewer canal, a channel of 44
 * squares, gets the debris and no fish, and its fountain basins stay
 * clean; ANDY3's rivers, its lake (42 squares), its shallows (24) and its
 * pond of room 145 (11), and ANDY1's two pools (14 each) are offered dead
 * leaves, a rag and a barrel, and get a school where the water is deep
 * enough (counts of RoomProbe -hdwater 1 -life). A sea's sheet rises 121
 * units (HDRenderWaterBodySquares), more than the 11 a cloth reaches under
 * the surface (0.7 of its box's 16), so ANDY1's pools and ANDY3's lake
 * and shallows take barrels alone (HDLifeFits). The weights and the least
 * squares are free choices, set by eye on the round's RoomProbe pictures. */
struct HDLifeLevel
{
	const char* stem;
	int minSquares;
	int channelOnly;	// 1: only bodies of class channel
	int rivers;		// 1: every river body as well
	int weight[HD_LIFE_KINDS - 1];
	int fish;
	int drips;
};

static const HDLifeLevel kHDLifeLevels[] =
{
	{ "ANDREA2", 16, 1, 0, { 4, 3, 3, 1 }, 0, 1 },
	{ "ANDY3", 8, 0, 1, { 0, 1, 4, 1 }, 1, 1 },
	{ "ANDY1", 8, 0, 0, { 0, 1, 4, 1 }, 1, 1 },
};

/* The numbers of the rules. A float for three water squares and at most
 * twelve a body: twelve keeps three bodies of a level inside the
 * thirty-two the life draws at most a frame, a quarter of the frame's 128
 * HD entries (hd_object.cpp, HD_OBJ_ITEMS) - Lara takes 6 to 10 of them. A
 * float is placed a quarter square inside its square, and it stops where
 * a step would bring it nearer than its model's reach (HDLifeReach), an
 * eighth at least (kHDLifeEdge), to a square that is not water of its
 * body deep enough to hold it (HDLifeNeed). The
 * wander is 60 units a second, re-aimed every 4 seconds; a float turns
 * toward its way at 40 degrees a second; a banked float tries again every
 * 6 to 9 seconds. A river float comes out of the slot under its upstream
 * end, rides, and goes into the slot under its downstream end (the
 * river's ends, below); a river shorter than three squares takes none, as
 * a float would come back every second there. All free choices, set by
 * eye on the round's RoomProbe pictures. */
static const int kHDLifePerSquares = 3;
static const int kHDLifeBodyMax = 12;
static const int kHDLifeDrawMax = 32;
static const float kHDLifeInset = 256.0f;
static const float kHDLifeEdge = 128.0f;
static const float kHDLifeWander = 60.0f;
static const float kHDLifeWanderEvery = 4.0f;
static const float kHDLifeTurn = 0.6981317f;	// 40 degrees, radians a second
static const float kHDLifeRetryMin = 6.0f;
static const float kHDLifeRetrySpan = 3.0f;
static const float kHDLifeUpstream = 512.0f;
static const int kHDLifeRiverMin = 3;

/* The river's ends. Past the last sheet row of every river the life takes
 * (ANDY3's four), along its flow, the classic level has a dry floor 256 to
 * 768 over the surface or a wall, and at six of the eight ends the water
 * goes on under it - the slot the water comes out of and goes into, 256
 * units high at four ends, 1024 at two (SectorCheck on the classic
 * ANDY3.TRC). A float comes out of that slot and goes back into it: its
 * slot point lies kHDLifeSlotReach past the end row's edge, under the
 * block, more than the 242 a barrel on its side reaches across the water
 * (HDLifeReach), so the whole of it is under the block there; its top
 * stands kHDLifeSlotTop under the surface. It rises to its floating place
 * over the first square and dives over the last one (kHDLifeRiseRun,
 * kHDLifeDiveRun), riding straight with the current over those two, and
 * comes back at the inlet once it has reached the outlet's slot point.
 * Its light falls to kHDLifeDiveLight of its own over the dive: a model's
 * push takes no alpha (hd_object.h, HDObjectPush), its light is the only
 * thing that fades it, and the water's own absorption does the same to
 * anything under it. The run, the depth and the light are free choices,
 * set by eye on the round's RoomProbe pictures. */
static const float kHDLifeSlotReach = 384.0f;
static const float kHDLifeSlotTop = 64.0f;
static const float kHDLifeRiseRun = 1024.0f;
static const float kHDLifeDiveRun = 1024.0f;
static const float kHDLifeDiveLight = 0.25f;

/* The spring's current (HDLifeSpringPush): its speed at the spring, units
 * a second, the distance it has fallen off to nothing at, and the radius
 * round the spring it leaves alone. Free choices, set by eye on RoomProbe
 * -life pictures of ANDREA2 room 194's basin over 20 seconds. */
static const float kHDLifeSpring = 40.0f;
static const float kHDLifeSpringReach = 1536.0f;
static const float kHDLifeSpringCore = 16.0f;

/* The bob and the tilt: a sine of the clock with a period of 2.5 to 4
 * seconds and a phase of its own per float; its height is a quarter of the
 * model's floating height but never more than its body's sheet rises
 * (HDLifeBobAmp), and the tilt is 3 degrees of pitch and 2 of roll on the
 * same curve. Free choices, set by eye on the round's RoomProbe pictures. */
static const float kHDLifeBobMin = 2.5f;
static const float kHDLifeBobSpan = 1.5f;
static const float kHDLifePitch = 0.0523599f;	// 3 degrees
static const float kHDLifeRoll = 0.0349066f;	// 2 degrees

/* The school: 4 to 8 fish half a square under the surface, in a body with
 * at least six squares 768 deep or more (the fish's lowest point lies 611
 * under the surface - 512 down, 64 of sway and half of FISH2's box, 70
 * high - and three clicks keep the floor under it). Its centre cruises at
 * 150 units a second toward a deep square it picks again every 6 seconds
 * or on arrival; its fish ride a ring of 150 to 300 units round it, half
 * as wide as long, at 20 degrees a second, with a 6 degree yaw wobble at
 * 1.5 hertz, rising and sinking 64 units at 0.7 radians a second. Lara
 * within 1024 units of the centre, a square, sends it to the deep square
 * farthest from her at twice the speed for 4 seconds. The count, the
 * speeds, the ring, the wobble and the times are free choices, set by eye
 * on the round's RoomProbe pictures. The centre keeps the ring's 300 and
 * half of FISH2's 173 long box, 387 units, from water less deep (a fish
 * of a wider ring swam through the bank in the pictures). */
static const int kHDLifeFishMin = 4;
static const int kHDLifeFishSpan = 5;
static const int kHDLifeDeepSquares = 6;
static const int kHDLifeDeep = 768;
static const float kHDLifeFishDown = 512.0f;
static const float kHDLifeSwim = 150.0f;
static const float kHDLifeRetarget = 6.0f;
static const float kHDLifeRingMin = 150.0f;
static const float kHDLifeRingSpan = 150.0f;
static const float kHDLifeCircle = 0.3490659f;	// 20 degrees a second
static const float kHDLifeWobble = 0.1047198f;	// 6 degrees
static const float kHDLifeWobbleHz = 1.5f;
static const float kHDLifeFleeReach = 1024.0f;
static const float kHDLifeSway = 64.0f;		// up and down, units
static const float kHDLifeSwayRate = 0.7f;	// radians a second
static const float kHDLifeSquash = 0.5f;	// the ring's width over length
static const float kHDLifeSchoolReach = 387.0f;
static const float kHDLifeFleeFor = 4.0f;

/* The drips: a body with four or more squares under a ceiling (its room is
 * not open air) gets one drip for eight such squares, two to six, each at
 * its own period of 3 to 9 seconds; a drip rings the sheet only within six
 * squares of Lara and at most once in two seconds a body, so the eight
 * rings a sheet draw shows (hd_render.cpp, kRingsPerDraw) are not crowded
 * out. Its ring is the size the engine's own drip gives (game\joby.cpp: 32
 * and up to 7 more). The counts, the periods, the reach and the gap are
 * free choices, set by eye on the round's RoomProbe pictures. */
static const int kHDLifeDripClosedMin = 4;
static const int kHDLifeDripPer = 8;
static const int kHDLifeDripMin = 2;
static const int kHDLifeDripMax = 6;
static const float kHDLifeDripPeriodMin = 3.0f;
static const float kHDLifeDripPeriodSpan = 6.0f;
static const float kHDLifeDripReach = 6144.0f;
static const float kHDLifeDripGap = 2.0f;
static const int kHDLifeDripSize = 32;

/* The share of its body's light a model takes: a fish half a square under
 * the surface takes half - at the full light FISH2's silver reads as white
 * glints through the sheet. A free choice, set by eye on the round's
 * RoomProbe pictures. */
static const float kHDLifeFishLight = 0.5f;

inline float HDLifeLightShare(int kind)
{
	return kind == HD_LIFE_FISH ? kHDLifeFishLight : 1.0f;
}

/* The share of its light a float takes at a dive's share `dive` (0 at
 * its floating place, 1 at a river's slot point): falling linearly to
 * kHDLifeDiveLight (the river's ends, over kHDLifeSlotReach). */
inline float HDLifeDiveLightShare(float dive)
{
	return 1.0f - (1.0f - kHDLifeDiveLight) * dive;
}

// One water square of a body: its corner, world x and z, multiples of 1024.
struct HDLifeSquare
{
	int x, z;
	int depth;	// down to the floor under it, units
	int closed;	// 1 when a ceiling is over it (its room is not open air)
};

/* One body as the life sees it: its surface, its current (a unit way in x
 * and z and units a second, 0 0 0 without one), 1 for a river, the most its
 * sheet's vertices rise over the surface (units), and its sheet squares,
 * each place once. */
struct HDLifeBody
{
	int index;		// the body's number in the water module
	int surfaceY;
	float flowX, flowZ, speed;
	int river;		// 1 for a body of class river
	int channel;		// 1 for a body of class channel
	float rise;
	std::vector<HDLifeSquare> squares;
	// The flow's projection over the squares' middles, for a river.
	float upMin, upMax;
	/* A river's slot points, world x and z: where its floats come out of
	 * the inlet's slot and go into the outlet's (HDLifeRiverEnds). */
	float inX = 0.0f, inZ = 0.0f, outX = 0.0f, outZ = 0.0f;
	/* 1 with a spring, the ripple source of a row in its water, at world
	 * x and z (HDRenderWaterBody::spring): its floats drift away from it
	 * (HDLifeSpringPush). */
	int spring = 0;
	float springX = 0.0f, springZ = 0.0f;
};

// A float or a fish of the pool.
struct HDLifeEntry
{
	int kind;
	int body;		// in the pool's body list
	int index;		// its number, the hash's first input
	int gen;		// how often a river float came back upstream
	float x, z;		// world
	float yaw;		// radians, 0 faces +z
	float phase, period;
	float amp;		// the bob's height, units
	int banked;		// 1 while a bank holds it
	float retry;		// the clock it tries again at
	int school;		// its school, or -1
	float ring, angle;	// its place on the school's ring
};

struct HDLifeSchool
{
	int body;
	float cx, cz;		// the centre, world
	int target;		// the square it swims to
	float retarget;		// the clock it picks again at
	float fleeUntil;	// the clock its flight ends at, 0 calm
	float heading;		// radians
	int fish;
};

struct HDLifeDrip
{
	int body;
	float x, z;
	float period, phase;
	float last;		// the clock of its last ring, -1 none yet
	int fired;		// how many times it fired
};

/* The pool's own hash: three steps of the engine's draw generator
 * (specific\function_stubs.cpp, GetRandomDraw: 0x41C64E6D, 12345, bits 10
 * up) over the entry's number, the second input mixed in after the first
 * step times 0x9E3779B1 (2^32 over the golden ratio, so neighbouring inputs
 * land far apart) - a number that depends on those two alone, never on the
 * frame. 0 .. 32767. A number that changes with time takes a clock step or
 * a count as its second input (the wander's step, a retry's or a school's
 * whole second, a drip's rings); one fixed per float takes a constant of
 * its own plus the float's comeback count: 101 the bob's period, 211 its
 * phase, 307 the yaw, 401 and 503 a comeback's x and z, 601 its kind,
 * 7919 its upstream square; a school's number is shifted by 7 off the
 * floats' first numbers. The constants are free choices - any that differ
 * serve, the mixer spreads them. */
inline unsigned HDLifeHash(unsigned a, unsigned b)
{
	unsigned s = a * 0x41C64E6Du + 12345u;

	s ^= b * 0x9E3779B1u;
	s = s * 0x41C64E6Du + 12345u;
	s = s * 0x41C64E6Du + 12345u;
	return (s >> 10) & 0x7FFF;
}

inline float HDLifeUnit(unsigned a, unsigned b)
{
	return (float)HDLifeHash(a, b) / 32767.0f;
}

// The pool's random draw, the engine's generator on a state of its own.
inline int HDLifeDraw(unsigned* state)
{
	*state = 0x41C64E6Du * *state + 12345u;
	return (int)((*state >> 10) & 0x7FFF);
}

inline int HDLifeFloor1024(float v)
{
	return (int)floorf(v / 1024.0f) * 1024;
}

// The square of the body that holds world x, z, or -1.
inline int HDLifeSquareAt(const HDLifeBody& b, float x, float z)
{
	const int sx = HDLifeFloor1024(x), sz = HDLifeFloor1024(z);

	for (size_t i = 0; i < b.squares.size(); i++)
		if (b.squares[i].x == sx && b.squares[i].z == sz)
			return (int)i;

	return -1;
}

/* Whether the square at corner sx, sz is the body's and at least `need`
 * deep. A float's need is above 0 (HDLifeNeed), so a square whose floor
 * lies at the surface is never water to it. */
inline bool HDLifeHolds(const HDLifeBody& b, int sx, int sz, float need)
{
	for (size_t i = 0; i < b.squares.size(); i++)
		if (b.squares[i].x == sx && b.squares[i].z == sz)
			return (float)b.squares[i].depth >= need;

	return false;
}

/* Whether world x, z is open water of the body for a float that reaches
 * `need` under the surface: its square holds it, and where it lies within
 * `margin` of an edge or a corner of that square, the square across holds
 * it as well. A float stops the margin away from a bank or from water too
 * shallow for it. */
inline bool HDLifeOpen(const HDLifeBody& b, float x, float z, float margin,
	float need)
{
	const int sx = HDLifeFloor1024(x), sz = HDLifeFloor1024(z);

	if (!HDLifeHolds(b, sx, sz, need))
		return false;

	const int ox = x - sx < margin ? -1024 : x - sx > 1024.0f - margin ?
		1024 : 0;
	const int oz = z - sz < margin ? -1024 : z - sz > 1024.0f - margin ?
		1024 : 0;

	if (ox && !HDLifeHolds(b, sx + ox, sz, need))
		return false;
	if (oz && !HDLifeHolds(b, sx, sz + oz, need))
		return false;
	if (ox && oz && !HDLifeHolds(b, sx + ox, sz + oz, need))
		return false;

	return true;
}

enum
{
	HD_LIFE_MOVED = 0,	// the whole step
	HD_LIFE_SLID,		// along a bank, one axis of it
	HD_LIFE_BLOCKED		// none of it
};

/* One step of a float by dx, dz, `margin` from the banks, in water at least
 * `need` deep: the whole step where it stays open water, else the x part
 * alone, else the z part alone - so a float pressed against a bank slides
 * along it - else nothing. */
inline int HDLifeStep(const HDLifeBody& b, float* x, float* z, float dx,
	float dz, float margin, float need)
{
	if (HDLifeOpen(b, *x + dx, *z + dz, margin, need))
	{
		*x += dx;
		*z += dz;
		return HD_LIFE_MOVED;
	}

	if (dx != 0.0f && HDLifeOpen(b, *x + dx, *z, margin, need))
	{
		*x += dx;
		return HD_LIFE_SLID;
	}

	if (dz != 0.0f && HDLifeOpen(b, *x, *z + dz, margin, need))
	{
		*z += dz;
		return HD_LIFE_SLID;
	}

	return HD_LIFE_BLOCKED;
}

/* The wander's way at clock t: a direction from the hash of the float's
 * number and the whole kHDLifeWanderEvery seconds, blended into the next
 * one over that time, so it turns smoothly and never draws a random per
 * frame. */
inline void HDLifeWanderAt(int index, float t, float* wx, float* wz)
{
	const float s = floorf(t / kHDLifeWanderEvery);
	const float f = t / kHDLifeWanderEvery - s;
	const float a0 = HDLifeUnit((unsigned)index, (unsigned)(int)s) *
		6.2831853f;
	const float a1 = HDLifeUnit((unsigned)index, (unsigned)(int)s + 1u) *
		6.2831853f;
	const float x = (1.0f - f) * sinf(a0) + f * sinf(a1);
	const float z = (1.0f - f) * cosf(a0) + f * cosf(a1);
	const float len = sqrtf(x * x + z * z);

	*wx = len > 1.0e-4f ? x / len : 0.0f;
	*wz = len > 1.0e-4f ? z / len : 0.0f;
}

// The float's velocity at clock t: its body's current and its own wander.
inline void HDLifeVelocity(const HDLifeBody& b, int index, float t, float* vx,
	float* vz)
{
	float wx, wz;

	HDLifeWanderAt(index, t, &wx, &wz);
	*vx = b.flowX * b.speed + wx * kHDLifeWander;
	*vz = b.flowZ * b.speed + wz * kHDLifeWander;
}

/* The spring's current: a float in a body with a spring is carried
 * straight away from it at kHDLifeSpring units a second at the spring,
 * falling off linearly to nothing kHDLifeSpringReach away, and not at all
 * within kHDLifeSpringCore of it, where the way out is not yet defined.
 * Added to the velocity (HDLifeFloatTick), so the wander still turns a
 * float and a bank still stops it. */
inline void HDLifeSpringPush(const HDLifeBody& b, float x, float z,
	float* vx, float* vz)
{
	if (!b.spring)
		return;

	const float dx = x - b.springX, dz = z - b.springZ;
	const float d = sqrtf(dx * dx + dz * dz);

	if (d < kHDLifeSpringCore || d >= kHDLifeSpringReach)
		return;

	const float s = kHDLifeSpring * (1.0f - d / kHDLifeSpringReach) / d;

	*vx += dx * s;
	*vz += dz * s;
}

// Turns `yaw` toward the way vx, vz by at most `rate` x dt.
inline float HDLifeTurnToward(float yaw, float vx, float vz, float rate,
	float dt)
{
	if (vx * vx + vz * vz < 1.0e-4f)
		return yaw;

	float d = atan2f(vx, vz) - yaw;

	while (d > 3.14159265f)
		d -= 6.2831853f;
	while (d < -3.14159265f)
		d += 6.2831853f;

	const float most = rate * dt;

	return yaw + (d > most ? most : d < -most ? -most : d);
}

// The flow's projection of a point, for a river's ends.
inline float HDLifeAlong(const HDLifeBody& b, float x, float z)
{
	return x * b.flowX + z * b.flowZ;
}

/* A river's ends: the projection of its squares' middles, and its slot
 * points - the middle of the end row's squares moved along the flow past
 * that row's edge by kHDLifeSlotReach, out of the water upstream and into
 * it downstream (the river's ends, over kHDLifeSlotReach). */
inline void HDLifeRiverEnds(HDLifeBody& b)
{
	b.upMin = 1.0e30f;
	b.upMax = -1.0e30f;

	for (size_t i = 0; i < b.squares.size(); i++)
	{
		const float p = HDLifeAlong(b, b.squares[i].x + 512.0f,
			b.squares[i].z + 512.0f);

		b.upMin = p < b.upMin ? p : b.upMin;
		b.upMax = p > b.upMax ? p : b.upMax;
	}

	float ix = 0.0f, iz = 0.0f, ox = 0.0f, oz = 0.0f;
	int in = 0, out = 0;

	for (size_t i = 0; i < b.squares.size(); i++)
	{
		const float mx = b.squares[i].x + 512.0f;
		const float mz = b.squares[i].z + 512.0f;
		const float p = HDLifeAlong(b, mx, mz);

		if (p <= b.upMin + 1.0f)
		{
			ix += mx;
			iz += mz;
			in++;
		}

		if (p >= b.upMax - 1.0f)
		{
			ox += mx;
			oz += mz;
			out++;
		}
	}

	const float past = 512.0f + kHDLifeSlotReach;

	b.inX = in ? ix / in - b.flowX * past : 0.0f;
	b.inZ = in ? iz / in - b.flowZ * past : 0.0f;
	b.outX = out ? ox / out + b.flowX * past : 0.0f;
	b.outZ = out ? oz / out + b.flowZ * past : 0.0f;
}

/* Whether a river float has reached the outlet's slot point: past the
 * downstream row's edge by kHDLifeSlotReach, under the block there. */
inline bool HDLifeAtSink(const HDLifeBody& b, float x, float z)
{
	return HDLifeAlong(b, x, z) >= b.upMax + 512.0f + kHDLifeSlotReach;
}

/* How far a river float stands into its rise or its dive at world x, z: 1
 * at a slot point and on, 0 over the river's middle - a smooth step over
 * the slot's reach and the first square (rising) or the last square and
 * the slot's reach (diving). 0 on every other body. */
inline float HDLifeDiveShare(const HDLifeBody& b, float x, float z)
{
	if (!b.river)
		return 0.0f;

	const float a = HDLifeAlong(b, x, z);
	const float edgeIn = b.upMin - 512.0f, edgeOut = b.upMax + 512.0f;
	auto smooth = [](float v)
	{
		v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
		return v * v * (3.0f - 2.0f * v);
	};
	const float rise = 1.0f - smooth((a - (edgeIn - kHDLifeSlotReach)) /
		(kHDLifeSlotReach + kHDLifeRiseRun));
	const float dive = smooth((a - (edgeOut - kHDLifeDiveRun)) /
		(kHDLifeDiveRun + kHDLifeSlotReach));

	return rise > dive ? rise : dive;
}

/* How far a float of floating height h and draft goes down at a slot
 * point: from its floating place until its top stands kHDLifeSlotTop
 * under the surface. */
inline float HDLifeDiveDepth(float h, float draft)
{
	return (1.0f - draft) * h + kHDLifeSlotTop;
}

/* Whether a river float rides straight with the current here: over its
 * first square and its last one and in the slots past them, where it
 * rises and dives (no wander, no bank, the way of the flow). */
inline bool HDLifeOnRun(const HDLifeBody& b, float x, float z)
{
	const float a = HDLifeAlong(b, x, z);

	return b.river && (a < b.upMin - 512.0f + kHDLifeRiseRun ||
		a > b.upMax + 512.0f - kHDLifeDiveRun);
}

// How many of the body's squares are at least `need` deep.
inline int HDLifeHoldCount(const HDLifeBody& b, float need)
{
	int n = 0;

	for (size_t i = 0; i < b.squares.size(); i++)
		n += (float)b.squares[i].depth >= need ? 1 : 0;

	return n;
}

/* The `pick`-th of the body's squares at least `need` deep, counted round,
 * or -1 when none is. Where every square is deep enough it is the
 * `pick`-th square itself. */
inline int HDLifeHoldSquare(const HDLifeBody& b, float need, int pick)
{
	const int n = HDLifeHoldCount(b, need);

	if (!n)
		return -1;

	pick %= n;

	for (size_t i = 0; i < b.squares.size(); i++)
		if ((float)b.squares[i].depth >= need && pick-- == 0)
			return (int)i;

	return -1;
}

/* The upstream squares of a river: those whose middle lies within
 * kHDLifeUpstream of the upstream end and that are at least `need` deep.
 * The spawn rule picks one of them by the hash of the float and its
 * comeback; with none, any square of the body deep enough; -1 when the
 * body has none. */
inline int HDLifeUpstreamSquare(const HDLifeBody& b, int index, int gen,
	float need)
{
	const unsigned h = HDLifeHash((unsigned)index, 7919u + (unsigned)gen);
	int count = 0;

	for (size_t i = 0; i < b.squares.size(); i++)
		if (HDLifeAlong(b, b.squares[i].x + 512.0f, b.squares[i].z +
			512.0f) <= b.upMin + kHDLifeUpstream &&
			(float)b.squares[i].depth >= need)
			count++;

	if (!count)
		return HDLifeHoldSquare(b, need, (int)h);

	int pick = (int)(h % (unsigned)count);

	for (size_t i = 0; i < b.squares.size(); i++)
		if (HDLifeAlong(b, b.squares[i].x + 512.0f, b.squares[i].z +
			512.0f) <= b.upMin + kHDLifeUpstream &&
			(float)b.squares[i].depth >= need && pick-- == 0)
			return (int)i;

	return -1;
}

/* A place inside a square, a quarter square from each of its edges, from
 * two numbers 0 .. 32767. */
inline void HDLifeInside(const HDLifeSquare& s, int a, int c, float* x,
	float* z)
{
	const float span = 1024.0f - 2.0f * kHDLifeInset;

	*x = s.x + kHDLifeInset + span * (float)a / 32767.0f;
	*z = s.z + kHDLifeInset + span * (float)c / 32767.0f;
}

// How many floats a body of that many squares takes.
inline int HDLifeCount(int squares)
{
	const int n = (squares + kHDLifePerSquares - 1) / kHDLifePerSquares;

	return n < kHDLifeBodyMax ? n : kHDLifeBodyMax;
}

/* Whether a kind fits a body: the share of a model `h` high under the
 * surface, `draft` x h, reaches deeper than the sheet of the body falls
 * (`rise`, HDRenderWaterBodySquares), so its bottom, held level, never
 * hangs over the drawn water; the tilt is not counted here and lifts a
 * corner by up to HDLifeTiltDrop. A crest running over a flat rag is what
 * a rag awash does, so the other side is not asked. */
inline bool HDLifeFits(float h, float draft, float rise)
{
	return draft * h >= rise;
}

/* The bob's height: a quarter of the model, never more than the sheet's
 * own swell (a float on still water lies still and only tilts), and never
 * more than the draft leaves over the sheet's fall - a float bobbing up as
 * the sheet falls still keeps its bottom, held level, in the water (the
 * tilt left out, as in HDLifeFits). 0 for a kind that does not fit. */
inline float HDLifeBobAmp(float h, float draft, float rise)
{
	float a = 0.25f * h;

	a = a < rise ? a : rise;
	a = a < draft * h - rise ? a : draft * h - rise;
	return a > 0.0f ? a : 0.0f;
}

// The bob at clock t: the height (units, y down) and the tilt's share.
inline float HDLifeBob(float t, float period, float phase, float amp)
{
	return amp * sinf(6.2831853f * t / period + phase);
}

inline float HDLifeTiltShare(float t, float period, float phase)
{
	return cosf(6.2831853f * t / period + phase);
}

/* How far the tilt lowers a corner of a model that reaches `reach` from
 * its middle across the water (HDLifeReach). The pitch, kHDLifePitch x
 * cos, turns about x, the roll, kHDLifeRoll x sin, about z; a corner c
 * along z and a along x drops by c sin(pitch) + a sin(roll), at most
 * c pitch + a roll, which is at most the corner's distance times the
 * length of the two angles (Cauchy-Schwarz), and that length is never more
 * than the larger angle, kHDLifePitch. */
inline float HDLifeTiltDrop(float reach)
{
	return reach * kHDLifePitch;
}

// Whether Lara at lx, lz sends a school with its centre at cx, cz fleeing.
inline bool HDLifeFlees(float lx, float lz, float cx, float cz)
{
	const float dx = lx - cx, dz = lz - cz;

	return dx * dx + dz * dz < kHDLifeFleeReach * kHDLifeFleeReach;
}

/* The deep square farthest from lx, lz - where a school flees to; -1 when
 * the body has none. */
inline int HDLifeFarthestDeep(const HDLifeBody& b, float lx, float lz)
{
	int best = -1;
	float most = -1.0f;

	for (size_t i = 0; i < b.squares.size(); i++)
	{
		if (b.squares[i].depth < kHDLifeDeep)
			continue;

		const float dx = b.squares[i].x + 512.0f - lx;
		const float dz = b.squares[i].z + 512.0f - lz;
		const float d = dx * dx + dz * dz;

		if (d > most)
		{
			most = d;
			best = (int)i;
		}
	}

	return best;
}

inline int HDLifeDeepCount(const HDLifeBody& b)
{
	int n = 0;

	for (size_t i = 0; i < b.squares.size(); i++)
		n += b.squares[i].depth >= kHDLifeDeep ? 1 : 0;

	return n;
}

// The `pick`-th deep square, counted round, or -1.
inline int HDLifeDeepSquare(const HDLifeBody& b, int pick)
{
	const int n = HDLifeDeepCount(b);

	if (!n)
		return -1;

	pick %= n;

	for (size_t i = 0; i < b.squares.size(); i++)
		if (b.squares[i].depth >= kHDLifeDeep && pick-- == 0)
			return (int)i;

	return -1;
}

/* Whether world x, z is deep open water of the body for a school's
 * centre: its square and the squares kHDLifeSchoolReach away along x, z
 * and the diagonals are the body's and deep. */
inline bool HDLifeDeepAt(const HDLifeBody& b, float x, float z)
{
	const float m = kHDLifeSchoolReach;
	const float px[3] = { x, x - m, x + m }, pz[3] = { z, z - m, z + m };

	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 3; k++)
		{
			const int at = HDLifeSquareAt(b, px[i], pz[k]);

			if (at < 0 || b.squares[(size_t)at].depth < kHDLifeDeep)
				return false;
		}

	return true;
}

/* One step of a school's centre toward its target square at `speed` for
 * `dt` seconds; it stays where the step would leave deep water. True on
 * arrival (within a quarter square). */
inline bool HDLifeSchoolStep(const HDLifeBody& b, HDLifeSchool* s,
	float speed, float dt)
{
	if (s->target < 0 || s->target >= (int)b.squares.size())
		return true;

	const float tx = b.squares[(size_t)s->target].x + 512.0f;
	const float tz = b.squares[(size_t)s->target].z + 512.0f;
	const float dx = tx - s->cx, dz = tz - s->cz;
	const float len = sqrtf(dx * dx + dz * dz);

	if (len < kHDLifeInset)
		return true;

	const float step = speed * dt < len ? speed * dt : len;
	const float nx = s->cx + dx / len * step, nz = s->cz + dz / len * step;

	s->heading = atan2f(dx, dz);

	if (HDLifeDeepAt(b, nx, nz))
	{
		s->cx = nx;
		s->cz = nz;
		return false;
	}

	return true;
}

/* How far a model laid `rest` quarter turns about z reaches out from its
 * middle across the water at any yaw: half the diagonal of its box seen
 * from above. A float keeps that far from a bank, kHDLifeEdge at least -
 * ANIMATING3_ANDY1 on its side reaches 242 and poked into a bank's planks
 * at the edge's 128 in the pictures. */
inline float HDLifeReach(int rest, const float* mins, const float* maxs)
{
	const float a = (rest & 1) ? maxs[1] - mins[1] : maxs[0] - mins[0];
	const float c = maxs[2] - mins[2];

	return 0.5f * sqrtf(a * a + c * c);
}

// The floating height of a model laid `rest` quarter turns about z.
inline float HDLifeHeight(int rest, const float* mins, const float* maxs)
{
	return (rest & 1) ? maxs[0] - mins[0] : maxs[1] - mins[1];
}

/* The matrix a model is drawn with, as row vectors (v * m), from its
 * vertices to the world offset from the entry's point: the rest pose
 * (`rest` quarter turns about z), the box's middle moved to the origin,
 * then the roll about z, the pitch about x and the yaw about y (0 faces
 * +z, toward +x as it grows - the classic yaw's way). */
inline void HDLifeMatrix(int rest, const float* mins, const float* maxs,
	float yaw, float pitch, float roll, float* out)
{
	const float q = 1.5707963f * (float)(rest & 3);
	const float cq = cosf(q), sq = sinf(q);
	// The rest pose, column vectors: Rz(q).
	const float R[9] = { cq, -sq, 0.0f, sq, cq, 0.0f, 0.0f, 0.0f, 1.0f };
	const float cy = cosf(yaw), sy = sinf(yaw);
	const float cp = cosf(pitch), sp = sinf(pitch);
	const float cr = cosf(roll), sr = sinf(roll);
	const float Y[9] = { cy, 0.0f, sy, 0.0f, 1.0f, 0.0f, -sy, 0.0f, cy };
	const float X[9] = { 1.0f, 0.0f, 0.0f, 0.0f, cp, -sp, 0.0f, sp, cp };
	const float Z[9] = { cr, -sr, 0.0f, sr, cr, 0.0f, 0.0f, 0.0f, 1.0f };
	float YX[9], A[9], L[9];

	auto mul = [](const float* a, const float* b, float* o)
	{
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				o[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] +
					a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
	};

	mul(Y, X, YX);
	mul(YX, Z, A);
	mul(A, R, L);

	const float bc[3] = { 0.5f * (mins[0] + maxs[0]),
		0.5f * (mins[1] + maxs[1]), 0.5f * (mins[2] + maxs[2]) };
	// The box's middle in the rest pose, and where A takes it.
	const float c[3] = { R[0] * bc[0] + R[1] * bc[1] + R[2] * bc[2],
		R[3] * bc[0] + R[4] * bc[1] + R[5] * bc[2],
		R[6] * bc[0] + R[7] * bc[1] + R[8] * bc[2] };

	for (int r = 0; r < 3; r++)
	{
		out[r * 4 + 0] = L[0 * 3 + r];
		out[r * 4 + 1] = L[1 * 3 + r];
		out[r * 4 + 2] = L[2 * 3 + r];
		out[r * 4 + 3] = 0.0f;
	}

	out[12] = -(A[0] * c[0] + A[1] * c[1] + A[2] * c[2]);
	out[13] = -(A[3] * c[0] + A[4] * c[1] + A[5] * c[2]);
	out[14] = -(A[6] * c[0] + A[7] * c[1] + A[8] * c[2]);
	out[15] = 1.0f;
}

/* The height of a float's middle at clock t, world y (down): its body's
 * surface, the draft (the middle stands (draft - 0.5) x h under it) and
 * the bob. */
inline float HDLifeFloatY(int surfaceY, float h, float draft, float bob)
{
	return (float)surfaceY + (draft - 0.5f) * h + bob;
}

// Whether a level's row takes a body with that many squares and class.
inline bool HDLifeTakes(const HDLifeLevel& level, int squares, int river,
	int channel)
{
	if (river && level.rivers)
		return squares >= kHDLifeRiverMin;
	if (level.channelOnly && !channel)
		return false;

	return squares >= level.minSquares;
}

/* The pool of one level: its bodies as the life sees them, the floats and
 * fish, the schools and the drips. `height` and `reach` are each kind's
 * floating height and reach (HDLifeHeight, HDLifeReach over its model's
 * box) and `has` whether its model loaded;
 * `ringAt` the clock of each body's last drip ring. */
struct HDLifePool
{
	std::vector<HDLifeBody> bodies;
	std::vector<HDLifeEntry> entries;
	std::vector<HDLifeSchool> schools;
	std::vector<HDLifeDrip> drips;
	std::vector<float> ringAt;
	float height[HD_LIFE_KINDS];
	float reach[HD_LIFE_KINDS];	// HDLifeReach
	int has[HD_LIFE_KINDS];
	int floats, fish;
	int unfit;		// floats a body's rise or depth left out
	float last;		// the clock of the last tick, -1 before the first
	int respawned;		// river floats come back upstream
};

// The level's row of kHDLifeLevels, or 0.
inline const HDLifeLevel* HDLifeLevelOf(const char* stem)
{
	for (size_t i = 0; stem && i < sizeof(kHDLifeLevels) /
		sizeof(kHDLifeLevels[0]); i++)
	{
		const char* a = kHDLifeLevels[i].stem;
		const char* b = stem;

		while (*a && *b && ((*a | 0x20) == (*b | 0x20)))
			a++, b++;

		if (!*a && !*b)
			return &kHDLifeLevels[i];
	}

	return 0;
}

/* How deep under the surface at rest a float of `kind` on body `b` reaches
 * at its lowest: its draft, its bob up to the top (the bottom then stands
 * draft x h + amp down, HDLifeFloatY) and the drop its tilt gives a corner
 * (HDLifeTiltDrop). A square shallower than that is no water to it -
 * ANDY3's shallows, room 34, has 23 squares whose floor is the surface
 * (RoomProbe -life), and a barrel there stood in the floor. */
inline float HDLifeNeed(const HDLifePool& p, int kind, const HDLifeBody& b)
{
	const float h = p.height[kind], draft = kHDLifeKinds[kind].draft;

	return draft * h + HDLifeBobAmp(h, draft, b.rise) +
		HDLifeTiltDrop(p.reach[kind]);
}

/* A kind for a float of body `bi`, from the level's weights and a number
 * 0 .. 32767, among the kinds that loaded, fit the body, have a square of
 * it deep enough (HDLifeNeed) and are not at their `most` there (entry
 * `self` not counted); -1 when none is left. */
inline int HDLifeRollKind(const HDLifePool& p, const HDLifeLevel& level,
	int bi, int r, int self)
{
	const HDLifeBody& b = p.bodies[(size_t)bi];
	int sum = 0;
	int ok[HD_LIFE_KINDS - 1];

	for (int k = 0; k < HD_LIFE_KINDS - 1; k++)
	{
		int there = 0;

		for (size_t i = 0; i < p.entries.size(); i++)
			there += (int)i != self && p.entries[i].body == bi &&
				p.entries[i].kind == k ? 1 : 0;

		ok[k] = p.has[k] && level.weight[k] > 0 &&
			HDLifeFits(p.height[k], kHDLifeKinds[k].draft, b.rise) &&
			HDLifeHoldCount(b, HDLifeNeed(p, k, b)) > 0 &&
			(!kHDLifeKinds[k].most || there < kHDLifeKinds[k].most) ?
			level.weight[k] : 0;
		sum += ok[k];
	}

	if (!sum)
		return -1;

	int at = r % sum;

	for (int k = 0; k < HD_LIFE_KINDS - 1; k++)
	{
		if (at < ok[k])
			return k;

		at -= ok[k];
	}

	return -1;
}

// A float's own numbers: its bob's period, phase and height and its yaw.
inline void HDLifeDress(HDLifePool& p, HDLifeEntry& e)
{
	const HDLifeBody& b = p.bodies[(size_t)e.body];

	e.period = kHDLifeBobMin + kHDLifeBobSpan *
		HDLifeUnit((unsigned)e.index, 101u + (unsigned)e.gen);
	e.phase = 6.2831853f * HDLifeUnit((unsigned)e.index,
		211u + (unsigned)e.gen);
	e.yaw = 6.2831853f * HDLifeUnit((unsigned)e.index,
		307u + (unsigned)e.gen);
	e.amp = e.kind >= 0 && e.kind < HD_LIFE_FISH ?
		HDLifeBobAmp(p.height[e.kind], kHDLifeKinds[e.kind].draft, b.rise) :
		0.0f;
	e.banked = 0;
	e.retry = 0.0f;
}

/* The level's pool, rolled with `draw` (0 .. 32767 a call: the engine's
 * GetRandomDraw, or RoomProbe's own copy of that generator). The bodies
 * must be in `p.bodies` and `height`, `reach` and `has` set. A body takes
 * a float for kHDLifePerSquares of its squares with water under them (a
 * floor at the surface is none), each on a square deep enough for its
 * kind; a body with no such square for any kind takes none. */
inline void HDLifeBuild(HDLifePool& p, const HDLifeLevel& level,
	int (*draw)(void*), void* ctx)
{
	p.entries.clear();
	p.schools.clear();
	p.drips.clear();
	p.ringAt.assign(p.bodies.size(), -1.0e6f);
	p.floats = p.fish = p.unfit = p.respawned = 0;
	p.last = -1.0f;

	int index = 0;

	for (size_t bi = 0; bi < p.bodies.size(); bi++)
	{
		HDLifeBody& b = p.bodies[bi];

		if (b.river)
			HDLifeRiverEnds(b);

		// Depth 1 and more: the squares with water under them.
		const int n = HDLifeCount(HDLifeHoldCount(b, 1.0f));

		for (int i = 0; i < n; i++)
		{
			const int kind = HDLifeRollKind(p, level, (int)bi, draw(ctx), -1);
			const int pick = draw(ctx);
			const int ax = draw(ctx), az = draw(ctx);
			const int sq = kind < 0 ? -1 :
				HDLifeHoldSquare(b, HDLifeNeed(p, kind, b), pick);

			if (sq < 0)
			{
				p.unfit++;
				continue;
			}

			HDLifeEntry e = {};

			e.kind = kind;
			e.body = (int)bi;
			e.index = index++;
			e.school = -1;
			HDLifeInside(b.squares[(size_t)sq], ax, az, &e.x, &e.z);
			HDLifeDress(p, e);
			p.entries.push_back(e);
			p.floats++;
		}

		/* The school starts on the first deep square from the roll whose
		 * middle holds it; none where the deep water is too narrow. */
		const int pick = level.fish && p.has[HD_LIFE_FISH] &&
			HDLifeDeepCount(b) >= kHDLifeDeepSquares ? draw(ctx) : -1;
		int start = -1;

		for (int k = 0; pick >= 0 && k < HDLifeDeepCount(b) && start < 0;
			k++)
		{
			const int at = HDLifeDeepSquare(b, pick + k);

			if (HDLifeDeepAt(b, b.squares[(size_t)at].x + 512.0f,
				b.squares[(size_t)at].z + 512.0f))
				start = at;
		}

		if (start >= 0)
		{
			HDLifeSchool s = {};

			s.body = (int)bi;
			s.fish = kHDLifeFishMin + draw(ctx) % kHDLifeFishSpan;
			s.cx = b.squares[(size_t)start].x + 512.0f;
			s.cz = b.squares[(size_t)start].z + 512.0f;
			s.target = HDLifeDeepSquare(b, draw(ctx));
			s.retarget = kHDLifeRetarget;
			s.heading = 6.2831853f * (float)draw(ctx) / 32767.0f;

			for (int f = 0; f < s.fish; f++)
			{
				HDLifeEntry e = {};

				e.kind = HD_LIFE_FISH;
				e.body = (int)bi;
				e.index = index++;
				e.school = (int)p.schools.size();
				e.ring = kHDLifeRingMin + kHDLifeRingSpan *
					(float)draw(ctx) / 32767.0f;
				e.angle = 6.2831853f * (float)f / (float)s.fish;
				e.x = s.cx;
				e.z = s.cz;
				e.phase = 6.2831853f * (float)draw(ctx) / 32767.0f;
				p.entries.push_back(e);
				p.fish++;
			}

			p.schools.push_back(s);
		}

		if (!level.drips)
			continue;

		std::vector<int> closed;

		for (size_t i = 0; i < b.squares.size(); i++)
			if (b.squares[i].closed)
				closed.push_back((int)i);

		if ((int)closed.size() < kHDLifeDripClosedMin)
			continue;

		int drips = (int)closed.size() / kHDLifeDripPer;

		drips = drips < kHDLifeDripMin ? kHDLifeDripMin :
			drips > kHDLifeDripMax ? kHDLifeDripMax : drips;

		for (int d = 0; d < drips; d++)
		{
			HDLifeDrip k = {};
			const int sq = closed[(size_t)(draw(ctx) % (int)closed.size())];
			const int ax = draw(ctx), az = draw(ctx);

			k.body = (int)bi;
			HDLifeInside(b.squares[(size_t)sq], ax, az, &k.x, &k.z);
			k.period = kHDLifeDripPeriodMin + kHDLifeDripPeriodSpan *
				(float)draw(ctx) / 32767.0f;
			k.phase = k.period * (float)draw(ctx) / 32767.0f;
			k.last = -1.0f;
			p.drips.push_back(k);
		}
	}
}

// A ring a drip asks for this tick: where, on which body, how large.
struct HDLifeRing
{
	int body;
	float x, z;
	int y;
	int size;
	int drip;
};

/* A river float comes back: a kind rolled again, and its place at the
 * inlet's slot point, across the flow where a square of the upstream row
 * deep enough for that kind puts it (HDLifeUpstreamSquare, HDLifeInside),
 * along the flow at the slot point - under the block, out of sight. */
inline void HDLifeComeBack(HDLifePool& p, const HDLifeLevel* level,
	HDLifeEntry& e)
{
	const HDLifeBody& b = p.bodies[(size_t)e.body];

	e.gen++;
	p.respawned++;

	// The kind first: the square it comes back on must hold it.
	if (level)
	{
		const int kind = HDLifeRollKind(p, *level, e.body, (int)HDLifeHash(
			(unsigned)e.index, 601u + (unsigned)e.gen),
			(int)(&e - &p.entries[0]));

		e.kind = kind >= 0 ? kind : e.kind;
	}

	const int sq = HDLifeUpstreamSquare(b, e.index, e.gen,
		HDLifeNeed(p, e.kind, b));

	if (sq >= 0)
		HDLifeInside(b.squares[(size_t)sq], (int)HDLifeHash(
			(unsigned)e.index, 401u + (unsigned)e.gen), (int)HDLifeHash(
			(unsigned)e.index, 503u + (unsigned)e.gen), &e.x, &e.z);

	const float back = HDLifeAlong(b, b.inX, b.inZ) - HDLifeAlong(b, e.x,
		e.z);

	e.x += b.flowX * back;
	e.z += b.flowZ * back;
	HDLifeDress(p, e);
	e.yaw = atan2f(b.flowX, b.flowZ);
}

/* One float's step for `dt` seconds at clock t (the end of the step). A
 * river float rides straight with the current over its first and last
 * squares (HDLifeOnRun) and comes back at the inlet once it has gone into
 * the outlet's slot; a float a bank holds stays there and tries its wander
 * again now and then. */
inline void HDLifeFloatTick(HDLifePool& p, const HDLifeLevel* level,
	HDLifeEntry& e, float t, float dt)
{
	const HDLifeBody& b = p.bodies[(size_t)e.body];
	float vx, vz;

	if (b.river && HDLifeOnRun(b, e.x, e.z))
	{
		e.x += b.flowX * b.speed * dt;
		e.z += b.flowZ * b.speed * dt;
		e.yaw = HDLifeTurnToward(e.yaw, b.flowX, b.flowZ, kHDLifeTurn, dt);
		e.banked = 0;

		if (HDLifeAtSink(b, e.x, e.z))
			HDLifeComeBack(p, level, e);

		return;
	}

	if (e.banked && t < e.retry)
		return;

	HDLifeVelocity(b, e.index, t, &vx, &vz);
	HDLifeSpringPush(b, e.x, e.z, &vx, &vz);
	e.yaw = HDLifeTurnToward(e.yaw, vx, vz, kHDLifeTurn, dt);

	const float margin = p.reach[e.kind] > kHDLifeEdge ? p.reach[e.kind] :
		kHDLifeEdge;
	/* A river's middle: the step may run on into the last square's run
	 * across its row's edge, which the bank test would refuse. */
	const float nx = e.x + vx * dt, nz = e.z + vz * dt;

	if (b.river && HDLifeOnRun(b, nx, nz) && HDLifeAlong(b, nx, nz) >
		HDLifeAlong(b, e.x, e.z))
	{
		e.x = nx;
		e.z = nz;
		e.banked = 0;
		return;
	}

	const int how = HDLifeStep(b, &e.x, &e.z, vx * dt, vz * dt, margin,
		HDLifeNeed(p, e.kind, b));

	if (how == HD_LIFE_BLOCKED)
	{
		e.banked = 1;
		e.retry = t + kHDLifeRetryMin + kHDLifeRetrySpan *
			HDLifeUnit((unsigned)e.index, (unsigned)(int)t);
	}
	else
		e.banked = 0;
}

/* One school's step: calm, it cruises to its target and picks another on
 * arrival or every kHDLifeRetarget seconds; Lara within reach sends it to
 * the deep square farthest from her at twice the speed for
 * kHDLifeFleeFor seconds. `laraOn` 0 has no Lara in the water. */
inline void HDLifeSchoolTick(HDLifePool& p, HDLifeSchool& s, int si, float t,
	float dt, int laraOn, float lx, float lz)
{
	const HDLifeBody& b = p.bodies[(size_t)s.body];

	if (laraOn && HDLifeFlees(lx, lz, s.cx, s.cz))
	{
		if (s.fleeUntil <= t)
			s.target = HDLifeFarthestDeep(b, lx, lz);

		s.fleeUntil = t + kHDLifeFleeFor;
	}

	const bool fleeing = s.fleeUntil > t;
	const bool there = HDLifeSchoolStep(b, &s, kHDLifeSwim *
		(fleeing ? 2.0f : 1.0f), dt);

	if (!fleeing && (there || t >= s.retarget))
	{
		s.target = HDLifeDeepSquare(b, (int)HDLifeHash(7u + (unsigned)si,
			(unsigned)(int)t));
		s.retarget = t + kHDLifeRetarget;
	}

	if (!fleeing && s.fleeUntil > 0.0f)
		s.fleeUntil = 0.0f;
}

/* The pool's tick to clock t. Steps of at most a tenth of a second, so a
 * long frame or a probe that jumps to its clock moves the same way the
 * game's frames do. Rings asked for go into `rings` (the caller writes
 * them into the game's ripples or the probe's -ripple list); `laraOn`,
 * lx, lz are Lara's place, 0 without her. */
inline void HDLifeTick(HDLifePool& p, const HDLifeLevel* level, float t,
	int laraOn, float lx, float lz, std::vector<HDLifeRing>* rings)
{
	if (p.last < 0.0f || t < p.last)
		p.last = t;

	while (p.last < t)
	{
		const float dt = t - p.last < 0.1f ? t - p.last : 0.1f;
		const float at = p.last + dt;

		for (size_t i = 0; i < p.entries.size(); i++)
			if (p.entries[i].kind != HD_LIFE_FISH)
				HDLifeFloatTick(p, level, p.entries[i], at, dt);

		for (size_t i = 0; i < p.schools.size(); i++)
			HDLifeSchoolTick(p, p.schools[i], (int)i, at, dt, laraOn, lx,
				lz);

		for (size_t i = 0; i < p.drips.size(); i++)
		{
			HDLifeDrip& d = p.drips[i];
			// The drip falls at phase + n x period; one fell in this step?
			const float n0 = floorf((p.last - d.phase) / d.period);
			const float n1 = floorf((at - d.phase) / d.period);

			if (n1 <= n0 || at < d.phase)
				continue;

			d.fired++;

			const float dx = d.x - lx, dz = d.z - lz;

			if (!laraOn || dx * dx + dz * dz >
				kHDLifeDripReach * kHDLifeDripReach)
				continue;

			if (at - p.ringAt[(size_t)d.body] < kHDLifeDripGap)
				continue;

			p.ringAt[(size_t)d.body] = at;
			d.last = at;

			if (rings)
			{
				HDLifeRing r;

				r.body = d.body;
				r.x = d.x;
				r.z = d.z;
				r.y = p.bodies[(size_t)d.body].surfaceY;
				r.size = kHDLifeDripSize + (int)(HDLifeHash((unsigned)i,
					(unsigned)d.fired) & 7);
				r.drip = (int)i;
				rings->push_back(r);
			}
		}

		p.last = at;
	}
}

/* The end of a body's LIFELOG line, the engine's and RoomProbe's alike: a
 * river's slot points, `in=x,y,z out=x,y,z` with y the depth a float's top
 * reaches there (kHDLifeSlotTop under the surface), or `in=none out=none`. */
inline void HDLifeBodyTail(const HDLifePool& p, int bi, char* out, size_t n)
{
	const HDLifeBody& b = p.bodies[(size_t)bi];

	if (b.river)
		snprintf(out, n, "in=%.0f,%.0f,%.0f out=%.0f,%.0f,%.0f", b.inX,
			b.surfaceY + kHDLifeSlotTop, b.inZ, b.outX, b.surfaceY +
			kHDLifeSlotTop, b.outZ);
	else
		snprintf(out, n, "in=none out=none");
}

/* Where an entry is drawn at clock t: its world place (x, y, z; y down) and
 * its yaw, pitch and roll. A float rides its body's surface with its draft
 * and its bob; a fish swims half a square down on its school's ring. */
inline void HDLifePose(const HDLifePool& p, const HDLifeEntry& e, float t,
	float* x, float* y, float* z, float* yaw, float* pitch, float* roll)
{
	const HDLifeBody& b = p.bodies[(size_t)e.body];

	if (e.kind == HD_LIFE_FISH && e.school >= 0)
	{
		const HDLifeSchool& s = p.schools[(size_t)e.school];
		const float fleeing = s.fleeUntil > t ? 2.0f : 1.0f;
		const float a = e.angle + kHDLifeCircle * fleeing * t;
		// The ring is squeezed along the heading, so the school is long.
		const float rx = e.ring * kHDLifeSquash * sinf(a);
		const float rz = e.ring * cosf(a);
		const float ch = cosf(s.heading), sh = sinf(s.heading);

		*x = s.cx + rx * ch + rz * sh;
		*z = s.cz - rx * sh + rz * ch;
		*y = (float)b.surfaceY + kHDLifeFishDown +
			kHDLifeSway * sinf(kHDLifeSwayRate * t + e.phase);
		*yaw = s.heading - kHDLifeKinds[HD_LIFE_FISH].forward *
			0.0174533f + kHDLifeWobble * sinf(6.2831853f *
			kHDLifeWobbleHz * t + e.phase);
		*pitch = 0.0f;
		*roll = 0.0f;
		return;
	}

	const float h = p.height[e.kind];
	const float draft = kHDLifeKinds[e.kind].draft;
	// Rising out of a river's inlet or diving into its outlet: no bob.
	const float dive = HDLifeDiveShare(b, e.x, e.z);
	const float bob = (1.0f - dive) * HDLifeBob(t, e.period, e.phase, e.amp);
	const float share = HDLifeTiltShare(t, e.period, e.phase);

	*x = e.x;
	*z = e.z;
	*y = HDLifeFloatY(b.surfaceY, h, draft, bob) + dive * HDLifeDiveDepth(h,
		draft);
	*yaw = e.yaw;
	*pitch = kHDLifePitch * share;
	*roll = kHDLifeRoll * sinf(6.2831853f * t / e.period + e.phase);
}

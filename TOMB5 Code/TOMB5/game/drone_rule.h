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

/* The drone's decisions, as functions of what it sees and nothing else.
 *
 * The drone is RICH3's gunship at the size of the pool's vulture (DRONE in
 * game\objects.h; the copy is shrunk by the randomizer). drone.cpp flies it
 * on the flyer zone after Lara and fires the gunship's gun; every choice it
 * makes on the way is one of the functions below:
 *
 *   distance (horizontal)  line of sight   last state   ->  state
 *   below NEAR             either          any              BACK  away
 *   below NEAR + SLACK     either          BACK             BACK
 *   any other              no              any              CLOSE along
 *                                                                 the path
 *   above FAR (+ SLACK     yes             any              CLOSE
 *     while it holds)
 *   in the band            yes             any              HOLD  strafe
 *
 * It fires in any state when four things hold at once: the line of sight,
 * Lara within RANGE, its nose within CONE of her and the burst phase -
 * BURST ticks of fire in every BURST + PAUSE, the phase set per drone by a
 * hash of its item number so two drones do not fire in step. A strafe leg
 * lasts LEG ticks; its side is a hash of the item number and the leg, and
 * a leg the room blocks is turned round at once (drone.cpp counts the
 * turns into the hash).
 *
 * THERE IS NO RANDOM NUMBER HERE. Everything that looks random - the burst
 * phase, the strafe side, the scatter of the aim - is DroneHash of the
 * drone's own state, so the same moment plays the same way.
 *
 * The rotor is the animation's: its one animation of ten frames turns the
 * main rotor 36 degrees a tick and the tail rotor alike (measured, RICH3's
 * slot 91, anim 650). The control adds no turn of its own.
 *
 * Lengths are world units, 1024 a sector; y grows downward. The model the
 * numbers are set for: 2269 long, 460 wide, 784 tall in its first frame,
 * the rotor 1935 across - the vulture's span. The band, the slack, the
 * range, the cone, the height and its clearance, the burst, the pause,
 * the leg, the speeds, the turn, the bank and the spread below are this
 * project's free choices for a flyer of that size, left to play. No
 * engine header here: ModernTest asserts on it. */

// The distance band, horizontal.
#define DRONE_NEAR			2048	// nearer than this it backs off
#define DRONE_FAR			4096	// farther than this it closes in
#define DRONE_SLACK			512		// the band's give at both edges
#define DRONE_RANGE			8192	// its gun reaches no farther
#define DRONE_CONE			0x1555	// 30 degrees: fires only nose on

// Height: above Lara's head, its box clear of floor and ceiling.
#define DRONE_ABOVE			1024
#define DRONE_CLEAR			64

// Fire: ticks of a burst and of the pause after it; a strafe leg.
#define DRONE_BURST			12
#define DRONE_PAUSE			36
#define DRONE_LEG			75

// Speeds, units a tick: along the path, away from Lara, across her line.
#define DRONE_SPEED_CLOSE	48
#define DRONE_SPEED_BACK	32
#define DRONE_SPEED_STRAFE	24

// Its nose turns 4 degrees a tick; it banks 10 into a strafe, 1 a tick.
#define DRONE_TURN			728
#define DRONE_BANK			1820
#define DRONE_BANK_STEP		182

// The aim's scatter round Lara's torso, each axis.
#define DRONE_SPREAD		160

// What the animation turns the rotor a tick: a tenth of a turn.
#define DRONE_ROTOR_STEP	6554

// The model's nodes: the muzzle flash (mesh 8) and its mesh_bits bit.
#define DRONE_MUZZLE_NODE	8
#define DRONE_FLASH_BIT		0x100

// The radius the creature code pushes off walls with must stay under half
// a sector (CreatureAnimation tests both edges of the square with it).
#define DRONE_RADIUS_MIN	64
#define DRONE_RADIUS_MAX	448

enum DroneState
{
	DRONE_CLOSE = 0,
	DRONE_HOLD = 1,
	DRONE_BACK = 2,
	DRONE_DEAD = 3
};

inline const char* DroneStateName(int state)
{
	switch (state)
	{
	case DRONE_CLOSE:	return "close";
	case DRONE_HOLD:	return "hold";
	case DRONE_BACK:	return "back";
	case DRONE_DEAD:	return "dead";
	}

	return "?";
}

// A 32 bit mix of two numbers: the same pair, the same answer.
inline unsigned DroneHash(unsigned a, unsigned b)
{
	unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u);

	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

// The state for this tick (the table above).
inline int DroneNextState(int last, long dist, bool los)
{
	if (last == DRONE_DEAD)
		return DRONE_DEAD;

	if (dist < DRONE_NEAR)
		return DRONE_BACK;

	if (last == DRONE_BACK && dist < DRONE_NEAR + DRONE_SLACK)
		return DRONE_BACK;

	if (!los)
		return DRONE_CLOSE;

	if (dist > DRONE_FAR + (last == DRONE_HOLD ? DRONE_SLACK : 0))
		return DRONE_CLOSE;

	return DRONE_HOLD;
}

// Whether this tick lies in a burst: BURST of every BURST + PAUSE ticks,
// the phase a hash of the item number.
inline bool DroneBurstOn(unsigned item, unsigned tick)
{
	const unsigned period = DRONE_BURST + DRONE_PAUSE;
	const unsigned phase = DroneHash(item, 0) % period;

	return (tick + phase) % period < DRONE_BURST;
}

// Whether it fires this tick. `nose` is the angle from its nose to Lara.
inline bool DroneFires(int state, bool los, long dist, short nose, bool burst)
{
	if (state == DRONE_DEAD || !los || !burst)
		return false;

	if (dist > DRONE_RANGE)
		return false;

	return nose >= -DRONE_CONE && nose <= DRONE_CONE;
}

// The side of a strafe leg: +1 or -1, a hash of the item and the leg.
inline int DroneStrafeSign(unsigned item, unsigned leg)
{
	return (DroneHash(item, leg + 1) & 1) ? 1 : -1;
}

// The nose's turn this tick toward a heading `diff` away.
inline short DroneTurnStep(short diff)
{
	if (diff > DRONE_TURN)
		return DRONE_TURN;

	if (diff < -DRONE_TURN)
		return -DRONE_TURN;

	return diff;
}

// The bank after this tick: into the strafe while it holds, level else.
inline short DroneBankStep(short bank, int state, int sign)
{
	const int want = state == DRONE_HOLD ? sign * DRONE_BANK : 0;

	if (bank < want - DRONE_BANK_STEP)
		return (short)(bank + DRONE_BANK_STEP);

	if (bank > want + DRONE_BANK_STEP)
		return (short)(bank - DRONE_BANK_STEP);

	return (short)want;
}

// The integer square root, rounded down.
inline unsigned long DroneSqrt(unsigned long long v)
{
	unsigned long long r = 0, bit = 1ull << 62;

	while (bit > v)
		bit >>= 2;

	while (bit)
	{
		if (v >= r + bit)
		{
			v -= r + bit;
			r = (r >> 1) + bit;
		}
		else
			r >>= 1;

		bit >>= 2;
	}

	return (unsigned long)r;
}

// `len` units along (x, z), or (x, z) itself if that is shorter.
inline void DroneAlong(long x, long z, long len, long* dx, long* dz)
{
	const unsigned long d = DroneSqrt((unsigned long long)((long long)x * x +
		(long long)z * z));

	if (d <= (unsigned long)len)
	{
		*dx = x;
		*dz = z;
		return;
	}

	*dx = (long)((long long)x * len / (long long)d);
	*dz = (long)((long long)z * len / (long long)d);
}

/* Its horizontal move this tick. (lx, lz) is Lara from the drone, (px, pz)
 * the target the flyer zone's path gives it, `sign` the strafe side:
 * CLOSE along the path, BACK straight away from her, HOLD across her line,
 * DEAD not at all. */
inline void DroneMove(int state, long lx, long lz, long px, long pz, int sign,
	long* dx, long* dz)
{
	*dx = 0;
	*dz = 0;

	switch (state)
	{
	case DRONE_CLOSE:
		DroneAlong(px, pz, DRONE_SPEED_CLOSE, dx, dz);
		break;

	case DRONE_BACK:
		if (lx || lz)
			DroneAlong(-lx, -lz, DRONE_SPEED_BACK, dx, dz);
		break;

	case DRONE_HOLD:
		if (lx || lz)
		{
			// Across her line: (lz, -lx) turned to the leg's side, at
			// full length however near she is.
			const unsigned long d = DroneSqrt((unsigned long long)
				((long long)lx * lx + (long long)lz * lz));

			*dx = (long)((long long)lz * sign * DRONE_SPEED_STRAFE / (long long)d);
			*dz = (long)((long long)-lx * sign * DRONE_SPEED_STRAFE / (long long)d);
		}
		break;
	}
}

/* The height it flies at: ABOVE over Lara's head, its box CLEAR of the
 * floor and the ceiling where it is. `top` and `bottom` are its frame
 * box's y (top negative, above its origin). A room lower than the drone
 * plus both clearances takes it half way between the two limits. */
inline long DroneHoverY(long laraTop, long floor, long ceiling, long top,
	long bottom)
{
	const long highest = ceiling - top + DRONE_CLEAR;	// least y
	const long lowest = floor - bottom - DRONE_CLEAR;	// greatest y
	const long want = laraTop - DRONE_ABOVE;

	if (highest > lowest)
		return (highest + lowest) / 2;

	if (want < highest)
		return highest;

	if (want > lowest)
		return lowest;

	return want;
}

// The scatter of the aim on one axis (0, 1, 2) this tick.
inline long DroneSpread(unsigned item, unsigned tick, int axis)
{
	const unsigned h = DroneHash(item * 3u + (unsigned)axis, tick);

	return (long)(h % (2 * DRONE_SPREAD + 1)) - DRONE_SPREAD;
}

// The object's radius: half its first frame's width, inside the limits.
inline long DroneRadius(long minX, long maxX)
{
	const long r = (maxX - minX) / 2;

	if (r < DRONE_RADIUS_MIN)
		return DRONE_RADIUS_MIN;

	return r > DRONE_RADIUS_MAX ? DRONE_RADIUS_MAX : r;
}

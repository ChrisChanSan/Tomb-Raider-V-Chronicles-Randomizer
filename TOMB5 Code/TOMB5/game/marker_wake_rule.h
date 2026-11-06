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

#include "chef_rule.h"

/* A creature on an AI marker's errand attacks once it notices Lara.
 *
 * GetAITarget (box.cpp) makes the marker the creature's enemy while a
 * PATROL1, AMBUSH or FOLLOW bit sends it somewhere, and CreatureAIInfo
 * then measures distance and angle against the marker. A wake test made
 * with those numbers asks whether the MARKER is near or in sight, and a
 * marker never is. Larson and Pierre stood until Lara hurt them; the
 * gladiator, imp, chef, brawler, hydra and maze monster took Lara as their
 * enemy only when she hurt them, and an alert left them on the stale
 * marker. The Sas family, Hitman, Mafia2 and Twogun measure Lara apart
 * from the marker (lara_info); they are the levels' own marker users and
 * keep their way.
 *
 * CreatureWakesOnLara (people.cpp) is the wake for the eight that had
 * none: while marker bits steer the creature or a marker is its enemy,
 * Lara hurting it, a noise (a shot nearby: AlertNearbyGuards sets
 * `alerted`; Lara near and running) or Lara in its sight (TargetVisible
 * against her) clears ALL its marker bits, makes Lara its enemy and
 * alerts it: it attacks at once, with no run to an ambush point and no
 * stand-and-turn. Nothing wakes it earlier than a guard in the same spot.
 *
 * The pure part below; no engine header here: ModernTest asserts on it. */

// GUARD, AMBUSH, PATROL1, MODIFY and FOLLOW (global\types.h, ai_bits).
#define MARKER_WAKE_BITS		0x1F

// The slot of the imp (game\objects.h); people.cpp checks that it agrees.
#define MARKER_WAKE_IMP			89

// Why a creature woke; 0 while it has not.
#define MARKER_WAKE_HURT		1
#define MARKER_WAKE_NOISE		2
#define MARKER_WAKE_SIGHT		3

/* The levels' own two imps of this rule: andy3 carries both, OCB 3 (the
 * stone throw), each on a FOLLOW marker that sends it 3072 west at the
 * level's flyby; every pool imp has OCB 0. With 0 they take the new way
 * (they stop where they notice Lara, take her as enemy and throw from
 * there); with 1 an imp with an OCB of its own keeps the old one. */
#define MARKER_WAKE_KEEPS_LEVEL_IMPS	0

// Lara's squared ground distance from the creature, as CreatureAIInfo
// gives it: beyond 32000 on an axis the square would overflow a long.
inline long MarkerWakeDistance(long dx, long dz)
{
	if (dx > 32000 || dx < -32000 || dz > 32000 || dz < -32000)
		return 0x7FFFFFFF;

	return dx * dx + dz * dz;
}

// The guard's wake without sight: Lara within 2048 and faster than a
// walk, or a hit.
inline bool MarkerWakeByNoise(long distance, long lara_speed, long hit)
{
	return (distance < 0x400000 && lara_speed > 20) || hit != 0;
}

// Whether the creature is on a marker's errand: marker bits steer it, or
// a marker it reached or lost its bits at is still its enemy.
inline bool MarkerWakeOnErrand(long ai_bits, bool enemy_is_marker)
{
	return (ai_bits & MARKER_WAKE_BITS) != 0 || enemy_is_marker;
}

/* Whether the level's own mark keeps a creature out of the rule: the
 * kitchen chef, whom a chef's OCB without bit 0 marks (ChefHunts: the
 * randomizer gives every chef it places OCB 1, joby3's own has OCB 0 and
 * his GUARD marker ai2 under him), and, with the switch above, the imps
 * with an OCB of their own. Larson and Pierre carry their mark apart
 * (PierreScripted, pierre.cpp). */
inline bool MarkerWakeLevelKeeps(int object, int ocb)
{
	if (object == CHEF_RULE_CHEF)
		return !ChefHunts(object, ocb);

	if (object == MARKER_WAKE_IMP)
		return MARKER_WAKE_KEEPS_LEVEL_IMPS && ocb != 0;

	return false;
}

// The cause without sight: a hit or Lara's hurt first, then a noise - an
// alert or Lara near and running. 0 leaves the sight test to decide.
inline int MarkerWakeCause(long distance, long lara_speed, bool hurt,
	bool alerted)
{
	if (hurt)
		return MARKER_WAKE_HURT;

	if (alerted || MarkerWakeByNoise(distance, lara_speed, 0))
		return MARKER_WAKE_NOISE;

	return 0;
}

// The AILOG word of a cause.
inline const char* MarkerWakeWhy(int cause)
{
	return cause == MARKER_WAKE_HURT ? "hurt" :
		cause == MARKER_WAKE_NOISE ? "noise" : "sight";
}

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

/* A creature on an AI marker's errand looks at Lara (#350).
 *
 * GetAITarget (box.cpp) makes the marker the creature's enemy while a
 * PATROL1, AMBUSH or FOLLOW bit sends it somewhere, and CreatureAIInfo
 * then measures distance and angle against the marker. A wake test made
 * with those numbers asks whether the MARKER is near or in sight, and a
 * marker never is: such a creature stands until Lara hurts it. The Sas
 * family, Mafia2 and Twogun measure Lara apart from the marker for their
 * wake test (lara_info); CreatureSeesLara (people.cpp) is that guard's
 * test for the controls that had none: Lara near and running, a hit, or
 * Lara in its sight (TargetVisible against her). Nothing here wakes a
 * creature earlier than a guard in the same spot.
 *
 * The pure part below; no engine header here: ModernTest asserts on it. */

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

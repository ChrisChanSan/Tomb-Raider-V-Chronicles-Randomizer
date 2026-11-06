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

/* Which creatures read the AI markers (#346).
 *
 * GetAIPickups (setup.cpp) runs at every level start over the items whose
 * kind is `intelligent` and hands each the AI objects (AI_GUARD to
 * AI_FOLLOW) within 512 units in its room: the bits in ai_bits, the
 * marker's OCB in item_flags[3], and every marker but AI_GUARD consumed.
 * Only GetAITarget (box.cpp) ever turns those bits into a goal, and only
 * a control that calls it reads them. `intelligent` is wider: 35 kinds
 * carry it, and 8 of them run a control that never calls GetAITarget -
 * the T-Rex (SAS slot), the compy (SAILOR slot), the lion, the dog, the
 * vulture (CROW slot), the drone and both Roman gods. Those took the bits
 * anyway, consumed the marker another creature's chain or twin needed,
 * and the Roman god's attack waits behind a GUARD bit he can never clear
 * (romangod.cpp, the `!(ai_bits & GUARD)` before every strike).
 *
 * The rule is said by the control itself, not by a list of slots: a
 * creature reads the markers when its control is one of the functions
 * below, each of which calls GetAITarget. A slot that gets one of these
 * controls later reads the markers with it, and one that loses it stops.
 * ModernTest (group ai_marker_rule) checks the list against every
 * GetAITarget call in game\ and against setup.cpp's bindings.
 *
 * No engine header here: ModernTest asserts on it. */

void SasControl(short item_number);			// sas.cpp
void HitmanControl(short item_number);		// hitman.cpp
void PierreControl(short item_number);		// pierre.cpp
void MafiaControl(short item_number);		// mafia2.cpp
void TwogunControl(short item_number);		// twogun.cpp
void HuskieControl(short item_number);		// huskie.cpp
void GladiatorControl(short item_number);	// gladiatr.cpp
void ImpControl(short item_number);			// imp.cpp
void ChefControl(short item_number);		// chef.cpp
void HydraControl(short item_number);		// hydra.cpp
void RaptorControl(short item_number);		// raptor.cpp
void MazeMonsterControl(short item_number);	// willwisp.cpp
void WillOWispControl(short item_number);	// willwisp.cpp
void SwampyControl(short item_number);		// swampy.cpp
void MinisubControl(short item_number);		// minisub.cpp
void JellyControl(short item_number);		// minisub.cpp, no slot uses it
void SniperControl(short item_number);		// sniper.cpp
void SkeletonControl(short item_number);	// skeleton.cpp

typedef void (*AIMarkerControl)(short item_number);

// The 18 controls that call GetAITarget; true when `control` is one.
inline bool ControlReadsAIMarkers(AIMarkerControl control)
{
	return control == SasControl || control == HitmanControl ||
		control == PierreControl || control == MafiaControl ||
		control == TwogunControl || control == HuskieControl ||
		control == GladiatorControl || control == ImpControl ||
		control == ChefControl || control == HydraControl ||
		control == RaptorControl || control == MazeMonsterControl ||
		control == WillOWispControl || control == SwampyControl ||
		control == MinisubControl || control == JellyControl ||
		control == SniperControl || control == SkeletonControl;
}

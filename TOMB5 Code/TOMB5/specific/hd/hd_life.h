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

/* The water's life: small things floating on the water sheets, a school of
 * fish under them and drips that ring them (the rules and their numbers in
 * hd_life_rules.h). An item-less pool, built at level load out of the
 * sheets' bodies and rolled with GetRandomDraw, never saved, drawn as HD
 * models through the frame's object list. Nothing of it runs or allocates
 * without the sheets (a level that built none, tomb5.hd_enable 0).
 *
 * The order of a level: HDLifeModelPlan inside HDObjectLevelBuild (the
 * models go into the level's one model list), HDLifeLevelBegin after it,
 * HDLifeFrame once a frame before the objects' list is drawn,
 * HDLifeLevelEnd when the level goes. */

#include <vector>

struct HDModelSlot;

// The life's model slots: kHDLifeKinds' index on this base, past the
// inventory ring's 0x7000 + slot and short of the swaps' nodes at 0x8000.
#define HD_LIFE_SLOT_BASE	0x7800

void HDLifeModelPlan(const char* stem, std::vector<HDModelSlot>& wanted);
void HDLifeLevelBegin(const char* stem);
void HDLifeLevelEnd();
// The frame's tick and draw at the drawn clock, seconds.
void HDLifeFrame(double clock);

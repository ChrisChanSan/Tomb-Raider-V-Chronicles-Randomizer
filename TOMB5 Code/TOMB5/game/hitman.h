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
#include "../global/types.h"

/* Damage panels the cyborg can open, in the order they open.
 *
 * The HD object path needs this order too: the remaster ships one whole
 * body per damage stage, <MODEL>_<node+1>.TRM, cumulative in exactly this
 * order, so which of the eleven files is correct depends on which panel
 * opened last. hd_object.cpp reads this array rather than keep its own
 * copy that could drift out of sync. */
#define HITMAN_PANELS	11
extern char hitman_nodes[16];

void InitialiseHitman(short item_number);
void HitmanControl(short item_number);

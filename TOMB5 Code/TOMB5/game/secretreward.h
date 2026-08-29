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

#define SECRET_REWARD_MAX	16		// items one level may award

struct SECRET_REWARD_ITEM
{
	short object_number;
	short count;
};

/*
 * Reads SecretRewards.txt next to the executable, exactly as BalanceLoad
 * reads Balance.txt: a missing file is written from the defaults, a file
 * missing lines is completed after reading. Called once at startup.
 */
void SecretRewardLoad();

/*
 * What this level awards for every secret, and how many entries there are.
 * Returns 0 for a level that awards nothing.
 */
const SECRET_REWARD_ITEM* SecretRewardFor(long level, long* count);

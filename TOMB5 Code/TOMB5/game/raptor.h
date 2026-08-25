/*
 * Raptor from Tomb Raider 3, ported to TOMB5 in 2026.
 *
 * Taken from tomb3 (game/raptor.h, GPLv3) - the same license as TOMB5 and
 * this project. The code is deliberately taken over directly instead of
 * rewritten: rewriting would obscure its origin.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 */
#pragma once
#include "../global/types.h"

void RaptorControl(short item_number);

enum RAPTOR_STATES
{
	RAPTOR_EMPTY,
	RAPTOR_STOP,
	RAPTOR_WALK,
	RAPTOR_RUN,
	RAPTOR_ATTACK1,
	RAPTOR_DEATH,
	RAPTOR_WARNING,
	RAPTOR_ATTACK2,
	RAPTOR_ATTACK3
};

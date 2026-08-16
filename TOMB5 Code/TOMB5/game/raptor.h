/*
 * Raptor aus Tomb Raider 3, portiert 2026 nach TOMB5.
 *
 * Uebernommen aus tomb3 (game/raptor.h, GPLv3) - dieselbe Lizenz wie TOMB5
 * und dieses Projekt. Der Code wird bewusst direkt uebernommen statt
 * umgeschrieben: Umschreiben wuerde die Herkunft verschleiern.
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

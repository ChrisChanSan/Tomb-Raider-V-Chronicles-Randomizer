/*
 * Compsognathus aus Tomb Raider 3, portiert 2026 nach TOMB5 (#99).
 *
 * Uebernommen aus tomb3 (game/compy.h, GPLv3) - dieselbe Lizenz wie TOMB5
 * und dieses Projekt. Der Code wird bewusst direkt uebernommen statt
 * umgeschrieben: Umschreiben wuerde die Herkunft verschleiern.
 *
 * CarcassControl aus derselben Quelldatei bleibt draussen - der Kadaver ist
 * ein eigenes TR3-Objekt, das wir nicht mitbringen. Siehe compy.cpp.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 */
#pragma once
#include "../global/types.h"

void InitialiseCompy(short item_number);
void CompyControl(short item_number);

enum COMPY_STATES
{
	COMPY_STOP,
	COMPY_RUN,
	COMPY_JUMP,
	COMPY_ATTACK,
	COMPY_DEATH
};

extern long compy_scared_timer;
extern long compys_attack_lara;

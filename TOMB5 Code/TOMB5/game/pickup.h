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

void RegeneratePickups();
long KeyTrigger(short item_num);
long PickupTrigger(short item_num);
void PuzzleDoneCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void PuzzleHoleCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void SearchObjectControl(short item_number);
void SearchObjectCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void MonitorScreenCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void AnimatingPickUp(short item_number);
void KeyHoleCollision(short item_number, ITEM_INFO* l, COLL_INFO* coll);
void PickUpCollision(short item_number, ITEM_INFO* l, COLL_INFO* coll);
void CollectCarriedItems(ITEM_INFO* item);

extern uchar NumRPickups;
extern short RPickups[16];
extern char KeyTriggerActive;

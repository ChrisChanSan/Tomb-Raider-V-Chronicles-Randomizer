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

void ShutThatDoor(DOORPOS_DATA* d, DOOR_DATA* dd);
void OpenThatDoor(DOORPOS_DATA* d, DOOR_DATA* dd);
void DoorControl(short item_number);
void DoorCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void PushPullKickDoorControl(short item_number);
void PushPullKickDoorCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void DoubleDoorCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void UnderwaterDoorCollision(short item_num, ITEM_INFO* l, COLL_INFO* coll);
void SequenceDoorControl(short item_number);
void ProcessClosedDoors();
void ClosedDoorsLevelEnd();

extern ITEM_INFO* ClosedDoors[32];

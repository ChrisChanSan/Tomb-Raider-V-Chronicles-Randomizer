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

void aFixUpRoom(ROOM_INFO* r, char* s);
void aUpdate();
void aTransformClip_D3DV(D3DVECTOR* vec, D3DTLVERTEX* v, long nVtx, long nClip);
void aTransform_D3DV(D3DVECTOR* vec, D3DTLVERTEX* v, long nVtx);
void aInit();
char* aReadCutData(long n, FILE* file);
long aCalcDepackBufferSz(char* data);
void aMakeCutsceneResident(long n1, long n2, long n3, long n4);
char* aFetchCutData(long n);
long DoCredits();
void ResetCredits();

extern long aWibble;

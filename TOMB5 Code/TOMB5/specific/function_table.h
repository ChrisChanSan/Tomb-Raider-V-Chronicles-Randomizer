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

void InitialiseFunctionTable();
void HWInitialise();
HRESULT HWBeginScene();
HRESULT HWEndScene();
void HWDrawPrimitive(D3DPRIMITIVETYPE type, void* vertices, long numVerts);
bool _NVisible(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2);
bool _Visible(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2);
void SetCullCW();
void SetCullCCW();

extern void (*AddQuadSorted)(D3DTLVERTEX* v, short v0, short v1, short v2, short v3, TEXTURESTRUCT* tex, long double_sided);
extern void (*AddTriSorted)(D3DTLVERTEX* v, short v0, short v1, short v2, TEXTURESTRUCT* tex, long double_sided);
extern void (*AddQuadZBuffer)(D3DTLVERTEX* v, short v0, short v1, short v2, short v3, TEXTURESTRUCT* tex, long double_sided);
extern void (*AddTriZBuffer)(D3DTLVERTEX* v, short v0, short v1, short v2, TEXTURESTRUCT* tex, long double_sided);
extern void (*AddLineSorted)(D3DTLVERTEX* v0, D3DTLVERTEX* v1, short drawtype);
extern bool (*IsVisible)(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2);
extern HRESULT(*_BeginScene)();
extern HRESULT(*_EndScene)();

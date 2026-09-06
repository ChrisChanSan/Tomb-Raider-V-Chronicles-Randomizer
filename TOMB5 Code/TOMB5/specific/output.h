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

void S_DrawPickup(short object_number);
void aTransformLightClipMesh(MESH_DATA* mesh);
void aTransformLightPrelightClipMesh(MESH_DATA* mesh);
void phd_PutPolygons_train(short* lol, long x);
void RenderLoadPic(long unused);
long S_GetObjectBounds(short* bounds);
void S_AnimateTextures(long n);
long aCheckMeshClip(MESH_DATA* mesh);
extern long aMeshClipRejects;
extern long aClipReportObj;
HRESULT DDCopyBitmap(LPDIRECT3DSURFACE9 surf, HBITMAP hbm, long x, long y, long dx, long dy);
HRESULT _LoadBitmap(LPDIRECT3DSURFACE9 surf, LPCSTR name);
void do_boot_screen(long language);
void aCalcColorSplit(long col, long* pC, long* pS);
long S_DumpScreen();
long S_DumpScreenFrame();

/* THE PRESENT CLOCK (#148, order 1).
 *
 * gPresentMinTicks is how many 1/60 s ticks S_DumpScreen waits for before it
 * presents. TWO is the engine's own thirty frames a second and the value this
 * variable holds everywhere except inside DrawPhaseGame's own present, which
 * lowers it to one while fps_mode says 60 and puts it straight back.
 *
 * That one line is the whole clock split. Everything else that presents by
 * itself - the inventory, the load/save screen, the death menu, the loading
 * fade - runs its own animation counters once per present and therefore has
 * to keep thirty; leaving the default at two is what keeps them there without
 * touching a line of any of them.
 *
 * TargetPresentHz() is the same decision as a number, for the two places that
 * need a rate rather than a tick count: the frame budget FPSLOG and PROFLOG
 * measure a late frame against, and the resolution caption's countdown. */
extern long gPresentMinTicks;
long TargetPresentHz();		// 30 or 60, out of tomb5.fps_mode
void StashSkinVertices(long node);
void SkinVerticesToScratch(long node);
void StashSkinNormals(long node);
void SkinNormalsToScratch(long node);
void S_InitialisePolyList();
void S_OutputPolyList();
void S_InsertRoom(ROOM_INFO* r, long a);
void phd_PutPolygons(short* objptr, long clipstatus);
void phd_PutPolygonsSkyMesh(short* objptr, long clipstatus);
void phd_PutPolygonsPickup(short* objptr, float x, float y, long color);
void phd_PutPolygons_seethrough(short* objptr, long fade);
void phd_PutPolygonsSpcXLU(short* objptr, long clipstatus);
void phd_PutPolygonsSpcEnvmap(short* objptr, long clipstatus);
long GetFixedScale(long unit);

extern D3DTLVERTEX aVertexBuffer[1024];
extern long aGlobalSkinMesh;
extern long GlobalAlpha;
extern long GlobalAmbient;
extern float AnimatingTexturesV[16][8][3];
extern float aBoundingBox[24];

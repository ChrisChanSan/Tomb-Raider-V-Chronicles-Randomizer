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

/* The present clock.
 *
 * gPresentMinTicks is how many 1/60 s ticks S_DumpScreen waits for before it
 * presents. Two is the engine's own 30 frames a second and the value this
 * variable holds everywhere except inside DrawPhaseGame's own present,
 * which lowers it to one while fps_mode says 60 and puts it straight back.
 *
 * That one line is the whole clock split. Everything else that presents by
 * itself - the inventory, the load/save screen, the death menu, the
 * loading fade - runs its own animation counters once per present and
 * therefore has to keep 30; leaving the default at two is what keeps them
 * there without touching a line of any of them.
 *
 * The title is one of them: it does not present itself - DoTitle draws
 * through DrawPhaseGame like the game does - but TitleOptions runs once
 * per present while dbinput is refreshed once per tick, so at 60 every
 * keypress would be read twice and the menu would walk two rows at a time.
 * The title guards against that itself (TitleInputShadow, game/gameflow.cpp);
 * the note over GameFrameMinTicks in output.cpp has the whole rule.
 *
 * It is also the frame budget. FPSLOG and PROFLOG judge a frame late
 * against gPresentMinTicks * 1/60 s plus half a tick of slack, read at the
 * moment the frame is judged and not out of tomb5.fps_mode - which is why
 * DrawPhaseGame restores the two only after ProfileFrameEnd has run.
 *
 * Zero is a third state. At fps_mode 2 the game frame waits for no tick at
 * all, because D3DPRESENT_INTERVAL_ONE makes the present itself wait for
 * the panel; the frame's slot is then the panel's period and not a whole
 * number of ticks, which is what PresentSlotMs() is for. Both FPSLOG and
 * PROFLOG take their budget from it so that the two cannot drift apart on
 * the definition.
 *
 * ActiveFpsMode() is tomb5.fps_mode as the engine can really honour it: 2
 * collapses to 1 whenever the present does not wait (VSync off, an adapter
 * without the interval, a driver that overrode it). Everything that has to
 * agree about the frame rate asks this and never the option directly.
 *
 * TargetPresentHz() is that mode as a number - 30, 60, or the display's
 * own refresh rate - for the log lines that name it. It is not a budget;
 * use PresentSlotMs() for that. */
extern long gPresentMinTicks;
long ActiveFpsMode();		// 0, 1 or 2 - what is really running
long TargetPresentHz();		// 30, 60 or the panel's rate
double PresentSlotMs();		// the slot the present that just ran asked for
long GameFrameMinTicks();	// what DrawPhaseGame's own present may wait for
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

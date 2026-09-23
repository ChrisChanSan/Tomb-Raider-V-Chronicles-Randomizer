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

void InsertRoom(ROOM_INFO* r);
void InsertRoomlet(ROOMLET* roomlet);
void aRoomletTransformLight(float* verts, long nVerts, long nLights, long nWaterVerts, long nShoreVerts);
uchar aRoomVertexWaterMarks(ROOM_INFO* r, ROOMLET* roomlet, long vertexIndex);
void aBuildFogBulbList();
void ProcessMeshData(long num_meshes);
long aBuildRoomletLights(ROOMLET* r);
void aRoomInit();
void aResetFogBulbList();
void TriggerFogBulbFX(long r, long g, long b, long x, long y, long z, long rad, long den);
void aBuildFXFogBulbList();
void InitBuckets();
void DrawBucket(TEXTUREBUCKET* bucket);
void DrawBuckets();
void FindBucket(long tpage, D3DTLBUMPVERTEX** Vpp, long** nVtxpp);
long CheckBoundsClip(float* box);

extern FOGBULB_STRUCT ActiveFogBulbs[64];
extern FOGBULB_STRUCT FogBulbs[16];
extern short CheckClipBox[8 * 3];
extern MESH_DATA** mesh_vtxbuf;
extern TEXTUREBUCKET Bucket[30];
extern float clip_left;
extern float clip_top;
extern float clip_right;
extern float clip_bottom;
extern long NumActiveFogBulbs;
extern long num_level_meshes;

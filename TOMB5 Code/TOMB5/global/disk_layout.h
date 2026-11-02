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
#include "types.h"
#include <stddef.h>
#include <string.h>

/* The level file's records that hold pointers at run time, and how the
 * loader turns them into the runtime structs.
 *
 * The .TRC was written by a 32-bit program. Its room headers, roomlets and
 * anims are that program's structs: ROOM_INFO of 208 bytes, ROOMLET of 56,
 * ANIM_STRUCT of 40, each pointer a 4-byte value. Read raw into the runtime
 * struct, as the loader did, they fit only while a pointer is 4 bytes wide;
 * on a 64-bit build ROOM_INFO is 280 bytes, ROOMLET 72 and ANIM_STRUCT 44,
 * and every field behind the first pointer would come from the wrong bytes.
 * So the loader reads the file's form (ROOM_INFO_DISK, ROOMLET_DISK,
 * ANIM_STRUCT_DISK in types.h) and converts field by field. On the 32-bit
 * build both forms are the same bytes, and the conversion gives what the
 * raw read gave.
 *
 * An offset is added as an int32_t: on the 32-bit build the sum wraps as
 * the old pointer arithmetic did, and the 64-bit build adds the same
 * signed distance. */

static_assert(sizeof(ROOM_INFO_DISK) == 208, "the file's room header");
static_assert(offsetof(ROOM_INFO_DISK, pRoomlets) == 172,
	"the file's room header: pRoomlets");
static_assert(sizeof(ROOMLET_DISK) == 56, "the file's roomlet");
static_assert(sizeof(ANIM_STRUCT_DISK) == 40, "the file's anim");
static_assert(offsetof(ANIM_STRUCT_DISK, frame_ptr) == 0,
	"the file's anim: frame_ptr");

/* `base` plus the file's offset `off`. */
template <class T> inline T* DiskOffset(char* base, uint32_t off)
{
	return (T*)(base + (int32_t)off);
}

/* The runtime ROOM_INFO of a room whose header the file holds as `d`;
 * `base` is the end of that header in the room's block, where the file's
 * offsets count from. The pointers the file leaves to the run time
 * (data, light, SourceVB, FaceData, the normals, prelight, prelightwater,
 * verts) are read nowhere and come out null. The roomlets stay where
 * pRoomlets points: in the file's form, which RoomletFromDisk turns into
 * the runtime one. */
inline void RoomFromDisk(ROOM_INFO* r, const ROOM_INFO_DISK* d, char* base)
{
	memset(r, 0, sizeof(ROOM_INFO));
	r->door = DiskOffset<short>(base, d->door);
	r->floor = DiskOffset<FLOOR_INFO>(base, d->floor);
	r->mesh = DiskOffset<MESH_INFO>(base, d->mesh);
	r->x = d->x;
	r->y = d->y;
	r->z = d->z;
	r->minfloor = d->minfloor;
	r->maxceiling = d->maxceiling;
	r->x_size = d->x_size;
	r->y_size = d->y_size;
	r->ambient = d->ambient;
	r->num_lights = d->num_lights;
	r->num_meshes = d->num_meshes;
	r->ReverbType = d->ReverbType;
	r->FlipNumber = d->FlipNumber;
	r->MeshEffect = d->MeshEffect;
	r->bound_active = d->bound_active;
	r->left = d->left;
	r->right = d->right;
	r->top = d->top;
	r->bottom = d->bottom;
	r->test_left = d->test_left;
	r->test_right = d->test_right;
	r->test_top = d->test_top;
	r->test_bottom = d->test_bottom;
	r->item_number = d->item_number;
	r->fx_number = d->fx_number;
	r->flipped_room = d->flipped_room;
	r->flags = d->flags;
	r->nVerts = d->nVerts;
	r->nWaterVerts = d->nWaterVerts;
	r->nShoreVerts = d->nShoreVerts;
	r->posx = d->posx;
	r->posy = d->posy;
	r->posz = d->posz;
	r->watercalc = d->watercalc;
	r->gt3cnt = d->gt3cnt;
	r->gt4cnt = d->gt4cnt;
	r->pclight = DiskOffset<PCLIGHT_INFO>(base, d->pclight);
	r->fogbulb = DiskOffset<FOGBULB_INFO>(base, d->fogbulb);
	r->nPCLight = d->nPCLight;
	r->nFogBulbs = d->nFogBulbs;
	r->cy0 = d->cy0;
	r->cy1 = d->cy1;
	r->nRoomlets = d->nRoomlets;
	r->pRoomlets = DiskOffset<ROOMLET>(base, d->pRoomlets);
	r->pRmVtx = DiskOffset<float>(base, d->pRmVtx);
	r->pRmFace = DiskOffset<short>(base, d->pRmFace);
	r->pRmPrelight = DiskOffset<long>(base, d->pRmPrelight);
	r->vDumpSz = d->vDumpSz;
	r->fLeft = d->fLeft;
	r->fRight = d->fRight;
	r->fTop = d->fTop;
	r->fBottom = d->fBottom;
}

/* The runtime ROOMLET of the file's `d`, its faces, prelight and vertices
 * at `fac`, `pre` and `vtx` in the room's streams. pVtx is read nowhere and
 * comes out null. */
inline ROOMLET RoomletFromDisk(const ROOMLET_DISK* d, short* fac, long* pre,
	float* vtx)
{
	ROOMLET r;

	r.nVtx = d->nVtx;
	r.nWVtx = d->nWVtx;
	r.nSVtx = d->nSVtx;
	r.nQuad = d->nQuad;
	r.nTri = d->nTri;
	r.nWQuad = d->nWQuad;
	r.nWTri = d->nWTri;
	r.padd = d->padd;
	memcpy(r.bBox, d->bBox, sizeof(r.bBox));
	r.pVtx = 0;
	r.pSVtx = vtx;
	r.pFac = fac;
	r.pPrelight = pre;
	return r;
}

/* The runtime ANIM_STRUCT of the file's `d`, its frames in `frames`. */
inline void AnimFromDisk(ANIM_STRUCT* a, const ANIM_STRUCT_DISK* d,
	short* frames)
{
	a->frame_ptr = DiskOffset<short>((char*)frames, d->frame_ptr);
	a->interpolation = d->interpolation;
	a->current_anim_state = d->current_anim_state;
	a->velocity = d->velocity;
	a->acceleration = d->acceleration;
	a->Xvelocity = d->Xvelocity;
	a->Xacceleration = d->Xacceleration;
	a->frame_base = d->frame_base;
	a->frame_end = d->frame_end;
	a->jump_anim_num = d->jump_anim_num;
	a->jump_frame_num = d->jump_frame_num;
	a->number_changes = d->number_changes;
	a->change_index = d->change_index;
	a->number_commands = d->number_commands;
	a->command_index = d->command_index;
}

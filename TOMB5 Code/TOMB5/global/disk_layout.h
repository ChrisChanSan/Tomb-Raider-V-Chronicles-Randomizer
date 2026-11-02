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
#include "../game/save_pointer_rule.h"	// the save's tags, asserted below
#include <stddef.h>
#include <string.h>

/* The level file's records that hold pointers at run time, how the loader
 * turns them into the runtime structs, and the sizes the level file and the
 * save are read and written by - asserted here, so that no build can change
 * one of them unnoticed. Included by the level loader (file.cpp,
 * alexstuff.cpp) and the save (savegame.cpp, rope.cpp, gamemain.cpp).
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

// The level file's records that hold pointers, in their file form; a mesh
// pointer is a 4-byte offset, read by ReadLong.
static_assert(sizeof(ROOM_INFO_DISK) == 208, "the file's room header");
static_assert(offsetof(ROOM_INFO_DISK, pRoomlets) == 172,
	"the file's room header: pRoomlets");
static_assert(sizeof(ROOMLET_DISK) == 56, "the file's roomlet");
static_assert(sizeof(ANIM_STRUCT_DISK) == 40, "the file's anim");
static_assert(offsetof(ANIM_STRUCT_DISK, frame_ptr) == 0,
	"the file's anim: frame_ptr");
static_assert(sizeof(long) == 4, "the file's mesh offsets, read as longs");

// The level file's records without a pointer, read raw on both builds.
static_assert(sizeof(MESH_INFO) == 20, "a room's static mesh");
static_assert(sizeof(FLOOR_INFO) == 8, "a floor sector");
static_assert(sizeof(PCLIGHT_INFO) == 88, "a room light");
static_assert(sizeof(BOX_INFO) == 8, "a box");
static_assert(sizeof(AIOBJECT) == 24, "an AI object");
static_assert(sizeof(CHANGE_STRUCT) == 6, "an anim change");
static_assert(sizeof(RANGE_STRUCT) == 8, "an anim range");
static_assert(sizeof(OBJECT_VECTOR) == 16, "a camera or sink");
static_assert(sizeof(SPOTCAM) == 40, "a spot camera");
static_assert(sizeof(SAMPLE_INFO) == 8, "a sound's info");
static_assert(sizeof(PHDTEXTURESTRUCT) == 40, "a texture info");
static_assert(sizeof(PHDSPRITESTRUCT) == 16, "a sprite");
static_assert(sizeof(NEW_CUTSCENE) == 104, "a cutseq.bin header");

// The save: the file image, the same on both builds.
static_assert(sizeof(SAVEGAME_INFO) == 29397, "the save's image");
static_assert(offsetof(SAVEGAME_INFO, buffer) == 417, "the save: buffer");
static_assert(sizeof(LARA_INFO_SAVE) == 349, "the save: Lara");
static_assert(offsetof(LARA_INFO_SAVE, mesh_ptrs) == 96,
	"the save: Lara's meshes");
static_assert(offsetof(LARA_INFO_SAVE, CornerX) == 222, "the save: CornerX");
static_assert(offsetof(LARA_INFO_SAVE, GeneralPtr) == 258,
	"the save: GeneralPtr");
static_assert(sizeof(PENDULUM_SAVE) == 32, "the save: the pendulum");
static_assert(offsetof(PENDULUM_SAVE, Rope) == 28,
	"the save: the pendulum's rope");
static_assert(sizeof(ROPE_STRUCT) == 1460, "the save: a rope");
static_assert(sizeof(PHD_3DPOS) == 18, "the save: a position");
static_assert(sizeof(STATS) == 20, "the save: the statistics");
static_assert(sizeof(tomb5_save_info) == 7, "the save's tail");
static_assert(offsetof(CREATURE_INFO, enemy) == 18 &&
	offsetof(CREATURE_INFO, enemy) + sizeof(int32_t) == 22,
	"the save: a creature's 22 bytes end with its enemy's field");

// The save's tags lie above every distance into the pool, and apart.
static_assert(MALLOC_SIZE - 1 < SAVE_PTR_TAG_MESH,
	"the save: a distance into the pool is no tag");
static_assert(SAVE_PTR_TAG_MESH != SAVE_PTR_TAG_ITEM &&
	SAVE_PTR_TAG_MESH != SAVE_PTR_TAG_NUM &&
	SAVE_PTR_TAG_ITEM != SAVE_PTR_TAG_NUM, "the save: three tags apart");
static_assert((SAVE_PTR_TAG_MESH & SAVE_PTR_NUM_MASK) == 0 &&
	(SAVE_PTR_TAG_ITEM & SAVE_PTR_NUM_MASK) == 0 &&
	(SAVE_PTR_TAG_NUM & SAVE_PTR_NUM_MASK) == 0,
	"the save: a tag leaves the number its 24 bits");

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

/* The save's Lara (LARA_INFO_SAVE, types.h) is LARA_INFO in the 32-bit
 * layout. Between her 21 pointers lie seven runs of bytes - numbers,
 * flags, bit fields - that are the same bytes in both; each run is copied
 * whole, and these say that it is the same length in both structs, so a
 * field added to one and not the other stops the build. The pointers are
 * game\savegame.cpp's (SaveLaraData, RestoreLaraData). */
#define LARA_RUN(a, b) \
	(offsetof(LARA_INFO, b) - offsetof(LARA_INFO, a))
#define LARA_RUN_SAVE(a, b) \
	(offsetof(LARA_INFO_SAVE, b) - offsetof(LARA_INFO_SAVE, a))
#define LARA_RUN_CHECK(a, b) \
	static_assert(LARA_RUN(a, b) == LARA_RUN_SAVE(a, b), \
		"Lara's save: the bytes from " #a " to " #b)

LARA_RUN_CHECK(item_number, spaz_effect);
LARA_RUN_CHECK(mesh_effects, mesh_ptrs);
LARA_RUN_CHECK(target_angles, left_arm);
LARA_RUN_CHECK(left_arm.frame_number, right_arm);
LARA_RUN_CHECK(right_arm.frame_number, creature);
LARA_RUN_CHECK(CornerX, GeneralPtr);
static_assert(sizeof(LARA_INFO) - offsetof(LARA_INFO, RopeOffset) ==
	sizeof(LARA_INFO_SAVE) - offsetof(LARA_INFO_SAVE, RopeOffset),
	"Lara's save: the bytes from RopeOffset to the end");

/* Copies Lara's bytes that are no pointer from `l` into the save's `s`
 * (`toSave`) or back; the pointers are left as they are. */
inline void LaraCopyRuns(LARA_INFO* l, LARA_INFO_SAVE* s, int toSave)
{
	static const size_t rt[7] = { offsetof(LARA_INFO, item_number),
		offsetof(LARA_INFO, mesh_effects), offsetof(LARA_INFO, target_angles),
		offsetof(LARA_INFO, left_arm.frame_number),
		offsetof(LARA_INFO, right_arm.frame_number),
		offsetof(LARA_INFO, CornerX), offsetof(LARA_INFO, RopeOffset) };
	static const size_t sv[7] = { offsetof(LARA_INFO_SAVE, item_number),
		offsetof(LARA_INFO_SAVE, mesh_effects),
		offsetof(LARA_INFO_SAVE, target_angles),
		offsetof(LARA_INFO_SAVE, left_arm.frame_number),
		offsetof(LARA_INFO_SAVE, right_arm.frame_number),
		offsetof(LARA_INFO_SAVE, CornerX),
		offsetof(LARA_INFO_SAVE, RopeOffset) };
	static const size_t len[7] = { LARA_RUN(item_number, spaz_effect),
		LARA_RUN(mesh_effects, mesh_ptrs), LARA_RUN(target_angles, left_arm),
		LARA_RUN(left_arm.frame_number, right_arm),
		LARA_RUN(right_arm.frame_number, creature),
		LARA_RUN(CornerX, GeneralPtr),
		sizeof(LARA_INFO) - offsetof(LARA_INFO, RopeOffset) };

	for (int i = 0; i < 7; i++)
	{
		if (toSave)
			memcpy((char*)s + sv[i], (const char*)l + rt[i], len[i]);
		else
			memcpy((char*)l + rt[i], (const char*)s + sv[i], len[i]);
	}
}

/* The pendulum into the save's form and back: Rope as its byte distance
 * from `ropes` (RopeList), added back as an int32_t. */
inline void PendulumToSave(PENDULUM_SAVE* s, const PENDULUM* p,
	const ROPE_STRUCT* ropes)
{
	s->Position = p->Position;
	s->Velocity = p->Velocity;
	s->node = p->node;
	s->Rope = (uint32_t)((uintptr_t)p->Rope - (uintptr_t)ropes);
}

inline void PendulumFromSave(PENDULUM* p, const PENDULUM_SAVE* s,
	ROPE_STRUCT* ropes)
{
	p->Position = s->Position;
	p->Velocity = s->Velocity;
	p->node = s->node;
	p->Rope = DiskOffset<ROPE_STRUCT>((char*)ropes, s->Rope);
}

/* A room's `n` roomlets out of the file's `in` into `out` - which may be the
 * same place where the two forms are one size: each is read whole before
 * its own is written. Their faces, prelight and vertices follow one another
 * in the room's streams from `fac`, `pre` and `vtx`. */
inline void RoomletsFromDisk(ROOMLET* out, const ROOMLET_DISK* in, long n,
	short* fac, long* pre, float* vtx)
{
	for (long i = 0; i < n; i++)
	{
		const ROOMLET_DISK one = in[i];

		out[i] = RoomletFromDisk(&one, fac, pre, vtx);
		fac += (5 * one.nTri) + (6 * one.nQuad);
		pre += one.nVtx;
		vtx += 7 * one.nVtx;
	}
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

/* Where the file's `n` anims wait in the anims' own block until the frames
 * are read: at its end, behind what the runtime form adds (4 bytes each on
 * a 64-bit build, nothing on 32-bit). */
inline ANIM_STRUCT_DISK* AnimDiskSlot(ANIM_STRUCT* anims, long n)
{
	return (ANIM_STRUCT_DISK*)((char*)anims +
		n * (long)(sizeof(ANIM_STRUCT) - sizeof(ANIM_STRUCT_DISK)));
}

/* The `n` anims out of their slot, forwards: anim i is written no further
 * on than the file's anim i + 1 begins, and is read whole before. */
inline void AnimsFromDisk(ANIM_STRUCT* anims, const ANIM_STRUCT_DISK* disk,
	long n, short* frames)
{
	for (long i = 0; i < n; i++)
	{
		const ANIM_STRUCT_DISK one = disk[i];

		AnimFromDisk(&anims[i], &one, frames);
	}
}

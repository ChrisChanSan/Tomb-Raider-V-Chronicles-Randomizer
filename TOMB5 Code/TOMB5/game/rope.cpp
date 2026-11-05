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
#include "../tomb5/pch.h"
#include "rope.h"
#include "control.h"
#include "../specific/3dmath.h"
#include "lara_states.h"
#include "draw.h"
#include "sound.h"
#include "lara.h"
#include "../specific/specificfx.h"
#include "../specific/input.h"
#include "../global/disk_layout.h"	// the pendulum as the save holds it
#include "savegame.h"
#include "camera.h"
#include "../specific/function_stubs.h"

static PENDULUM NullPendulum = { {0, 0, 0}, {0, 0, 0}, 0, 0 };

ROPE_STRUCT RopeList[8];
PENDULUM CurrentPendulum;
long nRope = 0;

void InitialiseRope(short item_number)
{
	ITEM_INFO* item;
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	PHD_VECTOR dir;
	short room_number;

	item = &items[item_number];
	room_number = item->room_number;
	pos.x = item->pos.x_pos;
	pos.y = item->pos.y_pos;
	pos.z = item->pos.z_pos;
	floor = GetFloor(pos.x, pos.y, pos.z, &room_number);
	pos.y = GetCeiling(floor, pos.x, pos.y, pos.z);

	dir.x = 0;
	dir.y = 0x4000;
	dir.z = 0;
	CreateRope(&RopeList[nRope], &pos, &dir, 128, item);
	item->trigger_flags = (short)nRope++;
}

void CreateRope(ROPE_STRUCT* rope, PHD_VECTOR* pos, PHD_VECTOR* dir, long slength, ITEM_INFO* item)
{
	long lp;

	rope->Position = *pos;
	rope->SegmentLength = slength << 16;
	dir->x <<= 16;
	dir->y <<= 16;
	dir->z <<= 16;
	Normalise(dir);

	if (item->trigger_flags == -1)
		rope->Coiled = 30;
	else
		rope->Coiled = 0;

	for (lp = 0; lp < 24; lp++)
	{
		rope->Segment[lp].x = __int64(rope->SegmentLength * lp) * dir->x >> 16;
		rope->Segment[lp].y = __int64(rope->SegmentLength * lp) * dir->y >> 16;
		rope->Segment[lp].z = __int64(rope->SegmentLength * lp) * dir->z >> 16;
		rope->Velocity[lp].x = 0;
		rope->Velocity[lp].y = 0;
		rope->Velocity[lp].z = 0;

		if (item->trigger_flags == -1)
		{
			rope->Segment[lp].x = 1024 * lp;
			rope->Segment[lp].y >>= 4;
			rope->Velocity[lp].x = 0x4000;
			rope->Velocity[lp].y = 0x300000 - 0x20000 * lp;
			rope->Velocity[lp].z = 0x4000;
		}
	}

	rope->Active = 0;
}

PHD_VECTOR* Normalise(PHD_VECTOR* v)
{
	long mod, x, y, z, dist;

	x = v->x >> 16;
	y = v->y >> 16;
	z = v->z >> 16;

	if (!x && !y && !z)
		return v;

	dist = phd_sqrt(abs(SQUARE(x) + SQUARE(y) + SQUARE(z)));
	mod = 0x10000 / dist;
	v->x = (__int64)mod * v->x >> 16;
	v->y = (__int64)mod * v->y >> 16;
	v->z = (__int64)mod * v->z >> 16;
	return v;
}

void GetRopePos(ROPE_STRUCT* rope, long pos, long* x, long* y, long* z)
{
	long segment;

	segment = pos >> 7;
	*x = (rope->NormalisedSegment[segment].x * (pos & 0x7F) >> 16) + (rope->MeshSegment[segment].x >> 16) + rope->Position.x;
	*y = (rope->NormalisedSegment[segment].y * (pos & 0x7F) >> 16) + (rope->MeshSegment[segment].y >> 16) + rope->Position.y;
	*z = (rope->NormalisedSegment[segment].z * (pos & 0x7F) >> 16) + (rope->MeshSegment[segment].z >> 16) + rope->Position.z;
}

long mDotProduct(PHD_VECTOR* a, PHD_VECTOR* b)
{
	return (a->x * b->x + a->y * b->y + a->z * b->z) >> W2V_SHIFT;
}

void vMul(PHD_VECTOR* v, long scale, PHD_VECTOR* d)
{
	d->x = scale * v->x >> W2V_SHIFT;
	d->y = scale * v->y >> W2V_SHIFT;
	d->z = scale * v->z >> W2V_SHIFT;
}

void mCrossProduct(PHD_VECTOR* a, PHD_VECTOR* b, PHD_VECTOR* n)
{
	PHD_VECTOR t;

	t.x = a->y * b->z - a->z * b->y;
	t.y = a->z * b->x - a->x * b->z;
	t.z = a->x * b->y - a->y * b->x;
	n->x = t.x >> W2V_SHIFT;
	n->y = t.y >> W2V_SHIFT;
	n->z = t.z >> W2V_SHIFT;
}

void phd_GetMatrixAngles(long* m, short* dest)
{
	long sy, cy;
	short roll, pitch, yaw;

	pitch = (short)phd_atan(phd_sqrt(SQUARE(m[M22]) + SQUARE(m[M02])), m[M12]);

	if (m[M12] >= 0 && pitch > 0 || m[M12] < 0 && pitch < 0)
		pitch = -pitch;

	yaw = (short)phd_atan(m[M22], m[M02]);
	sy = phd_sin(yaw);
	cy = phd_cos(yaw);
	roll = (short)phd_atan(m[M00] * cy - m[M20] * sy, m[M21] * sy - m[M01] * cy);
	dest[0] = pitch;
	dest[1] = yaw;
	dest[2] = roll;
}

void RopeControl(short item_number)
{
	ROPE_STRUCT* currope;

	currope = &RopeList[items[item_number].trigger_flags];

	if (TriggerActive(&items[item_number]))
	{
		currope->Active = 1;
		CalculateRope(currope);
	}
	else
		currope->Active = 0;
}

/* The catch takes either key, and so must the hold. This is the jump onto
 * the rope, out of AS_REACH or AS_UPJUMP with gravity under her, which is a
 * grab, so IN_ACTION_ANY. LaraClimbRope() below has to follow: it drops her
 * the moment the key is not down, so a catch with one key and a hold on the
 * other would throw her off on the very next tick under "Separate". See
 * IN_ACTION_ANY in global\types.h. */
void RopeCollision(short item_number, ITEM_INFO* l, COLL_INFO* coll)
{
	ROPE_STRUCT* rope;
	short* bounds;
	long i, x, y, z, rad;

	rope = &RopeList[items[item_number].trigger_flags];

	if (input & IN_ACTION_ANY && lara.gun_status == LG_NO_ARMS && (l->current_anim_state == AS_REACH || l->current_anim_state == AS_UPJUMP) &&
		l->gravity_status && l->fallspeed > 0 && rope->Active)
	{
		bounds = GetBoundsAccurate(l);
		x = l->pos.x_pos;
		y = l->pos.y_pos + bounds[2] + 512;
		z = l->pos.z_pos + (bounds[5] * phd_cos(l->pos.y_rot) >> W2V_SHIFT);
		rad = l->current_anim_state == AS_REACH ? 128 : 320;
		i = RopeNodeCollision(rope, x, y, z, rad);

		if (i >= 0)
		{
			if (l->current_anim_state == AS_REACH)
			{
				l->anim_number = 379;
				l->current_anim_state = AS_ROPEFWD;
				lara.RopeFrame = (anims[ANIM_SWINGFWD].frame_base + 32) * 256;
				lara.RopeDFrame = (anims[ANIM_SWINGFWD].frame_base + 60) * 256;
			}
			else
			{
				l->anim_number = ANIM_UPJUMP2ROPE;
				l->current_anim_state = AS_ROPE;
			}

			l->frame_number = anims[l->anim_number].frame_base;
			l->gravity_status = 0;
			l->fallspeed = 0;
			lara.gun_status = LG_HANDS_BUSY;
			lara.RopePtr = items[item_number].trigger_flags;
			lara.RopeSegment = (char)i;
			lara.RopeY = l->pos.y_rot;
			AlignLaraToRope(l);
			CurrentPendulum.Velocity.x = 0;
			CurrentPendulum.Velocity.y = 0;
			CurrentPendulum.Velocity.z = 0;
			ApplyVelocityToRope(i, l->pos.y_rot, 16 * lara_item->speed);
		}
	}
}

void CalculateRope(ROPE_STRUCT* Rope)
{
	PENDULUM* Pendulum;
	PHD_VECTOR dir;
	long n, bSetFlag;

	bSetFlag = 0;

	if (Rope->Coiled)
	{
		Rope->Coiled--;
	
		if (!Rope->Coiled)
		{
			for (n = 0; n < 24; n++)
				Rope->Velocity[n].y = 0;
		}
	}

	if (Rope == &RopeList[lara.RopePtr])
	{
		Pendulum = &CurrentPendulum;
		
		if (CurrentPendulum.node != lara.RopeSegment + 1)
		{
			SetPendulumPoint(Rope, lara.RopeSegment + 1);
			bSetFlag = 1;
		}
	}
	else
	{
		Pendulum = &NullPendulum;
		
		if (lara.RopePtr == -1 && CurrentPendulum.Rope)
		{
			for (n = 0; n < CurrentPendulum.node; n++)
			{
				CurrentPendulum.Rope->Velocity[n].x = CurrentPendulum.Rope->Velocity[CurrentPendulum.node].x;
				CurrentPendulum.Rope->Velocity[n].y = CurrentPendulum.Rope->Velocity[CurrentPendulum.node].y;
				CurrentPendulum.Rope->Velocity[n].z = CurrentPendulum.Rope->Velocity[CurrentPendulum.node].z;
			}
		
			CurrentPendulum.Position.x = 0;
			CurrentPendulum.Position.y = 0;
			CurrentPendulum.Position.z = 0;
			CurrentPendulum.Velocity.x = 0;
			CurrentPendulum.Velocity.y = 0;
			CurrentPendulum.Velocity.z = 0;
			CurrentPendulum.node = -1;
			CurrentPendulum.Rope = 0;
		}
	}

	if (lara.RopePtr != -1)
	{
		dir.x = Pendulum->Position.x - Rope->Segment[0].x;
		dir.y = Pendulum->Position.y - Rope->Segment[0].y;
		dir.z = Pendulum->Position.z - Rope->Segment[0].z;
		Normalise(&dir);
	
		for (n = Pendulum->node; n >= 0; n--)
		{
			Rope->Segment[n].x = Rope->MeshSegment[n - 1].x + ((__int64)Rope->SegmentLength * dir.x >> 16);
			Rope->Segment[n].y = Rope->MeshSegment[n - 1].y + ((__int64)Rope->SegmentLength * dir.y >> 16);
			Rope->Segment[n].z = Rope->MeshSegment[n - 1].z + ((__int64)Rope->SegmentLength * dir.z >> 16);
			Rope->Velocity[n].x = 0;
			Rope->Velocity[n].y = 0;
			Rope->Velocity[n].z = 0;
		}
		
		if (bSetFlag)
		{
			dir.x = Pendulum->Position.x - Rope->Segment[Pendulum->node].x;
			dir.y = Pendulum->Position.y - Rope->Segment[Pendulum->node].y;
			dir.z = Pendulum->Position.z - Rope->Segment[Pendulum->node].z;
			Rope->Segment[Pendulum->node].x = Pendulum->Position.x;
			Rope->Segment[Pendulum->node].y = Pendulum->Position.y;
			Rope->Segment[Pendulum->node].z = Pendulum->Position.z;
		
			for (n = Pendulum->node; n < 24; n++)
			{
				Rope->Segment[n].x -= dir.x;
				Rope->Segment[n].y -= dir.y;
				Rope->Segment[n].z -= dir.z;
				Rope->Velocity[n].x = 0;
				Rope->Velocity[n].y = 0;
				Rope->Velocity[n].z = 0;
			}
		}

		ModelRigidRope(&Rope->Segment[0], &Pendulum->Position, &Rope->Velocity[0], &Pendulum->Velocity, Rope->SegmentLength * Pendulum->node);
		Pendulum->Velocity.y += 393216;
		Pendulum->Position.x += Pendulum->Velocity.x;
		Pendulum->Position.y += Pendulum->Velocity.y;
		Pendulum->Position.z += Pendulum->Velocity.z;
		Pendulum->Velocity.x -= Pendulum->Velocity.x >> 8;
		Pendulum->Velocity.z -= Pendulum->Velocity.z >> 8;
	}

	for (n = Pendulum->node; n < 23; n++)
		ModelRigid(&Rope->Segment[n], &Rope->Segment[n + 1], &Rope->Velocity[n], &Rope->Velocity[n + 1], Rope->SegmentLength);
	
	for (n = 0; n < 24; n++)
	{
		Rope->Segment[n].x += Rope->Velocity[n].x;
		Rope->Segment[n].y += Rope->Velocity[n].y;
		Rope->Segment[n].z += Rope->Velocity[n].z;
	}
	
	for (n = Pendulum->node; n < 24; n++)
	{
		Rope->Velocity[n].y += 0x30000;
	
		if (Pendulum->Rope)
		{
			Rope->Velocity[n].x -= Rope->Velocity[n].x >> 4;
			Rope->Velocity[n].z -= Rope->Velocity[n].z >> 4;
		}
		else
		{
			Rope->Velocity[n].x -= Rope->Velocity[n].x >> 7;
			Rope->Velocity[n].z -= Rope->Velocity[n].z >> 7;
		}
	}
	
	Rope->Segment[0].x = 0;
	Rope->Segment[0].y = 0;
	Rope->Segment[0].z = 0;
	Rope->Velocity[0].x = 0;
	Rope->Velocity[0].y = 0;
	Rope->Velocity[0].z = 0;

	for (n = 0; n < 23; n++)
	{
		Rope->NormalisedSegment[n].x = Rope->Segment[n + 1].x - Rope->Segment[n].x;
		Rope->NormalisedSegment[n].y = Rope->Segment[n + 1].y - Rope->Segment[n].y;
		Rope->NormalisedSegment[n].z = Rope->Segment[n + 1].z - Rope->Segment[n].z;
		Normalise(&Rope->NormalisedSegment[n]);
	}

	if (Rope != &RopeList[lara.RopePtr])
	{
		Rope->MeshSegment[0].x = Rope->Segment[0].x;
		Rope->MeshSegment[0].y = Rope->Segment[0].y;
		Rope->MeshSegment[0].z = Rope->Segment[0].z;
		Rope->MeshSegment[1].x = Rope->Segment[0].x + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[0].x >> 16);
		Rope->MeshSegment[1].y = Rope->Segment[0].y + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[0].y >> 16);
		Rope->MeshSegment[1].z = Rope->Segment[0].z + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[0].z >> 16);
	
		for (n = 2; n < 24; n++)
		{
			Rope->MeshSegment[n].x = Rope->MeshSegment[n - 1].x + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n - 1].x >> 16);
			Rope->MeshSegment[n].y = Rope->MeshSegment[n - 1].y + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n - 1].y >> 16);
			Rope->MeshSegment[n].z = Rope->MeshSegment[n - 1].z + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n - 1].z >> 16);
		}
	}
	else
	{
		Rope->MeshSegment[Pendulum->node].x = Rope->Segment[Pendulum->node].x;
		Rope->MeshSegment[Pendulum->node].y = Rope->Segment[Pendulum->node].y;
		Rope->MeshSegment[Pendulum->node].z = Rope->Segment[Pendulum->node].z;
		Rope->MeshSegment[Pendulum->node + 1].x = Rope->Segment[Pendulum->node].x + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[Pendulum->node].x >> 16);
		Rope->MeshSegment[Pendulum->node + 1].y = Rope->Segment[Pendulum->node].y + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[Pendulum->node].y >> 16);
		Rope->MeshSegment[Pendulum->node + 1].z = Rope->Segment[Pendulum->node].z + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[Pendulum->node].z >> 16);
		
		for (n = Pendulum->node + 1; n < 23; n++)
		{
			Rope->MeshSegment[n + 1].x = Rope->MeshSegment[n].x + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n].x >> 16);
			Rope->MeshSegment[n + 1].y = Rope->MeshSegment[n].y + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n].y >> 16);
			Rope->MeshSegment[n + 1].z = Rope->MeshSegment[n].z + ((__int64)Rope->SegmentLength * Rope->NormalisedSegment[n].z >> 16);
		}
		
		for (n = 0; n < Pendulum->node; n++)
		{
			Rope->MeshSegment[n].x = Rope->Segment[n].x;
			Rope->MeshSegment[n].y = Rope->Segment[n].y;
			Rope->MeshSegment[n].z = Rope->Segment[n].z;
		}
	}
}

long RopeNodeCollision(ROPE_STRUCT* rope, long x, long y, long z, long rad)
{
	long i, rx, ry, rz;

	for (i = 0; i < 22; i++)
	{
		if (y > rope->Position.y + (rope->MeshSegment[i].y >> 16) && y < rope->Position.y + (rope->MeshSegment[i + 1].y >> 16))
		{
			rx = x - ((rope->MeshSegment[i + 1].x + rope->MeshSegment[i].x) >> 17) - rope->Position.x;
			ry = y - ((rope->MeshSegment[i + 1].y + rope->MeshSegment[i].y) >> 17) - rope->Position.y;
			rz = z - ((rope->MeshSegment[i + 1].z + rope->MeshSegment[i].z) >> 17) - rope->Position.z;

			if (SQUARE(rx) + SQUARE(ry) + SQUARE(rz) < SQUARE(rad + 64))
				return i;
		}
	}

	return -1;
}

void SetPendulumVelocity(long x, long y, long z)
{
	long scale;

	if (CurrentPendulum.node >> 1 < 12)
	{
		scale = 4096 / (24 - 2 * (CurrentPendulum.node >> 1)) * 256;
		x = (__int64)scale * x >> 16;
		y = (__int64)scale * y >> 16;
		z = (__int64)scale * z >> 16;
	}

	CurrentPendulum.Velocity.x += x;
	CurrentPendulum.Velocity.y += y;
	CurrentPendulum.Velocity.z += z;
}

void SetPendulumPoint(ROPE_STRUCT* Rope, long node)
{
	CurrentPendulum.Position.x = Rope->Segment[node].x;
	CurrentPendulum.Position.y = Rope->Segment[node].y;
	CurrentPendulum.Position.z = Rope->Segment[node].z;

	if (CurrentPendulum.node == -1)
	{
		CurrentPendulum.Velocity.x += Rope->Velocity[node].x;
		CurrentPendulum.Velocity.y += Rope->Velocity[node].y;
		CurrentPendulum.Velocity.z += Rope->Velocity[node].z;
	}

	CurrentPendulum.node = node;
	CurrentPendulum.Rope = Rope;
}

void ModelRigidRope(PHD_VECTOR* pa, PHD_VECTOR* pb, PHD_VECTOR* va, PHD_VECTOR* vb, long rlength)
{
	PHD_VECTOR delta, d, a, b;
	long length, scale;

	a.x = pb->x - pa->x;
	a.y = pb->y - pa->y;
	a.z = pb->z - pa->z;
	b.x = vb->x;
	b.y = vb->y;
	b.z = vb->z;
	d.x = a.x + b.x;
	d.y = a.y + b.y;
	d.z = a.z + b.z;
	length = phd_sqrt(abs(SQUARE(d.x >> 16) + SQUARE(d.y >> 16) + SQUARE(d.z >> 16)));
	scale = (length << 16) - rlength;
	Normalise(&d);
	delta.x = (__int64)scale * d.x >> 16;
	delta.y = (__int64)scale * d.y >> 16;
	delta.z = (__int64)scale * d.z >> 16;
	vb->x -= delta.x;
	vb->y -= delta.y;
	vb->z -= delta.z;
}

void ModelRigid(PHD_VECTOR* pa, PHD_VECTOR* pb, PHD_VECTOR* va, PHD_VECTOR* vb, long rlength)
{
	PHD_VECTOR delta, d, a, b;
	long length, scale;

	a.x = pb->x - pa->x;
	a.y = pb->y - pa->y;
	a.z = pb->z - pa->z;
	b.x = vb->x - va->x;
	b.y = vb->y - va->y;
	b.z = vb->z - va->z;
	d.x = a.x + b.x;
	d.y = a.y + b.y;
	d.z = a.z + b.z;
	length = phd_sqrt(abs(SQUARE(d.x >> 16) + SQUARE(d.y >> 16) + SQUARE(d.z >> 16)));
	scale = ((length << 16) - rlength) >> 1;
	Normalise(&d);
	delta.x = (__int64)scale * d.x >> 16;
	delta.y = (__int64)scale * d.y >> 16;
	delta.z = (__int64)scale * d.z >> 16;
	va->x += delta.x;
	va->y += delta.y;
	va->z += delta.z;
	vb->x -= delta.x;
	vb->y -= delta.y;
	vb->z -= delta.z;
}

/* The length Normalise measures: the vector before its << 16, through the
 * same integer square root. */
static long RopeLogLength(long x, long y, long z)
{
	return phd_sqrt(abs(SQUARE(x) + SQUARE(y) + SQUARE(z)));
}

/* ROPELOG, one line per AlignLaraToRope call, behind -log: every tick she
 * hangs on a rope (AnimateLara) and the catch (RopeCollision):
 *   f       GlobalCounter
 *   state, anim, frame   her current_anim_state, anim_number, frame_number
 *   RopeY   lara.RopeY, her heading round the rope, 256 per tick of a turn;
 *           angle the short ropeangle made of it (RopeY - 16380)
 *   seg, off   lara.RopeSegment and lara.RopeOffset
 *   f7      the frame's root y (frame[7]) the rope point reads
 *   len     the three lengths Normalise measures, as it measures them: the
 *           rope's direction at her grip (191 on a still rope), the turned
 *           facing n2 + v2 (about 4096) and u x v (about 16384; above 16384
 *           Normalise's integer reciprocal comes out at 3 instead of 4, n
 *           keeps 75% of its length and z_rot comes out up to a quarter too
 *           large)
 *   xyz     the angles she is given: x_rot, y_rot, z_rot
 *   pos     her position after the 112-unit push off the rope, and her
 *           room_number at the call
 * The draw's side of the same ticks is ROPELOG draw (game\draw.cpp). */
static void RopeLog(ITEM_INFO* l, short ropeangle, short* frame, long ulen, long vlen, long nlen)
{
	Log("ROPELOG f=%d state=%d anim=%d frame=%d RopeY=%d angle=%d seg=%d off=%d f7=%d len=%ld/%ld/%ld "
		"xyz=%d,%d,%d pos=%ld,%ld,%ld/%d",
		(int)GlobalCounter, (int)l->current_anim_state, (int)l->anim_number, (int)l->frame_number,
		(int)lara.RopeY, (int)ropeangle, (int)lara.RopeSegment, (int)lara.RopeOffset, (int)frame[7],
		ulen, vlen, nlen, (int)l->pos.x_rot, (int)l->pos.y_rot, (int)l->pos.z_rot,
		l->pos.x_pos, l->pos.y_pos, l->pos.z_pos, (int)l->room_number);
}

void AlignLaraToRope(ITEM_INFO* l)
{
	ROPE_STRUCT* rope;
	PHD_VECTOR v, u, n, up, v1, v2, n2;
	long* mptr;
	short* frame;
	long x, y, z, x1, y1, z1, i;
	long temp[indices_count];
	long ulen, vlen, nlen;
	short xyz[3];
	short ropeangle;

	up.x = 4096;
	up.y = 0;
	up.z = 0;
	frame = GetBestFrame(l);
	ropeangle = lara.RopeY - 16380;
	rope = &RopeList[lara.RopePtr];
	i = lara.RopeSegment;
	GetRopePos(rope, (i - 1) * 128 + frame[7], &x, &y, &z);
	GetRopePos(rope, (i - 1) * 128 + frame[7] - 192, &x1, &y1, &z1);
	u.x = (x - x1) << 16;
	u.y = (y - y1) << 16;
	u.z = (z - z1) << 16;
	ulen = LogFam_ROPELOG && gLogEnabled ? RopeLogLength(x - x1, y - y1, z - z1) : 0;
	Normalise(&u);
	u.x >>= 2;
	u.y >>= 2;
	u.z >>= 2;
	vMul(&u, mDotProduct(&up, &u), &v);
	v.x = up.x - v.x;
	v.y = up.y - v.y;
	v.z = up.z - v.z;
	v1 = v;
	v2 = v;
	n2 = u;
	vMul(&v1, phd_cos(ropeangle), &v1);
	vMul(&n2, mDotProduct(&n2, &v), &n2);
	vMul(&n2, 4096 - phd_cos(ropeangle), &n2);
	mCrossProduct(&u, &v, &v2);
	vMul(&v2, phd_sin(ropeangle), &v2);
	n2.x += v1.x;
	n2.y += v1.y;
	n2.z += v1.z;
	v.x = (n2.x + v2.x) << 16;
	v.y = (n2.y + v2.y) << 16;
	v.z = (n2.z + v2.z) << 16;
	vlen = LogFam_ROPELOG && gLogEnabled ? RopeLogLength(n2.x + v2.x, n2.y + v2.y, n2.z + v2.z) : 0;
	Normalise(&v);
	v.x >>= 2;
	v.y >>= 2;
	v.z >>= 2;
	mCrossProduct(&u, &v, &n);
	nlen = LogFam_ROPELOG && gLogEnabled ? RopeLogLength(n.x, n.y, n.z) : 0;
	n.x <<= 16;
	n.y <<= 16;
	n.z <<= 16;
	Normalise(&n);
	n.x >>= 2;
	n.y >>= 2;
	n.z >>= 2;
	temp[M00] = n.x;
	temp[M01] = u.x;
	temp[M02] = v.x;
	temp[M10] = n.y;
	temp[M11] = u.y;
	temp[M12] = v.y;
	temp[M20] = n.z;
	temp[M21] = u.z;
	temp[M22] = v.z;
	phd_GetMatrixAngles(temp, xyz);
	l->pos.x_pos = rope->Position.x + (rope->MeshSegment[i].x >> 16);
	l->pos.y_pos = rope->Position.y + (rope->MeshSegment[i].y >> 16) + lara.RopeOffset;
	l->pos.z_pos = rope->Position.z + (rope->MeshSegment[i].z >> 16);
	phd_PushUnitMatrix();
	phd_RotYXZ(xyz[1], xyz[0], xyz[2]);
	mptr = phd_mxptr;
	l->pos.x_pos += -112 * mptr[2] >> W2V_SHIFT;
	l->pos.y_pos += -112 * mptr[6] >> W2V_SHIFT;
	l->pos.z_pos += -112 * mptr[10] >> W2V_SHIFT;
	phd_PopMatrix();
	l->pos.x_rot = xyz[0];
	l->pos.y_rot = xyz[1];
	l->pos.z_rot = xyz[2];

	if (LogFam_ROPELOG && gLogEnabled)
		RopeLog(l, ropeangle, frame, ulen, vlen, nlen);
}

void LaraClimbRope(ITEM_INFO* item, COLL_INFO* coll)
{
	if (!(input & IN_ACTION_ANY))
	{
		FallFromRope(item);
		return;
	}

	camera.target_angle = 5460;

	if (lara.RopeCount)
	{
		if (!lara.RopeFlag)
		{
			lara.RopeCount--;
			lara.RopeOffset += lara.RopeDownVel;

			if (!lara.RopeCount)
				lara.RopeFlag = 1;

			return;
		}
	}
	else if (!lara.RopeFlag)
	{
		lara.RopeOffset = 0;
		lara.RopeDownVel = (ulong)(RopeList[lara.RopePtr].MeshSegment[lara.RopeSegment + 1].y - RopeList[lara.RopePtr].MeshSegment[lara.RopeSegment].y) >> 17;
		lara.RopeCount = 0;
		lara.RopeOffset += lara.RopeDownVel;
		lara.RopeFlag = 1;
		return;
	}

	if (item->anim_number == ANIM_ROPESLIDEL && item->frame_number == anims[item->anim_number].frame_end)
	{
		SoundEffect(SFX_LARA_ROPEDOWN_LOOP, &lara_item->pos, 0);
		item->frame_number = anims[item->anim_number].frame_base;
		lara.RopeFlag = 0;
		lara.RopeSegment++;
		lara.RopeOffset = 0;
	}

	if (!(input & IN_BACK) || lara.RopeSegment >= 21)
		item->goal_anim_state = AS_ROPE;
}

void DrawRopeList()
{
	for (int i = 0; i < nRope; i++)
	{
		if (RopeList[i].Active)
			DrawRope(&RopeList[i]);
	}
}

void ProjectRopePoints(ROPE_STRUCT* Rope)
{
	D3DVECTOR Output;
	PHD_VECTOR t;
	float zv;
	long sw, sh, n;

	sw = phd_winwidth >> 1;
	sh = phd_winheight >> 1;
	phd_PushMatrix();
	phd_TranslateAbs(Rope->Position.x, Rope->Position.y, Rope->Position.z);

	for (n = 0; n < 24; n++)
	{
		t.x = Rope->MeshSegment[n].x >> 16;
		t.y = Rope->MeshSegment[n].y >> 16;
		t.z = Rope->MeshSegment[n].z >> 16;
		Output.x = (D3DVALUE) (t.x * phd_mxptr[M00] + t.y * phd_mxptr[M01] + t.z * phd_mxptr[M02] + phd_mxptr[M03]);
		Output.y = (D3DVALUE) (t.x * phd_mxptr[M10] + t.y * phd_mxptr[M11] + t.z * phd_mxptr[M12] + phd_mxptr[M13]);
		Output.z = (D3DVALUE) (t.x * phd_mxptr[M20] + t.y * phd_mxptr[M21] + t.z * phd_mxptr[M22] + phd_mxptr[M23]);
		zv = phd_persp / Output.z;
		Rope->Coords[n][0] = long(Output.x * zv + sw);
		Rope->Coords[n][1] = long(Output.y * zv + sh);
		Rope->Coords[n][2] = (long)Output.z;
	}

	phd_PopMatrix();
}

void init_all_ropes()
{
	for (int i = 0; i < 8; i++)
		RopeList[i].Active = 0;

	nRope = 0;
}

/* The pendulum goes into the save as PENDULUM_SAVE (types.h), 32 bytes on
 * both builds: written raw, a PENDULUM is 36 bytes on a 64-bit build and
 * would shift everything the save holds behind it. */
void SaveRope()
{
	PENDULUM_SAVE save;

	WriteSG(&RopeList[lara.RopePtr], sizeof(ROPE_STRUCT));
	PendulumToSave(&save, &CurrentPendulum, RopeList);
	WriteSG(&save, sizeof(PENDULUM_SAVE));
}

void LoadRope()
{
	PENDULUM_SAVE save;

	ReadSG(&RopeList[lara.RopePtr], sizeof(ROPE_STRUCT));
	ReadSG(&save, sizeof(PENDULUM_SAVE));
	PendulumFromSave(&CurrentPendulum, &save, RopeList);
}

void StraightenRope(ITEM_INFO* item)
{
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	PHD_VECTOR dir;
	short room_number;

	room_number = item->room_number;
	pos.x = item->pos.x_pos;
	pos.y = item->pos.y_pos;
	pos.z = item->pos.z_pos;
	floor = GetFloor(pos.x, pos.y, pos.z, &room_number);
	pos.y = GetCeiling(floor, pos.x, pos.y, pos.z);

	dir.x = 0;
	dir.y = 0x4000;
	dir.z = 0;
	_Straighten(&RopeList[nRope], &pos, &dir, 128);
}

void _Straighten(ROPE_STRUCT* rope, PHD_VECTOR* pos, PHD_VECTOR* dir, long slength)
{
	long n;

	rope->Position = *pos;
	rope->SegmentLength = slength << 16;
	dir->x <<= 16;
	dir->y <<= 16;
	dir->z <<= 16;
	Normalise(dir);
	rope->Coiled = 0;

	for (n = 0; n < 24; n++)
	{
		rope->Segment[n].x = __int64(rope->SegmentLength * n) * dir->x >> 16;
		rope->Segment[n].y = __int64(rope->SegmentLength * n) * dir->y >> 16;
		rope->Segment[n].z = __int64(rope->SegmentLength * n) * dir->z >> 16;
		rope->Velocity[n].x = 0;
		rope->Velocity[n].y = 0;
		rope->Velocity[n].z = 0;
	}
}

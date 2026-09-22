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
#include "laraskin.h"
#include "objects.h"
#include "gameflow.h"
#include "delstuff.h"
#include "control.h"
#include "lara.h"	// LaraDrawType - outfit marker for the braid weld table
#include "../specific/function_stubs.h"	// Log - the seam in the seam sewing

static uchar SkinJoints[14][4] =
{
	{ 0, 1, 1, 3 },
	{ 1, 2, 4, 5 },
	{ 2, 3, 6, 7 },
	{ 0, 4, 2, 8 },
	{ 4, 5, 9, 10 },
	{ 5, 6, 11, 12 },
	{ 7, 0, 13, 0 },
	{ 7, 8, 14, 17 },
	{ 8, 9, 18, 19 },
	{ 9, 10, 20, 21 },
	{ 7, 11, 15, 22 },
	{ 11, 12, 23, 24 },
	{ 12, 13, 25, 26 },
	{ 14, 7, 27, 16 }
};

static char HairSkinVertNums[6][12] =
{
	{ 37, 38, 39, 40, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 79, 77, 78, 76, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 68, 71, 69, 70, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 2, 3, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 25, 26, 27, 28, -1, 0, 0, 0, 0, 0, 0, 0 }
};

static char HairScratchVertNums[4][12] =
{
	{ 0, 3, 1, 2, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 3, 1, 2, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 2, 3, -1, 0, 0, 0, 0, 0, 0, 0 }
};

static PHD_VECTOR* SkinXYZPtr;
static PHD_VECTOR BelowMeshXYZ;
static PHD_VECTOR JointMeshXYZ;
static PHD_VECTOR AboveMeshXYZ;
static PHD_VECTOR XYZList[32];
static uchar PointsToCalc[26][12];

void OptomiseSkinningData()
{
	OBJECT_INFO* object;
	short** meshpp;
	short* JointMesh, * MeshJ, * MeshNormals, * Src, * Dest;
	short VertTable[128];
	short NormalTable[128];
	long c, lp, lp1, numvertsj, numvertstocalc, padval, numtris, numquads;
	uchar RemapTable[32];

	object = &objects[LARA_SKIN_JOINTS];
	meshpp = &meshes[object->mesh_index + 1];

	for (c = 0; c < 14; ++c)
	{
		JointMesh = *meshpp;
		meshpp++;
		numvertsj = JointMesh[5] & 0xFF;
		numvertstocalc = JointMesh[5] & 0xFF;

		if (!numvertstocalc)
			numvertstocalc = (ushort)JointMesh[5] >> 8;

		lp = 0;

		while (PointsToCalc[c][lp] != 255)
			lp++;

		if (!numvertsj)
			continue;

		for (lp1 = 0; lp1 < lp; lp1++)
			RemapTable[PointsToCalc[c][lp1]] = (uchar)lp1;

		padval = lp;

		for (lp1 = 0; ScratchVertNums[SkinJoints[c][2]][lp1] != -1; lp1++)
		{
			RemapTable[ScratchVertNums[SkinJoints[c][2]][lp1]] = (uchar)padval;
			ScratchVertNums[SkinJoints[c][2]][lp1] = (char)padval;
			padval++;
		}

		for (lp1 = 0; ScratchVertNums[SkinJoints[c][3]][lp1] != -1; lp1++)
		{
			RemapTable[ScratchVertNums[SkinJoints[c][3]][lp1]] = (uchar)padval;
			ScratchVertNums[SkinJoints[c][3]][lp1] = (char)padval;
			padval++;
		}

		MeshJ = &JointMesh[6];
		MeshNormals = &JointMesh[3 * numvertstocalc + 7];

		for (lp1 = 0; lp1 < numvertsj; lp1++)
		{
			VertTable[4 * lp1] = MeshJ[0];
			VertTable[4 * lp1 + 1] = MeshJ[1];
			VertTable[4 * lp1 + 2] = MeshJ[2];
			NormalTable[4 * lp1] = MeshNormals[0];
			NormalTable[4 * lp1 + 1] = MeshNormals[1];
			NormalTable[4 * lp1 + 2] = MeshNormals[2];
			VertTable[4 * lp1 + 3] = 0;
			NormalTable[4 * lp1 + 3] = 0;
			MeshJ += 3;
			MeshNormals += 3;
		}

		for (lp1 = 0; lp1 < lp; lp1++)
		{
			Src = &VertTable[4 * PointsToCalc[c][lp1]];
			Dest = &JointMesh[3 * (RemapTable[PointsToCalc[c][lp1]] + 2)];
			Dest[0] = Src[0];
			Dest[1] = Src[1];
			Dest[2] = Src[2];
			Src = &NormalTable[4 * PointsToCalc[c][lp1]];
			Dest = &JointMesh[3 * (RemapTable[PointsToCalc[c][lp1]] + lp) + 7];
			Dest[0] = Src[0];
			Dest[1] = Src[1];
			Dest[2] = Src[2];
		}

		Dest = &JointMesh[6 * numvertstocalc + 7];
		numquads = *Dest;
		Dest++;

		for (lp1 = 0; lp1 < numquads; lp1++)
		{
			Dest[0] = RemapTable[Dest[0]];
			Dest[1] = RemapTable[Dest[1]];
			Dest[2] = RemapTable[Dest[2]];
			Dest[3] = RemapTable[Dest[3]];
			Dest += 6;
		}

		numtris = *Dest;
		Dest++;

		for (lp1 = 0; lp1 < numtris; lp1++)
		{
			Dest[0] = RemapTable[Dest[0]];
			Dest[1] = RemapTable[Dest[1]];
			Dest[2] = RemapTable[Dest[2]];
			Dest += 5;
		}

		JointMesh[5] = (short)lp;
		JointMesh[3 * (lp + 2)] = 0;
		Src = &JointMesh[6 * numvertstocalc + 7];
		Dest = &JointMesh[6 * lp + 7];
		numquads = 6 * *Src + 1;

		for (lp1 = 0; lp1 < numquads; lp1++)
		{
			*Dest = *Src;
			Src++;
			Dest++;
		}

		Src = &JointMesh[6 * numvertstocalc + numquads + 7];
		Dest = &JointMesh[6 * lp + numquads + 7];
		numtris = 5 * *Src + 1;

		for (lp1 = 0; lp1 < numtris; lp1++)
		{
			*Dest = *Src;
			Src++;
			Dest++;
		}
	}
}

// The stack is XYZList[32]; both push and pop moved the pointer with no
// bound. Its push/pop marks come from the level's bone tree, so a skin
// whose bones are not TR5's - a foreign outfit loaded into LARA_SKIN -
// walks the pointer clean out of the array. Both directions write, and
// XYZList is a global, so an overrun corrupts its neighbours: a hard
// crash inside LoadObjects, before a single vertex is matched, with
// nothing in the log to say a skeleton was to blame.
//
// Clamped rather than refused, like the seam cap further down: a wrong
// skeleton should draw wrongly, not kill the level load.
void PushXYZ()
{
	PHD_VECTOR* ptr;

	if (SkinXYZPtr >= &XYZList[31])
	{
		Log("SKINLOG: bone stack full (32), push ignored - is this skin's bone tree Lara's?");
		return;
	}

	ptr = SkinXYZPtr++;
	SkinXYZPtr->x = ptr->x;
	SkinXYZPtr->y = ptr->y;
	SkinXYZPtr->z = ptr->z;
}

void PopXYZ()
{
	if (SkinXYZPtr <= XYZList)
	{
		Log("SKINLOG: bone stack empty, pop ignored - more pops than pushes in the bone tree");
		return;
	}

	SkinXYZPtr--;
}

void CreateSkinningData()
{
	MESH_DATA* aboveMesh;
	MESH_DATA* belowMesh;
	MESH_DATA* jointMesh;
	MESH_DATA* hairMesh;
	OBJECT_INFO* obj;
	short** meshpp;
	long* bone;
	short* joint;
	short* LaraMesh;
	long vertCount, aboveVerts, belowVerts, jointVerts, laraVerts, laraX, laraY, laraZ, jointX, jointY, jointZ, calcPointsCounter;
	short aboveMeshNum, belowMeshNum, jointMeshNum;
	uchar vertBuf[128];

	for (int i = 0; i < 14; i++)
	{
		SkinXYZPtr = XYZList;
		SkinXYZPtr->x = 0;
		SkinXYZPtr->y = 0;
		SkinXYZPtr->z = 0;
		obj = &objects[LARA_SKIN];
		meshpp = &meshes[obj->mesh_index];
		bone = &bones[obj->bone_index];
		aboveMeshNum = SkinJoints[i][0];
		belowMeshNum = SkinJoints[i][1];
		aboveMesh = (MESH_DATA*)*meshpp;
		belowMesh = (MESH_DATA*)*meshpp;
		jointMesh = (MESH_DATA*)*meshpp;

		if (aboveMeshNum)
		{
			meshpp++;

			for (int j = 1; j < obj->nmeshes; j++, bone += 4, meshpp++)
			{
				if (bone[0] & 1)
					PopXYZ();

				if (bone[0] & 2)
					PushXYZ();

				SkinXYZPtr->x += bone[1];
				SkinXYZPtr->y += bone[2];
				SkinXYZPtr->z += bone[3];

				if (aboveMeshNum == j)
				{
					aboveMesh = (MESH_DATA*)*meshpp;
					AboveMeshXYZ.x = SkinXYZPtr->x;
					AboveMeshXYZ.y = SkinXYZPtr->y;
					AboveMeshXYZ.z = SkinXYZPtr->z;
					break;
				}
			}
		}
		else
		{
			aboveMesh = (MESH_DATA*)*meshpp;
			AboveMeshXYZ.x = 0;
			AboveMeshXYZ.y = 0;
			AboveMeshXYZ.z = 0;
		}

		SkinXYZPtr = XYZList;
		SkinXYZPtr->x = 0;
		SkinXYZPtr->y = 0;
		SkinXYZPtr->z = 0;
		meshpp = &meshes[obj->mesh_index];
		bone = &bones[obj->bone_index];

		if (belowMeshNum)
		{
			meshpp++;

			for (int j = 1; j < obj->nmeshes; j++, bone += 4, meshpp++)
			{
				if (bone[0] & 1)
					PopXYZ();

				if (bone[0] & 2)
					PushXYZ();

				SkinXYZPtr->x += bone[1];
				SkinXYZPtr->y += bone[2];
				SkinXYZPtr->z += bone[3];

				if (belowMeshNum == j)
				{
					belowMesh = (MESH_DATA*)*meshpp;
					BelowMeshXYZ.x = SkinXYZPtr->x;
					BelowMeshXYZ.y = SkinXYZPtr->y;
					BelowMeshXYZ.z = SkinXYZPtr->z;
					break;
				}
			}
		}
		else
		{
			belowMesh = (MESH_DATA*)*meshpp;
			BelowMeshXYZ.x = 0;
			BelowMeshXYZ.y = 0;
			BelowMeshXYZ.z = 0;
		}

		SkinXYZPtr = XYZList;
		SkinXYZPtr->x = 0;
		SkinXYZPtr->y = 0;
		SkinXYZPtr->z = 0;
		obj = &objects[LARA_SKIN_JOINTS];
		meshpp = &meshes[obj->mesh_index + 1];
		bone = &bones[obj->bone_index];
		jointMeshNum = i + 1;

		for (int j = 1; j < obj->nmeshes; j++, bone += 4, meshpp++)
		{
			if (*bone & 1)
				PopXYZ();

			if (*bone & 2)
				PushXYZ();

			SkinXYZPtr->x += bone[1];
			SkinXYZPtr->y += bone[2];
			SkinXYZPtr->z += bone[3];

			if (jointMeshNum == j)
			{
				jointMesh = (MESH_DATA*)*meshpp;
				JointMeshXYZ.x = SkinXYZPtr->x;
				JointMeshXYZ.y = SkinXYZPtr->y;
				JointMeshXYZ.z = SkinXYZPtr->z;
				break;
			}
		}

		vertCount = 0;

		// SKINREACH: how far the seam misses, not just whether it hits. The
		// exact-coordinate pairing below runs on +-1 unit, so a foreign
		// skin scores zero and that alone says nothing about why. What
		// decides whether the joint layer can be rescued at all is the
		// distance to the nearest skin vertex: small, and a wider
		// tolerance can sew a foreign body; large, and the joints stand
		// nowhere near its rims and only remodelling helps. Computed
		// inside the loops that already run, so nothing extra is walked.
		long nearMin[2] = { 0x7FFFFFFF, 0x7FFFFFFF };
		long nearMax[2] = { 0, 0 };
		int  within8[2] = { 0, 0 };
		int  within32[2] = { 0, 0 };
		aboveVerts = 0;
		jointVerts = jointMesh->nVerts & 0xFF;
		laraVerts = aboveMesh->nVerts & 0xFF;

		// vertBuf is [128] and jointVerts is a byte out of the mesh header, so
		// it can say up to 255. Vanilla joints stay far below that; a damaged
		// or foreign one would have cleared past the buffer.
		if (jointVerts > (long)sizeof(vertBuf))
		{
			Log("SKINLOG joint %d: joint mesh claims %d vertices, clamped to %d",
				i, jointVerts, (long)sizeof(vertBuf));
			jointVerts = (long)sizeof(vertBuf);
		}

		if (jointVerts > 0)
			memset(vertBuf, 0, jointVerts);

		joint = &jointMesh->nNorms;

		for (int j = 0; j < jointVerts; j++)
		{
			jointX = JointMeshXYZ.x + joint[0];
			jointY = JointMeshXYZ.y + joint[1];
			jointZ = JointMeshXYZ.z + joint[2];
			joint += 3;
			LaraMesh = &aboveMesh->nNorms;
			long nearest = 0x7FFFFFFF;

			for (int ii = 0; ii < laraVerts; ii++)
			{
				laraX = AboveMeshXYZ.x + LaraMesh[0];
				laraY = AboveMeshXYZ.y + LaraMesh[1];
				laraZ = AboveMeshXYZ.z + LaraMesh[2];
				LaraMesh += 3;

				{
					const long d = abs(laraX - jointX) + abs(laraY - jointY) + abs(laraZ - jointZ);
					if (d < nearest) nearest = d;
				}

				if (abs(laraX - jointX) <= 1 && abs(laraY - jointY) <= 1 && abs(laraZ - jointZ) <= 1)
				{
					vertCount++;
					vertBuf[j] = 1;
					// SkinVertNums/ScratchVertNums are [40][12] and the row
					// needs its -1 terminator, so eleven pairs fit; nothing
					// used to enforce that. With Lara's own skin it never
					// mattered, because the joints were authored against
					// exactly these meshes - but the seam is matched at
					// runtime by coordinate, so a skin the joints were not
					// built for can produce far more hits and write straight
					// past the row into the neighbouring one. Both arrays
					// are globals, so an overrun took the rest with it:
					// loading died inside CreateSkinningData before a level
					// ever appeared.
					//
					// Capped rather than refused: eleven pairs are enough to
					// sew a seam, and a foreign skin should look wrong, not
					// crash.
					if (aboveVerts >= 11)
					{
						Log("SKINLOG joint %d: more than 11 upper matches, capped", i);
						break;
					}

					SkinVertNums[SkinJoints[i][2]][aboveVerts] = ii;
					ScratchVertNums[SkinJoints[i][2]][aboveVerts] = j;
					aboveVerts++;
				}
			}

			if (laraVerts > 0)
			{
				if (nearest < nearMin[0]) nearMin[0] = nearest;
				if (nearest > nearMax[0]) nearMax[0] = nearest;
				if (nearest <= 8)  within8[0]++;
				if (nearest <= 32) within32[0]++;
			}
		}

		SkinVertNums[SkinJoints[i][2]][aboveVerts] = -1;
		ScratchVertNums[SkinJoints[i][2]][aboveVerts] = -1;
		belowVerts = 0;
		laraVerts = belowMesh->nVerts & 0xFF;
		joint = &jointMesh->nNorms;

		for (int j = 0; j < jointVerts; j++)
		{
			jointX = JointMeshXYZ.x + joint[0];
			jointY = JointMeshXYZ.y + joint[1];
			jointZ = JointMeshXYZ.z + joint[2];
			joint += 3;
			LaraMesh = &belowMesh->nNorms;
			long nearest = 0x7FFFFFFF;

			for (int ii = 0; ii < laraVerts; ii++)
			{
				laraY = BelowMeshXYZ.y + LaraMesh[1];
				laraX = BelowMeshXYZ.x + LaraMesh[0];
				laraZ = BelowMeshXYZ.z + LaraMesh[2];
				LaraMesh += 3;

				{
					const long d = abs(laraX - jointX) + abs(laraY - jointY) + abs(laraZ - jointZ);
					if (d < nearest) nearest = d;
				}

				if (abs(laraX - jointX) <= 1 && abs(laraY - jointY) <= 1 && abs(laraZ - jointZ) <= 1)
				{
					vertCount++;
					vertBuf[j] = 1;
					// Same cap as above, other side of the joint.
					if (belowVerts >= 11)
					{
						Log("SKINLOG joint %d: more than 11 lower matches, capped", i);
						break;
					}

					SkinVertNums[SkinJoints[i][3]][belowVerts] = ii;
					ScratchVertNums[SkinJoints[i][3]][belowVerts] = j;
					belowVerts++;
				}
			}

			if (laraVerts > 0)
			{
				if (nearest < nearMin[1]) nearMin[1] = nearest;
				if (nearest > nearMax[1]) nearMax[1] = nearest;
				if (nearest <= 8)  within8[1]++;
				if (nearest <= 32) within32[1]++;
			}
		}

		SkinVertNums[SkinJoints[i][3]][belowVerts] = -1;
		ScratchVertNums[SkinJoints[i][3]][belowVerts] = -1;

		// SKINREACH: answers "would a wider tolerance help?" - the numbers
		// are Manhattan distances in world units from a joint vertex to
		// the closest skin vertex on that side.
		if (jointVerts > 0)
			Log("SKINREACH joint %2d of %2d verts: above nearest %d..%d (%d within 8, %d within 32), "
				"below nearest %d..%d (%d within 8, %d within 32)",
				i, jointVerts,
				nearMin[0] == 0x7FFFFFFF ? -1 : nearMin[0], nearMax[0], within8[0], within32[0],
				nearMin[1] == 0x7FFFFFFF ? -1 : nearMin[1], nearMax[1], within8[1], within32[1]);
		// Can this seam be sewn at all? A joint has to sit on the skin
		// with most of its vertices; a few unmatched ones are normal and
		// are what PointsToCalc exists for, since those get computed
		// instead of copied. But a joint that finds only one or two
		// partners by coincidence is not sewn, it is guessed, and TR5's
		// own joint geometry then hangs in a foreign body: the pieces
		// that stay wrong are exactly the joints with a handful of
		// accidental hits.
		//
		// Lara's own skin is far from this line and matches almost
		// everything. The exact pass counts only if it matched every
		// vertex, on both sides of the seam.
		//
		// A partial match is worse than none, as the neck shows: keeping
		// TR5's own head means the upper side of that seam matches
		// perfectly, while on a foreign torso the lower side then matches
		// a handful of vertices by coincidence. The joint then passes as
		// usable and the fallback that would pair the rest sensibly never
		// runs; the unmatched vertices go to PointsToCalc and are
		// computed assuming the skin the joints were authored for -
		// producing a stray triangle from the chin to the forearm.
		//
		// Vanilla is unaffected: its joints match completely and are
		// usable exactly as before. Even a vanilla joint that did not
		// match completely would come out the same, because the fallback
		// pairs it at distance zero.
		SkinJointUsable[i] = (jointVerts > 0 && aboveVerts > 0 && belowVerts > 0
							  && vertCount == jointVerts) ? 1 : 0;

		// Second attempt, only if the first one failed: pair by nearest
		// neighbour instead of by exact coordinate.
		//
		// The exact pass wants the joint vertex and the skin vertex to
		// sit on the same spot to within one unit, which holds only for
		// the skin the joints were authored against. A foreign body
		// scores zero there, but SKINREACH measures how far off it really
		// is - typically 2 to 30 units on a Lara some 750 units tall. The
		// joints stand roughly where they belong; they just miss the
		// exact-match tolerance.
		//
		// Pairing by nearest neighbour costs nothing in looks, because a
		// joint does not bring its own vertex positions: at draw time
		// every one of its vertices is overwritten with the position of
		// the skin vertex it was paired with (SkinVerticesToScratch in
		// drawlara.cpp). The joint mesh only contributes the order of its
		// triangles, so it bridges whatever rim it is given.
		//
		// The search only ever runs against the two meshes this seam
		// joins, never the whole body, so the worst a wrong pairing can
		// do is twist one seam - it cannot reach across to another limb.
		//
		// Vanilla never reaches this block: its joints are usable after
		// the exact pass above.
		if (!SkinJointUsable[i] && jointVerts > 0)
		{
			const long SEAM_REACH = 96;		//manhattan units, roughly a limb's thickness

			// Which side a joint vertex belongs to is not a question of
			// distance but of how the joint is built: it is two rings, one
			// meant for the mesh above and one for the mesh below. Handing
			// each vertex to whichever mesh happens to be nearer looks
			// obvious and is wrong - on a foreign body one side is nearer
			// systematically, the ring collapses into a flat fan,
			// producing a visible grey slab across Lara's waist.
			//
			// The axis that separates the two rings is known: it is the
			// offset between the two bones this seam joins. Projecting
			// every joint vertex onto it, the sign says which ring it is
			// in.
			long axisX = AboveMeshXYZ.x - BelowMeshXYZ.x;
			long axisY = AboveMeshXYZ.y - BelowMeshXYZ.y;
			long axisZ = AboveMeshXYZ.z - BelowMeshXYZ.z;
			const bool haveAxis = (abs(axisX) + abs(axisY) + abs(axisZ)) >= 8;

			long midX = 0, midY = 0, midZ = 0;
			joint = &jointMesh->nNorms;

			for (int j = 0; j < jointVerts; j++)
			{
				midX += joint[0];
				midY += joint[1];
				midZ += joint[2];
				joint += 3;
			}

			if (jointVerts > 0)
			{
				midX /= jointVerts;
				midY /= jointVerts;
				midZ /= jointVerts;
			}

			aboveVerts = 0;
			belowVerts = 0;
			vertCount = 0;
			memset(vertBuf, 0, jointVerts);

			joint = &jointMesh->nNorms;
			long worst = 0;
			float worstTurn = 0.0F;

			// This does not pull the paired skin vertex onto the joint
			// vertex.
			//
			// The idea is right in principle - TR5's skin stops short of a
			// joint and the joint mesh covers the gap, while a TR1-3 limb
			// runs through and overlaps, so the seam is asked to bridge a
			// gap that is not there. Moving the rim to close that gap
			// ought to fix it at the root.
			//
			// It does not work, because the two rings are genuinely far
			// apart: the moves come out at up to 50 units on limbs about
			// 100 units thick, and the paired vertex is not always a rim
			// vertex - only the nearest one. Moving an interior vertex
			// onto the ring tears the surface and visibly deforms the
			// body.
			//
			// What it would take instead is the rim reshaped offline, in
			// the package: a proper ring of vertices where TR5 expects
			// one, with the faces rebuilt around it. That is modelling,
			// not a load-time nudge.

			// The middle of the seam, which every direction is measured from.
			const long cx = JointMeshXYZ.x + midX;
			const long cy = JointMeshXYZ.y + midY;
			const long cz = JointMeshXYZ.z + midZ;

			// Distance decides; direction only breaks a tie.
			//
			// Letting direction decide outright pulls pairings out to the
			// full 96 unit reach - a seam that was 21 units off can become
			// 96 - and stretches the whole body, because a vertex on the
			// far side of the seam plane can still share a direction from
			// the middle: direction alone says far too little.
			//
			// Direction is useful only where distance cannot settle it:
			// two candidates almost equally near, where picking the wrong
			// one crosses the joint's triangles. TIE_SLACK is how much
			// "almost" is allowed to mean, kept small on purpose so that
			// inside it no choice can be much wronger than the other.
			const long TIE_SLACK = 8;

			auto pick = [&](short* mesh, long nv, long ox, long oy, long oz,
							long jx, long jy, long jz, long& outDist, int& outWho, float& outTurn)
			{
				outDist = 0x7FFFFFFF;
				outWho = -1;
				outTurn = 1.0F;

				// First pass: how near does it get at all?
				long nearest = 0x7FFFFFFF;
				short* p = mesh;
				for (int ii = 0; ii < nv; ii++, p += 3)
				{
					const long d = abs(ox + p[0] - jx) + abs(oy + p[1] - jy) + abs(oz + p[2] - jz);
					if (d < nearest) nearest = d;
				}
				if (nearest > SEAM_REACH)
					return;

				// Second pass: of those that get that near, the best aligned.
				const float ax = float(jx - cx), ay = float(jy - cy), az = float(jz - cz);
				const float al = sqrtf(ax * ax + ay * ay + az * az);
				float bestCos = -2.0F;

				for (int ii = 0; ii < nv; ii++, mesh += 3)
				{
					const long sx = ox + mesh[0], sy = oy + mesh[1], sz = oz + mesh[2];
					const long d = abs(sx - jx) + abs(sy - jy) + abs(sz - jz);

					if (d > nearest + TIE_SLACK)
						continue;

					const float bx = float(sx - cx), by = float(sy - cy), bz = float(sz - cz);
					const float bl = sqrtf(bx * bx + by * by + bz * bz);
					// A vertex sitting ON the middle has no direction; it can
					// still be taken, just never in preference to one that has.
					const float c = (al > 0.5F && bl > 0.5F)
								  ? (ax * bx + ay * by + az * bz) / (al * bl) : -1.0F;

					if (c > bestCos)
					{
						bestCos = c;
						outDist = d;
						outWho = ii;
						outTurn = c;
					}
				}
			};

			for (int j = 0; j < jointVerts; j++)
			{
				jointX = JointMeshXYZ.x + joint[0];
				jointY = JointMeshXYZ.y + joint[1];
				jointZ = JointMeshXYZ.z + joint[2];
				joint += 3;

				long bestAbove = 0x7FFFFFFF, bestBelow = 0x7FFFFFFF;
				int whoAbove = -1, whoBelow = -1;
				float turnAbove = 0.0F, turnBelow = 0.0F;

				pick(&aboveMesh->nNorms, aboveMesh->nVerts & 0xFF,
					 AboveMeshXYZ.x, AboveMeshXYZ.y, AboveMeshXYZ.z,
					 jointX, jointY, jointZ, bestAbove, whoAbove, turnAbove);
				pick(&belowMesh->nNorms, belowMesh->nVerts & 0xFF,
					 BelowMeshXYZ.x, BelowMeshXYZ.y, BelowMeshXYZ.z,
					 jointX, jointY, jointZ, bestBelow, whoBelow, turnBelow);

				// The ring decides, and only where the two bones sit on top
				// of each other and leave no axis does the distance have to.
				const long side = (jointX - JointMeshXYZ.x - midX) * axisX
								+ (jointY - JointMeshXYZ.y - midY) * axisY
								+ (jointZ - JointMeshXYZ.z - midZ) * axisZ;
				const bool useAbove = haveAxis ? (side >= 0) : (bestAbove <= bestBelow);
				const long best = useAbove ? bestAbove : bestBelow;
				const int who = useAbove ? whoAbove : whoBelow;

				if (who < 0 || best > SEAM_REACH)
					continue;

				if (useAbove)
				{
					if (aboveVerts >= 11)
						continue;

					SkinVertNums[SkinJoints[i][2]][aboveVerts] = who;
					ScratchVertNums[SkinJoints[i][2]][aboveVerts] = j;
					aboveVerts++;
				}
				else
				{
					if (belowVerts >= 11)
						continue;

					SkinVertNums[SkinJoints[i][3]][belowVerts] = who;
					ScratchVertNums[SkinJoints[i][3]][belowVerts] = j;
					belowVerts++;
				}

				vertBuf[j] = 1;
				vertCount++;

				if (best > worst)
					worst = best;

				// 1 means the pair points the same way from the middle of the
				// seam, 0 means a right angle away. The worst of these says
				// whether the ring is sewn straight or twisted.
				const float turn = useAbove ? turnAbove : turnBelow;
				if (1.0F - turn > worstTurn)
					worstTurn = 1.0F - turn;
			}

			SkinVertNums[SkinJoints[i][2]][aboveVerts] = -1;
			ScratchVertNums[SkinJoints[i][2]][aboveVerts] = -1;
			SkinVertNums[SkinJoints[i][3]][belowVerts] = -1;
			ScratchVertNums[SkinJoints[i][3]][belowVerts] = -1;

			SkinJointUsable[i] = (aboveVerts > 0 && belowVerts > 0
								  && vertCount * 2 >= jointVerts) ? 1 : 0;

			Log("SKINLOG joint %d: nearest neighbour pass sewed %d of %d (%d above, %d below, split by %s), "
				"furthest %d units, worst turn %d/1000 - %s",
				i, vertCount, jointVerts, aboveVerts, belowVerts,
				haveAxis ? "seam axis" : "distance", worst, (int)(worstTurn * 1000.0F),
				SkinJointUsable[i] ? "joint used" : "still left out");
		}

		calcPointsCounter = 0;

		// This list collects the joint vertices that found no partner
		// above - the ones that have to be computed instead of copied.
		// PointsToCalc is [26][12] and the row needs its 0xFF terminator,
		// so eleven fit; nothing used to enforce that.
		//
		// With Lara's own skin that is ample, because nearly every joint
		// vertex finds its match and only a handful land here. With a
		// foreign skin it is the opposite: almost nothing matches, so
		// almost the whole joint mesh ends up in this row and runs
		// straight through the array - the cause of a hard crash inside
		// LoadObjects with a foreign body in LARA_SKIN. The caps on the
		// two match lists above do not help here, since with a foreign
		// skin those stay small while this one explodes for the same
		// reason.
		//
		// jointMeshNum indexes the row and is used as jointMeshNum - 1, so
		// it is range-checked as well; it comes out of the joint
		// numbering above.
		if (jointMeshNum >= 1 && jointMeshNum <= 26)
		{
			if (vertCount == jointVerts)
				jointMesh->nVerts <<= 8;
			else
			{
				for (int j = 0; j < jointVerts; j++)
				{
					if (!vertBuf[j])
					{
						if (calcPointsCounter >= 11)
						{
							Log("SKINLOG joint %d: %d of %d joint vertices unmatched, capped at 11 - "
								"is this skin the one the joints were built for?",
								i, jointVerts - vertCount, jointVerts);
							break;
						}

						PointsToCalc[jointMeshNum - 1][calcPointsCounter] = j;
						calcPointsCounter++;
					}
				}
			}

			PointsToCalc[jointMeshNum - 1][calcPointsCounter] = 0xFF;
		}
		else
			Log("SKINLOG joint %d: joint mesh number %d out of range 1..26", i, jointMeshNum);
	}

	for (int i = 0; ; i++)
	{
		if (gfLevelFlags & GF_YOUNGLARA)
		{
			SkinVertNums[28][i] = HairSkinVertNums[1][i];
			SkinVertNums[34][i] = HairSkinVertNums[2][i];
		}
		// The braid root's weld table depends on the outfit, not only the
		// level: HairSkinVertNums[5] names the Russia head's vertex
		// numbers (cap), [0] the standard head's. Keying on the level name
		// alone assumes the outfit never changes within a level. With the
		// randomizer marking the Joby outfit as LaraType 2 (LARA_BUNHEAD),
		// [5] must apply outside the Russia levels too; conversely, in the
		// Russia levels [5] applies only with LARA_NORMAL (an unmodified
		// vanilla level) - if Lara wears a foreign outfit there
		// (CATSUIT/PONYTAIL), the standard head comes with it. Without
		// this, the braid root welds to the wrong head vertices,
		// producing a "braid skewer" through the head with the Joby
		// outfit at rich1.
		else if ((gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD)
		{
			SkinVertNums[28][i] = HairSkinVertNums[5][i];
			SkinVertNums[34][i] = HairSkinVertNums[5][i];
		}
		else
		{
			SkinVertNums[28][i] = HairSkinVertNums[0][i];
			SkinVertNums[34][i] = HairSkinVertNums[0][i];
		}

		ScratchVertNums[28][i] = HairScratchVertNums[0][i];
		ScratchVertNums[34][i] = HairScratchVertNums[1][i];
		SkinVertNums[29][i] = HairSkinVertNums[3][i];
		ScratchVertNums[29][i] = HairScratchVertNums[2][i];
		SkinVertNums[35][i] = HairSkinVertNums[3][i];
		ScratchVertNums[35][i] = HairScratchVertNums[2][i];

		if (HairSkinVertNums[0][i] == -1)
			break;
	}

	for (int i = 0; i < 4; i += 2)
	{
		for (int j = 0; ; j++)
		{
			SkinVertNums[i + 30][j] = HairSkinVertNums[4][j];
			SkinVertNums[i + 31][j] = HairSkinVertNums[3][j];
			ScratchVertNums[i + 30][j] = HairScratchVertNums[3][j];
			ScratchVertNums[i + 31][j] = HairScratchVertNums[2][j];
			ScratchVertNums[i + 36][j] = HairScratchVertNums[3][j];
			ScratchVertNums[i + 37][j] = HairScratchVertNums[2][j];
			SkinVertNums[i + 36][j] = HairSkinVertNums[4][j];
			SkinVertNums[i + 37][j] = HairSkinVertNums[3][j];

			if (HairSkinVertNums[2][j] == -1)
				break;
		}
	}

	obj = &objects[HAIR];
	meshpp = &meshes[obj->mesh_index];

	for (int i = 0; i < 3; i++, meshpp += 2)
	{
		hairMesh = (MESH_DATA*)*meshpp;
		hairMesh->nVerts <<= 8;
	}

	OptomiseSkinningData();
}

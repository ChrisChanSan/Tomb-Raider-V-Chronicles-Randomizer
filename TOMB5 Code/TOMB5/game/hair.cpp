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
#include "hair.h"
#include "../specific/3dmath.h"
#include "../specific/output.h"
#include "gameflow.h"
#include "objects.h"
#include "draw.h"
#include "lara_states.h"
#include "control.h"
#include "../specific/function_stubs.h"
#include "delstuff.h"
#include "deltapak.h"
#include "lara.h"
#include "effect2.h"
#include "../specific/hd/hd_lara.h"		// #123 fix round D (D3): the braid root and the pack
#include "../specific/hd/hd_laradraw.h"	// #123 fix round D (D3): HDLaraDrawActive

static char HairRotScratchVertNums[5][12] =
{
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 5, 6, 7, 4, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 6, 7, 4, 5, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 7, 4, 5, 6, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 }
};

HAIR_STRUCT hairs[2][7];

/* WHAT THE LAST FRAME LOOKED LIKE, for HAIRLOG's four difference fields
 * (#123, fix round G, G1). A log line that says where the braid IS answers
 * half the question; the play test's finding is about how far it MOVED, and
 * counting that by hand out of a thousand lines is what fix round G had to do
 * once. `seen` is cleared with the simulation, so the frame after a level
 * load prints no difference rather than a wrong one. */
static long hair_log_root[2][3];
static long hair_log_tip[2][3];
static short hair_log_yaw[2][6];
static short hair_log_roll[2][6];
static bool hair_log_seen[2];

static long hair_wind = 0;
static long hair_wind_angle = 0;
static long hair_dwind_angle = 0;
static long first_hair[2];

void DrawHair()
{
	HAIR_STRUCT* hair;
	short** meshpp;
	long ii;

	for (int i = 0; i < 2; i++)
	{
		ii = i * 6;
		meshpp = &meshes[objects[HAIR].mesh_index];
		meshpp += 2;

		hair = &hairs[i][1];

		for (int j = 1; j < 6; j += 2, meshpp += 4, hair += 2)
		{
			phd_PushMatrix();
			phd_TranslateAbs(hair->pos.x_pos, hair->pos.y_pos, hair->pos.z_pos);
			phd_RotY(hair->pos.y_rot);
			phd_RotX(hair->pos.x_rot);
			phd_PutPolygons(*meshpp, -1);

			if (j == 5)
				StashSkinVertices(33 + ii);
			else
			{
				StashSkinVertices(28 + ii + j);
				StashSkinVertices(29 + ii + j);
			}

			phd_PopMatrix();
		}

		meshpp = &meshes[objects[HAIR].mesh_index];

		for (int j = 0; j < 6; j += 2, meshpp += 4)
		{
			SkinVerticesToScratch(28 + ii + j);
			GetCorrectStashPoints(i, j, 29 + ii + j);
			SkinVerticesToScratch(29 + ii + j);
			phd_PutPolygons(*meshpp, -1);
		}

		if (!(gfLevelFlags & GF_YOUNGLARA))
			break;
	}
}

/*
 * THE HD BACKPACK, AS TWO COLLISION SPHERES (#123, fix round D, D3b; put in
 * both frame branches in fix round F, F2).
 *
 * The user's finding of round D: the HD braid goes straight through the HD
 * pack. It has to - the five spheres HairControl pushes it out of are CLASSIC
 * Lara meshes, and the pack of an HD outfit stands 92 units behind the torso
 * joint where the classic torso sphere reaches 40. Two spheres and not one
 * because the pack is a flat slab; the numbers and how they were measured are
 * in hd_lara.h.
 *
 * They are placed from the TORSO NODE, which is the frame the caller stands
 * in when it calls this - after the torso's own rotation, so the pack turns
 * with her chest the way its geometry does.
 *
 * ONLY when the HD wardrobe is drawing. The classic body's pack is part of
 * the classic torso mesh and sphere[1] already covers it; adding these to the
 * classic braid would push it off her back for no reason.
 *
 * WHY IT TAKES A FLAG. HairControl has two halves - one for a frame the
 * animation interpolates and one for a frame it does not - and they walk the
 * mesh tree with two different sets of matrix calls. Round D wrote this block
 * into the second half only, so the pack came and went from frame to frame
 * and the braid was pushed out of it on some frames and not on others. That
 * was half of the tremble of user finding F2, and it is why this is a
 * function with a switch rather than a block that can be forgotten in one
 * place. ModelProbe -braidsim measures both.
 *
 * Returns how many spheres it wrote, so the caller can add it to its own
 * count.
 */
static int HdPackSpheres(SPHERE* sphere, bool interpolate)
{
	static const long kPackY[2] =
		{ HD_LARA_PACK_Y_TOP, HD_LARA_PACK_Y_BOTTOM };

	if (!HDLaraDrawActive())
		return 0;

	for (int i = 0; i < 2; i++)
	{
		if (interpolate)
		{
			phd_PushMatrix_I();
			phd_TranslateRel_I(HD_LARA_PACK_X, kPackY[i], HD_LARA_PACK_Z);
			InterpolateMatrix();
		}
		else
		{
			phd_PushMatrix();
			phd_TranslateRel(HD_LARA_PACK_X, kPackY[i], HD_LARA_PACK_Z);
		}

		sphere[i].x = (long)aMXPtr[M03];
		sphere[i].y = (long)aMXPtr[M13];
		sphere[i].z = (long)aMXPtr[M23];
		sphere[i].r = HD_LARA_PACK_R;

		if (interpolate)
			phd_PopMatrix_I();
		else
			phd_PopMatrix();
	}

	return 2;
}

void HairControl(long in_cutscene, long pigtail, short* cutscenething)
{
	OBJECT_INFO* obj;
	HAIR_STRUCT* hair;
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	/* SIX SPHERES ARE THE CLASSIC BODY, the last two the HD backpack (#123,
	 * fix round D, D3b). `spheres` says how many of them the loop below
	 * really tests, so the classic braid meets exactly what it always met. */
	SPHERE sphere[8];
	int spheres = 6;
	int pack_hits = 0;	// #123 fix round F (F2): for the HAIRLOG line
	long root_z = HD_LARA_BRAID_ROOT_Z;	// which root the frame really used
	long* bone;
	short* rot[2];
	short* frm[2];
	short* objptr;
	short* frame;
	long frac, rate, water, height, dist, x, y, z, dx, dy, dz;
	short room_num, spaz;

	obj = &objects[LARA];

	if (!cutscenething)
	{
		if (lara.hit_direction < 0)
		{
			frame = GetBestFrame(lara_item);
			frac = GetFrames(lara_item, frm, &rate); // Interpolate the frames if necessary
		}
		else
		{
			if (!lara.hit_direction)
				spaz = lara.IsDucked ? ANIM_SPAZ_DUCKF : ANIM_SPAZ_FORWARD;
			else if (lara.hit_direction == 1)
				spaz = lara.IsDucked ? ANIM_SPAZ_DUCKR : ANIM_SPAZ_RIGHT;
			else if (lara.hit_direction == 2)
				spaz = lara.IsDucked ? ANIM_SPAZ_DUCKB : ANIM_SPAZ_BACK;
			else
				spaz = lara.IsDucked ? ANIM_SPAZ_DUCKL : ANIM_SPAZ_LEFT;

			frame = anims[spaz].frame_ptr;
			frame += lara.hit_frame * (anims[spaz].interpolation >> 8);
			frm[0] = frame;
			frac = 0;
		}
	}
	else
	{
		frame = cutscenething;
		frm[0] = frame;
		frac = 0;
	}

	phd_PushUnitMatrix();
	phd_SetTrans(lara_item->pos.x_pos, lara_item->pos.y_pos, lara_item->pos.z_pos);
	phd_RotYXZ(lara_item->pos.y_rot, lara_item->pos.x_rot, lara_item->pos.z_rot);

	if (frac)
	{
		InitInterpolate(frac, rate);
		rot[0] = frm[0] + 9;
		rot[1] = frm[1] + 9;
		bone = &bones[obj->bone_index];
		phd_TranslateRel_ID(frm[0][6], frm[0][7], frm[0][8], frm[1][6], frm[1][7], frm[1][8]);
		gar_RotYXZsuperpack_I(&rot[0], &rot[1], 0);

		phd_PushMatrix_I();
		objptr = lara.mesh_ptrs[LM_HIPS];
		phd_TranslateRel_I(*objptr, objptr[1], objptr[2]);
		InterpolateMatrix();
		sphere[0].x = (long)aMXPtr[M03];
		sphere[0].y = (long)aMXPtr[M13];
		sphere[0].z = (long)aMXPtr[M23];
		sphere[0].r = objptr[3];
		phd_PopMatrix_I();

		phd_TranslateRel_I(bone[25], bone[26], bone[27]);

		if (lara.weapon_item != NO_ITEM && lara.gun_type == WEAPON_HK &&
			(items[lara.weapon_item].current_anim_state == 0 || items[lara.weapon_item].current_anim_state == 1 ||
				items[lara.weapon_item].current_anim_state == 2 || items[lara.weapon_item].current_anim_state == 3 ||
				items[lara.weapon_item].current_anim_state == 4))
		{
			rot[0] = &lara.right_arm.frame_base[lara.right_arm.frame_number * (anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
			rot[1] = rot[0];
			gar_RotYXZsuperpack_I(&rot[0], &rot[1], 7);
		}
		else
			gar_RotYXZsuperpack_I(&rot[0], &rot[1], 6);

		phd_RotYXZ_I(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);

		phd_PushMatrix_I();
		objptr = lara.mesh_ptrs[LM_TORSO];

		// The braid's special anchoring belongs to the RUSSIA OUTFIT (cap,
		// bomber jacket), not to the level - Core equated the two because
		// the outfit never changes section in vanilla. For the Joby outfit
		// the randomizer sets LaraType 2 (LARA_BUNHEAD, otherwise unused;
		// the draw paths treat it like LARA_NORMAL), so the braid follows
		// the outfit. In unmodified Russia levels (LARA_NORMAL) nothing
		// changes; if Lara wears a foreign outfit there via the randomizer
		// (CATSUIT/PONYTAIL), the standard anchoring comes with it.
		if ((gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD)
			phd_TranslateRel_I(*objptr - 11, objptr[1] - 16, objptr[2] + 25);
		else
			phd_TranslateRel_I(*objptr - 9, objptr[1], objptr[2] + 25);	//repositioned to avoid floating hair

		InterpolateMatrix();
		sphere[1].x = (long)aMXPtr[M03];
		sphere[1].y = (long)aMXPtr[M13];
		sphere[1].z = (long)aMXPtr[M23];

		// The braid's special anchoring belongs to the RUSSIA OUTFIT (cap,
		// bomber jacket), not to the level - Core equated the two because
		// the outfit never changes section in vanilla. For the Joby outfit
		// the randomizer sets LaraType 2 (LARA_BUNHEAD, otherwise unused;
		// the draw paths treat it like LARA_NORMAL), so the braid follows
		// the outfit. In unmodified Russia levels (LARA_NORMAL) nothing
		// changes; if Lara wears a foreign outfit there via the randomizer
		// (CATSUIT/PONYTAIL), the standard anchoring comes with it.
		if ((gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD)
			sphere[1].r = 94;
		else
			sphere[1].r = objptr[3];

		if (gfLevelFlags & GF_YOUNGLARA)
			sphere[1].r -= (sphere[1].r >> 2) + (sphere[1].r >> 3);

		phd_PopMatrix_I();

		// The HD backpack, in the torso frame (#123, fix round D D3b, in this
		// branch as well since fix round F, F2).
		if (!pigtail)
			spheres += HdPackSpheres(&sphere[6], true);

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);
		gar_RotYXZsuperpack_I(&rot[0], &rot[1], 0);
		objptr = lara.mesh_ptrs[LM_RINARM];
		phd_TranslateRel_I(*objptr, objptr[1], objptr[2]);
		InterpolateMatrix();
		sphere[3].x = (long)aMXPtr[M03];
		sphere[3].y = (long)aMXPtr[M13];
		sphere[3].z = (long)aMXPtr[M23];
		sphere[3].r = 4 * objptr[3] / 3;	//limited rad to avoid floating hair when crawling with the new neck sphere
		phd_PopMatrix_I();

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[41], bone[42], bone[43]);
		gar_RotYXZsuperpack_I(&rot[0], &rot[1], 2);
		objptr = lara.mesh_ptrs[LM_LINARM];
		phd_TranslateRel_I(*objptr, objptr[1], objptr[2]);
		InterpolateMatrix();
		sphere[4].x = (long)aMXPtr[M03];
		sphere[4].y = (long)aMXPtr[M13];
		sphere[4].z = (long)aMXPtr[M23];
		sphere[4].r = 4 * objptr[3] / 3;	//limited rad to avoid floating hair when crawling with the new neck sphere
		phd_PopMatrix_I();
		phd_TranslateRel_I(bone[53], bone[54], bone[55]);
		gar_RotYXZsuperpack_I(&rot[0], &rot[1], 2);
		phd_RotYXZ_I(lara.head_y_rot, lara.head_x_rot, lara.head_z_rot);

		phd_PushMatrix_I();
		objptr = lara.mesh_ptrs[LM_HEAD];
		phd_TranslateRel_I(*objptr - 2, objptr[1], objptr[2]);	//repositioned to avoid floating hair
		InterpolateMatrix();
		sphere[2].x = (long)aMXPtr[M03];
		sphere[2].y = (long)aMXPtr[M13];
		sphere[2].z = (long)aMXPtr[M23];
		sphere[2].r = objptr[3];
		phd_PopMatrix_I();

		if (gfLevelFlags & GF_YOUNGLARA)
		{
			sphere[1].x = (sphere[1].x + sphere[2].x) >> 1;
			sphere[1].y = (sphere[1].y + sphere[2].y) >> 1;
			sphere[1].z = (sphere[1].z + sphere[2].z) >> 1;
		}

		sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;//sphere[5] is an extra sphere added between the head and torso sphere to simulate a neck sphere
		sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
		sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
		sphere[5].r = gfLevelFlags & GF_YOUNGLARA ? 0 : 5 * sphere[2].r / 4;

		/* THE BRAID ROOT, the same three lines as the other branch has -
		 * which is the point (#123, fix round F, F2). */
		if (pigtail)
		{
			root_z = -50;
			phd_TranslateRel_I(44, -48, root_z);
		}
		else if (gfLevelFlags & GF_YOUNGLARA)
		{
			root_z = -50;
			phd_TranslateRel_I(-52, -48, root_z);
		}
		else
		{
			root_z = HD_LARA_BRAID_ROOT_Z;
			phd_TranslateRel_I(HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y,
				root_z);
		}

		InterpolateMatrix();
		pos.x = (long)aMXPtr[M03];
		pos.y = (long)aMXPtr[M13];
		pos.z = (long)aMXPtr[M23];
	}
	else
	{
		rot[0] = frm[0] + 9;
		bone = &bones[obj->bone_index];
		phd_TranslateRel(frm[0][6], frm[0][7], frm[0][8]);
		gar_RotYXZsuperpack(&rot[0], 0);

		phd_PushMatrix();
		objptr = lara.mesh_ptrs[LM_HIPS];
		phd_TranslateRel(*objptr, objptr[1], objptr[2]);
		sphere[0].x = (long)aMXPtr[M03];
		sphere[0].y = (long)aMXPtr[M13];
		sphere[0].z = (long)aMXPtr[M23];
		sphere[0].r = objptr[3];
		phd_PopMatrix();

		phd_TranslateRel(bone[25], bone[26], bone[27]);

		if (lara.weapon_item != NO_ITEM && lara.gun_type == WEAPON_HK &&
			(items[lara.weapon_item].current_anim_state == 0 || items[lara.weapon_item].current_anim_state == 1 ||
				items[lara.weapon_item].current_anim_state == 2 || items[lara.weapon_item].current_anim_state == 3 ||
				items[lara.weapon_item].current_anim_state == 4))
		{
			rot[0] = lara.right_arm.frame_base + lara.right_arm.frame_number * (anims[lara.right_arm.anim_number].interpolation >> 8) + 9;
			gar_RotYXZsuperpack(&rot[0], 7);
		}
		else
			gar_RotYXZsuperpack(&rot[0], 6);

		phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);

		phd_PushMatrix();
		objptr = lara.mesh_ptrs[LM_TORSO];

		// The braid's special anchoring belongs to the RUSSIA OUTFIT (cap,
		// bomber jacket), not to the level - Core equated the two because
		// the outfit never changes section in vanilla. For the Joby outfit
		// the randomizer sets LaraType 2 (LARA_BUNHEAD, otherwise unused;
		// the draw paths treat it like LARA_NORMAL), so the braid follows
		// the outfit. In unmodified Russia levels (LARA_NORMAL) nothing
		// changes; if Lara wears a foreign outfit there via the randomizer
		// (CATSUIT/PONYTAIL), the standard anchoring comes with it.
		if ((gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD)
			phd_TranslateRel(*objptr - 11, objptr[1] - 16, objptr[2] + 25);
		else
			phd_TranslateRel(*objptr - 9, objptr[1], objptr[2] + 25);

		sphere[1].x = (long)aMXPtr[M03];
		sphere[1].y = (long)aMXPtr[M13];
		sphere[1].z = (long)aMXPtr[M23];

		// The braid's special anchoring belongs to the RUSSIA OUTFIT (cap,
		// bomber jacket), not to the level - Core equated the two because
		// the outfit never changes section in vanilla. For the Joby outfit
		// the randomizer sets LaraType 2 (LARA_BUNHEAD, otherwise unused;
		// the draw paths treat it like LARA_NORMAL), so the braid follows
		// the outfit. In unmodified Russia levels (LARA_NORMAL) nothing
		// changes; if Lara wears a foreign outfit there via the randomizer
		// (CATSUIT/PONYTAIL), the standard anchoring comes with it.
		if ((gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD)
			sphere[1].r = 94;
		else
			sphere[1].r = objptr[3];

		if (gfLevelFlags & GF_YOUNGLARA)
			sphere[1].r -= (sphere[1].r >> 2) + (sphere[1].r >> 3);

		phd_PopMatrix();

		// The HD backpack, in the torso frame - see HdPackSpheres above
		// (#123, fix round D D3b, both branches since fix round F, F2).
		if (!pigtail)
			spheres += HdPackSpheres(&sphere[6], false);

		phd_PushMatrix();
		phd_TranslateRel(bone[29], bone[30], bone[31]);
		gar_RotYXZsuperpack(&rot[0], 0);
		objptr = lara.mesh_ptrs[LM_RINARM];
		phd_TranslateRel(*objptr, objptr[1], objptr[2]);
		sphere[3].x = (long)aMXPtr[M03];
		sphere[3].y = (long)aMXPtr[M13];
		sphere[3].z = (long)aMXPtr[M23];
		sphere[3].r = 4 * objptr[3] / 3;
		phd_PopMatrix();

		phd_PushMatrix();
		phd_TranslateRel(bone[41], bone[42], bone[43]);
		gar_RotYXZsuperpack(&rot[0], 2);
		objptr = lara.mesh_ptrs[LM_LINARM];
		phd_TranslateRel(*objptr, objptr[1], objptr[2]);
		sphere[4].x = (long)aMXPtr[M03];
		sphere[4].y = (long)aMXPtr[M13];
		sphere[4].z = (long)aMXPtr[M23];
		sphere[4].r = 4 * objptr[3] / 3;
		phd_PopMatrix();
		phd_TranslateRel(bone[53], bone[54], bone[55]);
		gar_RotYXZsuperpack(&rot[0], 2);
		phd_RotYXZ(lara.head_y_rot, lara.head_x_rot, lara.head_z_rot);

		phd_PushMatrix();
		objptr = lara.mesh_ptrs[LM_HEAD];
		phd_TranslateRel(*objptr - 2, objptr[1], objptr[2]);
		sphere[2].x = (long)aMXPtr[M03];
		sphere[2].y = (long)aMXPtr[M13];
		sphere[2].z = (long)aMXPtr[M23];
		sphere[2].r = objptr[3];
		phd_PopMatrix();

		if (gfLevelFlags & GF_YOUNGLARA)
		{
			sphere[1].x = (sphere[1].x + sphere[2].x) >> 1;
			sphere[1].y = (sphere[1].y + sphere[2].y) >> 1;
			sphere[1].z = (sphere[1].z + sphere[2].z) >> 1;
		}

		sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;
		sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
		sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
		sphere[5].r = gfLevelFlags & GF_YOUNGLARA ? 0 : 5 * sphere[2].r / 4;

		/* THE BRAID ROOT, the same for every adult Lara (#123, fix round F,
		 * F2 - round D's HD variant is withdrawn; the reason is at
		 * HD_LARA_BRAID_ROOT_Z_HD in hd_lara.h). */
		if (pigtail)
		{
			root_z = -50;
			phd_TranslateRel(44, -48, root_z);
		}
		else if (gfLevelFlags & GF_YOUNGLARA)
		{
			root_z = -50;
			phd_TranslateRel(-52, -48, root_z);
		}
		else
		{
			root_z = HD_LARA_BRAID_ROOT_Z;
			phd_TranslateRel(HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y,
				root_z);
		}

		pos.x = (long)aMXPtr[M03];
		pos.y = (long)aMXPtr[M13];
		pos.z = (long)aMXPtr[M23];
	}

	phd_PopMatrix();
	obj = &objects[HAIR];
	bone = &bones[obj->bone_index];
	hair = &hairs[pigtail][0];

	if (first_hair[pigtail])
	{
		first_hair[pigtail] = 0;
		hair->pos.x_pos = pos.x;
		hair->pos.y_pos = pos.y;
		hair->pos.z_pos = pos.z;

		for (int i = 0; i < 6; i++, bone += 4)
		{
			phd_PushUnitMatrix();
			phd_SetTrans(hair->pos.x_pos, hair->pos.y_pos, hair->pos.z_pos);
			phd_RotYXZ(hair->pos.y_rot, hair->pos.x_rot, 0);
			phd_TranslateRel(bone[1], bone[2], bone[3]);
			(hair + 1)->pos.x_pos = (long)aMXPtr[M03];
			(hair + 1)->pos.y_pos = (long)aMXPtr[M13];
			(hair + 1)->pos.z_pos = (long)aMXPtr[M23];
			phd_PopMatrix();
			hair++;
		}

		SmokeWindZ = 0;
		SmokeWindX = 0;
		hair_wind = 0;
		hair_dwind_angle = 2048;
		hair_wind_angle = 2048;
		hair_log_seen[pigtail] = false;	// #123 fix round G (G1)
		return;
	}

	hair->pos.x_pos = pos.x;
	hair->pos.y_pos = pos.y;
	hair->pos.z_pos = pos.z;
	room_num = lara_item->room_number;

	if (in_cutscene)
		water = NO_HEIGHT;
	else
	{
		x = lara_item->pos.x_pos + (frame[0] + frame[1]) / 2;;
		y = lara_item->pos.y_pos + (frame[2] + frame[3]) / 2;
		z = lara_item->pos.z_pos + (frame[4] + frame[5]) / 2;
		water = GetWaterHeight(x, y, z, room_num);
	}

	hair_wind += (GetRandomControl() & 7) - 3;

	if (hair_wind <= -2)
		hair_wind++;
	else if (hair_wind >= 9)
		hair_wind--;

	hair_dwind_angle = (hair_dwind_angle + 2 * (GetRandomControl() & 0x3F) - 64) & 0x1FFE;

	if (hair_dwind_angle < 1024)
		hair_dwind_angle = 2048 - hair_dwind_angle;
	else if (hair_dwind_angle > 3072)
		hair_dwind_angle += 6144 - (2 * hair_dwind_angle);

	hair_wind_angle = (hair_wind_angle + ((hair_dwind_angle - hair_wind_angle) >> 3)) & 0x1FFE;
	SmokeWindX = (hair_wind * rcossin_tbl[hair_wind_angle]) >> 12;
	SmokeWindZ = (hair_wind * rcossin_tbl[hair_wind_angle + 1]) >> 12;
	hair++;

	for (int i = 1; i < 7; i++, bone += 4)
	{
		pos.x = hair->pos.x_pos;
		pos.y = hair->pos.y_pos;
		pos.z = hair->pos.z_pos;

		if (in_cutscene)
			height = 32767;
		else
		{
			floor = GetFloor(hair->pos.x_pos, hair->pos.y_pos, hair->pos.z_pos, &room_num);
			height = GetHeight(floor, hair->pos.x_pos, hair->pos.y_pos, hair->pos.z_pos);
		}

		hair->pos.x_pos += 3 * hair->vel.x / 4;
		hair->pos.y_pos += 3 * hair->vel.y / 4;
		hair->pos.z_pos += 3 * hair->vel.z / 4;

		if (lara.water_status == LW_ABOVE_WATER && room[room_num].flags & ROOM_NOT_INSIDE)
		{
			hair->pos.x_pos += SmokeWindX;
			hair->pos.z_pos += SmokeWindZ;
		}

		if (water == NO_HEIGHT || hair->pos.y_pos < water)
		{
			hair->pos.y_pos += 10;

			if (water != NO_HEIGHT && hair->pos.y_pos > water)
				hair->pos.y_pos = water;
		}

		if (hair->pos.y_pos > height)
		{
			hair->pos.x_pos = pos.x;

			if (hair->pos.y_pos - height <= 256)	//snap to floor if it goes below, no more than 1 click to avoid hairection when going through corners
				hair->pos.y_pos = height;

			hair->pos.z_pos = pos.z;
		}

		for (int j = 0; j < spheres; j++)	//6 instead of 5 for new sphere, 8 with the HD pack (#123 D3b)
		{
			x = hair->pos.x_pos - sphere[j].x;
			y = hair->pos.y_pos - sphere[j].y;
			z = hair->pos.z_pos - sphere[j].z;
			dist = SQUARE(x) + SQUARE(y) + SQUARE(z);

			if (dist < SQUARE(sphere[j].r))
			{
				dist = phd_sqrt(dist);

				if (!dist)
					dist = 1;

				hair->pos.x_pos = sphere[j].x + x * sphere[j].r / dist;
				hair->pos.y_pos = sphere[j].y + y * sphere[j].r / dist;
				hair->pos.z_pos = sphere[j].z + z * sphere[j].r / dist;

				// #123 fix round F (F2): how often the HD pack itself had
				// work to do, for the HAIRLOG line below.
				if (j >= 6)
					pack_hits++;
			}
		}

		dx = (hair->pos.x_pos - (hair - 1)->pos.x_pos);
		dy = (hair->pos.y_pos - (hair - 1)->pos.y_pos);
		dz = (hair->pos.z_pos - (hair - 1)->pos.z_pos);
		dist = phd_sqrt(SQUARE(dz) + SQUARE(dx));

		if (!((cutseq_num == 17 && ((GLOBAL_cutseq_frame >= 205 && GLOBAL_cutseq_frame <= 211) || (GLOBAL_cutseq_frame >= 474 && GLOBAL_cutseq_frame <= 480))) ||
			(cutseq_num == 8 && (GLOBAL_cutseq_frame >= 840 && GLOBAL_cutseq_frame <= 846)) ||
			(cutseq_num == 27 && (GLOBAL_cutseq_frame >= 928 && GLOBAL_cutseq_frame <= 934))))	//To avoid hair jumps in cuts
		{
			(hair - 1)->pos.y_rot = (short)phd_atan(dz, dx);
			(hair - 1)->pos.x_rot = (short)-phd_atan(dist, dy);
		}

		phd_PushUnitMatrix();
		phd_SetTrans((hair - 1)->pos.x_pos, (hair - 1)->pos.y_pos, (hair - 1)->pos.z_pos);
		phd_RotYXZ((hair - 1)->pos.y_rot, (hair - 1)->pos.x_rot, 0);

		if (i == 6)
			phd_TranslateRel(bone[-3], bone[-2], bone[-1]);
		else
			phd_TranslateRel(bone[1], bone[2], bone[3]);

		hair->pos.x_pos = (long)aMXPtr[M03];
		hair->pos.y_pos = (long)aMXPtr[M13];
		hair->pos.z_pos = (long)aMXPtr[M23];

		if ((cutseq_num == 16 && ((GLOBAL_cutseq_frame >= 409 && GLOBAL_cutseq_frame < 411) || GLOBAL_cutseq_frame == 1873 || GLOBAL_cutseq_frame == 3049)) ||
			(cutseq_num == 17 && ((GLOBAL_cutseq_frame >= 205 && GLOBAL_cutseq_frame <= 211) || (GLOBAL_cutseq_frame >= 474 && GLOBAL_cutseq_frame <= 480))) ||
			(cutseq_num == 8 && (GLOBAL_cutseq_frame >= 840 && GLOBAL_cutseq_frame <= 846)) ||
			(cutseq_num == 27 && (GLOBAL_cutseq_frame >= 928 && GLOBAL_cutseq_frame <= 934)))//To avoid hair jumps in cuts
		{
			hair->vel.x = 0;
			hair->vel.y = 0;
			hair->vel.z = 0;
		}
		else
		{
			hair->vel.x = hair->pos.x_pos - pos.x;
			hair->vel.y = hair->pos.y_pos - pos.y;
			hair->vel.z = hair->pos.z_pos - pos.z;
		}

		phd_PopMatrix();
		hair++;
	}

	/* THE DRAWN BRAID'S ROLL (#123, fix round G, G1). Worked out HERE and not
	 * in the draw, because the rule holds a value from frame to frame and
	 * this is the one place per frame where the chain is new - the draw may
	 * run twice (the mirror) or not at all (first person). The second braid
	 * is young Lara's and never meets the HD wardrobe. */
	if (!pigtail)
		HDLaraBraidRollStep();

	/* HAIRLOG (#123, fix round F, F2; the four difference fields are fix
	 * round G, G1): one line per braid per frame under -log, so that the play
	 * test can say what the braid was standing on and how far it moved.
	 *
	 *   rootz     WHICH root offset the frame used - the tremble of finding
	 *             F2 was two of these taking turns. It has to read the same
	 *             number on every line of a run; a line where it changes is
	 *             the bug back again.
	 *   root      where hairs[0][0] landed, in the room.
	 *   spheres   how many collision spheres this frame tested - 6 without
	 *             the HD wardrobe, 8 with it, and it must not alternate.
	 *   packhits  how many times the two pack spheres pushed a link out this
	 *             frame - up to two per link, so up to twelve. A steady small
	 *             number is the pack doing its work; a zero on an HD Lara who
	 *             is not in the water means the braid never reaches the pack
	 *             at all.
	 *   tip       the last link, so that a braid that is somewhere it should
	 *             not be can be found without a screenshot.
	 *   rootstep  how far the ROOT moved since the last frame, |dx|+|dy|+|dz|
	 *             in classic units. This is Lara herself; the braid is
	 *             ALLOWED to move this much and more.
	 *   tipstep   the same for the tip. Fix round G counted the play test's
	 *             own log by hand: on the 924 frames where rootstep was 0,
	 *             tipstep was under 2 on 728 of them and the two long
	 *             standing stretches read exactly 0 for fifty frames on end.
	 *             A tipstep over 40 with a rootstep of 0 is a real fault; one
	 *             with a rootstep of 40 is Lara running.
	 *   dyaw      the worst one-frame change of any link's YAW, in degrees.
	 *             This is the tremble of finding G: the yaw is read off two
	 *             single-digit integers and jumps half a turn while the braid
	 *             stands still. Expect large numbers here - they are the
	 *             disease, not the cure.
	 *   droll     the same after the drawn braid's own roll has taken the
	 *             noise back out (hd_laradraw.cpp, HDLaraBraidRolls). THIS is
	 *             what the picture shows, and on a Lara standing still it has
	 *             to stay in single digits. -1 means the classic braid, which
	 *             has no roll of its own and never had this trouble, because
	 *             its connectors are skinned.
	 *
	 * It stays in for good, like every other seam (feedback: the diagnostic
	 * seams stay). Two lines a frame at most, and only with -log. */
	if (!gLogEnabled)
		return;

	long root_step = -1, tip_step = -1;
	long worst_yaw = -1, worst_roll = -1;
	short roll[6];
	const bool hd_roll = !pigtail && HDLaraDrawActive() &&
		HDLaraBraidRolls(roll) == 6;

	if (!hd_roll)
		memset(roll, 0, sizeof(roll));

	if (hair_log_seen[pigtail])
	{
		root_step = labs(hairs[pigtail][0].pos.x_pos - hair_log_root[pigtail][0]) +
			labs(hairs[pigtail][0].pos.y_pos - hair_log_root[pigtail][1]) +
			labs(hairs[pigtail][0].pos.z_pos - hair_log_root[pigtail][2]);
		tip_step = labs(hairs[pigtail][6].pos.x_pos - hair_log_tip[pigtail][0]) +
			labs(hairs[pigtail][6].pos.y_pos - hair_log_tip[pigtail][1]) +
			labs(hairs[pigtail][6].pos.z_pos - hair_log_tip[pigtail][2]);
		worst_yaw = 0;
		worst_roll = 0;

		for (int i = 0; i < 6; i++)
		{
			const long yaw = labs((short)(hairs[pigtail][i].pos.y_rot -
				hair_log_yaw[pigtail][i]));
			const long turn = labs((short)((short)(hairs[pigtail][i].pos.y_rot +
				roll[i]) - hair_log_roll[pigtail][i]));

			if (yaw > worst_yaw)
				worst_yaw = yaw;

			if (turn > worst_roll)
				worst_roll = turn;
		}

		worst_yaw = worst_yaw * 360 / 65536;
		worst_roll = hd_roll ? worst_roll * 360 / 65536 : -1;
	}

	hair_log_root[pigtail][0] = hairs[pigtail][0].pos.x_pos;
	hair_log_root[pigtail][1] = hairs[pigtail][0].pos.y_pos;
	hair_log_root[pigtail][2] = hairs[pigtail][0].pos.z_pos;
	hair_log_tip[pigtail][0] = hairs[pigtail][6].pos.x_pos;
	hair_log_tip[pigtail][1] = hairs[pigtail][6].pos.y_pos;
	hair_log_tip[pigtail][2] = hairs[pigtail][6].pos.z_pos;

	for (int i = 0; i < 6; i++)
	{
		hair_log_yaw[pigtail][i] = hairs[pigtail][i].pos.y_rot;
		hair_log_roll[pigtail][i] = (short)(hairs[pigtail][i].pos.y_rot + roll[i]);
	}

	hair_log_seen[pigtail] = true;

	Log("HAIRLOG %s rootz %d root %d,%d,%d spheres %d packhits %d hd %d "
		"tip %d,%d,%d rootstep %d tipstep %d dyaw %d droll %d",
		pigtail ? "pigtail" : "braid", (int)root_z,
		(int)hairs[pigtail][0].pos.x_pos, (int)hairs[pigtail][0].pos.y_pos,
		(int)hairs[pigtail][0].pos.z_pos, spheres, pack_hits,
		(int)HDLaraDrawActive(),
		(int)hairs[pigtail][6].pos.x_pos, (int)hairs[pigtail][6].pos.y_pos,
		(int)hairs[pigtail][6].pos.z_pos, (int)root_step, (int)tip_step,
		(int)worst_yaw, (int)worst_roll);
}

void InitialiseHair()
{
	OBJECT_INFO* obj;
	HAIR_STRUCT* hptr;
	long* bone;

	for (int i = 0; i < 2; i++)
	{
		obj = &objects[HAIR];
		bone = &bones[obj->bone_index];
		bone += 4;
		hptr = &hairs[i][0];
		hptr->pos.y_rot = 0;
		hptr->pos.x_rot = -16384;
		first_hair[i] = 1;

		for (int j = 1; j < 7; j++, bone += 4)
		{
			hptr->pos.x_pos = bone[1];
			hptr->pos.y_pos = bone[2];
			hptr->pos.z_pos = bone[3];
			hptr->pos.x_rot = -16384;
			hptr->pos.y_rot = 0;
			hptr->pos.z_rot = 0;
			hptr->vel.x = 0;
			hptr->vel.y = 0;
			hptr->vel.z = 0;
			hptr++;
		}
	}
}

void GetCorrectStashPoints(long pigtail, long hair_node, long skin_node)
{
	HAIR_STRUCT* hair;
	ushort num, rot1, rot2;

	num = 0;
	hair = &hairs[pigtail][hair_node - 1];

	if (hair_node)
		rot1 = hair->pos.y_rot;
	else
		rot1 = ushort(0x8000 - (CamRot.y << 4));

	rot2 = hair[2].pos.y_rot;

	while (abs(rot1 - rot2) > 0x2000 && abs(rot1 - rot2) < 0xE000)
	{
		rot1 += 0x4000;
		num++;
	}

	for (int i = 0; i < 5; i++)
		ScratchVertNums[skin_node][i] = HairRotScratchVertNums[num][i];
}

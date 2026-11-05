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
#include "interpolate_math.h"		// InterpKeyAlpha - the factor LaraPoseFrames builds a far pair at
#include "lara_states.h"
#include "control.h"
#include "../specific/function_stubs.h"
#include "delstuff.h"
#include "deltapak.h"
#include "lara.h"
#include "effect2.h"
#include "../specific/hd/hd_lara.h"		// the braid root and the pack
#include "../specific/hd/hd_laradraw.h"	// HDLaraDrawActive

static char HairRotScratchVertNums[5][12] =
{
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 5, 6, 7, 4, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 6, 7, 4, 5, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 7, 4, 5, 6, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ 4, 5, 6, 7, -1, 0, 0, 0, 0, 0, 0, 0 }
};

HAIR_STRUCT hairs[2][7];

/* What the last frame looked like, for HAIRLOG's four difference fields.
 * A log line that says where the braid is answers only half the question;
 * these let it also say how far the braid moved between frames instead of
 * that having to be counted by hand out of a thousand log lines. `seen` is
 * cleared with the simulation, so the frame after a level load prints no
 * difference rather than a wrong one. */
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
 * The HD backpack, modeled as a box.
 *
 * The braid used to pass straight through the HD backpack: the six
 * classic collision spheres HairControl pushes it out of are classic Lara
 * meshes, and the pack of an HD outfit stands 92 units behind the torso
 * joint where the classic torso sphere only reaches 40. Two stacked
 * spheres were tried first, but the pack is a flat slab and two spheres
 * only touch that slab along its centre line, so it still clipped. The
 * pack is a box instead, six measured numbers instead of three, and the
 * braid is pushed out along the smallest penetration with its rear face
 * preferred. The numbers, the seven outfits they came from and the
 * plait's own radius are in hd_lara.h.
 *
 * It lives in the torso node's frame - the frame the caller stands in
 * when it calls this, after the torso's own rotation, so the pack turns
 * with her chest the way its geometry does. That is why the frame is
 * captured here (a rotation and an origin) rather than a handful of world
 * points: a box has an orientation and a sphere has not.
 *
 * Only used when the HD wardrobe is drawing: the classic body's pack is
 * part of the classic torso mesh and sphere[1] already covers it, so
 * pushing the classic braid out of this box too would lift it off her
 * back for no reason. With the HD path off, HdPackFrame returns false at
 * its first line and HdPackPush is never reached.
 *
 * It takes a flag because HairControl has two halves - one for a frame
 * the animation interpolates and one for a frame it does not - which walk
 * the mesh tree with two different sets of matrix calls; both must agree
 * on the box, or the pack would come and go from frame to frame and the
 * braid would be pushed out of it on some frames and not on others.
 * ModelProbe -braidsim and -braidanim measure both.
 */
struct HD_PACK_FRAME
{
	bool on;
	float rot[9];	// the torso frame's own axes: rot[3 * row + col]
	float org[3];	// the torso joint, in the world
};

static bool HdPackFrame(HD_PACK_FRAME* box, bool interpolate)
{
	box->on = false;

	if (!HDLaraDrawActive())
		return false;

	/* The same push the two spheres used, with a translation of nothing: what
	 * is wanted is the frame itself. The interpolated half has to run
	 * InterpolateMatrix to have a matrix at all - aMXPtr holds the blend of
	 * the two key frames only after that call. */
	if (interpolate)
	{
		phd_PushMatrix_I();
		phd_TranslateRel_I(0, 0, 0);
		InterpolateMatrix();
	}
	else
		phd_PushMatrix();

	box->rot[0] = aMXPtr[M00];  box->rot[1] = aMXPtr[M01];  box->rot[2] = aMXPtr[M02];
	box->rot[3] = aMXPtr[M10];  box->rot[4] = aMXPtr[M11];  box->rot[5] = aMXPtr[M12];
	box->rot[6] = aMXPtr[M20];  box->rot[7] = aMXPtr[M21];  box->rot[8] = aMXPtr[M22];
	box->org[0] = aMXPtr[M03];
	box->org[1] = aMXPtr[M13];
	box->org[2] = aMXPtr[M23];

	if (interpolate)
		phd_PopMatrix_I();
	else
		phd_PopMatrix();

	box->on = true;
	return true;
}

/*
 * Pushes one link out of the box. `link` is 1..6, the moving points of
 * hairs[0]; `x`, `y`, `z` its position in the world, changed in place.
 *
 * The frame is a pure rotation - HairControl builds its matrices on a unit
 * matrix (phd_PushUnitMatrix) and puts nothing but rotations and translations
 * on it, so the 3x3 is orthonormal and the way back into the torso's own
 * coordinates is its transpose. World = R * local + origin, so local =
 * R-transpose * (world - origin).
 *
 * Six faces, six penetrations, and the smallest of them wins - which is what
 * makes the push a push and not a teleport: a link that has just crept a unit
 * into the pack comes back out a unit. The rear face is preferred because it
 * is the face the braid belongs on; every other one leads into her back, under
 * the pack or out at the side. The preference is a divisor and not a rule, so
 * a link that is genuinely nearer another face still leaves by it.
 *
 * The velocity is left alone, exactly as the classic spheres leave it: the
 * caller works out `vel` at the end of the step as how far the point really
 * moved, and a push that zeroed it would be a second, hidden rule.
 */
static bool HdPackPush(const HD_PACK_FRAME* box, long link, long* x, long* y,
	long* z)
{
	static const long kBraidR[6] = HD_LARA_BRAID_RADII;

	if (!box->on || link < 1 || link > 6)
		return false;

	const float dx = (float)*x - box->org[0];
	const float dy = (float)*y - box->org[1];
	const float dz = (float)*z - box->org[2];

	const float lx = dx * box->rot[0] + dy * box->rot[3] + dz * box->rot[6];
	const float ly = dx * box->rot[1] + dy * box->rot[4] + dz * box->rot[7];
	const float lz = dx * box->rot[2] + dy * box->rot[5] + dz * box->rot[8];

	const float r = (float)kBraidR[link - 1];
	const float loX = -(float)HD_LARA_PACK_BOX_X - r;
	const float hiX = (float)HD_LARA_PACK_BOX_X + r;
	const float loY = (float)HD_LARA_PACK_BOX_Y_TOP - r;
	const float hiY = (float)HD_LARA_PACK_BOX_Y_BOTTOM + r;
	const float loZ = (float)HD_LARA_PACK_BOX_Z_BACK - r;
	const float hiZ = (float)HD_LARA_PACK_BOX_Z_FRONT + r;

	if (lx <= loX || lx >= hiX || ly <= loY || ly >= hiY ||
		lz <= loZ || lz >= hiZ)
		return false;

	/* The six ways out, and how far each of them is. */
	const float out[6] =
	{
		lx - loX, hiX - lx,
		ly - loY, hiY - ly,
		lz - loZ, hiZ - lz
	};

	int face = 0;
	float best = out[0];

	for (int i = 1; i < 6; i++)
	{
		/* Face 4 is the rear one - out along -z, which is behind her. */
		const float weigh = i == 4 ? out[i] / (float)HD_LARA_PACK_BOX_BIAS
			: out[i];
		const float against = face == 4 ? best / (float)HD_LARA_PACK_BOX_BIAS
			: best;

		if (weigh < against)
		{
			best = out[i];
			face = i;
		}
	}

	float move[3] = { 0.0f, 0.0f, 0.0f };

	move[face >> 1] = (face & 1) ? best : -best;

	*x += (long)(move[0] * box->rot[0] + move[1] * box->rot[1] +
		move[2] * box->rot[2]);
	*y += (long)(move[0] * box->rot[3] + move[1] * box->rot[4] +
		move[2] * box->rot[5]);
	*z += (long)(move[0] * box->rot[6] + move[1] * box->rot[7] +
		move[2] * box->rot[8]);

	return true;
}

void HairControl(long in_cutscene, long pigtail, short* cutscenething)
{
	OBJECT_INFO* obj;
	HAIR_STRUCT* hair;
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	/* Six spheres are the classic body and all the braid ever meets; the HD
	 * backpack is the box beside them. `spheres` stays a count so that the
	 * log can say the classic braid met exactly what it always met. */
	SPHERE sphere[6];
	int spheres = 6;
	HD_PACK_FRAME pack;
	int pack_hits = 0;	// for the HAIRLOG line
	long root_z = HD_LARA_BRAID_ROOT_Z;	// which root the frame really used
	long* bone;
	short* rot[2];
	short* frm[2];
	short* objptr;
	short* frame;
	long frac, rate, water, height, dist, x, y, z, dx, dy, dz;
	short room_num, spaz;

	obj = &objects[LARA];
	pack.on = false;

	if (!cutscenething)
	{
		if (lara.hit_direction < 0)
		{
			frame = GetBestFrame(lara_item);
			frac = GetFrames(lara_item, frm, &rate); // Interpolate the frames if necessary

			/* The braid hangs off the frames her body is drawn from
			 * (LaraPoseFrames, draw.cpp): the turned copies on the frame an
			 * early turn180_effect fires on, the first key or a built key
			 * frame on a far key pair. The blend below then blends those,
			 * and the braid's root stays on her head. */
			LaraPoseFrames(frm, frac, frac ? InterpKeyAlpha(frac, rate) : 0.0F, 1);
		}
		else
		{
			if (!lara.hit_direction)
				spaz = (short)(lara.IsDucked ? ANIM_SPAZ_DUCKF : ANIM_SPAZ_FORWARD);
			else if (lara.hit_direction == 1)
				spaz = (short)(lara.IsDucked ? ANIM_SPAZ_DUCKR : ANIM_SPAZ_RIGHT);
			else if (lara.hit_direction == 2)
				spaz = (short)(lara.IsDucked ? ANIM_SPAZ_DUCKB : ANIM_SPAZ_BACK);
			else
				spaz = (short)(lara.IsDucked ? ANIM_SPAZ_DUCKL : ANIM_SPAZ_LEFT);

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

		// The braid's special anchoring belongs to the Russia outfit (cap,
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

		// The braid's special anchoring belongs to the Russia outfit (cap,
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

		// The HD backpack, in the torso frame - see HdPackFrame above.
		if (!pigtail)
			HdPackFrame(&pack, true);

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

		/* The braid root - the same three lines the other branch below has,
		 * which is the point: both frame paths must agree on it. */
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
			/* The root moves into the hair knot for an HD Lara - see
			 * HD_LARA_BRAID_ROOT_Z_HD in hd_lara.h for the measurement and
			 * why the box lets it sit there. `pack.on` is read rather than
			 * calling HDLaraDrawActive() a second time: it is one answer
			 * read once per frame, so the two halves of this function
			 * cannot disagree about it. */
			root_z = pack.on ? HD_LARA_BRAID_ROOT_Z_HD : HD_LARA_BRAID_ROOT_Z;
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

		// The braid's special anchoring belongs to the Russia outfit (cap,
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

		// The braid's special anchoring belongs to the Russia outfit (cap,
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

		// The HD backpack, in the torso frame - see HdPackFrame above.
		if (!pigtail)
			HdPackFrame(&pack, false);

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

		/* The braid root: the classic position for a classic Lara, the
		 * nearer one for an HD Lara - the same three lines the other half
		 * above has, which is the point: both frame paths must agree. */
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
			root_z = pack.on ? HD_LARA_BRAID_ROOT_Z_HD : HD_LARA_BRAID_ROOT_Z;
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
		hair_log_seen[pigtail] = false;
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

		for (int j = 0; j < spheres; j++)	//6 instead of 5 for new sphere
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
			}
		}

		/* And out of the HD backpack. After the classic spheres and not
		 * among them, because it is a box and they are spheres - and last,
		 * so that a link the torso sphere has just pushed into the pack
		 * still leaves it. `pack.on` is false for the classic braid and
		 * for young Lara's pigtails, and this call is the test that reads
		 * it. */
		if (HdPackPush(&pack, i, &hair->pos.x_pos, &hair->pos.y_pos,
			&hair->pos.z_pos))
			pack_hits++;

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

	/* The drawn braid's roll is worked out here and not in the draw,
	 * because the rule holds a value from frame to frame and this is the
	 * one place per frame where the chain is new - the draw may run twice
	 * (the mirror) or not at all (first person). The second braid is young
	 * Lara's and never meets the HD wardrobe. */
	if (!pigtail)
		HDLaraBraidRollStep();

	/* HAIRLOG: one line per braid per frame under -log, so it can be read
	 * off what the braid was standing on and how far it moved.
	 *
	 *   rootz     which root offset the frame used. It has to read the
	 *             same number on every line of a run; a line where it
	 *             changes means the two frame branches disagree about it.
	 *   root      where hairs[0][0] landed, in the room.
	 *   spheres   how many collision spheres this frame tested. Six,
	 *             always - they are the classic body and nothing else.
	 *   pack      whether the HD backpack box was placed this frame. It
	 *             has to read the same number on every line of a run: 1
	 *             for an HD Lara, 0 for a classic one, and a line where it
	 *             changes means the two frame branches disagree.
	 *   packhits  how many links the box pushed out this frame - at most six,
	 *             one per link. A steady small number is the pack doing its
	 *             work; a zero on an HD Lara who is not in the water means the
	 *             braid never reaches the pack at all.
	 *   tip       the last link, so that a braid that is somewhere it should
	 *             not be can be found without a screenshot.
	 *   rootstep  how far the root moved since the last frame, |dx|+|dy|+|dz|
	 *             in classic units. This is Lara herself; the braid is
	 *             allowed to move this much and more.
	 *   tipstep   the same for the tip. Measured over 924 frames where
	 *             rootstep was 0: tipstep stayed under 2 on 728 of them,
	 *             with two stretches reading exactly 0 for fifty frames
	 *             straight. A tipstep over 40 with a rootstep of 0 is a
	 *             real fault; one with a rootstep of 40 is Lara running.
	 *   dyaw      the worst one-frame change of any link's yaw, in degrees.
	 *             The yaw is read off two single-digit integers and can
	 *             jump half a turn while the braid stands still - expect
	 *             large numbers here, they are the disease, not the cure.
	 *   droll     the same after the drawn braid's own roll has taken that
	 *             noise back out (hd_laradraw.cpp, HDLaraBraidRolls). This
	 *             is what the picture shows, and on a Lara standing still
	 *             it has to stay in single digits. -1 means the classic
	 *             braid, which has no roll of its own and never had this
	 *             trouble, because its connectors are skinned.
	 *
	 * Stays in for good, like every other diagnostic seam. Two lines a
	 * frame at most, and only with -log. */
	if (!LogFam_HAIRLOG || !gLogEnabled)
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

	Log("HAIRLOG %s rootz %d root %d,%d,%d spheres %d pack %d packhits %d hd %d "
		"tip %d,%d,%d rootstep %d tipstep %d dyaw %d droll %d",
		pigtail ? "pigtail" : "braid", (int)root_z,
		(int)hairs[pigtail][0].pos.x_pos, (int)hairs[pigtail][0].pos.y_pos,
		(int)hairs[pigtail][0].pos.z_pos, spheres, (int)pack.on, pack_hits,
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

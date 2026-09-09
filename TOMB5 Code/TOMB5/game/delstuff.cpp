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
#include "delstuff.h"
#include "../specific/3dmath.h"
#include "draw.h"
#include "lara_states.h"
#include "control.h"
#include "objects.h"
#include "lara.h"
#include "interpolate.h"		// InterpNotePoseHD (#148, order 4 / fix round R)
#include "interpolate_math.h"		// InterpBlendFloat - the arm's own blend (fix round U)
#include "../specific/hd/hd_laradraw.h"	// HDLaraDrawActive - which body wears that pose
#include "camera.h"			// FirstPersonActive - the still body (fix round U)
#include "../tomb5/tomb5.h"

short* GLaraShadowframe;
float lara_matrices[180];
float lara_joint_matrices[180];
long LaraNodeAmbient[2];
uchar LaraNodeUnderwater[15];
char SkinVertNums[40][12];

// Did this joint's seam find anything to sew? Set by CreateSkinningData, read
// by the three joint loops in specific/drawlara.cpp. A joint that matched no
// vertex at all cannot be computed, and drawing it anyway can only put a lump
// of geometry somewhere it does not belong.
char SkinJointUsable[14];
char ScratchVertNums[40][12];

void GetLaraJointPos(PHD_VECTOR* pos, long node)
{
	phd_PushMatrix();
	aMXPtr[M00] = lara_joint_matrices[node * indices_count + M00];
	aMXPtr[M01] = lara_joint_matrices[node * indices_count + M01];
	aMXPtr[M02] = lara_joint_matrices[node * indices_count + M02];
	aMXPtr[M03] = lara_joint_matrices[node * indices_count + M03];
	aMXPtr[M10] = lara_joint_matrices[node * indices_count + M10];
	aMXPtr[M11] = lara_joint_matrices[node * indices_count + M11];
	aMXPtr[M12] = lara_joint_matrices[node * indices_count + M12];
	aMXPtr[M13] = lara_joint_matrices[node * indices_count + M13];
	aMXPtr[M20] = lara_joint_matrices[node * indices_count + M20];
	aMXPtr[M21] = lara_joint_matrices[node * indices_count + M21];
	aMXPtr[M22] = lara_joint_matrices[node * indices_count + M22];
	aMXPtr[M23] = lara_joint_matrices[node * indices_count + M23];
	phd_TranslateRel(pos->x, pos->y, pos->z);
	pos->x = (long)aMXPtr[M03];
	pos->y = (long)aMXPtr[M13];
	pos->z = (long)aMXPtr[M23];
	pos->x += lara_item->pos.x_pos;
	pos->y += lara_item->pos.y_pos;
	pos->z += lara_item->pos.z_pos;
	phd_PopMatrix();
}

/* THE FIRST PERSON BODY, HELD STILL (#123, fix round U, U4).
 *
 * THE USER'S WORDS: "mute the animations except the head movement". In first
 * person the camera hangs in her head joint, so every breath of her idle
 * animation is a movement of the WHOLE PICTURE - and a picture that breathes
 * while the player is standing still reading a room is the one thing about
 * the view that cannot be turned off by looking away from it. The option is
 * "First person view / Body", `animated` (as before) or `still`.
 *
 * WHAT IS HELD, AND FOR HOW LONG. The body's pose alone, and only while she
 * is standing, crouching or kneeling STILL on the ground:
 *
 *   AS_STOP    standing, and aiming while standing, which keeps the same
 *              state - so a rifle held on a target stops breathing too.
 *   AS_POSE    the hands-on-hips idle.
 *   AS_DUCK    crouched still.
 *   AS_ALL4S   on all fours, not crawling.
 *
 * BY STATE AND NOT BY ANIMATION NUMBER. The animation Lara wears while
 * standing depends on what she is holding and on what she did last -
 * ANIM_STOP, ANIM_BREATH, a weapon's own idle, the duck idle - and a list of
 * numbers would be wrong in the first level that reaches a state by a
 * different road. `current_anim_state == goal_anim_state` is the second half
 * of the test: a state that is already on its way somewhere else is not an
 * idle, and holding it would freeze the frame a step BEFORE she moves.
 *
 * WHAT IS NOT HELD. The head's own look angles (lara.head_?_rot) are not in
 * the frame at all - they are applied on top of it - so the player turns and
 * tilts the view exactly as before. The ARMS are driven by the weapon slot's
 * animation, not by hers, so drawing, aiming, firing and reloading are
 * untouched - and they keep U3's blend in time as well, which is why the hold
 * is applied WITHOUT leaving the interpolated draw (see CalcLaraMatrices).
 * Every state that is not in the list above is untouched, which is every step,
 * jump, climb, pull, roll and swim.
 *
 * ONE STORED FRAME, AND WHICH ONE. The key frame the idle began on: the pose
 * is a frame that really exists in the file rather than a blend, so nothing
 * about it can drift, and it is the pose she was already wearing on the frame
 * the hold started - so switching INTO the hold cannot jump at all. Leaving it
 * costs at most what the idle animation travels in one cycle, which is the
 * breath: measured over ANDREA1's ANIM_BREATH (103, 44 frames at rate 4) with
 * ModelProbe, and it is the smallest movement Lara has.
 *
 * The hold is dropped the moment the state stops being an idle one, the
 * animation changes underneath it, or the view leaves first person - so a
 * savegame, a cutscene or a death cannot leave her frozen. Dropped and taken
 * again, that is: an idle state she keeps while the ANIMATION under it changes
 * (ANIM_STOP giving way to ANIM_BREATH, both of them AS_STOP) re-arms the latch
 * on the new animation's own frame, which is one step of the body on the frame
 * the engine changed the animation on and then still again. */
static short g_stillAnim = -1;
static short g_stillFrame = -1;

static bool LaraBodyStillFrame(short** frame)
{
	if (!tomb5.first_person_body_still || !tomb5.first_person ||
		!FirstPersonActive() || !lara_item || !anims)
	{
		g_stillAnim = -1;
		return false;
	}

	const short state = lara_item->current_anim_state;
	const bool idle = state == lara_item->goal_anim_state &&
		(state == AS_STOP || state == AS_POSE || state == AS_DUCK ||
			state == AS_ALL4S);

	if (!idle || lara_item->hit_points <= 0 || lara.hit_direction >= 0)
	{
		g_stillAnim = -1;
		return false;
	}

	if (g_stillAnim != lara_item->anim_number)
	{
		g_stillAnim = lara_item->anim_number;
		g_stillFrame = lara_item->frame_number;
	}

	const ANIM_STRUCT* anim = &anims[g_stillAnim];
	const long rate = anim->interpolation & 0xFF;
	const long size = anim->interpolation >> 8;

	if (rate < 1 || size < 1)
	{
		g_stillAnim = -1;
		return false;
	}

	const long at = (long)g_stillFrame - (long)anim->frame_base;

	if (at < 0)
	{
		g_stillAnim = -1;
		return false;
	}

	*frame = anim->frame_ptr + size * (at / rate);

	return true;
}

void CalcLaraMatrices(long flag)
{
	long* bone;
	short* frame;
	short* frmptr[2];
	short* still = 0;
	long frac;
	float blend = 0.0F;
	short spaz;

	bone = &bones[objects[lara_item->object_number].bone_index];

	/* #148, order 4. GetFrames on a frame that carries a logic tick and at
	 * fps_mode 0 - the same pair and the same factor as ever. On the frame
	 * between two ticks, the midpoint of the two poses those ticks draw, at
	 * which point `frac` is non-zero for animations that never interpolated
	 * at all in vanilla, so LARA COMES THROUGH THE INTERPOLATED BRANCH ON
	 * EVERY TWEEN FRAME - which is the one thing this order had to achieve
	 * for her.
	 *
	 * The hit reaction below is untouched: it is not an animation the item
	 * walks but a frame index picked by hand out of a spaz animation, and it
	 * keeps thirty hertz's own picture. */
	frac = GetFramesNow(lara_item, frmptr, &blend);

	/* AND THE BODY THE PLAYER ASKED TO STAND STILL (#123, fix round U, U4,
	 * corrected in U7).
	 *
	 * ONE STORED FRAME IN BOTH SLOTS, and the draw keeps the path it would
	 * have taken. A blend of a pose with itself is that pose - InterpBlendFloat
	 * and InterpBlendLong both return their argument exactly when the two ends
	 * are equal - so the BODY is frozen whichever branch runs.
	 *
	 * WHY NOT `frac = 0`, WHICH IS WHAT U4 WROTE. Zero forces the draw down
	 * Rich_CalcLaraMatrices_Normal, and that function reads lara.right_arm and
	 * lara.left_arm RAW in all six of its weapon branches - there is no
	 * InterpArmPrev and no InterpArmStack anywhere in it. At sixty hertz that
	 * switched U3's arm blend back off in exactly the case U3 was built for:
	 * standing still in first person with a weapon in hand, which keeps AS_STOP
	 * right through drawing, aiming, firing and reloading, so the hold stays
	 * armed the whole time. The interpolated branch has to stay REACHABLE for
	 * the arms; the only thing done to `frac` is to make it non-zero on a frame
	 * that had no pair of its own (a frame carrying a tick, or fps_mode 0),
	 * where the two identical frames make it a blend of the held pose with
	 * itself. Nothing is blended in TIME there either: InterpArmPrev answers
	 * false on a tick frame and at fps_mode 0, so the arms are read raw, out
	 * of the branch vanilla itself takes whenever `frac` is not zero.
	 *
	 * THE SHADOW keeps the held frame's own bounds below, which is what the
	 * non-interpolated draw did with `GLaraShadowframe = frame`.
	 *
	 * INTERPLOG still counts these frames in lara_pose and pose_hd, because
	 * the path really is the interpolated one; stretch= reads 1.00 on a held
	 * pose, a stored frame being rigid by definition.
	 *
	 * Off by default and never outside first person, so nothing here changes
	 * any other picture. */
	{
		short* held = 0;

		if (LaraBodyStillFrame(&held))
		{
			frmptr[0] = held;
			frmptr[1] = held;
			blend = 0.0F;
			still = held;

			if (!frac)
				frac = 1;
		}
	}

	/* AND LARA'S OWN HD BODY IS COUNTED (#148, order 4; added in fix round R).
	 *
	 * INTERPLOG's pose_hd used to count HD ITEMS alone (game\draw.cpp,
	 * DrawAnimatingItem), so a play test in first person - where the only HD
	 * body that matters is hers - read pose_hd=0.0 and could not tell "the HD
	 * Lara is not getting the blended pose" from "no HD item happened to move
	 * this second". It is the same fact for her as for an item: the pose was
	 * blended in TIME (frac == 2) and the body that wears it is the HD
	 * composite.
	 *
	 * flag 0 alone. CalcLaraMatrices runs two or three times a frame - the
	 * VIEW set, the joint set, a mirror - and flag 0 is the one whose output
	 * (lara_matrices) HDLaraDrawFrame reads. Counting the others would
	 * multiply the number by how many cameras happened to look at her. */
	// ARMLOG (fix round Y): which path the view set takes this frame.
	if (flag == 0)
		InterpArmLogPath(frac, blend, still != 0, HDLaraDrawActive());

	if (frac == 2 && flag == 0 && HDLaraDrawActive())
		InterpNotePoseHD();

	if (lara.hit_direction < 0)
	{
		if (frac)
		{
			/* The held pose's own bounds, when there is one: the interpolated
			 * branch takes them from the live animation, which is the one
			 * thing about the still body that would still breathe (U4,
			 * corrected in U7). Bounds are the first six shorts of a frame. */
			GLaraShadowframe = still ? still : GetBoundsAccurate(lara_item);
			Rich_CalcLaraMatrices_Interpolated(frmptr[0], frmptr[1], blend, bone, flag);

			/* WHAT THE BLEND LEFT OF HER BONES (#123, fix round U, U2). Only
			 * the VIEW set, and only on a frame blended in TIME: the joint
			 * set and the mirror set carry the same pose, and counting them
			 * would multiply the reading by how many cameras looked at her.
			 * -log only, and the whole of it is fifteen subtractions. */
			if (frac == 2 && flag == 0)
				InterpNoteLaraStretch(lara_matrices, bone,
					objects[lara_item->object_number].nmeshes,
					lara_item->anim_number, lara_item->frame_number);

			return;
		}
	}

	if (lara.hit_direction < 0)
		frame = *frmptr;
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

		frame = &anims[spaz].frame_ptr[lara.hit_frame * (anims[spaz].interpolation >> 8)];
	}

	Rich_CalcLaraMatrices_Normal(frame, bone, flag);
}

void Rich_CalcLaraMatrices_Normal(short* frame, long* bone, long flag)
{
	PHD_VECTOR vec;
	float* mx;
	short* rot;
	short* rot2;
	short gun;

	if (flag == 1)
		mx = lara_joint_matrices;
	else
		mx = lara_matrices;

	phd_PushMatrix();

	if (!flag || flag == 2)
		phd_TranslateAbs(lara_item->pos.x_pos, lara_item->pos.y_pos, lara_item->pos.z_pos);
	else
		phd_SetTrans(0, 0, 0);

	phd_RotYXZ(lara_item->pos.y_rot, lara_item->pos.x_rot, lara_item->pos.z_rot);

	if (flag == 2)
	{
		vec.z = -16384;
		vec.y = -16384;
		vec.x = -16384;
		ScaleCurrentMatrix(&vec);
	}

	phd_PushMatrix();
	rot = &frame[9];
	phd_TranslateRel(frame[6], frame[7], frame[8]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_PushMatrix();
	phd_TranslateRel(bone[1], bone[2], bone[3]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_TranslateRel(bone[5], bone[6], bone[7]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_TranslateRel(bone[9], bone[10], bone[11]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_PushMatrix();
	phd_TranslateRel(bone[13], bone[14], bone[15]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_TranslateRel(bone[17], bone[18], bone[19]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_TranslateRel(bone[21], bone[22], bone[23]);
	gar_RotYXZsuperpack(&rot, 0);
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_TranslateRel(bone[25], bone[26], bone[27]);

	if (lara.weapon_item != NO_ITEM && lara.gun_type == WEAPON_HK &&
		(items[lara.weapon_item].current_anim_state == 0 || items[lara.weapon_item].current_anim_state == 1 ||
			items[lara.weapon_item].current_anim_state == 2 || items[lara.weapon_item].current_anim_state == 3 ||
			items[lara.weapon_item].current_anim_state == 4))
	{
		rot = &lara.right_arm.frame_base[lara.right_arm.frame_number * (anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
		gar_RotYXZsuperpack(&rot, 7);
	}
	else
		gar_RotYXZsuperpack(&rot, 0);

	phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
	memcpy(mx, aMXPtr, 48);
	mx += 12;

	phd_PushMatrix();
	phd_TranslateRel(bone[53], bone[54], bone[55]);
	rot2 = rot;
	gar_RotYXZsuperpack(&rot2, 6);
	phd_RotYXZ(lara.head_y_rot, lara.head_x_rot, lara.head_z_rot);
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	gun = WEAPON_NONE;

	if (lara.gun_status == LG_READY || lara.gun_status == LG_FLARE || lara.gun_status == LG_DRAW_GUNS || lara.gun_status == LG_UNDRAW_GUNS)
		gun = lara.gun_type;

	switch (gun)
	{
	case WEAPON_NONE:
	case WEAPON_FLARE:
	case WEAPON_TORCH:
		phd_PushMatrix();
		phd_TranslateRel(bone[29], bone[30], bone[31]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_PushMatrix();
		phd_TranslateRel(bone[41], bone[42], bone[43]);

		if (lara.flare_control_left)
		{
			rot = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) * 
				(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];

			gar_RotYXZsuperpack(&rot, 11);
		}
		else
			gar_RotYXZsuperpack(&rot, 0);

		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		break;

	case WEAPON_PISTOLS:
	case WEAPON_UZI:
		phd_PushMatrix();
		phd_TranslateRel(bone[29], bone[30], bone[31]);

		aMXPtr[M00] = aMXPtr[-2 * indices_count + M00];	//going back 2 matrices
		aMXPtr[M01] = aMXPtr[-2 * indices_count + M01];
		aMXPtr[M02] = aMXPtr[-2 * indices_count + M02];
		aMXPtr[M10] = aMXPtr[-2 * indices_count + M10];
		aMXPtr[M11] = aMXPtr[-2 * indices_count + M11];
		aMXPtr[M12] = aMXPtr[-2 * indices_count + M12];
		aMXPtr[M20] = aMXPtr[-2 * indices_count + M20];
		aMXPtr[M21] = aMXPtr[-2 * indices_count + M21];
		aMXPtr[M22] = aMXPtr[-2 * indices_count + M22];

		phd_RotYXZ(lara.right_arm.y_rot, lara.right_arm.x_rot, lara.right_arm.z_rot);
		rot = &lara.right_arm.frame_base[(anims[lara.right_arm.anim_number].interpolation >> 8) * 
			(lara.right_arm.frame_number - anims[lara.right_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 8);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_PushMatrix();
		phd_TranslateRel(bone[41], bone[42], bone[43]);

		aMXPtr[M00] = aMXPtr[-2 * indices_count + M00];
		aMXPtr[M01] = aMXPtr[-2 * indices_count + M01];
		aMXPtr[M02] = aMXPtr[-2 * indices_count + M02];
		aMXPtr[M10] = aMXPtr[-2 * indices_count + M10];
		aMXPtr[M11] = aMXPtr[-2 * indices_count + M11];
		aMXPtr[M12] = aMXPtr[-2 * indices_count + M12];
		aMXPtr[M20] = aMXPtr[-2 * indices_count + M20];
		aMXPtr[M21] = aMXPtr[-2 * indices_count + M21];
		aMXPtr[M22] = aMXPtr[-2 * indices_count + M22];

		phd_RotYXZ(lara.left_arm.y_rot, lara.left_arm.x_rot, lara.left_arm.z_rot);
		rot = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) * 
			(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 11);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		break;

	case WEAPON_REVOLVER:
		phd_PushMatrix();
		phd_TranslateRel(bone[29], bone[30], bone[31]);

		aMXPtr[M00] = aMXPtr[-2 * indices_count + M00];
		aMXPtr[M01] = aMXPtr[-2 * indices_count + M01];
		aMXPtr[M02] = aMXPtr[-2 * indices_count + M02];
		aMXPtr[M10] = aMXPtr[-2 * indices_count + M10];
		aMXPtr[M11] = aMXPtr[-2 * indices_count + M11];
		aMXPtr[M12] = aMXPtr[-2 * indices_count + M12];
		aMXPtr[M20] = aMXPtr[-2 * indices_count + M20];
		aMXPtr[M21] = aMXPtr[-2 * indices_count + M21];
		aMXPtr[M22] = aMXPtr[-2 * indices_count + M22];

		phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
		rot = &lara.right_arm.frame_base[(anims[lara.right_arm.anim_number].interpolation >> 8) * 
			(lara.right_arm.frame_number - anims[lara.right_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 8);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_PushMatrix();
		phd_TranslateRel(bone[41], bone[42], bone[43]);

		aMXPtr[M00] = aMXPtr[-2 * indices_count + M00];
		aMXPtr[M01] = aMXPtr[-2 * indices_count + M01];
		aMXPtr[M02] = aMXPtr[-2 * indices_count + M02];
		aMXPtr[M10] = aMXPtr[-2 * indices_count + M10];
		aMXPtr[M11] = aMXPtr[-2 * indices_count + M11];
		aMXPtr[M12] = aMXPtr[-2 * indices_count + M12];
		aMXPtr[M20] = aMXPtr[-2 * indices_count + M20];
		aMXPtr[M21] = aMXPtr[-2 * indices_count + M21];
		aMXPtr[M22] = aMXPtr[-2 * indices_count + M22];

		phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
		rot = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) * 
			(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 11);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		break;

	case WEAPON_SHOTGUN:
	case WEAPON_HK:
	case WEAPON_CROSSBOW:
		phd_PushMatrix();
		phd_TranslateRel(bone[29], bone[30], bone[31]);
		rot = &lara.right_arm.frame_base[lara.right_arm.frame_number * 
			(anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
		gar_RotYXZsuperpack(&rot, 8);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_PushMatrix();
		phd_TranslateRel(bone[41], bone[42], bone[43]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		break;
	}
	
	phd_PopMatrix();
	phd_PopMatrix();
	phd_PopMatrix();
	GLaraShadowframe = frame;
}

/* #148, order 4: `frac` and `rate` became one float. The two integers only
 * ever reached InitInterpolate, which turned them into a factor by a
 * three-way test; the factor is now what is handed in, because the time
 * interpolation asks for midpoints that no pair of integers names. */
/* WHERE AN ARM'S ROTATION STREAM STARTS (#123, fix round U, U3).
 *
 * TWO WAYS ROUND THE SAME FRAME, and both are Core's. The pistol, uzi,
 * revolver and flare branches index the frame data by the frame number
 * RELATIVE to the animation's own first frame; the shotgun, HK and crossbow
 * branch indexes it by the frame number as it stands. This function is given
 * which, so that a previous frame is read exactly where the current one would
 * have been read from - a stream that starts one frame's worth of shorts away
 * from the right place is a pose out of another animation.
 *
 * Whichever it is, the step is one STORED frame per game frame: the arm chain
 * indexes by frame and not by key, so a weapon animation of rate 4 is read as
 * if it were rate 1. Measured over the six weapon slots of ANDREA1 (AnimMap),
 * where the rates run 1, 2, 3 and 4. */
static short* LaraArmStream(const InterpArmPose* pose, bool absolute)
{
	const long size = anims[pose->anim_number].interpolation >> 8;
	const long at = absolute ? (long)pose->frame_number
		: (long)pose->frame_number - (long)anims[pose->anim_number].frame_base;

	return &pose->frame_base[size * at + 9];
}

/* THE MIDPOINT OF TWO ARM POSES (#123, fix round U, U3). `out` holds this
 * tick's three matrices and `prev` the previous tick's, both built from the
 * same joint; twelve floats each, exactly what memcpy(mx, aMXPtr, 48) wrote.
 * InterpBlendFloat is the engine's own lerp, expression for expression, so
 * this is the same arithmetic InterpolateMatrix does to the body. */
static void LaraArmBlend(float* out, const float* prev, float alpha)
{
	for (int i = 0; i < 36; i++)
		out[i] = InterpBlendFloat(prev[i], out[i], alpha);
}

void Rich_CalcLaraMatrices_Interpolated(short* frame1, short* frame2, float alpha, long* bone, long flag)
{
	/* THE ARMS' OWN TIME BLEND (#123, fix round U, U3). Asked once per arm
	 * per call, before anything is drawn, because the answer decides which
	 * shape the arm branches below take. False for both at fps_mode 0 and on
	 * every frame that carries a logic tick, which is what keeps thirty hertz
	 * byte for byte what it was. */
	InterpArmPose armPose[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
	float armAlpha[2] = { 0.0F, 0.0F };
	bool armOk[2];
	float armPrev[36];

	armOk[INTERP_ARM_RIGHT] = InterpArmPrev(INTERP_ARM_RIGHT,
		&armPose[INTERP_ARM_RIGHT], &armAlpha[INTERP_ARM_RIGHT]);
	armOk[INTERP_ARM_LEFT] = InterpArmPrev(INTERP_ARM_LEFT,
		&armPose[INTERP_ARM_LEFT], &armAlpha[INTERP_ARM_LEFT]);

	PHD_VECTOR vec;
	float* mx;
	float* arms;
	short* rot;
	short* rot2;
	short* rotcopy;
	short* rot2copy;
	short gun;

	if (flag == 1)
		mx = lara_joint_matrices;
	else
		mx = lara_matrices;

	phd_PushMatrix();
	arms = aMXPtr;

	if (!flag || flag == 2)
		phd_TranslateAbs(lara_item->pos.x_pos, lara_item->pos.y_pos, lara_item->pos.z_pos);
	else
		phd_SetTrans(0, 0, 0);

	phd_RotYXZ(lara_item->pos.y_rot, lara_item->pos.x_rot, lara_item->pos.z_rot);

	if (flag == 2)
	{
		vec.z = -0x4000;
		vec.y = -0x4000;
		vec.x = -0x4000;
		ScaleCurrentMatrix(&vec);
	}

	phd_PushMatrix();
	rot = frame1 + 9;
	rot2 = frame2 + 9;
	InitInterpolateAlpha(alpha);
	phd_TranslateRel_ID(frame1[6], frame1[7], frame1[8], frame2[6], frame2[7], frame2[8]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);

	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_PushMatrix_I();
	phd_TranslateRel_I(bone[1], bone[2], bone[3]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_TranslateRel_I(bone[5], bone[6], bone[7]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_TranslateRel_I(bone[9], bone[10], bone[11]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();
	phd_PopMatrix_I();

	phd_PushMatrix_I();
	phd_TranslateRel_I(bone[13], bone[14], bone[15]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_TranslateRel_I(bone[17], bone[18], bone[19]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_TranslateRel_I(bone[21], bone[22], bone[23]);
	gar_RotYXZsuperpack_I(&rot, &rot2, 0);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();
	phd_PopMatrix_I();

	phd_TranslateRel_I(bone[25], bone[26], bone[27]);

	if (lara.weapon_item != NO_ITEM && lara.gun_type == WEAPON_HK &&
		(items[lara.weapon_item].current_anim_state == 0 || items[lara.weapon_item].current_anim_state == 1 ||
			items[lara.weapon_item].current_anim_state == 2 || items[lara.weapon_item].current_anim_state == 3 ||
			items[lara.weapon_item].current_anim_state == 4))
	{
		/* WITH THE HK IN HAND THE TORSO AND THE HEAD COME OUT OF THE WEAPON'S
		 * ANIMATION, not out of hers, and from here down the body's own two
		 * key frames have nothing more to say (#123, fix round U, U3). So the
		 * two stacks are collapsed to their blend at the BODY's factor - which
		 * is the joint everything below hangs from - and walked on with the
		 * ARM's: stack one with the previous tick's weapon frame, stack two
		 * with this tick's. Everything below this line, the head and both
		 * arms included, is then blended in TIME by the machinery that is
		 * already there. */
		rot2 = &lara.right_arm.frame_base[lara.right_arm.frame_number * (anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
		rot = rot2;

		if (armOk[INTERP_ARM_RIGHT])
		{
			rot = LaraArmStream(&armPose[INTERP_ARM_RIGHT], true);
			InterpArmStack(armAlpha[INTERP_ARM_RIGHT]);
		}

		gar_RotYXZsuperpack_I(&rot, &rot2, 7);
	}
	else
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);

	phd_RotYXZ_I(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_PushMatrix_I();
	phd_TranslateRel_I(bone[53], bone[54], bone[55]);
	rotcopy = rot;
	rot2copy = rot2;
	gar_RotYXZsuperpack_I(&rotcopy, &rot2copy, 6);
	phd_RotYXZ_I(lara.head_y_rot, lara.head_x_rot, lara.head_z_rot);
	phd_PushMatrix();
	InterpolateMatrix();
	memcpy(mx, aMXPtr, 48);
	mx += 12;
	phd_PopMatrix();

	phd_PopMatrix_I();
	gun = WEAPON_NONE;

	if (lara.gun_status == LG_READY || lara.gun_status == LG_FLARE || lara.gun_status == LG_DRAW_GUNS || lara.gun_status == LG_UNDRAW_GUNS)
		gun = lara.gun_type;

	/* AND THE ARMS' COUNTER IS ASKED HERE, WHERE `gun` IS KNOWN (#123, fix
	 * round U, U3, corrected in U7).
	 *
	 * InterpArmPrev answers out of lara.right_arm / lara.left_arm alone, and
	 * those fields keep their last values while her hands are empty: larafire.cpp
	 * parks frame_base on objects[PISTOLS_ANIM].frame_base, a constant, and
	 * nothing clears anim_number - so base, animation and frame all stand still,
	 * the rule says yes, and every tween frame of an unarmed Lara used to be
	 * counted as "blended" although the branch below is WEAPON_NONE, where the
	 * arms come out of HER OWN key frames and nothing is blended at all.
	 *
	 * So the counter is asked only where an arm really is driven by a weapon
	 * slot. The flare and the torch are left out with the empty hand on
	 * purpose: their one driven arm is still drawn from a single frame (see
	 * the flare block below), so there is nothing there to count either. */
	if (flag == 0 && gun != WEAPON_NONE && gun != WEAPON_FLARE && gun != WEAPON_TORCH)
		InterpNoteArm(armOk[INTERP_ARM_RIGHT] && armOk[INTERP_ARM_LEFT]);

	// ARMLOG (fix round Y): what this build did with the two arms.
	if (flag == 0)
		InterpArmLogNote(gun, armOk[INTERP_ARM_RIGHT], armOk[INTERP_ARM_LEFT],
			armAlpha[INTERP_ARM_RIGHT], armAlpha[INTERP_ARM_LEFT],
			armPose[INTERP_ARM_RIGHT].frame_number, armPose[INTERP_ARM_LEFT].frame_number);

	switch (gun)
	{
	case WEAPON_NONE:
	case WEAPON_FLARE:
	case WEAPON_TORCH:
		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();
		phd_PopMatrix_I();

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[41], bone[42], bone[43]);

		if (lara.flare_control_left)
		{
			rot2 = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) * 
				(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];
			rot = rot2;
			gar_RotYXZsuperpack_I(&rot, &rot2, 11);
		}
		else
			gar_RotYXZsuperpack_I(&rot, &rot2, 0);

		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();
		break;

	case WEAPON_PISTOLS:
	case WEAPON_UZI:
		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);

		/* THE SAME ARM, ONE TICK AGO (#123, fix round U, U3). This branch
		 * leaves the second stack behind at the shoulder - aInterpolateArmMatrix
		 * takes the rotation whole from the outer matrix and blends only the
		 * three translation elements - so there is no second stack to walk the
		 * previous frame on. It is walked TWICE from the same joint instead,
		 * on a pushed copy, and the two are blended element by element by the
		 * arm's own factor. Three nodes, on the tween frame only. */
		if (armOk[INTERP_ARM_RIGHT])
		{
			phd_PushMatrix();
			aInterpolateArmMatrix(arms);
			phd_RotYXZ(lara.right_arm.y_rot, lara.right_arm.x_rot, lara.right_arm.z_rot);
			rot = LaraArmStream(&armPose[INTERP_ARM_RIGHT], false);
			gar_RotYXZsuperpack(&rot, 8);
			memcpy(armPrev, aMXPtr, 48);

			phd_TranslateRel(bone[33], bone[34], bone[35]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 12, aMXPtr, 48);

			phd_TranslateRel(bone[37], bone[38], bone[39]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 24, aMXPtr, 48);
			phd_PopMatrix();
		}

		aInterpolateArmMatrix(arms);
		phd_RotYXZ(lara.right_arm.y_rot, lara.right_arm.x_rot, lara.right_arm.z_rot);
		rot = &lara.right_arm.frame_base[(anims[lara.right_arm.anim_number].interpolation >> 8) *
			(lara.right_arm.frame_number - anims[lara.right_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 8);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		if (armOk[INTERP_ARM_RIGHT])
			LaraArmBlend(mx - 36, armPrev, armAlpha[INTERP_ARM_RIGHT]);

		phd_PopMatrix_I();

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[41], bone[42], bone[43]);

		if (armOk[INTERP_ARM_LEFT])
		{
			phd_PushMatrix();
			aInterpolateArmMatrix(arms);
			phd_RotYXZ(lara.left_arm.y_rot, lara.left_arm.x_rot, lara.left_arm.z_rot);
			rot = LaraArmStream(&armPose[INTERP_ARM_LEFT], false);
			gar_RotYXZsuperpack(&rot, 11);
			memcpy(armPrev, aMXPtr, 48);

			phd_TranslateRel(bone[45], bone[46], bone[47]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 12, aMXPtr, 48);

			phd_TranslateRel(bone[49], bone[50], bone[51]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 24, aMXPtr, 48);
			phd_PopMatrix();
		}

		aInterpolateArmMatrix(arms);
		phd_RotYXZ(lara.left_arm.y_rot, lara.left_arm.x_rot, lara.left_arm.z_rot);
		rot = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) *
			(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 11);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		if (armOk[INTERP_ARM_LEFT])
			LaraArmBlend(mx - 36, armPrev, armAlpha[INTERP_ARM_LEFT]);

		break;

	case WEAPON_REVOLVER:
		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);

		/* The same two walks as the pistol branch above, with the revolver's
		 * own pre-rotation - the TORSO's angles and not the arm's aim
		 * (#123, fix round U, U3). */
		if (armOk[INTERP_ARM_RIGHT])
		{
			phd_PushMatrix();
			aInterpolateArmMatrix(arms);
			phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
			rot = LaraArmStream(&armPose[INTERP_ARM_RIGHT], false);
			gar_RotYXZsuperpack(&rot, 8);
			memcpy(armPrev, aMXPtr, 48);

			phd_TranslateRel(bone[33], bone[34], bone[35]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 12, aMXPtr, 48);

			phd_TranslateRel(bone[37], bone[38], bone[39]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 24, aMXPtr, 48);
			phd_PopMatrix();
		}

		aInterpolateArmMatrix(arms);
		phd_RotYXZ_I(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
		rot = &lara.right_arm.frame_base[(anims[lara.right_arm.anim_number].interpolation >> 8) *
			(lara.right_arm.frame_number - anims[lara.right_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 8);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		if (armOk[INTERP_ARM_RIGHT])
			LaraArmBlend(mx - 36, armPrev, armAlpha[INTERP_ARM_RIGHT]);

		phd_PopMatrix_I();

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[41], bone[42], bone[43]);

		if (armOk[INTERP_ARM_LEFT])
		{
			phd_PushMatrix();
			aInterpolateArmMatrix(arms);
			phd_RotYXZ(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
			rot = LaraArmStream(&armPose[INTERP_ARM_LEFT], false);
			gar_RotYXZsuperpack(&rot, 11);
			memcpy(armPrev, aMXPtr, 48);

			phd_TranslateRel(bone[45], bone[46], bone[47]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 12, aMXPtr, 48);

			phd_TranslateRel(bone[49], bone[50], bone[51]);
			gar_RotYXZsuperpack(&rot, 0);
			memcpy(armPrev + 24, aMXPtr, 48);
			phd_PopMatrix();
		}

		aInterpolateArmMatrix(arms);
		phd_RotYXZ_I(lara.torso_y_rot, lara.torso_x_rot, lara.torso_z_rot);
		rot = &lara.left_arm.frame_base[(anims[lara.left_arm.anim_number].interpolation >> 8) *
			(lara.left_arm.frame_number - anims[lara.left_arm.anim_number].frame_base) + 9];
		gar_RotYXZsuperpack(&rot, 11);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		phd_TranslateRel(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack(&rot, 0);
		memcpy(mx, aMXPtr, 48);
		mx += 12;

		if (armOk[INTERP_ARM_LEFT])
			LaraArmBlend(mx - 36, armPrev, armAlpha[INTERP_ARM_LEFT]);

		break;

	case WEAPON_SHOTGUN:
	case WEAPON_HK:
	case WEAPON_CROSSBOW:
		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);

		/* BOTH STACKS ARE ALREADY THE WEAPON'S HERE - this branch fed them the
		 * same frame and blended a pose with itself. Stack one takes the
		 * PREVIOUS tick's frame instead and the factor becomes the arm's own,
		 * so the InterpolateMatrix at each node below is the midpoint in TIME
		 * (#123, fix round U, U3). With the HK the torso collapsed the stacks
		 * once already and then walked them apart through the torso and the
		 * head, so this second collapse takes the SHOULDER to its midpoint and
		 * hangs the arm off that - see the note over InterpArmStack
		 * (game\draw.cpp), which says why that order is the one wanted
		 * (corrected in U7). */
		rot2 = &lara.right_arm.frame_base[lara.right_arm.frame_number *
			(anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
		rot = rot2;

		if (armOk[INTERP_ARM_RIGHT])
		{
			rot = LaraArmStream(&armPose[INTERP_ARM_RIGHT], true);
			InterpArmStack(armAlpha[INTERP_ARM_RIGHT]);
		}

		gar_RotYXZsuperpack_I(&rot, &rot2, 8);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[33], bone[34], bone[35]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[37], bone[38], bone[39]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();
		phd_PopMatrix_I();

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[41], bone[42], bone[43]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[45], bone[46], bone[47]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();

		phd_TranslateRel_I(bone[49], bone[50], bone[51]);
		gar_RotYXZsuperpack_I(&rot, &rot2, 0);
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(mx, aMXPtr, 48);
		mx += 12;
		phd_PopMatrix();
		break;
	}

	phd_PopMatrix();
	phd_PopMatrix();
	phd_PopMatrix();
}

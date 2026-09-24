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
#include "interpolate.h"		// InterpNotePoseHD - counts Lara's own HD pose blends
#include "interpolate_math.h"		// InterpBlendFloat - blends the arm matrices in time
#include "../specific/hd/hd_laradraw.h"	// HDLaraDrawActive - which body wears that pose
#include "camera.h"			// FirstPersonActive - whether the still-body option applies
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

/* Optionally holds the first-person body still, keeping the camera (which
 * hangs in Lara's head joint) from picking up her idle animation's breathing
 * while the player stands still reading the view. The option is "First
 * person view / Body": `animated` (as before) or `still`.
 *
 * The body's pose alone is held, and only while she is standing, crouching
 * or kneeling still on the ground:
 *
 *   AS_STOP    standing, and aiming while standing (same state) - so a
 *              rifle held on a target stops breathing too.
 *   AS_POSE    the hands-on-hips idle.
 *   AS_DUCK    crouched still.
 *   AS_ALL4S   on all fours, not crawling.
 *
 * Matched by state and not by animation number, because the animation Lara
 * wears while standing depends on what she is holding and on what she did
 * last (ANIM_STOP, ANIM_BREATH, a weapon's own idle, the duck idle), and a
 * fixed list of animation numbers would miss any level that reaches the
 * state by a different one. `current_anim_state == goal_anim_state` is the
 * other half of the test: a state already transitioning elsewhere is not an
 * idle, and holding it would freeze the frame a step before she moves.
 *
 * Not held: the head's own look angles (lara.head_?_rot) are applied on top
 * of the pose, not inside it, so the player still turns and tilts the view
 * freely. The arms are driven by the weapon slot's animation, not by the
 * body's, so drawing, aiming, firing and reloading are untouched, and they
 * keep their own time blend, which is why the hold is applied without
 * leaving the interpolated draw path (see CalcLaraMatrices). Every other
 * state - every step, jump, climb, pull, roll and swim - is untouched.
 *
 * The held pose is the key frame the idle began on: a frame that exists in
 * the file rather than a blend, so it cannot drift, and it is the pose she
 * was already wearing when the hold started, so entering the hold cannot
 * jump. Leaving it costs at most one cycle of the idle animation - measured
 * over ANDREA1's ANIM_BREATH (animation 103, 44 frames at rate 4), the
 * smallest movement Lara has.
 *
 * The hold is dropped the moment the state stops being an idle one, the
 * animation underneath it changes, or the view leaves first person, so a
 * savegame, a cutscene or a death cannot leave her frozen. An idle state
 * that keeps running while its animation changes underneath it (ANIM_STOP
 * giving way to ANIM_BREATH, both AS_STOP) drops and re-arms the hold on
 * the new animation's own frame - one step of the body on the frame the
 * animation changed, then still again. */
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

	/* GetFrames on a frame that carries a logic tick, or at fps_mode 0,
	 * returns the same pair and factor as before. On the frame between two
	 * ticks it returns the midpoint of the two poses those ticks draw, so
	 * `frac` is non-zero even for animations that never interpolated in
	 * vanilla - Lara takes the interpolated branch on every tween frame,
	 * which is what the time interpolation needs from her pose.
	 *
	 * The hit reaction below is untouched: it is not an animation the item
	 * walks but a frame index picked by hand out of a spaz animation, and
	 * it keeps its original, non-interpolated picture. */
	frac = GetFramesNow(lara_item, frmptr, &blend);

	/* The two places where the base engine's own pose goes wrong, at every
	 * fps_mode (LaraPoseFrames, draw.cpp): the frame an early turn180_effect
	 * fires on is drawn from copies turned by half a turn, so her body does
	 * not face the other way for one tick in the twists; and a key pair whose
	 * hips turn more than 90 degrees is drawn from its first key (the
	 * tightrope turn) or from a key frame built at the tick's weight (the
	 * twists' 209 and 213, rich3's 219) in place of vanilla's collapsing
	 * blend. Both put their frames into both slots where they blend, which
	 * keeps the interpolated branch and the arms' own time blend. Every other
	 * pair is drawn as before. */
	LaraPoseFrames(frmptr, frac, blend, flag);

	/* Applies the still-body hold from LaraBodyStillFrame, if any, without
	 * leaving the interpolated draw path.
	 *
	 * Putting the same stored frame in both slots makes the draw keep the
	 * branch it would already be taking: a blend of a pose with itself is
	 * that pose (InterpBlendFloat and InterpBlendLong both return their
	 * argument exactly when the two ends are equal), so the body is frozen
	 * whichever branch runs.
	 *
	 * `frac` is forced non-zero rather than left at zero, because zero
	 * would send the draw down Rich_CalcLaraMatrices_Normal, which reads
	 * lara.right_arm and lara.left_arm raw in all six of its weapon
	 * branches - it has no InterpArmPrev or InterpArmStack call at all. At
	 * a higher frame rate that would turn off the arms' own time blend in
	 * exactly the case it exists for: standing still in first person with
	 * a weapon in hand, an AS_STOP state that persists through drawing,
	 * aiming, firing and reloading. Forcing `frac` non-zero only on a
	 * frame that had no pair of its own (a tick frame, or fps_mode 0)
	 * keeps the interpolated branch reachable for the arms and blends
	 * nothing extra in time either: InterpArmPrev answers false on such a
	 * frame, so the arms are still read raw, out of the same branch
	 * vanilla takes whenever `frac` is not zero.
	 *
	 * GLaraShadowframe below keeps the held frame's own bounds, matching
	 * what the non-interpolated draw did with `GLaraShadowframe = frame`.
	 * The interpolation diagnostic log still counts these frames as
	 * interpolated, since the path really is; its stretch reading is 1.00
	 * on a held pose, a stored frame being rigid by definition.
	 *
	 * Off by default, and never applied outside first person, so nothing
	 * here changes any other view. */
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

	/* Refuses the root-offset time blend for Lara's own view/joint draws
	 * on the two frames that do not come from a neighbouring tick of the
	 * animation the item is walking, and so have no previous tick to blend
	 * the offset with:
	 *
	 *   the hit reaction is a frame index picked by hand out of a spaz
	 *   animation, and keeps its original, non-interpolated picture;
	 *   the still body above is a frame the player asked to be held, and
	 *   blending its root toward the live animation's last offset would be
	 *   exactly the breathing that option exists to stop.
	 *
	 * The refusal is a one-byte-per-slot latch; the rest of this function
	 * then draws exactly as it did before the root offset could be
	 * time-blended at all. */
	if (still || lara.hit_direction >= 0)
		InterpRootRefused(lara_item, 0);

	/* Counts a pose blended in time (frac == 2) as belonging to Lara's own
	 * HD composite body for the interpolation diagnostic log's pose_hd
	 * count, which otherwise only counted HD items (see DrawAnimatingItem
	 * in draw.cpp) and so could not tell a first-person view - where the
	 * only HD body in the scene is hers - apart from a frame with no HD
	 * item at all.
	 *
	 * Checked with flag == 0 only: CalcLaraMatrices runs two or three
	 * times a frame (the view set, the joint set, a mirror), and flag 0 is
	 * the one whose output (lara_matrices) HDLaraDrawFrame reads; counting
	 * the others would multiply the reading by how many cameras looked at
	 * her. */
	// Diagnostic log of which path the view set takes this frame.
	if (flag == 0)
		InterpArmLogPath(frac, blend, still != 0, HDLaraDrawActive());

	if (frac == 2 && flag == 0 && HDLaraDrawActive())
		InterpNotePoseHD();

	if (lara.hit_direction < 0)
	{
		if (frac)
		{
			/* The held pose's own bounds, when there is one: the
			 * interpolated branch otherwise takes them from the live
			 * animation, which is the one thing about the still body that
			 * would still move. Bounds are the first six shorts of a
			 * frame. */
			GLaraShadowframe = still ? still : GetBoundsAccurate(lara_item);
			Rich_CalcLaraMatrices_Interpolated(frmptr[0], frmptr[1], blend, bone, flag);

			/* Logs how far the blend moved her bones. View set only, and
			 * only on a frame blended in time: the joint set and the
			 * mirror set carry the same pose, and counting them would
			 * multiply the reading by how many cameras looked at her.
			 * Diagnostic log only; the computation itself is fifteen
			 * subtractions. */
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

	/* The root offset on a frame whose pose the neighbour rule refused -
	 * and this is the branch the ladder climb really comes through: the
	 * frame after a climb animation wraps is the animation's first, so
	 * GetFrames finds no remainder, `frac` is zero, and Lara is built here
	 * rather than in the interpolated half. InterpRootNow answers false on
	 * every frame that is not a refused tween frame of hers, including the
	 * cutscene decoder's own call to this function, where no snapshot was
	 * captured, and then the line below is the one that has always stood
	 * here. */
	{
		short* pair[2] = { frame, frame };
		long root[3];

		if (InterpRootNow(lara_item, pair, 0, 0.0F, root))
			phd_TranslateRel(root[0], root[1], root[2]);
		else
			phd_TranslateRel(frame[6], frame[7], frame[8]);
	}

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

/* `frac` and `rate` are combined into a single float factor. The pair of
 * integers only ever reached InitInterpolate, which turned them into a
 * factor by a three-way test; the factor is now passed in directly,
 * because time-based interpolation asks for midpoints no pair of integers
 * can name. */
/* Finds where an arm's rotation stream starts in the frame data.
 *
 * Two addressing schemes are used for the same kind of frame, both from
 * the original engine: the pistol, uzi, revolver and flare branches index
 * the frame data by the frame number relative to the animation's own
 * first frame; the shotgun, HK and crossbow branch indexes it by the raw
 * frame number. This function is told which, so a previous frame is read
 * from exactly where the current one would be read from - a stream offset
 * by one frame's worth of shorts reads a pose from another animation
 * entirely.
 *
 * Either way the step is one stored frame per game frame: the arm chain
 * indexes by frame, not by key frame, so a weapon animation of rate 4 is
 * read as if it were rate 1. Measured over the six weapon slots of
 * ANDREA1, where the rates run 1, 2, 3 and 4. */
static short* LaraArmStream(const InterpArmPose* pose, bool absolute)
{
	const long size = anims[pose->anim_number].interpolation >> 8;
	const long at = absolute ? (long)pose->frame_number
		: (long)pose->frame_number - (long)anims[pose->anim_number].frame_base;

	return &pose->frame_base[size * at + 9];
}

/* Blends two arm poses toward their midpoint in time. `out` holds this
 * tick's three matrices and `prev` the previous tick's, both built from
 * the same joint - twelve floats each, exactly what memcpy(mx, aMXPtr, 48)
 * writes. InterpBlendFloat is the same lerp, expression for expression,
 * that InterpolateMatrix applies to the body. */
static void LaraArmBlend(float* out, const float* prev, float alpha)
{
	for (int i = 0; i < 36; i++)
		out[i] = InterpBlendFloat(prev[i], out[i], alpha);
}

void Rich_CalcLaraMatrices_Interpolated(short* frame1, short* frame2, float alpha, long* bone, long flag)
{
	/* Reads the previous tick's arm pose once per arm, before anything is
	 * drawn, since the answer decides which shape the arm branches below
	 * take. Both answer false at fps_mode 0 and on every frame that
	 * carries a logic tick, which keeps the original 30 Hz output
	 * unchanged byte for byte. */
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

	/* The same rule as in the branch above: on a frame whose pose the
	 * neighbour rule refused, the root offset is blended in time instead.
	 * The one value goes to both stacks - they carry the same translation
	 * and differ only in the rotations hung off it, so InterpolateMatrix
	 * gives it back unchanged and the pose stays this tick's. */
	{
		short* pair[2] = { frame1, frame2 };
		long root[3];

		if (InterpRootNow(lara_item, pair, 1, alpha, root))
			phd_TranslateRel_ID(root[0], root[1], root[2], root[0], root[1], root[2]);
		else
			phd_TranslateRel_ID(frame1[6], frame1[7], frame1[8], frame2[6], frame2[7], frame2[8]);
	}

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
		/* With the HK in hand, the torso and the head come out of the
		 * weapon's animation, not out of hers, so from here down the
		 * body's own two key frames have nothing more to say. The two
		 * stacks are collapsed to their blend at the body's factor - the
		 * joint everything below hangs from - and walked on with the arm's
		 * factor instead: stack one with the previous tick's weapon frame,
		 * stack two with this tick's. Everything below this line, the head
		 * and both arms included, is then blended in time by the
		 * machinery already there. */
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

	/* The blended-arms counter is checked here, once `gun` is known.
	 *
	 * InterpArmPrev answers purely from lara.right_arm / lara.left_arm,
	 * and those fields keep their last values while her hands are empty:
	 * larafire.cpp parks frame_base on objects[PISTOLS_ANIM].frame_base, a
	 * constant, and nothing clears anim_number, so base, animation and
	 * frame all stand still and the answer comes back yes - every tween
	 * frame of an unarmed Lara would be counted as blended, although the
	 * branch below is WEAPON_NONE, where the arms come out of her own key
	 * frames and nothing is blended at all.
	 *
	 * The counter is therefore checked only where an arm really is driven
	 * by a weapon slot. The flare and the torch are left out with the
	 * empty hand on purpose: their one driven arm is still drawn from a
	 * single frame (see the flare block below), so there is nothing there
	 * to count either. */
	if (flag == 0 && gun != WEAPON_NONE && gun != WEAPON_FLARE && gun != WEAPON_TORCH)
		InterpNoteArm(armOk[INTERP_ARM_RIGHT] && armOk[INTERP_ARM_LEFT]);

	// Diagnostic log of what this build did with the two arms.
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

		/* Builds the same arm as it stood one tick ago. This branch leaves
		 * the second stack behind at the shoulder - aInterpolateArmMatrix
		 * takes the rotation whole from the outer matrix and blends only
		 * the three translation elements - so there is no second stack to
		 * walk the previous frame on. It is walked twice from the same
		 * joint instead, on a pushed copy, and the two are blended element
		 * by element by the arm's own factor. Three nodes, on the tween
		 * frame only. */
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

		/* The same two walks as the pistol branch above, with the
		 * revolver's own pre-rotation - the torso's angles, not the
		 * arm's aim. */
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
		/* Both stacks are already the weapon's here - this branch would
		 * otherwise feed them the same frame and blend a pose with itself.
		 * Stack one takes the previous tick's frame instead and the
		 * factor becomes the arm's own, so the InterpolateMatrix call at
		 * each node below finds the midpoint in time.
		 *
		 * The collapse happens on the torso level itself, before either
		 * arm is pushed. Collapsing a pushed copy inside the right arm's
		 * own push instead, and letting the pop after that arm hand the
		 * left arm the torso level as it had been, leaves two stacks
		 * holding the body's two key frames there, blended from then on
		 * at the arm's factor instead of the body's, because
		 * InterpArmStack had set the factor and nothing set it back. For
		 * a rate 1 animation the two factors are equal; for every other
		 * rate the left shoulder would drift off the drawn torso through
		 * the tick and snap back on the next - measured at up to seventy
		 * units of travel and snap-back in a third of all ticks with the
		 * shotgun in hand, while the right hand moved five units a frame
		 * with no jump at all.
		 *
		 * Collapsing here instead gives both chains the same root, the
		 * torso the drawn body has. For the right arm it is the joint it
		 * had before: a translation applied to both stacks alike commutes
		 * with their blend. With the HK in state 0 to 4 the torso
		 * collapses once already and then walks the two stacks apart
		 * through the torso and the head, so this takes the torso to its
		 * midpoint in time and hangs both arms off that one joint, the
		 * order the note over InterpArmStack in draw.cpp describes. */
		rot2 = &lara.right_arm.frame_base[lara.right_arm.frame_number *
			(anims[lara.right_arm.anim_number].interpolation >> 8) + 9];
		rot = rot2;

		if (armOk[INTERP_ARM_RIGHT])
		{
			rot = LaraArmStream(&armPose[INTERP_ARM_RIGHT], true);
			InterpArmStack(armAlpha[INTERP_ARM_RIGHT]);
		}

		phd_PushMatrix_I();
		phd_TranslateRel_I(bone[29], bone[30], bone[31]);
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

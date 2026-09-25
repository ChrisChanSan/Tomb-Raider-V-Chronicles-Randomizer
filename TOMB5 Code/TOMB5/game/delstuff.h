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

void GetLaraJointPos(PHD_VECTOR* pos, long node);
void CalcLaraMatrices(long flag);
void Rich_CalcLaraMatrices_Normal(short* frame, long* bone, long flag);
void Rich_CalcLaraMatrices_Interpolated(short* frame1, short* frame2, float alpha, long* bone, long flag);

/* LM node -> LMX matrix: where a mesh tree node's matrix stands in
 * lara_matrices.
 *
 * lara_matrices holds fifteen matrices in LMX_ order - the order DrawLara
 * walks and the order Rich_CalcLaraMatrices_Normal builds them in: the hips,
 * the left leg, the right leg, the torso, then the head out of bone[53..55],
 * then the two arms. A node number walks the mesh tree, which is LM_ order,
 * where the head is last. The only difference is the head: LM 14 is LMX 8,
 * and LM 8..13 are LMX 9..14. Written out in full so it reads against
 * lara_mesh_sweetness_table (specific\drawlara.cpp), the draw order, which is
 * its inverse. The HD seam and the stretch diagnostic both read through it. */
constexpr int lara_lmx_of_node[15] =
{
	LMX_HIPS,			// LM_HIPS
	LMX_THIGH_L, LMX_CALF_L, LMX_FOOT_L,	// LM_LTHIGH, LSHIN, LFOOT
	LMX_THIGH_R, LMX_CALF_R, LMX_FOOT_R,	// LM_RTHIGH, RSHIN, RFOOT
	LMX_TORSO,			// LM_TORSO
	LMX_UARM_R, LMX_LARM_R, LMX_HAND_R,	// LM_RINARM, ROUTARM, RHAND
	LMX_UARM_L, LMX_LARM_L, LMX_HAND_L,	// LM_LINARM, LOUTARM, LHAND
	LMX_HEAD			// LM_HEAD - the odd one
};

extern short* GLaraShadowframe;
extern float lara_matrices[180];
extern float lara_joint_matrices[180];
extern long LaraNodeAmbient[2];
extern uchar LaraNodeUnderwater[15];
extern char SkinVertNums[40][12];
extern char ScratchVertNums[40][12];
extern char SkinJointUsable[14];

/*
 * Modified 2026 by ChrisChanSan.
 *
 * The composite's rules are TROne's (Copyright 2026 Todesengel, GPLv3) and
 * live in hd_lara.cpp; the braid's six links being rebuilt as
 * TranslateAbs / RotY / RotX per link rather than TR5's three rigid pieces
 * plus three skinned connectors is TROne's draw_hair
 * (host\render\game_renderer.cpp:1498-1546). The seam under all of it - the
 * LMX to LM turn, the per node ambient, the hand pointer comparison, the
 * first person mask and the mirror - is new. See THIRD-PARTY.md.
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
#include "../../tomb5/pch.h"
#include "hd_laradraw.h"
#include "hd_lara.h"
#include "hd_wardrobe.h"		// the outfit scan (#153, round M2)
#include "../registry.h"		// hd_outfit, the player's own choice (M2 fix)
#include "hd_model.h"
#include "hd_object.h"
#include "hd_render.h"
#include "hd_bind.h"
#include "../3dmath.h"
#include "../d3dmatrix.h"
#include "../lighting.h"
#include "../function_stubs.h"
#include "../profile.h"			// PROFLOG phases (#148)
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// meshes, bones
#include "../../game/lara.h"		// lara, lara_item, LaraDrawType
#include "../../game/lara2gun.h"	// LHolster
#include "../../game/delstuff.h"	// lara_matrices, LaraNodeAmbient/Underwater
#include "../../game/hair.h"		// hairs
#include "../../game/camera.h"		// FirstPersonActive
#include "../../game/gameflow.h"	// gfLevelId, gfLevelFlags
#include "../../game/interpolate.h"	// InterpFrameAlpha - what this frame was drawn at (#148)

#include <math.h>

namespace
{
	/* THE ONE TABLE THIS FILE EXISTS FOR. lara_matrices holds fifteen
	 * matrices in LMX_ order - the order DrawLara walks and the order the
	 * matrices are BUILT in (game\delstuff.cpp, Rich_CalcLaraMatrices_Normal:
	 * hips, the left leg, the right leg, the torso, then the HEAD out of
	 * bone[53..55], then the two arms). A .TRM's three bone bytes are mesh
	 * tree nodes, which are LM_ order, where the head is last.
	 *
	 * So the whole difference between the two orders is the head: LM node 14
	 * is LMX matrix 8, and LM 8..13 are LMX 9..14. Written out all the same,
	 * because a reader should not have to work that out, and because the
	 * classic path's own lara_mesh_sweetness_table is this table's inverse
	 * and the two have to be readable against each other. */
	const int kLmxOfNode[HD_LARA_NODES] =
	{
		LMX_HIPS,			// LM_HIPS
		LMX_THIGH_L, LMX_CALF_L, LMX_FOOT_L,	// LM_LTHIGH, LSHIN, LFOOT
		LMX_THIGH_R, LMX_CALF_R, LMX_FOOT_R,	// LM_RTHIGH, RSHIN, RFOOT
		LMX_TORSO,			// LM_TORSO
		LMX_UARM_R, LMX_LARM_R, LMX_HAND_R,	// LM_RINARM, ROUTARM, RHAND
		LMX_UARM_L, LMX_LARM_L, LMX_HAND_L,	// LM_LINARM, LOUTARM, LHAND
		LMX_HEAD			// LM_HEAD - the odd one
	};

	/* ------------------------------------------------------------- state -- */

	/* THE LEVEL'S WARDROBE. The two bind poses have to outlive
	 * HDModelLevelBegin - it keeps the pointer while it runs - so they are
	 * here and not on HDLaraDrawPlan's stack. */
	HDLaraLevel g_level;
	float g_bodyBind[HD_LARA_NODES * 3];
	float g_hairBind[HD_LARA_HAIR_NODES * 3];
	int g_laraSlots[64];
	int g_laraSlotCount = 0;
	const char* g_why = "";
	bool g_active = false;
	bool g_planned = false;

	/* WHICH SLOTS CAN OWN A HAND. Worked out once per level out of hd_bind's
	 * name table, so that the per frame answer is a handful of pointer
	 * comparisons and not a walk over 471 slots. LARA is in here as well:
	 * putting a gun away puts LARA's own mesh back at LM_RHAND, and that is
	 * the rest hands. */
	int g_handSlots[24];
	int g_handSlotCount = 0;

	/* Per level, for the log. */
	int g_frame_parts = 0;
	bool g_checked = false;
	bool g_frame_logged = false;
	int g_dropped = 0;

	/* The two edge trackers of stage S8. They are LEVEL state and not frame
	 * state: what they remember is the last value that was written to the
	 * log, so that a view left on across a level boundary says so once more
	 * in the new level's lines rather than staying silent. */
	bool g_fp_seen = false;
	bool g_fp_was = false;
	bool g_binos_was = false;
	bool g_binos_pending = false;

	/* Fix round R: the throttle of the HDLOG neckcut line - GlobalCounter
	 * when it was last written. The counter ticks thirty times a second at
	 * every fps_mode, so 30 is one second of game time. */
	bool g_neck_probe_ever = false;
	unsigned short g_neck_probe_at = 0;

	/* The reason the registry choice hands back (#153, round M2, the fix
	 * round). A literal with the lifetime of the program, because
	 * HDLaraOutfitFor's `why` is kept by the caller and written into the log
	 * lines of a whole level. */
	const char* const g_chosen_why =
		"the outfit the player chose (registry hd_outfit)";

	/* ------------------------------------------------------ the wardrobe -- */

	/* WHICH OUTFIT, out of the engine's own facts and nothing else. Every one
	 * of the three questions is asked the way the engine asks it elsewhere,
	 * which is what keeps this from drifting away from the classic path:
	 *
	 *   the cap    game\hair.cpp asks this exact expression three times and
	 *              game\laraskin.cpp once. It is the only thing in this
	 *              engine that can tell the Russia cap from the Rome body,
	 *              because a vanilla Russia level carries the same outfit
	 *              byte Rome does.
	 *   young      the script flag or the outfit byte. THE SOURCE OF TRUTH IS
	 *              THE ENGINE'S OWN BODY ASSEMBLY, and it took a spiel test
	 *              to notice that this line was not (#123, stage S7-2, F1).
	 *              LaraInitialiseMeshes (game\laramisc.cpp) and its twin in
	 *              game\deltapak.cpp are the only two places in this engine
	 *              that build a CHILD Lara - they lay ANIMATING6_MIP's torso
	 *              over the skin - and both ask `Irish level range AND
	 *              GF_YOUNGLARA`. The flag is necessary; the range on its own
	 *              is not sufficient, and this line used to treat it as if it
	 *              were. The randomizer clears the flag out of SCRIPT.DAT
	 *              (core\ScriptDat.cpp) and puts an adult skin in the level,
	 *              because young Lara can carry no weapon at all - so every
	 *              randomized Irish level was reported "young Lara stays
	 *              classic" while a grown Lara stood on the screen.
	 *
	 *              Measured, seed 380: vanilla ANDY1/2/3 carry data[0] = 1,
	 *              which is LaraDrawType LARA_YOUNG, and the flag; the
	 *              randomized three carry 2, 0 and 2 - BUNHEAD, NORMAL,
	 *              BUNHEAD - and no flag. Both of the two facts left in the
	 *              test flip with the change, and the range does not.
	 *   the guest  data[3] of the level header, raw (#123, stage S7). The
	 *              randomizer writes LARA_PONYTAIL both for a Tomb Raider III
	 *              outfit and for the Rome body inside a Russia level (#122,
	 *              Randomizer.cpp: the wardrobe roll), so the outfit byte
	 *              cannot say which of the two it is; data[3] can, and it
	 *              carries the engine's own HD_LARA_OUTFIT_* number so that
	 *              neither side translates. 0 in every vanilla file. */
	int outfit_of_level(const char** why)
	{
		const bool cap =
			(gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE &&
				LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD;

		const bool young = (gfLevelFlags & GF_YOUNGLARA) != 0 ||
			LaraDrawType == LARA_YOUNG;

		/* THE PLAYER'S OWN CHOICE COMES FIRST, AND YOUNG LARA COMES BEFORE IT
		 * (#153, round M2, the fix round).
		 *
		 * The wardrobe is a hundred outfits now (hd_wardrobe.h) and the level
		 * header has one byte for it, which can only ever name the nine the
		 * enum has. The first design put the key into the .hdm sidecar and
		 * had the randomizer roll it - and that was wrong twice over: a seed
		 * and its settings travel between players while an installation and a
		 * Mods folder do not, so a run would not reproduce; and the choice is
		 * not the run's to make. It is the PLAYER'S, and it belongs where a
		 * player's settings live.
		 *
		 * So the key is a REG_SZ named hd_outfit under the game's own settings
		 * key - the one hd_install and every tomb5. option live in - and this
		 * is the only thing that reads it. Read once per level build, not
		 * once per session, because the in-game changer of round M3 writes it
		 * while the game is running and the next level has to see it. Empty
		 * or absent is the ordinary case and costs one read-only registry
		 * open: the level's own facts, exactly as before this round.
		 *
		 * WHERE IT WINS. Over the level facts and over data[3] both, and that
		 * is deliberate - it is the only one of the three a person chose on
		 * purpose. The log line says what it overrode, so a picture nobody
		 * asked for is one grep from its cause.
		 *
		 * Young Lara stays classic whatever the registry says (plan section
		 * 9, the user's decision), which is why the question is asked here
		 * and not left to HDLaraOutfitFor below: a setting must not be able to
		 * put an adult body on the child. And a key this machine's scan has
		 * not got - the player took the mod out again, or moved the setting
		 * from another machine - falls through to the level's facts with a
		 * line in the log. */
		if (!young)
		{
			char chosen[HD_WARDROBE_KEY];

			if (REG_PeekString("tomb5", "hd_outfit", chosen, sizeof(chosen)))
			{
				const int found = HDWardrobeFind(chosen);

				if (found >= 0)
				{
					const char* factsWhy = "";
					const int facts = HDLaraOutfitFor(LaraDrawType, cap, young,
						(int)GfHdOutfit, &factsWhy);

					Log("HDLOG wardrobe hd_outfit=%s wins over the level facts "
						"(outfit=%d data[3]=%d - %s)", chosen, facts,
						(int)GfHdOutfit, factsWhy);

					*why = g_chosen_why;
					return HDLaraOutfitOfWear(found);
				}

				Log("HDLOG wardrobe hd_outfit=%s not found - level facts", chosen);
			}
		}

		return HDLaraOutfitFor(LaraDrawType, cap, young, (int)GfHdOutfit, why);
	}

	/* ---------------------------------------------------------- the frame -- */

	/* The engine's 3x4 into the row-major 4x4 hd_model wants - the same eight
	 * lines hd_object.cpp's HDObjectEnd uses, and for the same reason: it is
	 * what SetD3DMatrix does for the fixed-function pipe and what stage S1-2
	 * proved for the rooms. */
	void to_row_major(const float* in, float* out)
	{
		out[0] = in[M00];  out[1] = in[M10];  out[2] = in[M20];  out[3] = 0.0f;
		out[4] = in[M01];  out[5] = in[M11];  out[6] = in[M21];  out[7] = 0.0f;
		out[8] = in[M02];  out[9] = in[M12];  out[10] = in[M22]; out[11] = 0.0f;
		out[12] = in[M03]; out[13] = in[M13]; out[14] = in[M23]; out[15] = 1.0f;
	}

	/* WHICH SLOT OWNS THIS HAND THIS FRAME. The engine keeps it as a POINTER
	 * in lara.mesh_ptrs, so the only way back to a slot number is to compare
	 * it against the candidates - which is precisely what the classic path
	 * does itself when it wants to know whether the flare is still in her
	 * hand (game\larafire.cpp:331). Twelve comparisons at most, twice a
	 * frame.
	 *
	 * -1 when none of them matches, which is a mesh swap from outside the
	 * wardrobe - a cutscene's, or one the randomizer arranged. hd_lara then
	 * falls back to the rest hands, which is a hand and not a hole. */
	/* THE SPEECH HEAD, MEASURED AND NOT DRAWN (#123, fix round C, user finding
	 * C6: "the speech heads of every NPC and of Lara are still missing").
	 *
	 * WHAT THE CLASSIC ENGINE DOES. There is no speech head DRAW path and no
	 * overlay: a talking head is a MESH SWAP inside the normal draw.
	 *   - Lara: game\deltapak.cpp:2759, handle_lara_chatting - while the
	 *     cutscene frame is inside one of the level's chat ranges it writes
	 *     lara.mesh_ptrs[LM_HEAD] = the head mesh of one of the four slots
	 *     LARA_SPEECH_HEAD1..4, re-rolled EVERY FRAME (GetRandomControl() & 3).
	 *     Four heads with different mouths, cycled at 30 Hz, is the whole of
	 *     TR5's lip sync.
	 *   - Everyone else: deltapak.cpp:2763, handle_actor_chatting - the same
	 *     trick one level down, on the actor's own object: it sets the node's
	 *     bit in cutseq_meshswapbits and writes the mesh of
	 *     ACTOR1_SPEECH_HEAD1/2 or ACTOR2_SPEECH_HEAD1/2 into the SECOND
	 *     pointer of that node's mesh pair, which DrawCutSeqActors then draws
	 *     instead of the first (deltapak.cpp:3226 and 3260).
	 *
	 * WHY THE HD PATH SHOWS NONE OF IT. Both ends of the swap are meshes, and
	 * the HD path has no meshes: Lara's head comes from LARA_HEAD.TRM and an
	 * actor's from his body model or his own <NAME>_HEAD.TRM (fix round B).
	 * A swapped classic mesh has nowhere to go. And the six speech head SLOTS
	 * have no model of their own to swap to either - not one .MAP names them
	 * and neither 4\ITEM nor 5\ITEM ships a LARA_SPEECH_HEAD1.TRM or an
	 * ACTOR1_SPEECH_HEAD1.TRM.
	 *
	 * WHAT THE REMASTER DOES INSTEAD, measured over its own files: one rigged
	 * head per speaker - LARA_HEAD.TRM has bones up to 31, which are a FACIAL
	 * rig and not mesh tree nodes - and beside it a .TRL file PER SPOKEN LINE:
	 * LARA_HEAD_96.TRL .. LARA_HEAD_135.TRL, ADMIRAL_HEAD_113.TRL,
	 * PIERRE_HEAD_108.TRL and so on, 2.6 MiB apiece. That is facial animation
	 * data for a rig this engine already carries but does not animate, in a
	 * format nothing here reads. Driving it is a piece of work of its own and
	 * not a fix round; inventing a mouth movement instead would be inventing
	 * a look, which this project does not do.
	 *
	 * SO THE SEAM MEASURES IT. speech_head_slot() says which of the four Lara
	 * speech slots the engine has just put on her neck, and the frame log
	 * carries it, so the day the .TRL work starts the trigger is already
	 * visible and the user can see from a log whether a cutscene is chatting
	 * at all. It changes no picture. */
	int speech_head_slot()
	{
		short* have = lara.mesh_ptrs[LM_HEAD];

		if (!have || !meshes)
			return -1;

		for (int slot = LARA_SPEECH_HEAD1; slot <= LARA_SPEECH_HEAD4; slot++)
			if (objects[slot].loaded &&
				have == meshes[objects[slot].mesh_index + 2 * LM_HEAD])
				return slot;

		return -1;
	}

	int slot_of_hand(int node)
	{
		short* have = lara.mesh_ptrs[node];

		if (!have || !meshes)
			return -1;

		for (int i = 0; i < g_handSlotCount; i++)
		{
			const int slot = g_handSlots[i];

			if (have == meshes[objects[slot].mesh_index + 2 * node])
				return slot;
		}

		return -1;
	}

	/* THE BRAID'S SIX MATRICES.
	 *
	 * TR5 draws the braid out of SIX meshes but only THREE of them stand on
	 * their own: game\hair.cpp's DrawHair puts meshes 1, 3 and 5 at
	 * hairs[i][1], [3] and [5] with phd_TranslateAbs / RotY / RotX, and draws
	 * 0, 2 and 4 as SKINNED connectors out of stashed vertices of their
	 * neighbours. HAIR_OG.TRM has six bones, one per link, so the three
	 * connectors need a matrix the classic path never builds.
	 *
	 * They are not missing, only unused: the simulation writes a position AND
	 * an angle pair for every one of hairs[0][0..5] (HairControl's loop sets
	 * (hair-1)->pos.y_rot / x_rot for i = 1..6, which is links 0 to 5). So
	 * the connectors are built the same way the rigid ones are, which is what
	 * TROne does for all six.
	 *
	 * phd_TranslateAbs only replaces the TRANSLATION column and leaves the
	 * rotation where it is, so each link has to start from the BASE of the
	 * matrix stack - the world-to-view matrix - which is what DrawHair gets
	 * for free by being called with the stack unwound and what this has to
	 * put there on purpose, because the seam sits inside DrawLara's own push.
	 *
	 * The base and NOT aW2VMx, which is nearly the same matrix and not quite:
	 * phd_GenerateW2V puts the poison shake on the stack's base through
	 * ScaleCurrentMatrix and leaves aW2VMx alone (specific\3dmath.cpp,
	 * aGenerateW2V). Lara's body inherits that shake through lara_matrices;
	 * a braid built from aW2VMx would be the one part of her that stood
	 * still. */
	void braid_matrices(float* out /* HD_LARA_HAIR_NODES * 16 */)
	{
		short roll[HD_LARA_HAIR_NODES];

		HDLaraBraidRolls(roll);
		phd_PushMatrix();

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			const HAIR_STRUCT* link = &hairs[0][n];

			for (int i = 0; i < indices_count; i++)
			{
				aMXPtr[i] = aFMatrixStack[i];
				phd_mxptr[i] = matrix_stack[i];
			}

			phd_TranslateAbs(link->pos.x_pos, link->pos.y_pos, link->pos.z_pos);
			phd_RotY(link->pos.y_rot);
			phd_RotX(link->pos.x_rot);

			/* THE ROLL (#123, fix round G, G1). About the bone's OWN axis and
			 * therefore free: the +z the two rotations above just built does
			 * not move, so braidcheck below still lands on the next link to
			 * the rounding, and only the plait's twist changes. */
			phd_RotZ(roll[n]);
			to_row_major(aMXPtr, out + n * 16);
		}

		phd_PopMatrix();
	}

	/* ------------------------------------------------------- the self-test -- */

	/* laracheck - the same proof hd_object.cpp writes as itemcheck, on Lara's
	 * own matrices: a node matrix carries the node's ORIGIN into view space
	 * all by itself, so the classic screen position (the three lines of
	 * aTransformLightClipMesh) and the device's (the projection matrix, the w
	 * divide and the viewport rule) can be compared on it directly. It runs
	 * on the first HD Lara frame of a level and once. */
	void self_test(const float* matrices, int nodes, const float* hairMatrices)
	{
		if (!gLogEnabled || g_checked)
			return;

		g_checked = true;

		const double width = 2.0 * (double)f_centerx;
		const double height = 2.0 * (double)f_centery;

		D3DMATRIX projection;

		HDRenderMakeProjection((double)f_persp, (double)f_centerx,
			(double)f_centery, (double)f_a, (double)f_b, &projection);

		const float* p = (const float*)&projection;
		double worst_x = 0, worst_y = 0;
		int used = 0, worst_node = -1;

		for (int node = 0; node < nodes; node++)
		{
			const float* m = matrices + node * 16;
			const double vx = m[12];
			const double vy = m[13];
			const double vz = m[14];

			if (vz < (double)f_mznear)
				continue;

			const double zv = (double)f_persp / vz;
			const double classic_x = vx * zv + (double)f_centerx;
			const double classic_y = vy * zv + (double)f_centery;

			const double cx = vx * p[0] + vy * p[4] + vz * p[8] + p[12];
			const double cy = vx * p[1] + vy * p[5] + vz * p[9] + p[13];
			const double cw = vx * p[3] + vy * p[7] + vz * p[11] + p[15];

			if (cw <= 0.0)
				continue;

			const double dx = fabs((cx / cw + 1.0) * 0.5 * width - classic_x);
			const double dy = fabs((1.0 - cy / cw) * 0.5 * height - classic_y);

			if (dx > worst_x || dy > worst_y)
				worst_node = node;

			if (dx > worst_x) worst_x = dx;
			if (dy > worst_y) worst_y = dy;

			used++;
		}

		Log("HDLOG laracheck outfit=%s nodes=%d used=%d node=%d "
			"maxdx=%.5f maxdy=%.5f",
			HDLaraOutfitModel(HDLaraOutfitNow())
				? HDLaraOutfitModel(HDLaraOutfitNow()) : "-",
			nodes, used, worst_node, worst_x, worst_y);

		/* braidcheck - THE PROOF THAT THE THREE REBUILT CONNECTORS ARE IN THE
		 * RIGHT PLACE, and the only part of this seam that could be silently
		 * wrong: the classic path never builds a matrix for links 0, 2 and 4,
		 * so nothing in the game has ever used their angles.
		 *
		 * The identity comes from HairControl itself (game\hair.cpp): it
		 * derives every link's position by putting the link BEFORE it at its
		 * own position and angles and stepping one bone offset along that
		 * frame. So the frame built here for link n, stepped by the HAIR mesh
		 * tree's own offset, has to land on link n+1's own position - to the
		 * long/float rounding of the two roads and no further. A swapped
		 * RotY/RotX, a dropped sign or a stale angle all break it, and none
		 * of them is visible in a still picture of a braid hanging straight
		 * down.
		 *
		 * Measured in CLASSIC UNITS in view space, which is what the matrices
		 * carry - one unit is 1/1024 of a sector. */
		if (!hairMatrices || !g_level.hairBind)
			return;

		double worst_gap = 0;
		int worst_link = -1;

		for (int n = 0; n + 1 < HD_LARA_HAIR_NODES; n++)
		{
			const float* m = hairMatrices + n * 16;
			const float* next = hairMatrices + (n + 1) * 16;

			/* The step is the mesh tree's own offset between the two links -
			 * 56 units in every adult level, and read rather than assumed
			 * because young Lara's pigtails are half that. */
			const float step[3] =
			{
				g_level.hairBind[(n + 1) * 3 + 0] - g_level.hairBind[n * 3 + 0],
				g_level.hairBind[(n + 1) * 3 + 1] - g_level.hairBind[n * 3 + 1],
				g_level.hairBind[(n + 1) * 3 + 2] - g_level.hairBind[n * 3 + 2]
			};

			const double px = step[0] * m[0] + step[1] * m[4] + step[2] * m[8] + m[12];
			const double py = step[0] * m[1] + step[1] * m[5] + step[2] * m[9] + m[13];
			const double pz = step[0] * m[2] + step[1] * m[6] + step[2] * m[10] + m[14];

			const double dx = px - (double)next[12];
			const double dy = py - (double)next[13];
			const double dz = pz - (double)next[14];
			const double gap = sqrt(dx * dx + dy * dy + dz * dz);

			if (gap > worst_gap)
			{
				worst_gap = gap;
				worst_link = n;
			}
		}

		/* The pack rides along, so that a log says what the braid was pushed
		 * out of: a BOX in the torso frame since fix round I (I1), where fix
		 * rounds D to G had two spheres. The root is the near one for an HD
		 * Lara again since fix round I (I2) - HAIRLOG in game\hair.cpp prints
		 * the one the frame really used, every frame, and it must not
		 * alternate. */
		Log("HDLOG braidcheck links=%d step=%.0f worst_gap=%.3f at link %d "
			"(units, view space) root_z=%d packbox=x%d y%d..%d z%d..%d bias %d",
			HD_LARA_HAIR_NODES,
			g_level.hairBind[1 * 3 + 2] - g_level.hairBind[0 * 3 + 2],
			worst_gap, worst_link, HD_LARA_BRAID_ROOT_Z_HD,
			HD_LARA_PACK_BOX_X, HD_LARA_PACK_BOX_Y_TOP,
			HD_LARA_PACK_BOX_Y_BOTTOM, HD_LARA_PACK_BOX_Z_BACK,
			HD_LARA_PACK_BOX_Z_FRONT, HD_LARA_PACK_BOX_BIAS);
	}
}

/* ------------------------------------------------------------ the level -- */

int HDLaraDrawPlan(HDModelSlot* out, int max)
{
	const char* why = "";

	HDLaraDrawEnd();

	if (!out || max <= 0)
		return 0;

	/* The same guard the objects have, and the same key. Lara is a model like
	 * every other one and a level's models are one vertex buffer; a switch of
	 * her own would only be a second way to end up with half a picture. */
	if (!tomb5.hd_objects)
		return 0;

	g_level.outfit = outfit_of_level(&why);
	g_why = why;

	if (g_level.outfit == HD_LARA_OUTFIT_NONE)
		return 0;

	/* THE BODY'S BIND POSE, out of the slot whose mesh tree the matrices were
	 * BUILT from - game\delstuff.cpp walks bones[objects[LARA].bone_index],
	 * so that is the tree the .TRM has to be folded against.
	 * game\laramisc.cpp copies LARA_SKIN's meshes into LARA at level start
	 * but never its bones, so the two trees are the same walk read from one
	 * place instead of two. */
	if (!objects[LARA].loaded || objects[LARA].nmeshes < HD_LARA_NODES)
	{
		g_why = "this level's LARA has no fifteen node mesh tree";
		g_level.outfit = HD_LARA_OUTFIT_NONE;
		return 0;
	}

	HDObjectBindPose(LARA, g_bodyBind, HD_LARA_NODES);

	g_level.bodyBind = g_bodyBind;
	g_level.bodyNodes = HD_LARA_NODES;
	g_level.hairBind = 0;
	g_level.hairNodes = 0;

	// #123 fix round G (G1): a new level, a new braid, a new held reference.
	HDLaraBraidReset();

	if (objects[HAIR].loaded && objects[HAIR].nmeshes >= HD_LARA_HAIR_NODES)
	{
		HDObjectBindPose(HAIR, g_hairBind, HD_LARA_HAIR_NODES);
		g_level.hairBind = g_hairBind;
		g_level.hairNodes = HD_LARA_HAIR_NODES;
	}

	/* WHICH OF LARA'S SLOTS THIS LEVEL HAS. A weapon whose slot is not loaded
	 * cannot be put in her hands by the classic path either, so its wardrobe
	 * model is not read - which is what keeps a Rome level down to eleven
	 * files instead of twenty-one. */
	g_laraSlotCount = 0;
	g_handSlotCount = 0;

	for (int slot = 0; slot < NUMBER_OBJECTS; slot++)
	{
		if (!HDSlotIsLara(slot) || !objects[slot].loaded)
			continue;

		if (g_laraSlotCount < (int)(sizeof(g_laraSlots) / sizeof(g_laraSlots[0])))
			g_laraSlots[g_laraSlotCount++] = slot;

		/* A hand candidate needs enough meshes to HAVE an LM_LHAND, or the
		 * pointer comparison would read past the slot's own mesh table. */
		if (HDLaraPartForSlot(slot, HD_LARA_ROLE_HAND) >= 0 &&
			objects[slot].nmeshes > LM_LHAND &&
			g_handSlotCount < (int)(sizeof(g_handSlots) / sizeof(g_handSlots[0])))
			g_handSlots[g_handSlotCount++] = slot;
	}

	g_level.slots = g_laraSlotCount ? g_laraSlots : 0;
	g_level.slotCount = g_laraSlotCount;
	g_planned = true;

	return HDLaraPlan(&g_level, out, max);
}

void HDLaraDrawBuild(const char* stem)
{
	HDLaraStats stats;

	if (!stem)
		stem = "?";

	if (!g_planned || g_level.outfit == HD_LARA_OUTFIT_NONE)
	{
		Log("HDLOG level=%s lara outfit=none parts=0 laratype=%d data3=%d "
			"young=%d reason=%s", stem, (int)LaraDrawType, (int)GfHdOutfit,
			((gfLevelFlags & GF_YOUNGLARA) != 0 || LaraDrawType == LARA_YOUNG) ? 1 : 0,
			g_why && *g_why ? g_why : "the HD object path is off");
		return;
	}

	g_active = HDLaraLevelBuild(&g_level, &stats);

	Log("HDLOG level=%s lara outfit=%s(%s) laratype=%d data3=%d hd=%d "
		"parts=%d/%d twins=%d missing=%d "
		"slots=%d hands=%d braid=%d verts=%d idx=%d ranges=%d pages=%d "
		"vb_kib=%.0f ib_kib=%.0f drift=%.0f/%.0f over %d reason=%s",
		stem, HDLaraOutfitModel(g_level.outfit)
			? HDLaraOutfitModel(g_level.outfit) : "-",
		HDLaraOutfitLabel(g_level.outfit), (int)LaraDrawType, (int)GfHdOutfit,
		(int)g_active, stats.loaded,
		stats.wanted, stats.twins, stats.missing, g_laraSlotCount, g_handSlotCount,
		(int)HDLaraHasPart(HD_LARA_PART_BRAID), stats.vertices, stats.indices,
		stats.ranges, stats.pages, stats.vertexBytes / 1024.0,
		stats.indexBytes / 1024.0, stats.driftMean, stats.driftWorst,
		stats.driftParts, g_why);

	/* WHICH ENTRY OF THE WARDROBE THIS IS, IN ONE LINE (#153, round M2): the
	 * key, the file, where the file came from, the five flags with the origin
	 * of each, the material and the skin pages. A picture the user does not
	 * like is then one grep away from the row or the outfit.txt line that made
	 * it, which is the same promise the part lines below make. */
	{
		const int wear = HDLaraWearOfOutfit(g_level.outfit);
		const int at = wear >= 0 ? wear
			: HDWardrobeFindFile(HDLaraOutfitModel(g_level.outfit),
				HD_WARDROBE_SOURCE_46);
		const HDWardrobeEntry* e = HDWardrobeAt(at);
		static const char* const kFrom[] = { "default", "measured", "table", "outfit.txt" };

		if (e)
			Log("HDLOG level=%s wardrobe key=%s name=%s file=%s source=%s "
				"mod=%d hands=%d braid=%d holsters=%d gloves=%d rig=%d (%s) "
				"sheen=%.2f/%.0f (%s) skin=%d,%d,%d (%s) feet=%d (%s) pages=%s",
				stem, e->key, e->name, e->file,
				e->source == HD_WARDROBE_SOURCE_46 ? "46"
					: e->source == HD_WARDROBE_SOURCE_13 ? "13" : "mod",
				e->mod, (int)e->hands, (int)e->braid, (int)e->holsters,
				(int)e->gloves, (int)e->wearsRig, kFrom[e->fromFlags],
				e->sheen, e->sheenPower, kFrom[e->fromSheen],
				e->skin[0], e->skin[1], e->skin[2], kFrom[e->fromSkin],
				e->feet, kFrom[e->fromFeet],
				e->mod >= 0 ? "the mod's own namespace" : "the install's");
		else
			Log("HDLOG level=%s wardrobe outfit=%s has no scan entry - the "
				"wardrobe's own defaults are worn", stem,
				HDLaraOutfitModel(g_level.outfit)
					? HDLaraOutfitModel(g_level.outfit) : "-");
	}

	for (int i = 0; i < HDLaraNoteCount(); i++)
		Log("HDLOG level=%s lara note: %s", stem, HDLaraNote(i));

	/* Part by part, so that a wrong picture in the game can be looked up
	 * rather than guessed at - the same reason the objects get a line each.
	 * At most twenty-one lines and only with -log. */
	if (!gLogEnabled)
		return;

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraPartPlanned(part) || !HDLaraPartModel(g_level.outfit, part))
			continue;

		const int slot = HDLaraPartSlot(part);
		float mean = 0, worst = 0;
		int covered = 0;

		for (int node = 0; node < 32; node++)
			if (HDLaraPartCoverage(part) & (1u << node))
				covered++;

		HDModelBindDrift(slot, &mean, &worst);

		/* The sheen goes in the line as well (#123, fix round D, D2): the
		 * numbers are a material table and not a measurement, so a picture
		 * the user does not like has to be traceable to a number he can name
		 * without opening the source. */
		float sheen = 0.0f, sheenPower = 1.0f;
		float held = 0.0f, heldPower = 1.0f;

		HDLaraPartSheen(part, &sheen, &sheenPower);
		/* And what is IN the hand, on its own pages (#123, fix round I, I4):
		 * `held` is the weapon's material and `pagesheen` how many pages of
		 * this model were given it. A hand part that reads 0 pages is a hand
		 * with nothing in it - or a rule that has stopped working. */
		HDLaraHeldSheen(part, &held, &heldPower);

		Log("HDLOG level=%s lara part=%s model=%s hd=%d cover=%d(%08x) "
			"verts=%d drift=%.0f/%.0f sheen=%.2f/%.0f held=%.2f/%.0f "
			"pagesheen=%d",
			stem, HDLaraPartLabel(part), HDLaraPartModel(g_level.outfit, part),
			(int)HDLaraHasPart(part), covered, HDLaraPartCoverage(part),
			HDLaraHasPart(part) ? HDModelVertices(slot) : 0, mean, worst,
			sheen, sheenPower, held, heldPower,
			HDLaraHasPart(part) ? HDModelPageSheenCount(slot) : 0);
	}
}

void HDLaraDrawEnd()
{
	/* NOT ONE DIRECT3D CALL - hd_object.cpp's own level end, which calls
	 * this, runs on the LOAD thread. The models themselves are released by
	 * HDModelLevelEnd on the main thread, and Lara has nothing of her own
	 * besides these flags. */
	HDLaraLevelEnd();

	memset(&g_level, 0, sizeof(g_level));
	g_level.outfit = HD_LARA_OUTFIT_NONE;
	g_laraSlotCount = 0;
	g_handSlotCount = 0;
	g_why = "";
	g_active = false;
	g_planned = false;
	g_frame_parts = 0;
	g_checked = false;
	g_frame_logged = false;
	g_dropped = 0;
	g_fp_seen = false;
	g_fp_was = false;
	g_binos_was = false;
	g_binos_pending = false;
}

bool HDLaraDrawActive()
{
	return g_active && tomb5.hd_objects && HDLaraActive();
}

int HDLaraDrawOutfit()
{
	return g_level.outfit;
}

/* THE DRAWN BRAID'S ROLL (#123, fix round G, G1). The reasoning and what was
 * measured are at HD_LARA_BRAID_ROLL_NEAR in hd_lara.h; this is the
 * arithmetic.
 *
 * ONE reference for all six bones, so that two neighbours of a rigid plait
 * can never twist against each other - which is the thing the classic path
 * hides with its skinned connectors and the HD one cannot. The reference is
 * the azimuth of the WHOLE braid, root to tip: that vector is six bones long
 * where the yaw's own is one, so its horizontal part is ten to thirty times
 * better conditioned.
 *
 * And it is HELD while even that is noise. A braid hanging straight down has
 * a horizontal span of a couple of dozen units on a good frame and none at
 * all on a bad one, so below NEAR the reference does not move, above FAR it
 * follows the braid completely, and in between it follows part of the way -
 * no threshold anywhere that the picture can cross in one frame.
 *
 * Every angle is a short and the arithmetic wraps with it, so the difference
 * of two of them is already the short way round the circle.
 */
namespace
{
	short g_braid_lock = 0;
	bool g_braid_lock_seen = false;
	short g_braid_roll[HD_LARA_HAIR_NODES];
}

void HDLaraBraidReset()
{
	g_braid_lock = 0;
	g_braid_lock_seen = false;

	for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		g_braid_roll[n] = 0;
}

void HDLaraBraidRollStep()
{
	const long dx = hairs[0][HD_LARA_HAIR_NODES].pos.x_pos -
		hairs[0][0].pos.x_pos;
	const long dz = hairs[0][HD_LARA_HAIR_NODES].pos.z_pos -
		hairs[0][0].pos.z_pos;
	const long flat = phd_sqrt(SQUARE(dx) + SQUARE(dz));
	const short target = (short)phd_atan(dz, dx);
	long weight;	// 0..256, how much of the braid's own azimuth to believe

	if (flat <= HD_LARA_BRAID_ROLL_NEAR)
		weight = 0;
	else if (flat >= HD_LARA_BRAID_ROLL_FAR)
		weight = 256;
	else
		weight = 256 * (flat - HD_LARA_BRAID_ROLL_NEAR) /
			(HD_LARA_BRAID_ROLL_FAR - HD_LARA_BRAID_ROLL_NEAR);

	if (!g_braid_lock_seen)
	{
		g_braid_lock = target;
		g_braid_lock_seen = true;
	}
	else
		g_braid_lock = (short)(g_braid_lock +
			(short)(((long)(short)(target - g_braid_lock) * weight) / 256));

	for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		g_braid_roll[n] = (short)(g_braid_lock - hairs[0][n].pos.y_rot);
}

int HDLaraBraidRolls(short* out)
{
	if (!out)
		return 0;

	for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		out[n] = g_braid_roll[n];

	return HD_LARA_HAIR_NODES;
}

/* ------------------------------------------------------------ the frame -- */

void HDLaraDrawFrameReset()
{
	/* What the frame that has just ENDED put on the list. Read before the
	 * counter is cleared, because the binocular line below is a statement
	 * about a finished frame and not about the one starting now. */
	const int wasPushed = g_frame_parts;

	g_frame_parts = 0;

	/* THE TWO LINES STAGE S8 OWES THE LOG, and they belong HERE because this
	 * is the one place in the HD path that runs on every frame whether Lara
	 * is drawn or not - which is exactly the state the second one reports.
	 *
	 * Both are written on the EDGE and not per frame: a line a second for
	 * hours would bury everything around it, and what one wants to see is
	 * when the view came on, with which cut, and that the binoculars really
	 * do keep the whole composite off the list.
	 *
	 * Lara is not skipped anywhere in THIS file when the binoculars are up -
	 * the engine never gets that far. BinocularCamera sets
	 * lara_item->mesh_bits = 0 (game\camera.cpp:2185) and game\draw.cpp:982
	 * asks `if (lara_item->mesh_bits && !SCNoDrawLara)` before it calls
	 * DrawLara at all, so HDLaraDrawFrame does not run and nothing is pushed.
	 * The count is therefore taken one frame AFTER the scope came up, when
	 * the frame it describes was wholly inside the binocular view - on the
	 * edge frame itself she was still being drawn normally. */
	if (gLogEnabled)
	{
		const bool now = FirstPersonActive();

		if (!g_fp_seen || now != g_fp_was)
		{
			g_fp_seen = true;
			g_fp_was = now;
			/* One number since fix round U: the cut is a bone WEIGHT, and how
			 * much of the head node a fragment has to carry to go is the
			 * whole of it. */
			Log("HDLOG firstperson %s headcut=%.2f",
				now ? "on" : "off",
				now ? (double)HD_LARA_HEAD_CUT_WEIGHT : 0.0);
		}

		const bool binos = BinocularRange != 0;

		if (binos && !g_binos_was)
			g_binos_pending = true;
		else if (binos && g_binos_pending)
		{
			g_binos_pending = false;
			Log("HDLOG binoculars lara_pushed=%d mesh_bits=%d", wasPushed,
				(int)(lara_item ? lara_item->mesh_bits : -1));
		}
		else if (!binos && g_binos_was)
		{
			g_binos_pending = false;
			Log("HDLOG binoculars off");
		}

		g_binos_was = binos;
	}
}

void HDLaraDrawFrame(long mirror, bool honourMeshBits)
{
	float matrices[HD_LARA_NODES * 16];
	float nodeLight[HD_LARA_NODES * 3];
	HDObjLight lights[HD_MAX_OBJ_LIGHTS];
	HDLaraDraw pieces[8];
	HDLaraFrame now;
	float sunScale = 0.75f;
	ProfScope prof(PROF_HD_LARA);

	if (!HDLaraDrawActive())
		return;

	/* --- the fifteen matrices ------------------------------------------ */
	for (int node = 0; node < HD_LARA_NODES; node++)
		to_row_major(&lara_matrices[kLmxOfNode[node] * indices_count],
			matrices + node * 16);

	/* --- the light ------------------------------------------------------ */
	/* These three lines are the first three of phd_PutPolygons with the draw
	 * left out, exactly as hd_object.cpp does them for an item: the view
	 * matrix of node 0, the reset that builds aLightMatrix, and the setup
	 * that fills SunLights / PointLights / SpotLights and leaves the ambient
	 * in aAmbientR/G/B. CalculateObjectLightingLara has run a moment ago in
	 * the caller and filled lara_item->il.
	 *
	 * bLaraUnderWater is forced to -1 for it, which is the classic path's
	 * "this node is dry": InitObjectLighting then answers with
	 * LaraNodeAmbient[0] when she is in water at all, and with the item's own
	 * ambient when she is not. The WET answer is the other branch of the same
	 * three lines and is read straight out of LaraNodeAmbient[1] below rather
	 * than by calling the whole setup a second time - a second call would
	 * push every dynamic light onto the list twice. */
	const long saved_uw = bLaraUnderWater;

	SetD3DMatrix(&D3DMView, &lara_matrices[LMX_HIPS * indices_count]);
	SuperResetLights();
	bLaraUnderWater = -1;
	InitObjectLighting(lara_item);
	bLaraUnderWater = saved_uw;

	const float dry[3] =
	{
		(float)aAmbientR, (float)aAmbientG, (float)aAmbientB
	};

	const float wet[3] =
	{
		(float)CLRR(LaraNodeAmbient[1]),
		(float)CLRG(LaraNodeAmbient[1]),
		(float)CLRB(LaraNodeAmbient[1])
	};

	/* THE PER NODE AMBIENT, which Lara really has and an object does not: a
	 * body half in the water is lit by two rooms at once, and the classic
	 * path switches between them per node through bLaraUnderWater. The index
	 * is the LMX one, because LaraNodeUnderwater is filled in LMX order
	 * (specific\drawlara.cpp, SetLaraUnderwaterNodes). */
	for (int node = 0; node < HD_LARA_NODES; node++)
	{
		const bool under = bLaraInWater != 0 &&
			LaraNodeUnderwater[kLmxOfNode[node]] != 0;
		const float* from = under ? wet : dry;

		nodeLight[node * 3 + 0] = from[0];
		nodeLight[node * 3 + 1] = from[1];
		nodeLight[node * 3 + 2] = from[2];
	}

	const int lightCount = HDObjectLights(lara_item, lights,
		HD_MAX_OBJ_LIGHTS, &sunScale);

	/* --- which nodes are drawn at all ------------------------------------ */
	unsigned int mask = ~0u;

	if (honourMeshBits)
	{
		/* LARA'S mesh_bits ARE NOT AN ITEM'S. The skin's fifteen bits sit in
		 * the HIGH half and are counted BACKWARDS in LMX order - DrawLara__1
		 * asks `mesh_bits >> 16 & 1 << (15 - i)` with i the LMX index, so the
		 * head is bit 23 and not bit 14. The low half belongs to the fourteen
		 * seam pieces, which the HD body does not have. */
		mask = 0;

		for (int node = 0; node < HD_LARA_NODES; node++)
			if (lara_item->mesh_bits >> 0x10 & 1 << (15 - kLmxOfNode[node]))
				mask |= 1u << node;
	}

	/* FIRST PERSON (#126). The classic path leaves the head mesh out and does
	 * not call DrawHair, because both sit exactly where the camera does and
	 * blow through the picture. The HD composite has to do the same three
	 * things: the body's head node, the separate face, and the braid.
	 *
	 * What it does NOT have to copy is the reason the classic path hides the
	 * head this way rather than through mesh_bits - the skin's neighbours
	 * pull their rim points out of the head mesh's stash and lose them with
	 * it ("the third arm"). An HD model has no stash and no seam pieces: the
	 * head node is one range of triangles and dropping it drops nothing
	 * else. */
	const bool firstPerson = FirstPersonActive();

	if (firstPerson)
		mask &= ~(1u << HD_LARA_HEAD_NODE);

	/* AND A BODY THAT HAS BORROWED TR5'S HEAD DRAWS NONE OF ITS OWN (#156).
	 *
	 * The same bit, taken for a different reason and settled at level build:
	 * an outfit of the Tomb Raider I-III bundle carries headgear on the head
	 * node and no hair, so the head rule drops that node and puts node 14 of
	 * OUTFIT_CLASSIC in its place (HD_LARA_PART_HEAD below). It is ~0u for
	 * every body that keeps its own - which is every body of the IV-VI bundle,
	 * so the picture there is the one this engine drew before the round, to
	 * the bit.
	 *
	 * It is its OWN mask and not part of `mask`, because `mask` is what the
	 * FRAME says - mesh_bits and first person - and every other part of the
	 * composite is entitled to that. The borrowed head is drawn with `mask`
	 * like the rest of them, so a head shot away by mesh_bits takes it too. */
	const unsigned int bodyMask = mask & HDLaraBodyMask();

	/* AND THE NECK UNDER IT (stage S8, user finding F3). Masking the head
	 * node is where the classic path stops, and for a classic body that is
	 * enough: LM_HEAD is head AND neck in one mesh. An HD body is one skinned
	 * surface and the stump belongs to the TORSO node, so it stays - a collar
	 * of triangles 18.6 units from the eye when a wall holds the camera at
	 * the joint, which is inside the near plane and fills the picture. The
	 * diving suit puts its whole HELMET on the torso node the same way.
	 *
	 * Neither can be masked off without taking her chest with it, so they are
	 * cut in the shader instead: EVERY FRAGMENT WHOSE HEAD WEIGHT REACHES
	 * HD_LARA_HEAD_CUT_WEIGHT (#123, fix round U - stage S8 cut a sphere
	 * about the eye, rounds J to L a cylinder in the bind pose, and fix round
	 * S a plane carried by the head node; hd_lara.h carries the whole argument
	 * and the measurement over the eight wardrobe bodies).
	 *
	 * THERE IS NOTHING TO BUILD HERE, and that is the point of the round. Fix
	 * round S had to turn a bind-pose axis and a depth into four view space
	 * numbers with the head's own interpolated matrix, once a frame - and the
	 * play test came back with Lara cut away across her chest, because a plane
	 * fixed to the head tilts with the head. A bone weight is written in the
	 * .TRM, is the same number in every pose and at every camera angle, and
	 * the shader already has it in hand for the skinning.
	 *
	 * THE BODY ALONE carries it - the hands, the holsters, the weapon on her
	 * back and the LID that closes the hole are their own models and are
	 * pushed with none, so a hand that comes up to the camera is never cut
	 * away, and the lid, whose rim vertices are copies of the body's own, is
	 * never clipped by the cut its edge sits on.
	 *
	 * A level with no HD body has no cut, and then nothing is cut: half a cut
	 * is worse than none. */
	HDModelNeckCut neckCut = { 0 };
	int cutNode = HD_LARA_HEAD_NODE;
	float cutBar = 0.0f;
	const bool cutting = firstPerson && HDLaraHeadCut(&cutNode, &cutBar);

	if (cutting)
	{
		neckCut.on = 1.0f;
		neckCut.node = (float)cutNode;
		neckCut.bar = cutBar;
	}

	/* WHERE THE EYE STANDS IN WHAT IS LEFT OF HER (#123, fix round R; the
	 * fields are fix round U's).
	 *
	 * The cut itself is a fact about the FILE and cannot vary with the pose -
	 * which is why fix round S's drop= column, counting how many of the
	 * fifteen joints had fallen on the discarded side of a plane, has no
	 * successor: no joint can fall on the wrong side of a weight. What is
	 * still a question about the pose is where the EYE ends up, and a picture
	 * in a bind pose cannot answer that. So this measures it, once a second of
	 * game time and with -log only:
	 *
	 *   w=      the threshold in force, node= the node it is weighed on
	 *   head=   the head joint, torso= the torso joint, min= the nearest
	 *           joint of all fifteen and which one - all in view space. Under
	 *           the renderer's near plane at 20 (InitWindow, gamemain.cpp) the
	 *           lid is being clipped by the near plane and not by anything
	 *           this round can reach.
	 *   lid=    whether the lid was built for this outfit at all. */
	if (gLogEnabled && firstPerson)
	{
		const unsigned short now = GlobalCounter;

		if (!g_neck_probe_ever || (unsigned short)(now - g_neck_probe_at) >= 30)
		{
			float best = 0.0f;
			int bestNode = -1;

			for (int node = 0; node < HD_LARA_NODES; node++)
			{
				const float* m = matrices + node * 16;
				const float d = sqrtf(m[12] * m[12] + m[13] * m[13] +
					m[14] * m[14]);

				if (bestNode < 0 || d < best)
				{
					best = d;
					bestNode = node;
				}
			}

			const float* h = matrices + HD_LARA_HEAD_NODE * 16;
			const float* t = matrices + HD_LARA_TORSO * 16;

			g_neck_probe_ever = true;
			g_neck_probe_at = now;

			Log("HDLOG headcut cut=%d w=%.2f node=%d head=%.1f torso=%.1f "
				"min=%.1f@node%d lid=%d",
				cutting ? 1 : 0, cutting ? (double)cutBar : 0.0, cutNode,
				(double)sqrtf(h[12] * h[12] + h[13] * h[13] + h[14] * h[14]),
				(double)sqrtf(t[12] * t[12] + t[13] * t[13] + t[14] * t[14]),
				(double)best, bestNode,
				(int)HDLaraHasPart(HD_LARA_PART_NECK_CAP));
		}
	}

	/* AND ONE SIDED WITH IT, for the frames the cut runs in. The HD path draws
	 * every model double sided; cut a hole in a closed body and what shows
	 * through it is the body's own inside - her chest seen from within, her
	 * face from behind. The classic engine never shows that because it culls
	 * back faces in SOFTWARE before the card sees them, and no classic camera
	 * stands inside an object anyway. This one does. hd_model.h carries the
	 * measurement that says the wardrobe's winding is consistent enough to
	 * cull on. */
	/* AND THE OTHER SIDE IN A MIRROR (#123, fix round C, found while
	 * correcting the mirror paragraph of MODIFICATIONS.md for C8). The
	 * reflected draw really does flip the winding: game\mirror.cpp negates two
	 * of the three angles, adds half a turn to the third and then calls
	 * CalcLaraMatrices(2), which scales the matrix by -1 on ALL THREE axes
	 * (game\delstuff.cpp:124, ScaleCurrentMatrix). Three sign changes are a
	 * determinant of -1, and the composition works out to exactly the
	 * reflection in the mirror's z plane - which is why the classic engine
	 * turns its own software test round with SetCullCW / SetCullCCW for the
	 * length of that draw.
	 *
	 * It costs the HD path nothing anywhere else, because everything it draws
	 * is double sided. First person is the one frame that culls, and there a
	 * reflection would come out inside-out - so the mirrored draw culls the
	 * other side, exactly as mirror.cpp does. (First person inside a mirror
	 * room is a corner of a corner; the line is one term and removes the
	 * question.) */
	static_assert(HD_MODEL_CULL_BACK == HD_MODEL_CULL_CCW,
		"the mirrored first person draw picks the other side by name");

	const int cull = !firstPerson ? HD_MODEL_CULL_NONE
		: mirror ? HD_MODEL_CULL_CW : HD_MODEL_CULL_BACK;

	/* --- which parts ---------------------------------------------------- */
	now.rightHandSlot = slot_of_hand(LM_RHAND);
	now.leftHandSlot = slot_of_hand(LM_LHAND);
	now.leftHolsterSlot = LHolster;
	now.rightHolsterSlot = lara.holster;
	now.backGunSlot = lara.back_gun;

	const int pieceCount = HDLaraFrameParts(&now, pieces, 8);

	/* --- on the list ----------------------------------------------------- */
	/* The body first and the face on top of it, then the hands, the holsters
	 * and the weapon on the back, then the braid - the order hd_lara.h names,
	 * which is also the order the classic path draws in. It does not decide
	 * anything on its own: all of it is opaque and z-buffered, so the depth
	 * test sorts it whatever order it arrives in. What DOES depend on the
	 * order is a see-through pass, and the wardrobe has none.
	 *
	 * If the body cannot go on the list, nothing else may either - half a
	 * Lara is worse than the classic one. It cannot happen from here: the
	 * frame list is empty when DrawLara runs, because game\draw.cpp calls it
	 * before the PrintObjects loop. The counter is for the day that changes. */
	int drawn = 0;

	/* THE SHEEN, per part (#123, fix round D, D2). hd_lara.cpp's table says
	 * what a part is made of; the body's answer follows the OUTFIT, because
	 * the same part number is a Rome top in one level and a catsuit in the
	 * next. A part with no entry gets 0 and the shader's branch costs
	 * nothing. */
	float sheen = 0.0f, sheenPower = 1.0f;

	HDLaraPartSheen(HD_LARA_PART_BODY, &sheen, &sheenPower);

	if (!HDObjectPush(HDLaraPartSlot(HD_LARA_PART_BODY), matrices,
		HD_LARA_NODES, nodeLight, bodyMask, lights, lightCount, sunScale,
		cutting ? &neckCut : 0, cull, sheen, sheenPower))
	{
		g_dropped++;
		return;
	}

	drawn++;

	/* TR5'S OWN HEAD, FOR A BODY THAT HAS NONE (#156).
	 *
	 * Node 14 of the install's OUTFIT_CLASSIC - her hair, and two triangles
	 * of neck under it - drawn here and not with the pieces, because it
	 * belongs to the BODY: it stands where the body's own head node would
	 * have stood, wears the same node mask, the same light and the same
	 * matrices, and the face goes over it exactly as it goes over a IV-VI
	 * body's own head.
	 *
	 * BEFORE THE FACE, which is where the body's head node is drawn, so that
	 * the hair's see-through pass reaches the card in the order it reaches it
	 * when the head is part of the body. The wardrobe's only alpha is this.
	 *
	 * THE SAME CUT AS THE BODY, so a first person frame takes it away with
	 * the body's head - and `mask` has that node clear there anyway, so this
	 * draws nothing at all in first person and the neck lid is untouched.
	 *
	 * HDLaraHasPart is the whole guard: the part exists only for a body the
	 * head rule dressed, and for anybody else HDLaraPartSlot names a slot no
	 * model was built for, which HDObjectPush turns down at its first line. */
	if (HDLaraHasPart(HD_LARA_PART_HEAD))
	{
		float headSheen = 0.0f, headPower = 1.0f;

		HDLaraPartSheen(HD_LARA_PART_HEAD, &headSheen, &headPower);

		if (HDObjectPush(HDLaraPartSlot(HD_LARA_PART_HEAD), matrices,
			HD_LARA_NODES, nodeLight, mask, lights, lightCount, sunScale,
			cutting ? &neckCut : 0, cull, headSheen, headPower))
			drawn++;
	}

	/* THE INNER SKIN OF FIX ROUND K STOOD HERE (#123, fix round K, K1).
	 *
	 * The draw above keeps the outward faces and the cut takes her neck away,
	 * so a ray down the collar meets nothing at all - the hole fix round I
	 * built a cap for and fix round J re-aimed, and which the play test sent
	 * back as "a great flat plate". This is the other answer: the very same
	 * model, the very same matrices, lights and cut, with the OTHER side of
	 * every triangle kept. What a ray through the hole meets is then the far
	 * wall of her own chest.
	 *
	 * FOUR THINGS ABOUT THIS CALL ARE THE WHOLE DESIGN:
	 *
	 *   THE SAME CUT. Without it the inner wall of the neck TUBE would be
	 *   drawn, and that tube stands eighteen units from the eye - inside the
	 *   near plane and across the whole picture. The cut has to take both
	 *   walls or neither.
	 *
	 *   THE SAME NODE MASK, and not the torso's alone. A ray that leaves the
	 *   chest through an arm hole has to meet the inside of the arm; masked
	 *   to node 7 it would meet nothing and the hole would be back, smaller
	 *   and in a stranger place.
	 *
	 *   AFTER the outward draw, so the depth test does the hiding. Every
	 *   inward face of a closed limb sits behind that limb's own outward face
	 *   and is dropped; only where the outward face was cut away does an
	 *   inward one survive. That is why this costs a draw and not a picture.
	 *
	 *   THE SAME LIGHT. Same node ambient, same list, same sun factor - the
	 *   inside of her chest is lit like the outside of it, which is the only
	 *   way the seam at the cut's edge does not read as an edge. The sheen is
	 *   the skin's, because that is what the surface now is.
	 *
	 * The colour came from hd_lara.cpp's measurement of her own throat, set
	 * on the model at level build - and since fix round U the lid does not
	 * wear it either: it wears the RIM's own texture, page and coordinates
	 * both. The measurement stays as the build log's one line saying which
	 * page of an outfit is her skin. */
	/* It is gone (#123, fix round S, S1(4)). The LID below closes the hole
	 * now, and the second draw was measured out rather than argued out: forty
	 * pictures - five outfits, three pitches, both eyes the game uses, and the
	 * head turned forty degrees and the torso bent thirty on top of that - are
	 * IDENTICAL to the pixel with the inner draw and without it. It closed
	 * nothing the lid does not close.
	 *
	 * It also cost something: a second pass over 27,000 triangles every first
	 * person frame, and fix round R's own finding - a strap, a shoulder piece
	 * or a flap is ONE layer of triangles, its two sides sit at the same depth
	 * to the bit, and the inner draw won that tie, which is what the play test
	 * saw as torn edges and missing straps. R settled it with a D3DCMP_LESS
	 * for that draw; taking the draw away settles it for good.
	 *
	 * What stays is the MEASUREMENT it brought: which texel of which page is
	 * Lara's own throat (measure_neck_skin, hd_lara.cpp). The lid wore it from
	 * fix round S until fix round U, which gave the lid the RIM's own texture
	 * instead; the line is still in the build log, and it is how a lid that
	 * came out on a coat can be told from one that came out wrong. */

	/* AND THE LID OVER THE HOLE (#123, fix round S, S1).
	 *
	 * A disc in the cut's own plane, skinned rigidly to the head node - so it
	 * cannot drift off the plane, whatever the animation does with her head,
	 * because the plane is built out of the same matrix.
	 *
	 * FOUR THINGS ABOUT THIS CALL:
	 *
	 *   NO CUT. It lies exactly IN the plane, and a fragment exactly on a
	 *   plane is at the mercy of the last bit of a float. Handing it its own
	 *   cut would flicker it away; it has no geometry above the plane to cut
	 *   anyway.
	 *
	 *   NO CULL. The camera looks down on it from the head side, but a mirror
	 *   room turns the winding round and a lid one can see through from one
	 *   side is a hole. It is one triangle fan; drawing both sides is free.
	 *
	 *   EVERY NODE. Its rim vertices are copies of the body's own, so they are
	 *   skinned to whatever those are - the torso, and a little of the head.
	 *   Naming a mask would be naming the same set a second time and getting
	 *   it wrong once.
	 *
	 *   THE SKIN'S SHEEN, because what it is is the top of her neck. Its
	 *   texture is the rim's own, page and coordinates both, taken at level
	 *   build.
	 *
	 * A level whose lid could not be built (no bind pose, no rim at the cut)
	 * has none: HDLaraHasPart says so, first person keeps the open hole, and
	 * the note at level build says why. */
	if (firstPerson && cutting && HDLaraHasPart(HD_LARA_PART_NECK_CAP))
	{
		float capSheen = 0.0f, capPower = 1.0f;

		HDLaraPartSheen(HD_LARA_PART_NECK_CAP, &capSheen, &capPower);

		if (HDObjectPush(HDLaraPartSlot(HD_LARA_PART_NECK_CAP), matrices,
			HD_LARA_NODES, nodeLight, ~0u, lights,
			lightCount, sunScale, 0, HD_MODEL_CULL_NONE, capSheen, capPower))
			drawn++;
	}

	HDLaraPartSheen(HD_LARA_PART_FACE, &sheen, &sheenPower);

	if (!firstPerson && HDObjectPush(HDLaraPartSlot(HD_LARA_PART_FACE),
		matrices, HD_LARA_NODES, nodeLight, mask, lights, lightCount, sunScale,
		0, HD_MODEL_CULL_NONE, sheen, sheenPower))
		drawn++;

	/* THE POUCHES, UNDER WHATEVER GOES IN THEM (#123, fix round D, D4).
	 *
	 * Before the pieces and not with them, because it is not a piece: the rig
	 * belongs to the OUTFIT and not to the weapon. It is on Lara whether she
	 * has her guns in her hands or on her thighs, exactly as the built-in rig
	 * of OUTFIT_CLASSIC is - the pouches do not vanish when she draws.
	 *
	 * HDLaraHasPart is the whole guard: the part exists only for an outfit
	 * hd_lara.cpp's table names (kind 4, Outfit::wearsRig - OUTFIT_VCI and
	 * nobody else since fix round F), and for anybody else HDLaraPartSlot
	 * names a slot no model was built for, which HDObjectPush turns down at
	 * its first line.
	 *
	 * The node mask is the part's own coverage - nodes 0, 1 and 4, the belt
	 * and the two thighs - AND the frame's, so first person takes it off with
	 * everything else. */
	HDLaraPartSheen(HD_LARA_PART_HOLSTER_RIG, &sheen, &sheenPower);

	if (HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG) &&
		HDObjectPush(HDLaraPartSlot(HD_LARA_PART_HOLSTER_RIG), matrices,
			HD_LARA_NODES, nodeLight, HDLaraPartCoverage(HD_LARA_PART_HOLSTER_RIG) & mask,
			lights, lightCount, sunScale, 0, HD_MODEL_CULL_NONE,
			sheen, sheenPower))
		drawn++;

	for (int i = 0; i < pieceCount; i++)
	{
		HDLaraPartSheen(pieces[i].part, &sheen, &sheenPower);

		if (HDObjectPush(HDLaraPartSlot(pieces[i].part), matrices,
			HD_LARA_NODES, nodeLight, pieces[i].nodeMask & mask, lights,
			lightCount, sunScale, 0, HD_MODEL_CULL_NONE, sheen, sheenPower))
			drawn++;

		/* AND THE INSTALL'S OWN HANDS UNDER A MOD'S GUN (#153, round M2b).
		 *
		 * Same matrices, same node mask, same light, same sheen: it IS the
		 * hand of this part, in this pose - the mod's file has had its hands
		 * cut away and this is what stands in their place. HDLaraHasPartTwin
		 * is false for every part of an unmodded wardrobe, so this line costs
		 * a test and nothing else. */
		if (HDLaraHasPartTwin(pieces[i].part) &&
			HDObjectPush(HDLaraPartTwinSlot(pieces[i].part), matrices,
				HD_LARA_NODES, nodeLight, pieces[i].nodeMask & mask, lights,
				lightCount, sunScale, 0, HD_MODEL_CULL_NONE, sheen, sheenPower))
			drawn++;
	}

	/* THE BRAID, on its own six link chain and its own matrices. It is lit
	 * with the head's ambient, which is what the classic path does too -
	 * DrawHair runs with bLaraUnderWater set from LaraNodeUnderwater[8], the
	 * head. */
	float hairMatrices[HD_LARA_HAIR_NODES * 16];
	bool haveBraid = false;

	if (!firstPerson && HDLaraHasPart(HD_LARA_PART_BRAID))
	{
		float hairLight[HD_LARA_HAIR_NODES * 3];
		const int head = HD_LARA_HEAD_NODE;

		braid_matrices(hairMatrices);
		haveBraid = true;

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			hairLight[n * 3 + 0] = nodeLight[head * 3 + 0];
			hairLight[n * 3 + 1] = nodeLight[head * 3 + 1];
			hairLight[n * 3 + 2] = nodeLight[head * 3 + 2];
		}

		HDLaraPartSheen(HD_LARA_PART_BRAID, &sheen, &sheenPower);

		if (HDObjectPush(HDLaraPartSlot(HD_LARA_PART_BRAID), hairMatrices,
			HD_LARA_HAIR_NODES, hairLight, ~0u, lights, lightCount, sunScale,
			0, HD_MODEL_CULL_NONE, sheen, sheenPower))
			drawn++;
	}

	g_frame_parts += drawn;

	self_test(matrices, HD_LARA_NODES, haveBraid ? hairMatrices : 0);

	if (gLogEnabled && !g_frame_logged)
	{
		g_frame_logged = true;
		// interp= is what THIS frame was drawn at (#148, order 2): 1.00 for a
		// frame that landed on a logic tick or a build at fps_mode 0, 0.50
		// for one drawn halfway between two ticks. It says the HD seam saw
		// the interpolated root, which is the whole claim of that order -
		// the HD path was not touched for it and follows off the engine's
		// own matrix stack.
		Log("HDLOG frame lara parts=%d(%d this frame) interp=%.2f outfit=%s right=%s left=%s "
			"holsters=%s/%s back=%s braid=%d head=%s/%d mask=%08x body=%08x mirror=%d "
			"firstperson=%d headcut=%d lights=%d dropped=%d speechhead=%s",
			drawn, g_frame_parts, InterpFrameAlpha(), HDLaraOutfitModel(HDLaraOutfitNow())
				? HDLaraOutfitModel(HDLaraOutfitNow()) : "-",
			HDSlotName(now.rightHandSlot) ? HDSlotName(now.rightHandSlot) : "-",
			HDSlotName(now.leftHandSlot) ? HDSlotName(now.leftHandSlot) : "-",
			HDSlotName(now.leftHolsterSlot) ? HDSlotName(now.leftHolsterSlot) : "-",
			HDSlotName(now.rightHolsterSlot) ? HDSlotName(now.rightHolsterSlot) : "-",
			HDSlotName(now.backGunSlot) ? HDSlotName(now.backGunSlot) : "-",
			(int)HDLaraHasPart(HD_LARA_PART_BRAID),
			/* #156: the head rule in force and whether TR5's head is really
			 * on the card for it - "tr5/0" is a body told to drop its own
			 * head and given no donor, which is the one case where the line
			 * and the picture would otherwise be a puzzle. */
			HDWardrobeHeadWord(HDLaraHeadMode()),
			(int)HDLaraHasPart(HD_LARA_PART_HEAD),
			mask, bodyMask, (int)mirror,
			(int)firstPerson, (int)cutting, lightCount, g_dropped,
			HDSlotName(speech_head_slot()) ? HDSlotName(speech_head_slot()) : "-");
	}
}

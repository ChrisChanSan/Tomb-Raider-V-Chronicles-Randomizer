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
#include "hd_wardrobe.h"		// the outfit scan
#include "../registry.h"		// hd_outfit, the player's own choice
#include "hd_model.h"
#include "hd_trl.h"			// the loop between lines and the firing face
#include "hd_object.h"
#include "hd_render.h"
#include "hd_bind.h"
#include "../3dmath.h"
#include "../d3dmatrix.h"
#include "../lighting.h"
#include "../function_stubs.h"
#include "../profile.h"			// PROFLOG phases
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// meshes, bones
#include "../../game/lara.h"		// lara, lara_item, LaraDrawType
#include "../../game/lara2gun.h"	// LHolster
#include "../../game/delstuff.h"	// lara_matrices, LaraNodeAmbient/Underwater
#include "../../game/hair.h"		// hairs
#include "../../game/camera.h"		// FirstPersonActive
#include "../../game/gameflow.h"	// gfLevelId, gfLevelFlags
#include "../../game/interpolate.h"	// InterpFrameAlpha - what this frame was drawn at
#include "../../game/deltapak.h"	// the cutscene her lips speak in

#include <math.h>

namespace
{
	/* LM node -> LMX matrix is lara_lmx_of_node (game\delstuff.h): a .TRM's
	 * three bone bytes are mesh tree nodes, LM_ order, and lara_matrices is
	 * in LMX_ order. */
	static_assert(sizeof(lara_lmx_of_node) / sizeof(lara_lmx_of_node[0]) ==
		HD_LARA_NODES, "one matrix for every node of the HD composite");

	/* ------------------------------------------------------------- state -- */

	/* The level's wardrobe. The two bind poses have to outlive
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

	/* Which slots can own a hand. Worked out once per level out of hd_bind's
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

	/* The edge trackers of the first person and binocular log lines. They
	 * are level state and not frame state: what they remember is the last
	 * value written to the log, so that a view left on across a level
	 * boundary says so once more in the new level's lines. */
	bool g_fp_seen = false;
	bool g_fp_was = false;
	bool g_binos_was = false;
	bool g_binos_pending = false;

	/* The throttle of the HDLOG headcut line - GlobalCounter when it was
	 * last written. The counter ticks thirty times a second at every
	 * fps_mode, so 30 is one second of game time. */
	bool g_neck_probe_ever = false;
	unsigned short g_neck_probe_at = 0;

	/* The reason the registry choice hands back. A literal with the lifetime
	 * of the program, because
	 * HDLaraOutfitFor's `why` is kept by the caller and written into the log
	 * lines of a whole level. */
	const char* const g_chosen_why =
		"the outfit the player chose (registry hd_outfit)";

	/* ------------------------------------------------------ the wardrobe -- */

	/* Which outfit, out of the engine's own facts. Each of the three
	 * questions is asked the way the engine asks it elsewhere, so this cannot
	 * drift away from the classic path:
	 *
	 *   the cap    game\hair.cpp asks this exact expression three times and
	 *              game\laraskin.cpp once. It is the only thing in this
	 *              engine that can tell the Russia cap from the Rome body,
	 *              because a vanilla Russia level carries the same outfit
	 *              byte Rome does.
	 *   young      the script flag or the outfit byte - the facts the
	 *              engine's own body assembly uses. LaraInitialiseMeshes
	 *              (game\laramisc.cpp) and its twin in game\deltapak.cpp are
	 *              the only two places that build a child Lara (they lay
	 *              ANIMATING6_MIP's torso over the skin), and both ask `Irish
	 *              level range AND GF_YOUNGLARA`. The range alone is not
	 *              sufficient: the randomizer clears the flag out of
	 *              SCRIPT.DAT (core\ScriptDat.cpp) and puts an adult skin in
	 *              the level, because young Lara carries no weapon, and a test
	 *              on the range would keep the child on a grown Lara.
	 *
	 *              Measured: vanilla ANDY1/2/3 carry data[0] = 1, which is
	 *              LaraDrawType LARA_YOUNG, and the flag; a randomized run
	 *              (seed 380) carries 2, 0 and 2 - BUNHEAD, NORMAL, BUNHEAD -
	 *              and no flag. Both facts in the test flip with the change,
	 *              and the range does not.
	 *   the guest  data[3] of the level header, raw. The randomizer writes
	 *              LARA_PONYTAIL both for a Tomb Raider III outfit and for the
	 *              Rome body inside a Russia level (Randomizer.cpp: the
	 *              wardrobe roll), so the outfit byte cannot say which of the
	 *              two it is; data[3] can, and it carries the engine's own
	 *              HD_LARA_OUTFIT_* number so that neither side translates.
	 *              0 in every vanilla file. */
	int outfit_of_level(const char** why)
	{
		const bool cap =
			(gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE &&
				LaraDrawType == LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD;

		const bool young = (gfLevelFlags & GF_YOUNGLARA) != 0 ||
			LaraDrawType == LARA_YOUNG;

		/* The player's own choice comes first, and young Lara comes before it.
		 *
		 * The wardrobe holds far more outfits (hd_wardrobe.h) than the one
		 * byte of the level header can name. The choice is not the
		 * randomizer's: a seed travels between players while an installation
		 * and a Mods folder do not, so a rolled key would not reproduce. It is
		 * the player's, and lives where a player's settings live - a REG_SZ
		 * named hd_outfit under the game's own settings key (the one
		 * hd_install and every tomb5. option live in). This is the only
		 * reader. It is read once per level build, not once per session, so
		 * that a change made while the game runs reaches the next level.
		 * Empty or absent costs one read-only registry open and leaves the
		 * level's own facts.
		 *
		 * It wins over the level facts and over data[3] both, because it is
		 * the only one of the three a person chose on purpose. The log line
		 * says what it overrode.
		 *
		 * Young Lara stays classic whatever the registry says, which is why
		 * the question is asked here and not left to HDLaraOutfitFor below: a
		 * setting must not put an adult body on the child. A key this
		 * machine's scan has not got (the mod was removed, or the setting
		 * came from another machine) falls through to the level's facts with
		 * a line in the log. */
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
	 * what SetD3DMatrix does for the fixed-function pipe and what the rooms
	 * use. */
	void to_row_major(const float* in, float* out)
	{
		out[0] = in[M00];  out[1] = in[M10];  out[2] = in[M20];  out[3] = 0.0f;
		out[4] = in[M01];  out[5] = in[M11];  out[6] = in[M21];  out[7] = 0.0f;
		out[8] = in[M02];  out[9] = in[M12];  out[10] = in[M22]; out[11] = 0.0f;
		out[12] = in[M03]; out[13] = in[M13]; out[14] = in[M23]; out[15] = 1.0f;
	}

	/* slot_of_hand, below speech_head_slot: which slot owns this hand this
	 * frame. The engine keeps it as a pointer
	 * in lara.mesh_ptrs, so the only way back to a slot number is to compare
	 * it against the candidates - which is what the classic path does itself
	 * when it asks whether the flare is still in her hand (game\larafire.cpp).
	 * Twelve comparisons at most, twice a frame.
	 *
	 * -1 when none of them matches, which is a mesh swap from outside the
	 * wardrobe - a cutscene's, or one the randomizer arranged. hd_lara then
	 * falls back to the rest hands, which is a hand and not a hole. */
	/* The speech head, measured and not drawn.
	 *
	 * The classic engine has no speech head draw path and no overlay: a
	 * talking head is a mesh swap inside the normal draw.
	 *   - Lara: game\deltapak.cpp, handle_lara_chatting - while the cutscene
	 *     frame is inside one of the level's chat ranges it writes
	 *     lara.mesh_ptrs[LM_HEAD] = the head mesh of one of the four slots
	 *     LARA_SPEECH_HEAD1..4, re-rolled every frame (GetRandomControl() & 3).
	 *     Four heads with different mouths, cycled at 30 Hz, is the whole of
	 *     TR5's lip sync.
	 *   - Everyone else: deltapak.cpp, handle_actor_chatting - the same trick
	 *     on the actor's own object: it sets the node's bit in
	 *     cutseq_meshswapbits and writes the mesh of ACTOR1_SPEECH_HEAD1/2 or
	 *     ACTOR2_SPEECH_HEAD1/2 into the second pointer of that node's mesh
	 *     pair, which DrawCutSeqActors then draws instead of the first.
	 *
	 * The HD path shows none of it. Both ends of the swap are meshes, and the
	 * HD path has no meshes: Lara's head comes from LARA_HEAD.TRM and an
	 * actor's from his body model or his own <NAME>_HEAD.TRM. The six speech
	 * head slots have no model to swap to either - no .MAP names them and
	 * neither 4\ITEM nor 5\ITEM ships a LARA_SPEECH_HEAD1.TRM or an
	 * ACTOR1_SPEECH_HEAD1.TRM.
	 *
	 * The remaster's files carry instead one rigged head per speaker -
	 * LARA_HEAD.TRM has bones up to 31, a facial rig and not mesh tree nodes -
	 * and beside it a .TRL file per spoken line: LARA_HEAD_96.TRL ..
	 * LARA_HEAD_135.TRL, ADMIRAL_HEAD_113.TRL, PIERRE_HEAD_108.TRL and so on,
	 * 2.6 MiB apiece: facial animation data for a rig this engine carries but
	 * does not animate, in a format nothing here reads. No mouth movement is
	 * invented in its place.
	 *
	 * So speech_head_slot() reports which of the four Lara speech slots the
	 * engine has just put on her neck, and the frame log carries it, so a log
	 * shows whether a cutscene is chatting. It changes no picture. */
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

	/* ------------------------------------------------------- her face -- */

	/* Whether the classic game has LARA_SCREAM's head on her: game\
	 * larafire.cpp (LaraGun, LG_READY) puts it on every tick the fire key is
	 * held with a gun ready and LARA's back every tick it is not; game\
	 * deltapak.cpp (deal_with_pistols) puts it on for the 16 ticks after
	 * each shot a cutscene fires. The HD face follows the pointer, so it
	 * follows every one of those writes and nothing else - also where the
	 * classic head stays on after the gun was put away some other way.
	 * A level whose two heads are one mesh has no swap to follow. */
	bool scream_head()
	{
		short* have = lara.mesh_ptrs[LM_HEAD];

		if (!have || !meshes || !objects[LARA_SCREAM].loaded)
			return false;

		short* const scream = meshes[objects[LARA_SCREAM].mesh_index + 2 * LM_HEAD];

		return have == scream &&
			scream != meshes[objects[LARA].mesh_index + 2 * LM_HEAD];
	}

	/* What her face shows. Lara's face is LARA_HEAD.TRM in every head mode
	 * (hd_lara.cpp), and its own joint block is the source of both faces
	 * below (hd_trl.h):
	 *
	 *   cut     the track the cut she acts in plays, where her face has its
	 *           file, as a speaking actor's face does, LIPLOG and all.
	 *   fire    the firing face, while the classic head is LARA_SCREAM's
	 *           (scream_head): three ticks in, three ticks out.
	 *   loop    the loop between lines: her eyes blink and glance and her
	 *           brows move, the mouth stays shut.
	 *   static  the face as it was built: first person (no face is drawn),
	 *           a head node the frame hides (the binoculars and the laser
	 *           sight write mesh_bits 0), a face without a loop, and death -
	 *           the loop stops where it stood and eases to the bind over
	 *           HD_LIPS_EASE_FRAMES ticks, the way a track ends.
	 *
	 * in that order. The clock is GlobalCounter, one step a logic tick at
	 * every frame rate: it stands in the inventory and the pause, so the
	 * face holds its frame there, and a draw that runs twice in a frame -
	 * the mirror - shows the same frame twice. It wraps at 65536 ticks, a
	 * jump of the loop every 36 minutes. */
	enum FaceKind
	{
		FACE_STATIC = 0,
		FACE_CUT,
		FACE_FIRE,
		FACE_LOOP
	};

	const char* face_word(int kind)
	{
		switch (kind)
		{
		case FACE_CUT: return "cut";
		case FACE_FIRE: return "fire";
		case FACE_LOOP: return "loop";
		default: return "static";
		}
	}

	/* The face across draws: level state, reset with the level
	 * (HDLaraDrawEnd). */
	struct FaceState
	{
		bool seen;			// a face has been chosen in this level
		int kind;			// the last choice, FaceKind
		const char* stillWhy;		// why the face last stood still
		unsigned short tick;		// the GlobalCounter the fire step is of
		int fireStep;			// HDLipsFireStep's, 0 .. HD_LIPS_EASE_IN_TICKS
		bool dead;			// hit_points <= 0 at the last choice
		unsigned short deadAt;		// the GlobalCounter she was first seen dead at
		int deadFrame;			// the loop's frame then, -1 for none
		int loopFrame;			// the loop's frame shown last, -1 for none
		unsigned short blinkAt;		// the GlobalCounter a blink was last looked for at
		bool blinkSeen;
	};

	FaceState g_face;

	void face_reset()
	{
		memset(&g_face, 0, sizeof(g_face));
		g_face.kind = FACE_STATIC;
		g_face.stillWhy = "start";
		g_face.deadFrame = -1;
		g_face.loopFrame = -1;
	}

	/* The track and the frame her face is pushed with, and the log's
	 * changes of face. `headShown` is the head node's bit in the frame's
	 * mask; `cutTrack` and `cutFrame` are the cut's pair or -1. Reads
	 * lara.mesh_ptrs[LM_HEAD], lara_item->hit_points and GlobalCounter and
	 * writes none of them. */
	void choose_face(int slot, bool firstPerson, bool headShown, int cutTrack,
		int cutFrame, int* track, int* frame)
	{
		const unsigned short now = (unsigned short)GlobalCounter;
		const bool scream = scream_head();
		const bool dead = lara_item && lara_item->hit_points <= 0;

		*track = -1;
		*frame = -1;

		/* The fire step, on by the ticks since the last choice. The swap is
		 * written in a logic tick and a draw comes after it, so one look a
		 * tick sees every change; the first choice of a level counts as one
		 * tick. */
		if (!g_face.seen)
			g_face.tick = (unsigned short)(now - 1);

		const int ticks = (unsigned short)(now - g_face.tick);

		if (ticks)
		{
			g_face.fireStep = HDLipsFireStep(g_face.fireStep, scream, ticks);
			g_face.tick = now;
		}

		if (dead && !g_face.dead)
		{
			g_face.deadAt = now;
			g_face.deadFrame = g_face.kind == FACE_LOOP ? g_face.loopFrame : -1;
		}

		g_face.dead = dead;

		const int loop = HDModelLipsLoop(slot, false);
		int kind = FACE_STATIC;
		int shown = -1;
		const char* why = "no block";

		if (firstPerson)
			why = "first person";
		else if (cutTrack >= 0 && HDModelLipsOpen(slot, false, cutTrack) > 0)
		{
			kind = FACE_CUT;
			*track = cutTrack;
			*frame = cutFrame;
			shown = cutFrame;
			why = "cutscene";
		}
		else if (!headShown)
			why = "masked";
		else if (loop <= 0)
			why = "no block";
		else if (g_face.fireStep > 0 &&
			HDModelLipsOpen(slot, false, HD_LIPS_TRACK_FIRE) > 0)
		{
			kind = FACE_FIRE;
			*track = HD_LIPS_TRACK_FIRE;
			*frame = HDModelLipsFireKey(dead ? -1 : now % loop, g_face.fireStep);
			shown = HD_LIPS_FIRE_FRAME;
			why = scream ? "fire on" : "fire off";
		}
		else if (!dead && HDModelLipsOpen(slot, false, HD_LIPS_TRACK_LOOP) > 0)
		{
			kind = FACE_LOOP;
			*track = HD_LIPS_TRACK_LOOP;
			*frame = HDModelLipsLoopKey(now % loop, 0);
			shown = now % loop;
			why = g_face.kind == FACE_FIRE ? "fire off"
				: g_face.kind == FACE_CUT ? "cutscene" : g_face.stillWhy;
		}
		else if (dead)
		{
			/* Dead: the loop stops on the frame it showed and eases to the
			 * bind the way a track eases past its end. */
			const int past = (unsigned short)(now - g_face.deadAt) + 1;

			why = "dead";

			if (g_face.deadFrame >= 0 && past < HD_LIPS_EASE_FRAMES &&
				HDModelLipsOpen(slot, false, HD_LIPS_TRACK_LOOP) > 0)
			{
				kind = FACE_LOOP;
				*track = HD_LIPS_TRACK_LOOP;
				*frame = HDModelLipsLoopKey(g_face.deadFrame, past);
				shown = g_face.deadFrame;
			}
		}

		if (gLogEnabled && (!g_face.seen || kind != g_face.kind))
			Log("LIPLOG f=%d lara face=%s was=%s slot=%d frame=%d why=%s",
				(int)now, face_word(kind), face_word(g_face.kind), slot, shown, why);

		/* A blink: once a tick, on the frame it starts. */
		if (gLogEnabled && kind == FACE_LOOP && !dead &&
			(!g_face.blinkSeen || g_face.blinkAt != now))
		{
			const float lid = HDModelLipsBlink(slot, false, shown);

			g_face.blinkSeen = true;
			g_face.blinkAt = now;

			if (lid >= 0.0f)
				Log("LIPLOG f=%d lara blink frame=%d lid=%.1f", (int)now, shown,
					(double)lid);
		}

		if (kind == FACE_STATIC)
			g_face.stillWhy = why;

		g_face.loopFrame = kind == FACE_LOOP && !dead ? shown : -1;
		g_face.kind = kind;
		g_face.seen = true;
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

	/* The braid's six matrices.
	 *
	 * TR5 draws the braid out of six meshes but only three of them stand on
	 * their own: game\hair.cpp's DrawHair puts meshes 1, 3 and 5 at
	 * hairs[i][1], [3] and [5] with phd_TranslateAbs / RotY / RotX, and draws
	 * 0, 2 and 4 as skinned connectors out of stashed vertices of their
	 * neighbours. HAIR_OG.TRM has six bones, one per link, so the three
	 * connectors need a matrix the classic path never builds.
	 *
	 * They are not missing, only unused: the simulation writes a position AND
	 * an angle pair for every one of hairs[0][0..5] (HairControl's loop sets
	 * (hair-1)->pos.y_rot / x_rot for i = 1..6, which is links 0 to 5). So
	 * the connectors are built the same way the rigid ones are, which is what
	 * TROne does for all six.
	 *
	 * phd_TranslateAbs only replaces the translation column and leaves the
	 * rotation where it is, so each link has to start from the base of the
	 * matrix stack - the world-to-view matrix - which is what DrawHair gets
	 * for free by being called with the stack unwound and what this has to
	 * put there on purpose, because the seam sits inside DrawLara's own push.
	 *
	 * The base and not aW2VMx, which is nearly the same matrix and not quite:
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

			/* The roll. About the bone's own axis and therefore free: the +z the two rotations above just built does
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

		/* braidcheck - the proof that the three rebuilt connectors are in the
		 * right place, the only part of this seam that could be silently
		 * wrong: the classic path never builds a matrix for links 0, 2 and 4,
		 * so nothing in the game has ever used their angles.
		 *
		 * The identity comes from HairControl itself (game\hair.cpp): it
		 * derives every link's position by putting the link before it at its
		 * own position and angles and stepping one bone offset along that
		 * frame. So the frame built here for link n, stepped by the HAIR mesh
		 * tree's own offset, has to land on link n+1's own position - to the
		 * long/float rounding of the two roads and no further. A swapped
		 * RotY/RotX, a dropped sign or a stale angle all break it, and none
		 * of them is visible in a still picture of a braid hanging straight
		 * down.
		 *
		 * Measured in classic units in view space, which is what the matrices
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
		 * out of: a box in the torso frame. The root is the near one for an HD
		 * Lara - HAIRLOG in game\hair.cpp prints the one the frame really
		 * used, every frame, and it must not alternate. */
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
	 * every other one, built in the same pass as the level's other models
	 * (HDModelLevelBegin), each with a vertex buffer of its own; a switch of
	 * her own would only be a second way to end up with half a picture. */
	if (!tomb5.hd_objects)
		return 0;

	g_level.outfit = outfit_of_level(&why);
	g_why = why;

	if (g_level.outfit == HD_LARA_OUTFIT_NONE)
		return 0;

	/* The body's bind pose, out of the slot whose mesh tree the matrices were
	 * built from - game\delstuff.cpp walks bones[objects[LARA].bone_index],
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

	// A new level, a new braid, a new held roll reference.
	HDLaraBraidReset();

	if (objects[HAIR].loaded && objects[HAIR].nmeshes >= HD_LARA_HAIR_NODES)
	{
		HDObjectBindPose(HAIR, g_hairBind, HD_LARA_HAIR_NODES);
		g_level.hairBind = g_hairBind;
		g_level.hairNodes = HD_LARA_HAIR_NODES;
	}

	/* Which of Lara's slots this level has. A weapon whose slot is not loaded
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

		/* A hand candidate needs enough meshes to have an LM_LHAND, or the
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

	/* Which entry of the wardrobe this is, in one line: the key, the file,
	 * where the file came from, the five flags, the two materials, the skin
	 * pages, the footsteps and whose head it wears - each with the tier that
	 * decided it. An unexpected picture is then one grep away from the table
	 * row, the outfit.txt line or the list block that made it. */
	{
		const int wear = HDLaraWearOfOutfit(g_level.outfit);
		const int at = wear >= 0 ? wear
			: HDWardrobeFindFile(HDLaraOutfitModel(g_level.outfit),
				HD_WARDROBE_SOURCE_46);
		const HDWardrobeEntry* e = HDWardrobeAt(at);

		/* The from-words come out of hd_wardrobe, where the tiers are
		 * defined, so a new tier cannot be forgotten here and no local array
		 * can be read past its end. */
		if (e)
			Log("HDLOG level=%s wardrobe key=%s name=%s file=%s source=%s "
				"mod=%d hands=%d braid=%d holsters=%d gloves=%d rig=%d (%s) "
				"sheen=%.2f/%.0f (%s) skinsheen=%.2f/%.0f (%s) "
				"skin=%d,%d,%d (%s) feet=%d (%s) head=%s (%s) pages=%s",
				stem, e->key, e->name, e->file,
				e->source == HD_WARDROBE_SOURCE_46 ? "46"
					: e->source == HD_WARDROBE_SOURCE_13 ? "13" : "mod",
				e->mod, (int)e->hands, (int)e->braid, (int)e->holsters,
				(int)e->gloves, (int)e->wearsRig, HDWardrobeFromTag(e->fromFlags),
				e->sheen, e->sheenPower, HDWardrobeFromTag(e->fromSheen),
				e->skinSheen, e->skinSheenPower, HDWardrobeFromTag(e->fromSkinSheen),
				e->skin[0], e->skin[1], e->skin[2], HDWardrobeFromTag(e->fromSkin),
				e->feet, HDWardrobeFromTag(e->fromFeet),
				HDWardrobeHeadWord(e->head), HDWardrobeFromTag(e->fromHead),
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
	 * One line per part the plan asked for, at most HD_LARA_PART_COUNT of
	 * them (twenty-three), and only with -log. */
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

		/* The sheen goes in the line as well: the numbers are a material
		 * table and not a measurement, so a picture has to be traceable to a
		 * number without opening the source. */
		float sheen = 0.0f, sheenPower = 1.0f;
		float held = 0.0f, heldPower = 1.0f;

		HDLaraPartSheen(part, &sheen, &sheenPower);
		/* And what is in the hand, on its own pages:
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
	/* No Direct3D call here: hd_object.cpp's own level end, which calls
	 * this, runs on the load thread. The models themselves are released by
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
	face_reset();
}

bool HDLaraDrawActive()
{
	return g_active && tomb5.hd_objects && HDLaraActive();
}

int HDLaraDrawOutfit()
{
	return g_level.outfit;
}

/* The drawn braid's roll. The reasoning and the measured values are at
 * HD_LARA_BRAID_ROLL_NEAR in hd_lara.h; this is the arithmetic.
 *
 * One reference for all six bones, so that two neighbours of a rigid plait
 * can never twist against each other - which the classic path hides with
 * its skinned connectors and the HD one cannot. The reference is the azimuth
 * of the whole braid, root to tip: that vector is six bones long where the
 * yaw's own is one, so its horizontal part is ten to thirty times better
 * conditioned.
 *
 * And it is held while even that is noise. A braid hanging straight down has
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
	/* What the frame that has just ended put on the list. Read before the
	 * counter is cleared, because the binocular line below is a statement
	 * about a finished frame and not about the one starting now. */
	const int wasPushed = g_frame_parts;

	g_frame_parts = 0;

	/* The first person and binocular log lines. They belong here because
	 * this is the one place in the HD path that runs on every frame whether
	 * Lara is drawn or not - which is exactly the state the second reports.
	 *
	 * Both are written on the edge and not per frame: a line a second for
	 * hours would bury everything around it. They show when the view came
	 * on, with which cut, and that the binoculars keep the whole composite
	 * off the list.
	 *
	 * Lara is not skipped anywhere in this file when the binoculars are up -
	 * the engine never gets that far. BinocularCamera (game\camera.cpp) sets
	 * lara_item->mesh_bits = 0 and game\draw.cpp asks
	 * `if (lara_item->mesh_bits && !SCNoDrawLara)` before it calls DrawLara
	 * at all, so HDLaraDrawFrame does not run and nothing is pushed. The
	 * count is therefore taken one frame after the scope came up, when the
	 * frame it describes was wholly inside the binocular view - on the edge
	 * frame itself she was still drawn normally. */
	if (gLogEnabled)
	{
		const bool now = FirstPersonActive();

		if (!g_fp_seen || now != g_fp_was)
		{
			g_fp_seen = true;
			g_fp_was = now;
			/* One number: the cut is a bone weight - how much of the head
			 * node a fragment has to carry to be cut. */
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
		to_row_major(&lara_matrices[lara_lmx_of_node[node] * indices_count],
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
	 * ambient when she is not. The wet answer is the other branch of the same
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

	/* The per node ambient, which Lara really has and an object does not: a
	 * body half in the water is lit by two rooms at once, and the classic
	 * path switches between them per node through bLaraUnderWater. The index
	 * is the LMX one, because LaraNodeUnderwater is filled in LMX order
	 * (specific\drawlara.cpp, SetLaraUnderwaterNodes). */
	for (int node = 0; node < HD_LARA_NODES; node++)
	{
		const bool under = bLaraInWater != 0 &&
			LaraNodeUnderwater[lara_lmx_of_node[node]] != 0;
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
		/* Lara's mesh_bits are not an item's. The skin's fifteen bits sit in
		 * the high half and are counted backwards in LMX order - DrawLara__1
		 * asks `mesh_bits >> 16 & 1 << (15 - i)` with i the LMX index, so the
		 * head is bit 23 and not bit 14. The low half belongs to the fourteen
		 * seam pieces, which the HD body does not have. */
		mask = 0;

		for (int node = 0; node < HD_LARA_NODES; node++)
			if (lara_item->mesh_bits >> 0x10 & 1 << (15 - lara_lmx_of_node[node]))
				mask |= 1u << node;
	}

	/* First person. The classic path leaves the head mesh out and does not
	 * call DrawHair, because both sit exactly where the camera does and blow
	 * through the picture. The HD composite does the same for three things:
	 * the body's head node, the separate face, and the braid.
	 *
	 * What it does not have to copy is the reason the classic path hides the
	 * head this way rather than through mesh_bits - the skin's neighbours
	 * pull their rim points out of the head mesh's stash and lose them with
	 * it ("the third arm"). An HD model has no stash and no seam pieces: the
	 * head node is one range of triangles and dropping it drops nothing
	 * else. */
	const bool firstPerson = FirstPersonActive();

	if (firstPerson)
		mask &= ~(1u << HD_LARA_HEAD_NODE);

	/* A body that has borrowed TR5's head draws none of its own.
	 *
	 * The same bit, taken for a different reason and settled at level build:
	 * an outfit of the Tomb Raider I-III bundle carries headgear on the head
	 * node and no hair, so the head rule drops that node and puts node 14 of
	 * OUTFIT_CLASSIC in its place (HD_LARA_PART_HEAD below). It is ~0u for
	 * every body that keeps its own - every body of the IV-VI bundle - so
	 * those draw exactly as without the rule.
	 *
	 * It is a mask of its own and not part of `mask`, because `mask` is what
	 * the frame says - mesh_bits and first person - and every other part of
	 * the composite uses that. The borrowed head is drawn with `mask` like
	 * the rest, so a head shot away by mesh_bits takes it too. */
	const unsigned int bodyMask = mask & HDLaraBodyMask();

	/* And the neck under it. Masking the head node is where the classic path
	 * stops, and for a classic body that is enough: LM_HEAD is head and neck
	 * in one mesh. An HD body is one skinned surface and the stump belongs to
	 * the torso node, so it stays - a collar of triangles 18.6 units from the
	 * eye when a wall holds the camera at the joint, inside the near plane
	 * and filling the picture. The diving suit puts its whole helmet on the
	 * torso node the same way.
	 *
	 * Neither can be masked off without taking her chest with it, so they are
	 * cut in the shader instead: every fragment whose head weight reaches
	 * HD_LARA_HEAD_CUT_WEIGHT (hd_lara.h carries the argument and the
	 * measurement over the eight wardrobe bodies). A bone weight is written
	 * in the .TRM, is the same number in every pose and at every camera
	 * angle, and the shader already has it for the skinning - so nothing is
	 * built here per frame, and unlike a plane fixed to the head it cannot
	 * tilt across her chest.
	 *
	 * The body alone carries it - the hands, the holsters, the weapon on her
	 * back and the lid that closes the hole are their own models and are
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

	/* Where the eye stands in what is left of her. The cut is a fact about
	 * the file and cannot vary with the pose; where the eye ends up can, and
	 * a bind pose cannot show it. So this line measures it, once a second of
	 * game time and with -log only:
	 *
	 *   cut=    whether the cut is on
	 *   w=      the threshold in force, node= the node it is weighed on
	 *   head=   the head joint, torso= the torso joint, min= the nearest
	 *           joint of all fifteen and which one - all distances from the
	 *           eye in view space. Under the renderer's near plane at 20
	 *           (InitWindow, gamemain.cpp) the lid is clipped by the near
	 *           plane and not by the cut.
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

	/* One sided in the frames the cut runs in. The HD path draws every model
	 * double sided; cut a hole in a closed body and what shows through it is
	 * the body's own inside - her chest seen from within, her face from
	 * behind. The classic engine never shows that because it culls back faces
	 * in software before the card sees them, and no classic camera stands
	 * inside an object anyway. This one does. hd_model.h carries the
	 * measurement that says the wardrobe's winding is consistent enough to
	 * cull on. */
	/* And the other side in a mirror. The reflected draw flips the winding:
	 * game\mirror.cpp negates two of the three angles, adds half a turn to
	 * the third and then calls CalcLaraMatrices(2), which scales the matrix
	 * by -1 on all three axes (game\delstuff.cpp, ScaleCurrentMatrix). Three
	 * sign changes are a determinant of -1, and the composition works out to
	 * exactly the reflection in the mirror's z plane - which is why the
	 * classic engine turns its own software test round with SetCullCW /
	 * SetCullCCW for the length of that draw.
	 *
	 * Everything else the HD path draws is double sided. First person is the
	 * one frame that culls, and there a reflection would come out inside-out,
	 * so the mirrored draw culls the other side, as mirror.cpp does. */
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
	 * test sorts it whatever order it arrives in. Only a see-through pass
	 * would depend on the order.
	 *
	 * If the body cannot go on the list, nothing else may either - half a
	 * Lara is worse than the classic one. It cannot happen from here: the
	 * frame list is empty when DrawLara runs, because game\draw.cpp calls it
	 * before the PrintObjects loop. The counter catches it if that changes. */
	int drawn = 0;

	/* The sheen, per part. hd_lara.cpp's table says what a part is made of;
	 * the body's answer follows the outfit, because
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

	/* TR5's own head, for a body that has none.
	 *
	 * Node 14 of the install's OUTFIT_CLASSIC - her hair, and two triangles
	 * of neck under it - drawn here and not with the pieces, because it
	 * belongs to the body: it stands where the body's own head node would
	 * have stood, wears the same node mask, the same light and the same
	 * matrices, and the face goes over it exactly as it goes over a IV-VI
	 * body's own head.
	 *
	 * Before the face, which is where the body's head node is drawn, so that
	 * the hair's see-through pass reaches the card in the order it reaches it
	 * when the head is part of the body. The wardrobe's only alpha is this.
	 *
	 * The same cut as the body, so a first person frame takes it away with
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

	/* No inward (back face) draw of the body: the lid below closes the hole.
	 * A second, inward pass over the same model with the same cut adds
	 * nothing the lid does not close (forty pictures over five outfits, three
	 * pitches, both eyes, head turned and torso bent are identical to the
	 * pixel with and without it), costs a second pass over 27,000 triangles
	 * every first person frame, and loses the depth tie on single-layer
	 * geometry - a strap, a shoulder piece or a flap has both sides at the
	 * same depth - which shows as torn edges and missing straps.
	 *
	 * The throat measurement (measure_neck_skin, hd_lara.cpp) stays in the
	 * build log: it tells a lid that came out on a coat from one that came
	 * out wrong. */

	/* The lid over the hole: a disc at the cut's edge that closes the neck.
	 *
	 *   No cut. It lies exactly on the cut's edge, and a fragment exactly on
	 *   the threshold is at the mercy of the last bit of a float; handing it
	 *   the cut would flicker it away.
	 *
	 *   No cull. The camera looks down on it from the head side, but a mirror
	 *   room turns the winding round and a lid one can see through from one
	 *   side is a hole. It is one triangle fan; drawing both sides is free.
	 *
	 *   Every node. Its rim vertices are copies of the body's own, so they are
	 *   skinned to whatever those are - the torso, and a little of the head.
	 *   A mask of its own would only repeat that set.
	 *
	 *   The skin's sheen, because it is the top of her neck. Its texture is
	 *   the rim's own, page and coordinates both, taken at level build.
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

	/* Her lips, in a cutscene she acts in: the cut's track at the frame on
	 * screen, exactly as an actor's (game\deltapak.cpp, DrawCutSeqActors).
	 * Actor 0 of a cut is Lara, and NO_ITEM when she is not in it
	 * (frigup_lara leaves her pose alone then); the credits play no audio.
	 * -1 everywhere else. */
	int lipsTrack = -1;
	int lipsFrame = -1;

	if (GLOBAL_playing_cutseq && GLOBAL_cutme && !bDoCredits &&
		GLOBAL_cutme->actor_data[0].objslot != NO_ITEM &&
		GLOBAL_cutme->audio_track >= 0)
	{
		lipsTrack = (int)GLOBAL_cutme->audio_track;
		lipsFrame = GLOBAL_cutseq_frame > 0 ? (int)GLOBAL_cutseq_frame - 1 : 0;
	}

	/* Between her lines: the cut's track where her face has its file, else
	 * the firing face or the loop - see choose_face. */
	int faceTrack = -1;
	int faceFrame = -1;

	choose_face(HDLaraPartSlot(HD_LARA_PART_FACE), firstPerson,
		(mask >> HD_LARA_HEAD_NODE & 1) != 0, lipsTrack, lipsFrame, &faceTrack,
		&faceFrame);

	if (!firstPerson && HDObjectPush(HDLaraPartSlot(HD_LARA_PART_FACE),
		matrices, HD_LARA_NODES, nodeLight, mask, lights, lightCount, sunScale,
		0, HD_MODEL_CULL_NONE, sheen, sheenPower, faceTrack, faceFrame))
		drawn++;

	/* The pouches, under whatever goes in them.
	 *
	 * Before the pieces and not with them, because it is not a piece: the rig
	 * belongs to the outfit and not to the weapon. It is on Lara whether she
	 * has her guns in her hands or on her thighs, exactly as the built-in rig
	 * of OUTFIT_CLASSIC is - the pouches do not vanish when she draws.
	 *
	 * HDLaraHasPart is the whole guard: the part exists only for an outfit
	 * the wardrobe marks with wearsRig (among the table rows only OUTFIT_VCI),
	 * and for anybody else HDLaraPartSlot
	 * names a slot no model was built for, which HDObjectPush turns down at
	 * its first line.
	 *
	 * The node mask is the part's own coverage - nodes 0, 1 and 4, the belt
	 * and the two thighs - and the frame's, so first person takes it off with
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

		/* And the install's own hands under a mod's gun.
		 *
		 * Same matrices, same node mask, same light, same sheen: it is the
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

	/* The braid, on its own six link chain and its own matrices. It is lit
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
		// lips= is the track her face speaks in this frame, loop or fire
		// for the loop between lines and the firing face, or - for none.
		char lipsWord[16] = "-";

		if (faceTrack == HD_LIPS_TRACK_LOOP)
			sprintf(lipsWord, "loop");
		else if (faceTrack == HD_LIPS_TRACK_FIRE)
			sprintf(lipsWord, "fire");
		else if (faceTrack >= 0 && HDModelLipsFrames(HDLaraPartSlot(HD_LARA_PART_FACE), false) > 0)
			sprintf(lipsWord, "%d", faceTrack);

		g_frame_logged = true;
		// interp= is what this frame was drawn at: 1.00 for a frame that
		// landed on a logic tick or a build at fps_mode 0, 0.50 for one drawn
		// halfway between two ticks. It shows the HD seam saw the
		// interpolated root; the HD path follows the engine's own matrix
		// stack and needs no interpolation of its own.
		Log("HDLOG frame lara parts=%d(%d this frame) interp=%.2f outfit=%s right=%s left=%s "
			"holsters=%s/%s back=%s braid=%d head=%s/%d mask=%08x body=%08x mirror=%d "
			"firstperson=%d headcut=%d lights=%d dropped=%d speechhead=%s lips=%s",
			drawn, g_frame_parts, InterpFrameAlpha(), HDLaraOutfitModel(HDLaraOutfitNow())
				? HDLaraOutfitModel(HDLaraOutfitNow()) : "-",
			HDSlotName(now.rightHandSlot) ? HDSlotName(now.rightHandSlot) : "-",
			HDSlotName(now.leftHandSlot) ? HDSlotName(now.leftHandSlot) : "-",
			HDSlotName(now.leftHolsterSlot) ? HDSlotName(now.leftHolsterSlot) : "-",
			HDSlotName(now.rightHolsterSlot) ? HDSlotName(now.rightHolsterSlot) : "-",
			HDSlotName(now.backGunSlot) ? HDSlotName(now.backGunSlot) : "-",
			(int)HDLaraHasPart(HD_LARA_PART_BRAID),
			/* The head rule in force and whether TR5's head is really on
			 * the card for it - "tr5/0" is a body told to drop its own
			 * head and given no donor, which is the one case where the line
			 * and the picture would otherwise be a puzzle. */
			HDWardrobeHeadWord(HDLaraHeadMode()),
			(int)HDLaraHasPart(HD_LARA_PART_HEAD),
			mask, bodyMask, (int)mirror,
			(int)firstPerson, (int)cutting, lightCount, g_dropped,
			HDSlotName(speech_head_slot()) ? HDSlotName(speech_head_slot()) : "-",
			lipsWord);
	}
}

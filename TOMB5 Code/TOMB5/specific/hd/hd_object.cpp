/*
 * Modified 2026 by ChrisChanSan.
 *
 * The routing rules are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3) - see hd_object.h for the list, function by function. The seam under
 * them, the skeleton test and the light conversion are new. So is the
 * inventory ring at the bottom of this file, except for one idea that is
 * TROne's: that the ring's OFF-CENTRE projection can be folded into the ITEM
 * for the length of one draw (games\tr5\glue\hostglue_2d5.cpp:502-506), which
 * is what lets the HD path keep one projection for the whole frame. TROne
 * folds it into the item's matrix as a shear; this engine folds it into the
 * projection, because the shader takes the vertex NORMAL through those
 * matrices - the whole argument stands over HDInvNode. The arithmetic was
 * derived from TR5's own projection line. See THIRD-PARTY.md.
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
#include "hd_object.h"
#include "hd_model.h"
#include "hd_laradraw.h"
#include "hd_bind.h"
#include "hd_install.h"
#include "hd_markers.h"			// the .hdm sidecar - the donor slots (S7-2)
#include "hd_mods.h"			// HDModsScan - the mods folder (#153, M1)
#include "hd_pagecache.h"		// HDPageCacheAltRoot, for a guest's own pages
#include "hd_render.h"
#include "hd_level.h"
#include "../function_stubs.h"
#include "../profile.h"			// PROFLOG phases (#148)
#include "../d3dmatrix.h"
#include "../3dmath.h"
#include "../lighting.h"
#include "../winmain.h"
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// items, bones, room
#include "../../game/draw.h"		// InterpolateMatrix
#include "../../game/effect2.h"		// dynamics
#include "../../game/hitman.h"		// hitman_nodes - the cyborg's panel order
#include "../../game/newinv2.h"		// InventoryActive - the sun is doubled in there

#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

/* A classic TR5 moveable has at most 32 meshes and the joint palette in the
 * shader is 32 registers wide, so 32 is the number everywhere. */
#define HD_OBJ_NODES	32

/* How many HD items one frame can hold. draw_rooms holds as many rooms as a
 * level may have (256 since fix round T5) and every one of them can hold
 * items, so this is not a bound anybody can reason about from the level data
 * - it is a fixed array with a counter, and the counter goes into the log if
 * it ever bites. 128 items is 1.1 MiB of static, which is the price of a
 * frame path that cannot fail. */
#define HD_OBJ_ITEMS	128

/* PSEUDO SLOTS FOR THE ALTERNATIVE MODELS (#123, fix round B, B3). Chosen
 * clear of the engine's 471 slots and of Lara's wardrobe block
 * (HD_LARA_SLOT_BASE, 0x5A00), because hd_model keeps its models in one map
 * keyed by slot number and two of them must never collide. */
#define HD_SWAP_SLOT_BASE	0x6000
#define HD_SWAP_NODE_BASE	0x8000

namespace
{
	/* WHAT ONE SLOT IS ALLOWED TO DO, decided once at level build. */
	struct Verdict
	{
		unsigned char hd;	// 1 = this slot's items draw HD
		unsigned char baked;	// 1 = the art already has it, draw NOTHING
		unsigned char head;	// 1 = the slot has its own <NAME>_HEAD.TRM
		const char* why;	// why not, for the log
		/* WHICH RULE NAMED THE MODEL (#123, fix round P): the very string the
		 * HDLOG bind line printed at level build, kept so that the ring's own
		 * line can say it again without asking the disk a second time. A
		 * static literal out of via_label() or "donor". */
		const char* via;
		int nodes;		// the classic mesh tree's node count
		int maxBone;		// the highest node the .TRM names
		float drift;		// the bind drift, mean over the covered nodes
		float worst;
		unsigned int covered;
	};

	Verdict g_slot[NUMBER_OBJECTS];
	bool g_active = false;
	char g_stem[64] = { 0 };

	int g_hd_slots = 0;
	int g_classic_slots = 0;

	/* One item, waiting for HDObjectFrameDraw. */
	struct FrameItem
	{
		int slot;
		int drawSlot;			// the alternative model, or == slot (B3)
		bool head;			// this slot has a separate HD face (B7)
		int nodes;
		unsigned int mask;		// item->mesh_bits
		float matrix[HD_OBJ_NODES * 16];
		float light[HD_OBJ_NODES * 3];
		HDObjLight lights[HD_MAX_OBJ_LIGHTS];
		int lightCount;
		float sunScale;
		HDModelNeckCut cut;	// stage S8 / fix round J: Lara's body alone
		int cull;		// and the one-sided draw that goes with it
		/* #123 fix round D (D2): the sheen. 0 for an item - the specular
		 * term is Lara's alone for now. */
		float specStrength;
		float specPower;
		/* #123 group M: the room this thing stands in, so that it is drawn
		 * inside the same portal window its room is - `item->room_number`
		 * for an item, and -1 for anything pushed without one, which is
		 * Lara's whole composite (she is in the camera's room and the
		 * camera's room is the whole screen). */
		int room;
		/* WHICH CAMERA THIS WAS COLLECTED WITH (fix round R). ViewSerial at
		 * the moment the matrices were taken; see the note over it in
		 * specific\3dmath.cpp. Read only by the log. */
		long view;
		/* THE INVENTORY RING'S OWN SCREEN CENTRE (fix round P, P2), as the
		 * two numbers HDModelOffCentre wants: (X - centreX) / persp and the
		 * same in y. Zero for everything in the world, which is every item
		 * outside the inventory, and then not a register is written.
		 *
		 * It rides on the ITEM and not on the matrices on purpose - see the
		 * note over HDModelOffCentre in hd_model.h. Fix round P put the same
		 * offset into the node matrices as a shear, which moved the item's
		 * NORMALS with it and lit the ring by screen position. */
		float offCentreX;
		float offCentreY;
	};

	FrameItem g_frame[HD_OBJ_ITEMS];
	int g_count = 0;
	int g_dropped = 0;

	/* The item the engine is walking right now. */
	bool g_open = false;
	int g_open_slot = -1;
	int g_open_swap = -1;		// the alternative model this item wants (B3)
	int g_open_nodes = 0;
	// #123 group M: the room of the item being walked, kept from
	// HDObjectBegin (which has the ITEM_INFO) to HDObjectEnd (which has not).
	int g_open_room = -1;
	unsigned int g_open_mask = 0;
	float g_raw[HD_OBJ_NODES * 12];		// the engine's own 3x4, per node
	unsigned int g_seen = 0;

	/* Per FRAME, emptied by HDObjectFrameReset - they go into a line that
	 * says "frame", so they have to mean one. */
	int g_items_classic = 0;
	int g_swap_ignored = 0;
	int g_swap_drawn = 0;
	int g_mask_used = 0;
	int g_items_baked = 0;

	/* ------------------------------------------- THE INVENTORY RING (P) -- */

	/* The item being walked came out of newinv2.cpp and carries the ring's
	 * own screen centre. Kept apart from g_open so that an ordinary item can
	 * never pick up a ring's centre and a ring item can never pick up a
	 * room. */
	bool g_ring_open = false;
	/* THE THREE THE PICKUP DISPLAY BORROWS (P2). The off-centre pair and
	 * the light colour are a property of the CALL rather than of the ring,
	 * and the two callers cannot overlap: every place that sets
	 * InventoryActive puts a mono screen up first, and S_OutputPolyList
	 * blocks the display's whole block while one is up
	 * (specific\output.cpp). One pair, not two that have to be kept in
	 * step. */
	float g_ring_kx = 0.0f;
	float g_ring_ky = 0.0f;
	long g_ring_color = 0;

	/* Per FRAME. `g_ring_frame` is read by hd_level.cpp (the fog bulbs); the
	 * two counters and the little list are the log line, which is written
	 * once per OPENING of the ring rather than once a frame - the ring is
	 * thirty frames a second of the same six objects. */
	bool g_ring_frame = false;
	int g_ring_open_row = -1;	// the open item's row in g_ring_log
	int g_ring_hd = 0;
	int g_ring_classic = 0;
	bool g_ring_logged = false;

	struct RingLogItem
	{
		int slot;
		unsigned int mask;
		bool hd;
		/* THE ITEM'S DEPTH: node 0's view z, which is the push down +z the
		 * ring gives the item - INVOBJ::scale1, plus the item's own root
		 * offset out of the animation frame.
		 *
		 * scale1 IS in this source tree: the table in game\newinv2.cpp
		 * (inventry_objects_list) holds the built-in value - 0x3E8 for the
		 * pistols, 0x898 for the passport pages - and init_new_inventry moves
		 * the binoculars in code. But it is not fixed: LoadGameflow
		 * (game\gameflow.cpp) overwrites objname, yoff, scale1 and the three
		 * rotations out of SCRIPT.DAT for every slot the level names, which
		 * is every key, puzzle, pickup and examine item. So the number is per
		 * LEVEL, and the line says it rather than the manual guessing at it.
		 *
		 * It also decides whether a ring item stands in front of the frozen
		 * backdrop, which S_DrawTile puts at sz 0.995 - the far plane, since
		 * the engine runs with znear 20 and zfar 20480
		 * (specific\gamemain.cpp, InitWindow) and sz is f_a - f_b/z. */
		float z;
	};

	/* Six on the ring, three in the ammo selector, one in the keypad and one
	 * in the examine screen - and the two rings can stand at once. Sixteen is
	 * four times what a frame has ever held. */
	RingLogItem g_ring_log[16];
	int g_ring_log_count = 0;

	/* ------------------------------------- THE PICKUP DISPLAY (P2) -- */

	/* THE SECOND CALLER OF DrawInventoryItemMe, and the whole of fix round P2.
	 *
	 * S_DrawPickup (specific\output.cpp) walks the very same mesh tree through
	 * the very same four call sites; what is different is WHEN. It runs inside
	 * S_OutputPolyList, after HDFrameDraw has drawn this frame's HD list and
	 * after HDFrameDrawGlow has finished with it, so an item pushed onto that
	 * list would suppress its own classic polygons and be drawn by nobody -
	 * which is what fix round P measured and why it left the display classic.
	 *
	 * P2 draws it on the spot instead: the node walk collects the matrices
	 * exactly as the ring's does, and HDInvEnd opens a bracket of its own
	 * (hd_level.cpp, HDFramePickupBegin), draws the one object and closes it
	 * again. Nothing reaches the frame list and g_count is not touched, so the
	 * list that HDObjectFrameReset empties on the next picture is still the
	 * world's alone.
	 *
	 * `g_pickup_open` is the mode the walk is in. The four numbers below it are
	 * the display's OWN projection, read at HDInvBegin out of f_persp /
	 * f_centerx / f_centery / f_a / f_b - not out of HDFrameCaptureProjection,
	 * which took the field of view the WORLD was collected with and can be a
	 * binocular zoom older than the display by a whole frame. */
	bool g_pickup_open = false;
	float g_pickup_persp = 0.0f;
	float g_pickup_centrex = 0.0f;
	float g_pickup_centrey = 0.0f;
	float g_pickup_za = 0.0f;
	float g_pickup_zb = 0.0f;

	/* The item is built here and drawn from here, never on g_frame: it lives
	 * for the length of one HDInvEnd. One item's worth of matrices, which is
	 * what the frame list holds a hundred of. */
	FrameItem g_pickup_item;

	/* ONE LINE PER SLOT PER LEVEL. The display can show the same medipack two
	 * hundred times in a level and the report is the same line every time, so
	 * the slot is struck off the first time it is drawn. Cleared where the
	 * level's own line is written, in HDObjectLevelBuild. */
	bool g_pickup_logged[NUMBER_OBJECTS];

	/* THE ENGINE'S 3x4 INTO THE ROW-MAJOR 4x4 THE SHADER WANTS, once per node
	 * of the item just walked - the same conversion SetD3DMatrix does for the
	 * fixed-function pipe, and the one stage S1-2 proved for the rooms.
	 *
	 * A node the walk never reached keeps the last one that was: TROne's
	 * fill_unseen_joints (game_renderer.cpp:1391), and the reason is that a
	 * slightly wrong limb beats a limb collapsed onto the camera. It cannot
	 * happen from DrawAnimatingItem or DrawInventoryItemMe, both of which walk
	 * every node - but HDObjectNode and HDInvNode are public calls and a zero
	 * matrix is not a picture, it is a divide by zero.
	 *
	 * In one place because the ring needs it as much as the world does (fix
	 * round P), and two copies of a matrix convention are one too many. */
	void store_matrices(FrameItem& entry)
	{
		int last = 0;

		for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
		{
			if (g_seen & (1u << node))
				last = node;

			const float* in = g_raw + last * 12;
			float* out = entry.matrix + node * 16;

			out[0] = in[M00];  out[1] = in[M10];  out[2] = in[M20];  out[3] = 0.0f;
			out[4] = in[M01];  out[5] = in[M11];  out[6] = in[M21];  out[7] = 0.0f;
			out[8] = in[M02];  out[9] = in[M12];  out[10] = in[M22]; out[11] = 0.0f;
			out[12] = in[M03]; out[13] = in[M13]; out[14] = in[M23]; out[15] = 1.0f;
		}
	}

	/* THE BAKED SLOTS (#123, fix round B, B1). Set per level and read in
	 * HDObjectBegin BEFORE the object path's own guard, because an object
	 * that is already in the room art is drawn twice whether hd_objects is on
	 * or off - the second copy comes out of the .TRG either way. */
	bool g_baked_any = false;
	int g_baked_slots = 0;

	/* ------------------------------------------------ THE MESH SWAPS (B3) --
	 *
	 * Stage S3-2 ignored them, on the argument that a .TRM is one whole model
	 * and carries no second geometry for a node. That argument was wrong: the
	 * remaster ships whole ALTERNATIVE models, and which they are was
	 * measured over 5\ITEM against the engine's own swap code.
	 *
	 *   MAFIA2_SWAP.TRM   MAFIA2.TRM has his pistols in the shoulder holsters
	 *                     and empty fists - which is his INITIAL state,
	 *                     meshswap_meshbits 0x2400 (game\mafia2.cpp:37, the
	 *                     hands swapped out of MESHSWAP2). When he draws them
	 *                     the engine writes 0x80 instead, the TORSO swap, and
	 *                     MAFIA2_SWAP.TRM is the same man with the guns in
	 *                     his hands and the holsters empty. Two pictures,
	 *                     one bit. This is the user's screenshot: he aims
	 *                     with a bare fist.
	 *   HITMAN_<n+1>.TRM  the cyborg's ELEVEN damage stages, cumulative. The
	 *                     file numbers are 5,6,7,8,11,12,13,14,15,16,20 and
	 *                     the panel nodes are 4,5,6,7,10,11,12,13,14,15,19 -
	 *                     the same eleven, off by one, and the same eleven
	 *                     game\hitman.cpp opens. Their vertex counts rise
	 *                     along the OPENING ORDER (16,15,14,7,6,13,8,5,11,
	 *                     12,20 -> 4115..5548 against the closed body's
	 *                     4035), which is what "cumulative" means: the right
	 *                     file is the one for the panel opened LAST.
	 *
	 * Every other swap in the game is a soldier's hands out of MESHSWAP1
	 * (setup.cpp), and there the remaster has no second file because it does
	 * not need one - MAFIA.TRM already holds the pistol in the hand, and so
	 * do SWAT, SWAT_PLUS and the guards. Those keep stage S3-2's answer.
	 *
	 * The table is keyed on the MODEL NAME, not on the slot: the randomizer's
	 * MESHSWAP_CROSS_HITMAN and CYBORG both bind HITMAN.TRM, and a donor slot
	 * carries its donor's model into a slot with any name at all. */
	enum SwapKind
	{
		SWAP_NONE = 0,
		SWAP_WHOLE,		// one alternative model, chosen by a bit
		SWAP_HITMAN		// the cyborg's eleven stages
	};

	struct SwapRule
	{
		const char* model;
		SwapKind kind;
		unsigned int bits;	// SWAP_WHOLE: the bit that picks the model
		const char* suffix;
	};

	const SwapRule kSwapRules[] =
	{
		{ "MAFIA2", SWAP_WHOLE, 0x80, "_SWAP" },
		{ "HITMAN", SWAP_HITMAN, 0, 0 }
	};

	/* One slot's swap plan: the pseudo slots its alternative models were
	 * loaded into. HD_OBJ_NODES wide because SWAP_HITMAN needs one per panel
	 * node; SWAP_WHOLE uses [0] alone. */
	struct SwapPlanSlot
	{
		int slot;
		SwapKind kind;
		unsigned int bits;
		int model[HD_OBJ_NODES];	// -1 where no file arrived
	};

	/* Eight is four times what the whole game needs: MAFIA2 and CYBORG, plus
	 * the randomizer's cross slots for the same two. */
	SwapPlanSlot g_swap_plan[8];
	int g_swap_plans = 0;
	int g_swap_models = 0;

	const SwapPlanSlot* swap_plan_of(int slot)
	{
		for (int i = 0; i < g_swap_plans; i++)
			if (g_swap_plan[i].slot == slot)
				return &g_swap_plan[i];

		return 0;
	}

	/* WHICH ALTERNATIVE MODEL THIS ITEM WANTS, or -1 for the plain one. */
	int swap_model_for(const SwapPlanSlot* plan, ITEM_INFO* item)
	{
		if (!plan || !item)
			return -1;

		if (plan->kind == SWAP_WHOLE)
			return ((unsigned int)item->meshswap_meshbits & plan->bits)
				? plan->model[0] : -1;

		if (plan->kind == SWAP_HITMAN)
		{
			/* item_flags[0] counts the panels the control has opened, and
			 * hitman_nodes says in which order - so the last one opened is
			 * the stage the remaster drew, and its file is the cumulative
			 * body up to and including it. Read from the item and not from
			 * the bit mask, because the mask alone cannot say which of two
			 * set bits came second. */
			int stage = item->item_flags[0];

			if (stage > HITMAN_PANELS)
				stage = HITMAN_PANELS;

			while (stage > 0)
			{
				const int node = hitman_nodes[stage - 1];

				if (node >= 0 && node < HD_OBJ_NODES && plan->model[node] >= 0)
					return plan->model[node];

				/* A stage whose file did not arrive falls back to the one
				 * before it rather than to the undamaged body. */
				stage--;
			}
		}

		return -1;
	}

	/* Per LEVEL: how often a light had to be turned away at the shader's
	 * twelve. Once is worth knowing, every frame is noise. */
	int g_clipped_lights = 0;
	int g_draw_calls = 0;
	bool g_checked = false;
	bool g_frame_logged = false;

	/* THE THRESHOLD OF THE BIND DRIFT lives in hd_model.h, because the
	 * diagnostic tool has to judge a slot by the same number the game does.
	 * How it was measured is in docs\Engine-Manual.md: ModelProbe -all over
	 * all fourteen randomized levels and their vanilla twins, 700-odd bound
	 * slots. The highest drift of a model that belongs where it stands is
	 * 239 and the lowest of one that does not is 308, and 256 sits in that
	 * gap - nearer the accepted end on purpose, because refusing a good model
	 * costs a classic-looking door and accepting a bad one costs a torn body.
	 *
	 * It is only the SECOND test. The first - does the .TRM name a node the
	 * slot does not have - is a certainty rather than a number, and it is the
	 * one that catches FIREROPE in eleven of the fourteen. */
	const float kDriftLimit = HD_SKELETON_DRIFT_LIMIT;

	/* ------------------------------------------------------ the bind pose -- */

	/* THE SLOT'S BIND POSE, out of the engine's own mesh tree. Four longs a
	 * node after the first - flags, x, y, z - and the flags carry the matrix
	 * stack: bit 0 pops, bit 1 pushes, and the push happens BEFORE the
	 * translation, so what is pushed is the parent's frame. It is
	 * DrawAnimatingItem's walk with every rotation left out, which is what a
	 * .TRM's vertices are measured against.
	 *
	 * The same walk stands in ModelProbe (ReadClassicSlot) against the .TRC
	 * on disk; this one reads the loaded level, so what the game binds is
	 * what the tool measured. */
	/* `tree`, when given, comes back as the PARENT of every node - the node
	 * whose frame this one's offset was added to, and -1 for the root
	 * (#123, fix round B, B4). It falls out of the same walk for nothing: the
	 * frame `current` holds at any moment belongs to exactly one node, and
	 * the push/pop stack carries that number along with the position. A
	 * separate HD face needs it, because its NECK belongs to the node the
	 * head node hangs off. */
	void bind_pose_of(int slot, float* out /* nodes * 3 */, int nodes,
		int* tree = 0 /* nodes */)
	{
		const OBJECT_INFO& object = objects[slot];
		float current[3] = { 0, 0, 0 };
		float stack[HD_OBJ_NODES][3];
		int owner = 0;
		int ownerStack[HD_OBJ_NODES];
		int top = 0;

		memset(out, 0, sizeof(float) * 3 * nodes);

		if (tree)
		{
			for (int node = 0; node < nodes; node++)
				tree[node] = node ? 0 : -1;
		}

		if (!bones)
			return;

		for (int node = 1; node < nodes; node++)
		{
			const long* bone = &bones[object.bone_index + (node - 1) * 4];
			const long flags = bone[0];

			if ((flags & 1) && top > 0)
			{
				--top;
				memcpy(current, stack[top], sizeof(current));
				owner = ownerStack[top];
			}

			if (flags & 2)
			{
				memcpy(stack[top], current, sizeof(current));
				ownerStack[top] = owner;

				if (top < HD_OBJ_NODES - 1)
					++top;
			}

			current[0] += (float)bone[1];
			current[1] += (float)bone[2];
			current[2] += (float)bone[3];

			out[node * 3 + 0] = current[0];
			out[node * 3 + 1] = current[1];
			out[node * 3 + 2] = current[2];

			if (tree)
				tree[node] = owner;

			owner = node;
		}
	}

	/* ---------------------------------------------------------- the light -- */

	/* WORLD -> VIEW. aW2VMx holds the camera's rotation in M00..M22 and its
	 * world position in M03 / M13 / M23, so a point is R * (p - c) - which is
	 * the very line phd_TranslateAbs computes (specific\3dmath.cpp:243-250).
	 * A direction is the same without the subtraction. */
	void world_to_view(float x, float y, float z, bool direction, float* out)
	{
		if (!direction)
		{
			x -= aW2VMx[M03];
			y -= aW2VMx[M13];
			z -= aW2VMx[M23];
		}

		out[0] = aW2VMx[M00] * x + aW2VMx[M01] * y + aW2VMx[M02] * z;
		out[1] = aW2VMx[M10] * x + aW2VMx[M11] * y + aW2VMx[M12] * z;
		out[2] = aW2VMx[M20] * x + aW2VMx[M21] * y + aW2VMx[M22] * z;
	}

	/* WHERE THE LIGHTS GO. A sink rather than the FrameItem itself, so that
	 * Lara's composite can ask for exactly this list without being an item -
	 * see HDObjectLights at the bottom of this file (#123, stage S4-2). */
	struct LightSink
	{
		HDObjLight* list;
		int max;
		int count;
	};

	bool add_light(LightSink& out, const HDObjLight& light)
	{
		if (out.count >= out.max)
		{
			g_clipped_lights++;
			return false;
		}

		out.list[out.count++] = light;
		return true;
	}

	/* THE OBJECT'S LIGHTS, from the very lists the classic path reads.
	 *
	 * SuperSetupLight (specific\lighting.cpp:79-182) turns each of them into a
	 * direction in the NODE's model space and a falloff measured from the
	 * node's world position. Both of those are per node and the shader can do
	 * better than either, so what travels here is the light itself: a sun's
	 * direction, everything else's position, all in view space. The activity,
	 * the fade and the range test are NOT redone - `Active` and the faded r/g/b
	 * are the engine's own answer, worked out in CreateLightList a moment ago
	 * inside CalculateObjectLighting.
	 *
	 * The order is on purpose. Suns first because there are at most a handful
	 * and they light the whole body; then the dynamics, because a torch, a
	 * flare and a gun flash are what a player watches for; then the room's own
	 * points and spots. Only past twelve does anything fall off the end, and
	 * then it is the least of them.
	 */
	void fill_lights(HDObjLight* list, int max, int* countOut, float* sunScale,
		ITEM_INFO* lit)
	{
		HDObjLight light;
		LightSink out;
		void* lists[2];
		long counts[2];

		out.list = list;
		out.max = max;
		out.count = 0;

		if (sunScale)
			*sunScale = InventoryActive ? 2.0f : 0.75f;

		lists[0] = lit ? lit->il.pCurrentLights : 0;
		lists[1] = lit ? lit->il.pPrevLights : 0;
		counts[0] = lit ? lit->il.nCurrentLights : 0;
		counts[1] = lit ? lit->il.nPrevLights : 0;

		/* --- the suns ---------------------------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active || source[i].Type != LIGHT_SUN)
					continue;

				/* MINUS one, because the level files carry the direction
				 * inverted - SuperSetupLight says so in the original comment
				 * and this is the same number. */
				const float length = (float)sqrt(SQUARE(source[i].nx) +
					SQUARE(source[i].ny) + SQUARE(source[i].nz));

				if (length <= 0.0f)
					continue;

				const float scale = -1.0f / length;
				float view[3];

				world_to_view(source[i].nx * scale, source[i].ny * scale,
					source[i].nz * scale, true, view);

				light.x = view[0];
				light.y = view[1];
				light.z = view[2];
				light.kind = (float)HD_OBJLIGHT_SUN;
				light.r = source[i].r * 255.0f;
				light.g = source[i].g * 255.0f;
				light.b = source[i].b * 255.0f;
				light.invRange = 0.0f;

				add_light(out, light);
			}
		}

		/* --- the dynamics ------------------------------------------------ */
		for (int i = 0; i < 32; i++)
		{
			DYNAMIC* dynamic = &dynamics[i];

			if (!dynamic->on)
				continue;

			/* SuperSetupDynamicLight's own falloff, which is NOT the room
			 * path's: an object uses falloff - (falloff >> 3), a roomlet
			 * (falloff >> 1) + (falloff >> 3). */
			const float falloff = (float)(dynamic->falloff - (dynamic->falloff >> 3));

			if (falloff <= 0.0f)
				continue;

			/* The engine tests the distance at each NODE and drops the light
			 * for that node alone. Here it is tested once against the item,
			 * with a node's reach of slack, and the shader's own falloff
			 * throws away what is left over - so the pixel is the same and
			 * only the upload is one longer. */
			const float dx = (float)dynamic->x - (float)(lit ? lit->il.item_pos.x : 0);
			const float dy = (float)dynamic->y - (float)(lit ? lit->il.item_pos.y : 0);
			const float dz = (float)dynamic->z - (float)(lit ? lit->il.item_pos.z : 0);

			if (sqrt(SQUARE(dx) + SQUARE(dy) + SQUARE(dz)) > falloff + 1024.0f)
				continue;

			float view[3];

			world_to_view((float)dynamic->x, (float)dynamic->y, (float)dynamic->z,
				false, view);

			light.x = view[0];
			light.y = view[1];
			light.z = view[2];
			light.kind = (float)HD_OBJLIGHT_POINT;
			light.r = (float)dynamic->r;
			light.g = (float)dynamic->g;
			light.b = (float)dynamic->b;
			light.invRange = 1.0f / falloff;

			add_light(out, light);
		}

		/* --- the room's points and spots --------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active)
					continue;

				/* A shadow light darkens the ambient and casts nothing; the
				 * engine has already taken it off aAmbient for us. */
				if (source[i].Type != LIGHT_POINT && source[i].Type != LIGHT_SPOT)
					continue;

				const bool spot = source[i].Type == LIGHT_SPOT;
				const float range = spot ? source[i].Cutoff : source[i].Outer;

				if (range <= 0.0f)
					continue;

				float view[3];

				world_to_view(source[i].x, source[i].y, source[i].z, false, view);

				light.x = view[0];
				light.y = view[1];
				light.z = view[2];
				light.kind = (float)(spot ? HD_OBJLIGHT_SPOT : HD_OBJLIGHT_POINT);
				light.r = source[i].r * 255.0f;
				light.g = source[i].g * 255.0f;
				light.b = source[i].b * 255.0f;
				light.invRange = 1.0f / range;

				add_light(out, light);
			}
		}

		if (countOut)
			*countOut = out.count;
	}

	/* ------------------------------------------------------- the self-test -- */

	/* THE TWO PROOFS, run on the first HD item of a level and written into the
	 * log. Neither of them is a self-test of a formula: both run the ENGINE's
	 * own code on one side.
	 *
	 * itemcheck - a node's origin lands in the same pixel on both roads. The
	 *   node matrix already carries the origin into view space (its
	 *   translation IS the view position of the node), so the classic side is
	 *   the three lines of aTransformLightClipMesh and the HD side is the
	 *   projection matrix the card is handed, through the viewport rule the
	 *   card really applies.
	 *
	 * lightcheck - the shader's light arithmetic gives the classic answer. The
	 *   classic side is aTransformLightClipMesh evaluated on the engine's own
	 *   SunLights / PointLights / SpotLights, which InitObjectLighting filled
	 *   a moment ago for THIS item at node 0; the HD side is the same
	 *   arithmetic on the view-space list this file just built, with the
	 *   sample normal turned into view space by the node's own matrix. If the
	 *   two disagree, the conversion is wrong - which is the one thing in the
	 *   whole object path that cannot be seen in a picture, only measured.
	 */
	/* THE LIGHT, BOTH WAYS, AS ONE CALL (#123, fix round S, S4).
	 *
	 * Lifted out of self_test so that the CREATURE probe below can ask the
	 * very same question of a guard in a dark room that the level proof asks
	 * of the first item of the level - one arithmetic, one set of numbers,
	 * and no chance of the two drifting apart.
	 *
	 * `classic` is aTransformLightClipMesh evaluated on the engine's own
	 * SunLights / PointLights / SpotLights, which InitObjectLighting filled
	 * for THIS item a moment ago, plus the ambient it left in aAmbientR/G/B.
	 * `hd` is the same arithmetic on the view-space list this file built for
	 * the shader, with the sample normal turned into view space by the
	 * node's own matrix. Both on ONE normal that is not axis aligned, so a
	 * swapped row or a dropped sign cannot cancel itself out. */
	void compare_light(const FrameItem& entry, float* classic, float* hd)
	{
		/* --- the light ------------------------------------------------------ */
		/* One normal that is not axis aligned, so that a swapped row or a
		 * dropped sign cannot cancel itself out. */
		const float nx = 0.4243f, ny = 0.5657f, nz = 0.7071f;

		classic[0] = (float)aAmbientR;
		classic[1] = (float)aAmbientG;
		classic[2] = (float)aAmbientB;

		for (long i = 0; i < NumPointLights; i++)
		{
			float val = (PointLights[i].vec.x * nx + PointLights[i].vec.y * ny +
				PointLights[i].vec.z * nz + 1.0f) * 0.5f;

			val *= val;

			if (val > 0)
			{
				val *= PointLights[i].rad;
				classic[0] += val * PointLights[i].r;
				classic[1] += val * PointLights[i].g;
				classic[2] += val * PointLights[i].b;
			}
		}

		for (long i = 0; i < NumSpotLights; i++)
		{
			float val = SpotLights[i].vec.x * nx + SpotLights[i].vec.y * ny +
				SpotLights[i].vec.z * nz;

			if (val > 0)
			{
				val *= SpotLights[i].rad;
				classic[0] += val * SpotLights[i].r;
				classic[1] += val * SpotLights[i].g;
				classic[2] += val * SpotLights[i].b;
			}
		}

		for (long i = 0; i < NumSunLights; i++)
		{
			float val = SunLights[i].vec.x * nx + SunLights[i].vec.y * ny +
				SunLights[i].vec.z * nz;

			if (val > 0)
			{
				val *= InventoryActive ? 2.0f : 0.75f;
				classic[0] += val * SunLights[i].r;
				classic[1] += val * SunLights[i].g;
				classic[2] += val * SunLights[i].b;
			}
		}

		/* The same normal in view space, and the same node's view position -
		 * the two things the shader has. */
		const float* m = entry.matrix;
		const float vnx = nx * m[0] + ny * m[4] + nz * m[8];
		const float vny = nx * m[1] + ny * m[5] + nz * m[9];
		const float vnz = nx * m[2] + ny * m[6] + nz * m[10];
		const float px = m[12], py = m[13], pz = m[14];

		hd[0] = entry.light[0];
		hd[1] = entry.light[1];
		hd[2] = entry.light[2];

		for (int i = 0; i < entry.lightCount; i++)
		{
			const HDObjLight& light = entry.lights[i];
			float lx = light.x, ly = light.y, lz = light.z;
			float dist = 0;

			if (light.kind >= 0.5f)
			{
				lx -= px;
				ly -= py;
				lz -= pz;
				dist = (float)sqrt(SQUARE(lx) + SQUARE(ly) + SQUARE(lz));

				if (dist > 1e-6f)
				{
					lx /= dist;
					ly /= dist;
					lz /= dist;
				}
			}

			const float ndl = vnx * lx + vny * ly + vnz * lz;
			const float rad = 1.0f - dist * light.invRange > 0.0f
				? 1.0f - dist * light.invRange : 0.0f;
			float val;

			if (light.kind < 0.5f)
				val = ndl > 0 ? ndl * (InventoryActive ? 2.0f : 0.75f) : 0.0f;
			else if (light.kind < 1.5f)
			{
				const float wrap = (ndl + 1.0f) * 0.5f;

				val = wrap * wrap * rad;
			}
			else
				val = (ndl > 0 ? ndl : 0.0f) * rad;

			hd[0] += val * light.r;
			hd[1] += val * light.g;
			hd[2] += val * light.b;
		}

	}

	void self_test(const FrameItem& entry)
	{
		if (!gLogEnabled || g_checked)
			return;

		g_checked = true;

		/* --- the matrix ---------------------------------------------------- */
		const double width = 2.0 * (double)f_centerx;
		const double height = 2.0 * (double)f_centery;

		D3DMATRIX projection;

		HDRenderMakeProjection((double)f_persp, (double)f_centerx, (double)f_centery,
			(double)f_a, (double)f_b, &projection);

		const float* p = (const float*)&projection;
		double worst_x = 0, worst_y = 0;
		int used = 0;

		for (int node = 0; node < entry.nodes; node++)
		{
			const float* m = entry.matrix + node * 16;
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

			if (dx > worst_x) worst_x = dx;
			if (dy > worst_y) worst_y = dy;

			used++;
		}

		Log("HDLOG itemcheck level=%s slot=%d(%s) nodes=%d used=%d "
			"maxdx=%.5f maxdy=%.5f",
			g_stem, entry.slot, HDSlotName(entry.slot) ? HDSlotName(entry.slot) : "?",
			entry.nodes, used, worst_x, worst_y);

		float classic[3], hd[3];

		compare_light(entry, classic, hd);

		Log("HDLOG lightcheck level=%s slot=%d classic=%.1f,%.1f,%.1f "
			"hd=%.1f,%.1f,%.1f diff=%.1f,%.1f,%.1f engine=%dsun/%dpoint/%dspot "
			"hd_lights=%d ambient=%d,%d,%d",
			g_stem, entry.slot, classic[0], classic[1], classic[2],
			hd[0], hd[1], hd[2],
			hd[0] - classic[0], hd[1] - classic[1], hd[2] - classic[2],
			(int)NumSunLights, (int)NumPointLights, (int)NumSpotLights,
			entry.lightCount, (int)aAmbientR, (int)aAmbientG, (int)aAmbientB);
	}

	/* AND THE SAME QUESTION OF A CREATURE IN A DARK ROOM (#123, fix round S,
	 * S4).
	 *
	 * The play test's video of the RICH levels: enemies far off in dark rooms
	 * come out PURE BLACK and are lit only by their own muzzle flash, while
	 * two shots from the same level show them lit. The question behind it is
	 * whether an HD body gets LESS light than the classic item would have -
	 * and it is not a question a picture can settle, because a dark room is
	 * dark in the classic engine too.
	 *
	 * So it is measured, on the creature itself and in the room it is standing
	 * in: the very comparison the level's own lightcheck makes, run once a
	 * second of game time for up to HD_LIGHT_PROBE_ITEMS creatures of the
	 * frame. `classic` is what aTransformLightClipMesh would give this item
	 * out of the engine's own light arrays; `hd` is what the shader gets. Two
	 * numbers that agree say the HD path is as dark as the classic one is, and
	 * a black guard is Core's own room; two that disagree name the term that
	 * is missing.
	 *
	 * `dist` is how far the creature stands from the lens, because the whole
	 * report is about DISTANT enemies, and `lit` is the brightest of the three
	 * channels so that a line can be read without adding them up.
	 *
	 * A creature and not every item: objects[].intelligent is the engine's own
	 * word for the things that walk about and shoot, which is what the video
	 * shows, and a level full of static meshes would otherwise bury the line.
	 * With -log only, and nothing branches on it. */
	/* How many creatures of one second the probe is allowed to name. Six is
	 * a busy room's worth and a line the user can read at a glance; more
	 * would bury the rest of the log without saying anything new, because
	 * the six are enough to tell "HD is darker" from "the room is dark". */
	#define HD_LIGHT_PROBE_ITEMS	6

	unsigned short g_light_probe_at = 0;
	bool g_light_probe_ever = false;
	int g_light_probe_left = 0;

	void creature_light(const FrameItem& entry)
	{
		if (!gLogEnabled)
			return;

		const unsigned short now = GlobalCounter;

		if (!g_light_probe_ever ||
			(unsigned short)(now - g_light_probe_at) >= 30)
		{
			g_light_probe_ever = true;
			g_light_probe_at = now;
			g_light_probe_left = HD_LIGHT_PROBE_ITEMS;
		}

		if (g_light_probe_left <= 0)
			return;

		if (entry.slot < 0 || entry.slot >= NUMBER_OBJECTS ||
			!objects[entry.slot].intelligent)
			return;

		g_light_probe_left--;

		float classic[3], hd[3];

		compare_light(entry, classic, hd);

		const float* m = entry.matrix;
		const double dist = sqrt((double)m[12] * m[12] + (double)m[13] * m[13] +
			(double)m[14] * m[14]);
		const float lit = hd[0] > hd[1]
			? (hd[0] > hd[2] ? hd[0] : hd[2])
			: (hd[1] > hd[2] ? hd[1] : hd[2]);

		Log("HDLOG creaturelight level=%s slot=%d(%s) room=%d dist=%d "
			"classic=%.1f,%.1f,%.1f hd=%.1f,%.1f,%.1f diff=%.1f,%.1f,%.1f "
			"lit=%.1f ambient=%d,%d,%d engine=%dsun/%dpoint/%dspot hd_lights=%d",
			g_stem, entry.slot,
			HDSlotName(entry.slot) ? HDSlotName(entry.slot) : "?",
			entry.room, (int)dist,
			classic[0], classic[1], classic[2], hd[0], hd[1], hd[2],
			hd[0] - classic[0], hd[1] - classic[1], hd[2] - classic[2],
			lit, (int)aAmbientR, (int)aAmbientG, (int)aAmbientB,
			(int)NumSunLights, (int)NumPointLights, (int)NumSpotLights,
			entry.lightCount);
	}

	/* FOR THE "HDLOG bind" LINE (#123, fix round O) - which of bind_against's
	 * four rules answered. "donor" is not one of HDBindVia's own values: it is
	 * set by the caller, below, for a slot whose DonorPlan exists, because
	 * that already says everything the log needs and bind_against does not
	 * know it is being asked on the donor's behalf. */
	const char* via_label(HDBindVia via)
	{
		switch (via)
		{
			case HD_BIND_VIA_MAP:     return "map";
			case HD_BIND_VIA_HAND:    return "hand";
			case HD_BIND_VIA_PREFIX5: return "prefix5";
			case HD_BIND_VIA_PREFIX:  return "prefix";
			case HD_BIND_VIA_PREFIX4: return "prefix4";
			case HD_BIND_VIA_CLASSIC: return "classic";
			default:                  return "none";
		}
	}
}

/* ---------------------------------------------------------- the level -- */

void HDObjectLevelBuild(const char* installRoot, const char* stem)
{
	std::vector<HDModelSlot> wanted;
	std::vector<float> binds;
	HDModelStats stats;
	const char* why = "";

	HDObjectLevelEnd();

	for (int i = 0; i < (int)sizeof(g_stem) - 1; i++)
	{
		g_stem[i] = stem ? stem[i] : 0;

		if (!g_stem[i])
			break;
	}

	g_stem[sizeof(g_stem) - 1] = 0;

	if (!installRoot || !*installRoot)
		return;

	/* THE LEVEL'S OWN .MAP, BACK IN HAND (#123, fix round B).
	 *
	 * hd_scan reads it during the level scan and frees it again at the end of
	 * that scan, and nothing read it back - so everything below bound by the
	 * SLOT'S OWN NAME, which is the third and last of the three rules in
	 * hd_bind.h and the wrong one for every slot the map names. Measured on
	 * the user's own run: ANDREA1 built 46 models with `dummy=0`, while the
	 * scan of the same level over the same files reported 55 bound and 14
	 * DUMMY. ANIMATING10 asked for ANIMATING10.TRM, which is in no ITEM
	 * folder, while the map says ANIMATING10_ANDREA1; the lantern, the lamp
	 * post and the wall arm asked for names that do not exist and drew
	 * classic on top of the art that already had them - bug B1.
	 *
	 * Loaded here rather than kept from the scan because the scan runs on
	 * another call and may not have run at all. Freed in HDObjectLevelEnd,
	 * which the next level's build calls first. */
	const bool haveMap = HDMapLoadForLevel(installRoot, g_stem);

	/* WHICH SLOTS THE ART ALREADY HAS (#123, fix round B, B1).
	 *
	 * A .MAP entry of DUMMY is not "no model for this slot" - it is the
	 * remaster saying it draws NOTHING here, and the reason it can is that
	 * the object is baked into the .TRG room art. Measured with BakeProbe
	 * over ANDREA1: every one of the 14 DUMMY slots that has items has the
	 * remaster's own triangles standing inside the item's own bounding box -
	 * the wall lantern (ANIMATING12) 1250 of them in 633x417x132 units, the
	 * lamp post (ANIMATING11) 730, the alcove (SEARCH_OBJECT3) 5001 - and no
	 * slot the map does NOT call DUMMY has more than 18, which is the room's
	 * own floor and wall inside a big enemy's box.
	 *
	 * So they must be drawn by nobody, exactly as a room's static meshes are.
	 * This is TROne's baked-mesh rule (game_renderer.cpp:3722-3736) arrived
	 * at from the other end: TROne keys on the mesh pointer of a static, we
	 * key on the remaster's own word for it.
	 *
	 * A DONOR slot is never baked. Its model came out of ANOTHER level file,
	 * so whatever this level's map says about the slot's vanilla content says
	 * nothing about what stands there now - and the art of THIS level has
	 * never seen it. */
	g_baked_slots = 0;
	g_baked_any = false;

	if (haveMap)
	{
		for (int slot = 0; slot < NUMBER_OBJECTS; slot++)
		{
			if (!objects[slot].loaded || objects[slot].nmeshes <= 0)
				continue;

			if (HDSlotIsLara(slot) || HDMarkersDonorOfSlot(slot))
				continue;

			HDBindKind kind = HD_BIND_NONE;
			bool fromMap = false;

			HDBindSlot(installRoot, slot, &kind, &fromMap, 0);

			if (kind != HD_BIND_DUMMY || !fromMap)
				continue;

			g_slot[slot].baked = 1;
			g_baked_slots++;
		}

		g_baked_any = g_baked_slots > 0;
	}

	Log("HDLOG level=%s map=%s lines=%d baked_slots=%d", g_stem,
		haveMap ? "read" : "none", HDMapCount(), g_baked_slots);

	/* The swap plans are filled further down, once the slot list exists; the
	 * counters are cleared here so that a level that returns early below
	 * leaves none of the previous level's behind. */
	g_swap_plans = 0;
	g_swap_models = 0;

	if (!tomb5.hd_objects)
		return;

	// The two shaders and the vertex declaration, made once and kept as long
	// as the device lives. A card that refuses them says so here, once, and
	// every object stays classic for the rest of the session.
	if (!HDModelReady() && !HDModelInit(App.dx.lpD3DDevice, &why))
	{
		Log("HDLOG level=%s objects refused reason=%s", g_stem, why);
		return;
	}

	/* WHERE THE OTHER BUNDLE IS (#123, stage S7-2). Set on both modules that
	 * need it and always, not only when this level has a guest: they name an
	 * installation, and a level with no guest offers no page and no name that
	 * could reach it. An empty path is the honest answer for a machine
	 * without Tomb Raider I-III Remastered, and then a guest's model is
	 * "missing" with the file name in the log. */
	HDModelTr3Root(HDInstallRootTr3());
	HDPageCacheAltRoot(HDInstallRootTr3());

	/* THE MODS FOLDER (#153, round M1). Scanned once per session and here
	 * because this is the first HD thing a level does; the sound path asks
	 * for the same scan when it gets there first, and one cached answer is
	 * what keeps the two from disagreeing about which mods are active. Its
	 * notes reach the log through the sink winmain installs. */
	HDModsScan();

	/* The bind poses first and in one block, because HDModelLevelBegin keeps
	 * a pointer into it while it runs and a std::vector that grows moves. */
	std::vector<int> slots;

	for (int slot = 0; slot < NUMBER_OBJECTS; slot++)
	{
		if (!objects[slot].loaded || objects[slot].nmeshes <= 0)
			continue;

		if (HDSlotIsLara(slot))
			continue;	// the wardrobe, planned below - HDBindSlot says so too

		slots.push_back(slot);
	}

	/* THE DONOR SLOTS (#123, stage S7-2). The sidecar says which of this
	 * level's slots hold a model that came out of ANOTHER level file, and the
	 * name the Remaster would give such a slot here is the wrong one -
	 * ANIMATING10 is a different object in every one of the fifteen files.
	 *
	 * Resolved BEFORE the loop that fills `wanted`, because the answer is the
	 * `name` that goes into it; and kept in a list of its own, because the
	 * log line each of them earns can only be written after the models are
	 * built and it is known whether the model really arrived. */
	struct DonorPlan
	{
		int slot;
		const char* level;	// the sidecar's own string - alive until the
					// next HDMarkersLoad, which is the next level
		int fromSlot;
		const char* model;	// 0 = this slot stays classic
		bool tr3;
		const char* why;
	};

	std::vector<DonorPlan> donors;

	for (size_t i = 0; i < slots.size(); i++)
	{
		const int slot = slots[i];
		const HDMarkerDonor* mark = HDMarkersDonorOfSlot(slot);

		if (!mark)
			continue;

		DonorPlan plan;

		plan.slot = slot;
		plan.level = mark->level;
		plan.fromSlot = mark->fromSlot;
		plan.model = 0;
		plan.tr3 = false;
		plan.why = "";

		if (_stricmp(mark->level, "TR3") == 0)
		{
			/* A guest out of Tomb Raider III (#92). It comes from no TR5
			 * level at all, so no .MAP can name it: the name is our own
			 * four-line table, and the model is in the OTHER bundle. */
			plan.tr3 = true;
			plan.model = HDBindTr3Guest(slot);
			plan.why = plan.model ? "tr3_guest" : "tr3_slot_unknown";
		}
		else
		{
			HDBindKind kind = HD_BIND_MODEL;
			bool fromMap = false;

			plan.model = HDBindDonorSlot(HDInstallRoot(), mark->level,
				mark->fromSlot, &kind, &fromMap, 0);

			if (kind == HD_BIND_DUMMY)
			{
				/* The donor level says the source slot draws nothing on
				 * purpose. Ours must draw nothing HD either - and "nothing
				 * HD" is the classic mesh, which is a picture. */
				plan.model = 0;
				plan.why = "donor_dummy";
			}
			else if (kind != HD_BIND_MODEL || !plan.model)
			{
				plan.model = 0;
				plan.why = "donor_no_name";
			}
			else
				plan.why = fromMap ? "donor_map" : "donor_slot_name";
		}

		donors.push_back(plan);
	}

	binds.resize(slots.size() * HD_OBJ_NODES * 3, 0.0f);
	/* The classic mesh TREE beside the bind pose, and kept alive for exactly
	 * as long (#123, fix round B, B4). */
	std::vector<int> trees(slots.size() * HD_OBJ_NODES, -1);

	/* The names of the alternative models (B3). Kept alive next to `binds`
	 * and for the same reason: HDModelSlot holds a pointer, not a copy, and
	 * a vector that grows moves its strings - so the room is taken once. */
	std::vector<std::string> swapNames;
	swapNames.reserve(sizeof(g_swap_plan) / sizeof(g_swap_plan[0]) * HD_OBJ_NODES);

	for (size_t i = 0; i < slots.size(); i++)
	{
		const int slot = slots[i];
		const int nodes = objects[slot].nmeshes < HD_OBJ_NODES
			? objects[slot].nmeshes : HD_OBJ_NODES;

		bind_pose_of(slot, &binds[i * HD_OBJ_NODES * 3], nodes,
			&trees[i * HD_OBJ_NODES]);

		HDModelSlot one;

		one.slot = slot;
		one.nodes = nodes;
		one.bind = &binds[i * HD_OBJ_NODES * 3];
		one.parent = &trees[i * HD_OBJ_NODES];
		one.name = 0;

		/* A donor slot that resolved to a name gets it; one that did not is
		 * LEFT OUT of the list entirely rather than bound by its own name.
		 * Binding it by name is the one answer that is certainly wrong: the
		 * slot is called SAS and holds a T-Rex, so SAS.TRM would draw a
		 * soldier with a dinosaur's skeleton. The skeleton test would very
		 * likely throw it out a moment later - "very likely" is not a reason
		 * to try. */
		const DonorPlan* plan = 0;

		for (size_t d = 0; d < donors.size(); d++)
			if (donors[d].slot == slot)
			{
				plan = &donors[d];
				break;
			}

		if (plan)
		{
			if (!plan->model)
				continue;

			one.name = plan->model;
			one.fromTr3 = plan->tr3;
		}

		wanted.push_back(one);

		/* THE ALTERNATIVE MODELS OF THIS SLOT (#123, fix round B, B3).
		 *
		 * They are ordinary models on ordinary pseudo slots - the same list,
		 * the same page cache, the same skeleton, the same bind pose - so
		 * nothing below this line has to know they exist. A guest out of
		 * Tomb Raider I-III is left out: the four TR3 slots have no swap in
		 * the engine and no second file in that bundle. */
		if (plan && plan->tr3)
			continue;

		const char* base = one.name;
		HDBindKind baseKind = HD_BIND_MODEL;

		if (!base)
			base = HDBindSlot(installRoot, slot, &baseKind, 0, 0);

		if (!base || baseKind != HD_BIND_MODEL)
			continue;

		const SwapRule* rule = 0;

		for (size_t r = 0; r < sizeof(kSwapRules) / sizeof(kSwapRules[0]); r++)
			if (_stricmp(kSwapRules[r].model, base) == 0)
			{
				rule = &kSwapRules[r];
				break;
			}

		if (!rule || g_swap_plans >= (int)(sizeof(g_swap_plan) / sizeof(g_swap_plan[0])))
			continue;

		/* AND ONLY IF THE LEVEL REALLY PUTS ONE DOWN. The cyborg's eleven
		 * stages are eleven whole bodies, and MESHSWAP_CROSS_HITMAN binds the
		 * same model while being a mesh warehouse that never has an item -
		 * loading its stages would be two megabytes for a slot nothing draws.
		 * A TR5 level's items are all in the file, so this is decided once
		 * and for good. */
		bool anyItem = false;

		for (int k = 0; k < level_items && !anyItem; k++)
			if (items[k].object_number == slot)
				anyItem = true;

		if (!anyItem)
			continue;

		SwapPlanSlot& swap = g_swap_plan[g_swap_plans];

		swap.slot = slot;
		swap.kind = rule->kind;
		swap.bits = rule->bits;

		for (int n = 0; n < HD_OBJ_NODES; n++)
			swap.model[n] = -1;

		if (rule->kind == SWAP_WHOLE)
		{
			swapNames.push_back(std::string(base) + rule->suffix);

			HDModelSlot alt = one;

			alt.slot = HD_SWAP_SLOT_BASE + slot;
			alt.name = swapNames.back().c_str();
			swap.model[0] = alt.slot;
			wanted.push_back(alt);
			g_swap_models++;
		}
		else if (rule->kind == SWAP_HITMAN)
		{
			for (int stage = 0; stage < HITMAN_PANELS; stage++)
			{
				const int node = hitman_nodes[stage];

				if (node < 0 || node >= one.nodes || node >= HD_OBJ_NODES)
					continue;

				char tail[16];
				std::sprintf(tail, "_%d", node + 1);
				swapNames.push_back(std::string(base) + tail);

				HDModelSlot alt = one;

				alt.slot = HD_SWAP_NODE_BASE + slot * HD_OBJ_NODES + node;
				alt.name = swapNames.back().c_str();
				swap.model[node] = alt.slot;
				wanted.push_back(alt);
				g_swap_models++;
			}
		}

		g_swap_plans++;
	}

	/* LARA'S WARDROBE, in the SAME HDModelLevelBegin (#123, stage S4-2). Her
	 * parts are models like any other - the same page cache, the same
	 * buffers, the same measurement - and the level begin call takes the
	 * whole list in one go, so they have to be in this list and not in a
	 * second one. hd_laradraw keeps the bind poses it hands over alive until
	 * the level ends, exactly as the `binds` block above does.
	 *
	 * BEFORE the log line below, so that one line counts everything the level
	 * really loaded, and after the ordinary slots so that a reader of the
	 * verdict table finds the objects where they were. */
	{
		HDModelSlot wardrobe[HD_LARA_PART_COUNT];
		const int parts = HDLaraDrawPlan(wardrobe, HD_LARA_PART_COUNT);

		for (int i = 0; i < parts; i++)
			wanted.push_back(wardrobe[i]);
	}

	if (wanted.empty())
		return;

	const DWORD started = GetTickCount();

	if (!HDModelLevelBegin(App.dx.lpD3DDevice, installRoot, &wanted[0],
		(int)wanted.size(), false, 1, &stats, &why))
	{
		Log("HDLOG level=%s objects refused reason=%s", g_stem, why);
		return;
	}

	/* The phases at the end of the line are round M2's fix round: a level
	 * build that took 672 ms before that round and 25,656 ms after it said
	 * only "models ms=", and the whole hunt was for which half of it had
	 * grown. key_ms is the one that had - see HDPageCacheKeyOf - and the
	 * three .TRM counters beside it answer the other question that was asked
	 * by guessing: how much of the wardrobe a level change reads. */
	Log("HDLOG level=%s models ms=%d slots=%d loaded=%d (5:%d 4:%d) "
		"mod:%d heads=%d lara=%d dummy=%d missing=%d refused=%d verts=%d idx=%d "
		"ranges=%d pages=%d vb_kib=%.0f ib_kib=%.0f read_ms=%d build_ms=%d "
		"find_ms=%d key_ms=%d page_ms=%d buf_ms=%d trm_open=%d trm_load=%d trm_kib=%.0f",
		g_stem, (int)(GetTickCount() - started), stats.slots, stats.loaded,
		stats.fromGame5, stats.fromGame4, stats.fromMod, stats.heads,
		stats.lara, stats.dummy, stats.missing, stats.refused, stats.vertices,
		stats.indices, stats.ranges, stats.pages, stats.vertexBytes / 1024.0,
		stats.indexBytes / 1024.0, (int)(stats.readMs + 0.5),
		(int)(stats.buildMs + 0.5), (int)(stats.findMs + 0.5),
		(int)(stats.keyMs + 0.5),
		(int)(stats.pageMs + 0.5), (int)(stats.bufferMs + 0.5),
		(int)stats.trmOpens, (int)stats.trmLoads, stats.trmBytes / 1024.0);

	/* The models' half of the texture budget (#123, stage S6). They arrive
	 * after the rooms and into the same cache, so `have` is what the rooms
	 * already put on the card and `budget` is the same number the rooms were
	 * measured against - one cap for the level, not one per caller. Most of a
	 * level's model pages are pages its rooms already wear, so `need` is
	 * usually a fraction of the rooms' and drops are rare. */
	Log("HDLOG level=%s models budget=%.0f need=%.0f have=%.0f got=%.0f MiB "
		"pages=%d frommod=%d dropped_mip0=%d levels=%d caps=%d format=%s oom=%d "
		"cache=%.0f MiB",
		g_stem, stats.pageStats.budgetBytes / 1048576.0,
		stats.pageStats.needBytes / 1048576.0,
		stats.pageStats.haveBytes / 1048576.0,
		stats.pageStats.bytes / 1048576.0, stats.pageStats.slots,
		stats.pageStats.fromMod,
		stats.pageStats.droppedMip0, stats.pageStats.droppedLevels,
		stats.pageStats.clampedByCaps,
		stats.pageStats.formatUsed == HD_PAGE_FORMAT_DXT5 ? "dxt5" : "rgba",
		stats.pageStats.outOfMemory, HDPageCacheBytes() / 1048576.0);

	for (int i = 0; i < HDModelNoteCount(); i++)
		Log("HDLOG level=%s model note: %s", g_stem, HDModelNote(i));

	/* THE VERDICT PER SLOT. One line each, so that a wrong picture in the game
	 * can be looked up rather than guessed at. */
	g_hd_slots = 0;
	g_classic_slots = 0;

	/* THE BIND LINE (#123, fix round O) - which of the four rules answered
	 * each slot, next to the verdict line below rather than folded into it:
	 * that one is unchanged since fix round B and this is a new question
	 * (WHY a name was tried), not a restatement of the skeleton test's
	 * answer (WHETHER it drew). A donor slot never calls HDBindSlot at all
	 * (its name is already `one.name`, set from the donor's own map before
	 * `wanted` was built) so its via is read off the DonorPlan list instead;
	 * everything else is asked again here, which costs a few stat() calls
	 * and nothing else - bind_against has no side effect the first call
	 * didn't already have. */
	int bind_total = 0, bind_map = 0, bind_hand = 0, bind_donor = 0,
		bind_prefix = 0, bind_classic = 0;

	for (size_t i = 0; i < wanted.size(); i++)
	{
		const int slot = wanted[i].slot;

		/* The wardrobe's pseudo slots (HD_LARA_SLOT_BASE and up) are not
		 * engine slots and have no verdict: no item ever carries one as its
		 * object_number, and g_slot is exactly NUMBER_OBJECTS long.
		 * hd_laradraw judges them, with its own line. */
		if (slot >= NUMBER_OBJECTS)
			continue;

		Verdict& verdict = g_slot[slot];

		{
			const DonorPlan* dplan = 0;

			for (size_t d = 0; d < donors.size(); d++)
				if (donors[d].slot == slot)
				{
					dplan = &donors[d];
					break;
				}

			const char* viaStr;
			const char* bindModel;

			if (dplan)
			{
				viaStr = "donor";
				bindModel = dplan->model ? dplan->model : "-";
				bind_donor++;
			}
			else
			{
				HDBindKind bk = HD_BIND_NONE;
				HDBindVia bv = HD_BIND_VIA_NONE;
				const char* bname = HDBindSlot(installRoot, slot, &bk, 0, &bv);

				bindModel = (bk == HD_BIND_MODEL && bname) ? bname : "-";
				viaStr = via_label(bv);

				switch (bv)
				{
					case HD_BIND_VIA_MAP:     bind_map++;     break;
					case HD_BIND_VIA_HAND:    bind_hand++;    break;
					case HD_BIND_VIA_PREFIX5:
					case HD_BIND_VIA_PREFIX:
					case HD_BIND_VIA_PREFIX4: bind_prefix++;  break;
					default:                  bind_classic++; break;
				}
			}

			bind_total++;
			verdict.via = viaStr;		// fix round P: for the ring's log line

			const int trmNodes = HDModelHas(slot) ? HDModelMaxBone(slot) + 1 : 0;

			Log("HDLOG bind slot=%d name=%s via=%s model=%s nodes=%d/%d",
				slot, HDSlotName(slot) ? HDSlotName(slot) : "?", viaStr,
				bindModel, trmNodes, wanted[i].nodes);
		}

		verdict.nodes = wanted[i].nodes;
		verdict.head = HDModelHasHead(slot) ? 1 : 0;
		verdict.maxBone = HDModelMaxBone(slot);
		verdict.covered = 0;
		verdict.drift = 0;
		verdict.worst = 0;
		verdict.hd = 0;
		verdict.why = verdict.baked ? "baked" : "no_model";

		if (!HDModelHas(slot))
		{
			/* A baked slot is not a classic one: nothing draws it. Counting
			 * it as classic would make the level line say a picture is being
			 * drawn that is not. */
			if (!verdict.baked)
			{
				g_classic_slots++;
				verdict.why = "model_missing";
			}

			/* #123, fix round B (B6): a LINE PER SLOT, including the ones
			 * that never reached the card. Until now only a slot with a
			 * model got one, so the level line's "classic" number was a
			 * total nobody could take apart - the user's report of
			 * "hd=22 classic=114" against the tool's 52 bound models had
			 * nothing to read. The model NOTE above names the file that is
			 * missing; this names the slot and the verdict. */
			Log("HDLOG slot=%d name=%s model=- nodes=%d maxbone=- cover=0 "
				"drift=-/- head=-1 hd=0 reason=%s",
				slot, HDSlotName(slot) ? HDSlotName(slot) : "?",
				wanted[i].nodes, verdict.why);
			continue;
		}

		for (int node = 0; node < wanted[i].nodes; node++)
			if (HDModelNodeCovered(slot, node))
				verdict.covered |= 1u << node;

		HDModelBindDrift(slot, &verdict.drift, &verdict.worst);

		/* THE SKELETON TEST, in the order of certainty.
		 *
		 * 1. A bone number the slot does not have. The randomizer puts
		 *    foreign creatures into slots - ANDREA1's SAS holds a 25 node
		 *    tree and SAS.TRM was drawn for 15 - and a vertex that names node
		 *    20 of a 15 node tree is not this skeleton's, full stop.
		 * 2. No geometry at all. Cannot happen with a model the reader
		 *    accepted, and is a guard and not a hope.
		 * 3. The bind drift, for a foreign model with the right node count:
		 *    the vertices of a node sit around that node in the tree they
		 *    were drawn for and nowhere near it in another. */
		if (verdict.maxBone >= wanted[i].nodes)
			verdict.why = "skeleton_nodes";
		else if (!verdict.covered)
		{
			/* A model that read but carries no triangle is the same
			 * statement DUMMY makes, and TRForge says so in as many words
			 * (vendor's HdMesh.cpp: "a model with nothing in it is an
			 * answer, not a failure ... either way the classic object must
			 * not stand in for it"). So it counts as baked and draws
			 * nothing, rather than falling back to the classic mesh on top
			 * of art that already has it. */
			verdict.why = "baked_empty";
			verdict.baked = 1;
			g_baked_slots++;
			g_baked_any = true;
		}
		else if (verdict.drift > kDriftLimit)
			verdict.why = "skeleton_drift";
		else
		{
			verdict.hd = 1;
			verdict.why = "ok";
		}

		if (verdict.hd)
			g_hd_slots++;
		else if (!verdict.baked)
			g_classic_slots++;

		int covered = 0;

		for (int node = 0; node < wanted[i].nodes; node++)
			if (verdict.covered & (1u << node))
				covered++;

		Log("HDLOG slot=%d name=%s model=%s nodes=%d maxbone=%d cover=%d "
			"drift=%.0f/%.0f head=%d hd=%d reason=%s",
			slot, HDSlotName(slot) ? HDSlotName(slot) : "?",
			HDModelName(slot) ? HDModelName(slot) : "-", wanted[i].nodes,
			verdict.maxBone, covered, verdict.drift, verdict.worst,
			HDModelHasHead(slot) ? HDModelHeadNode(slot) : -1,
			(int)verdict.hd, verdict.why);
	}

	Log("HDLOG level=%s objects hd=%d classic=%d baked=%d (of %d loaded "
		"slots), drift limit %.0f units",
		g_stem, g_hd_slots, g_classic_slots, g_baked_slots, (int)slots.size(),
		kDriftLimit);

	Log("HDLOG bind total=%d map=%d hand=%d donor=%d prefix=%d classic=%d",
		bind_total, bind_map, bind_hand, bind_donor, bind_prefix, bind_classic);

	/* THE ALTERNATIVE MODELS, after the build because only now is it known
	 * which of them really arrived (#123, fix round B, B3). One that did not
	 * is struck out of the plan, so swap_model_for falls back to the plain
	 * body rather than to an empty slot number - and a cyborg stage that is
	 * missing falls back to the stage before it, which is why they are
	 * counted per plan and not once. */
	if (g_swap_plans)
		Log("HDLOG level=%s swap plans=%d models=%d", g_stem, g_swap_plans,
			g_swap_models);

	for (int p = 0; p < g_swap_plans; p++)
	{
		SwapPlanSlot& swap = g_swap_plan[p];
		int have = 0, lost = 0;

		for (int n = 0; n < HD_OBJ_NODES; n++)
		{
			if (swap.model[n] < 0)
				continue;

			if (HDModelHas(swap.model[n]))
				have++;
			else
			{
				swap.model[n] = -1;
				lost++;
			}
		}

		/* A slot whose own body did not come out HD cannot draw an
		 * alternative either - the item goes down the classic path and never
		 * reaches this file's frame list. */
		if (swap.slot >= 0 && swap.slot < NUMBER_OBJECTS && !g_slot[swap.slot].hd)
			for (int n = 0; n < HD_OBJ_NODES; n++)
				swap.model[n] = -1;

		Log("HDLOG level=%s swap slot=%d name=%s kind=%s models=%d missing=%d",
			g_stem, swap.slot,
			HDSlotName(swap.slot) ? HDSlotName(swap.slot) : "?",
			swap.kind == SWAP_WHOLE ? "whole" : "hitman_stages", have, lost);
	}

	/* THE DONOR SLOTS, one line each (#123, stage S7-2), after the verdicts
	 * because `hd` is only known once the model is either on the card or not.
	 * A slot the sidecar named and that came out HD is the whole point of the
	 * stage; one that came out classic says here which of the four ways it
	 * got there - no name, the donor's DUMMY, no such file, or a skeleton the
	 * model does not fit. The last of those is the SLOT line above; this one
	 * says where the name came from. */
	for (size_t i = 0; i < donors.size(); i++)
	{
		const DonorPlan& plan = donors[i];
		const int slot = plan.slot;
		const bool hd = HDModelHas(slot) && g_slot[slot].hd != 0;
		const char* why = plan.why;

		if (plan.model && !HDModelHas(slot))
			why = plan.tr3
				? (HDInstallRootTr3()[0] ? "tr3_model_missing" : "tr3_not_installed")
				: "model_missing";
		else if (plan.model && !hd)
			why = g_slot[slot].why;		// the skeleton test said no

		Log("HDLOG donor slot=%d name=%s from=%s:%d model=%s hd=%d reason=%s",
			slot, HDSlotName(slot) ? HDSlotName(slot) : "?",
			plan.level, plan.fromSlot, plan.model ? plan.model : "-",
			hd ? 1 : 0, why);
	}

	if (!donors.empty())
		Log("HDLOG level=%s donors=%d donor_maps=%d map_lines=%d "
			"tr3_install=%s", g_stem, (int)donors.size(), HDDonorMapCount(),
			HDDonorMapEntries(), HDInstallRootTr3()[0] ? "found" : "absent");

	g_active = g_hd_slots > 0;

	/* THE PICKUP DISPLAY'S OWN LINE (#123, fix round P2), once per level.
	 *
	 * WHICH SLOTS IT COUNTS, and it is not a hand-written list: the display
	 * draws `inventry_objects_list[convert_obj_to_invobj(object_number)]`
	 * (specific\output.cpp, S_DrawPickup), so the only object slots it can
	 * ever name are the ones that table holds - and of those, four can never
	 * be picked up and are struck out here: the compass, the passport's two
	 * pages and the keypad. Everything else in the table is a weapon, ammo, a
	 * medipack, flares, binoculars, the laser sight, the silencer, a key, a
	 * puzzle piece, a pickup item, one of their combine halves, the crowbar,
	 * the torch, an examine note, the cloth or the bottle - which is exactly
	 * the list the callers of AddDisplayPickup (game\pickup.cpp,
	 * game\deltapak.cpp, game\newinv2.cpp's secret bonus) can produce.
	 *
	 * The table is read at BUILD time and it is per level: LoadGameflow
	 * (game\gameflow.cpp) overwrites its rows out of SCRIPT.DAT, but only the
	 * name, the offset, the scale and the rotations - never object_number - so
	 * the slot column is the same before and after. */
	memset(g_pickup_logged, 0, sizeof(g_pickup_logged));

	if (gLogEnabled)
	{
		char classic[512];
		int used = 0, hd = 0, seen = 0;
		bool named[NUMBER_OBJECTS];

		classic[0] = 0;
		memset(named, 0, sizeof(named));

		for (int i = 0; i < NUM_INVOBJ; i++)
		{
			const int slot = (int)inventry_objects_list[i].object_number;

			if (slot < 0 || slot >= NUMBER_OBJECTS || named[slot])
				continue;

			if (slot == COMPASS_ITEM || slot == PC_LOAD_INV_ITEM ||
				slot == PC_SAVE_INV_ITEM || slot == PUZZLE_HOLE8)
				continue;		// no caller of AddDisplayPickup can name one

			named[slot] = true;
			seen++;

			if (g_slot[slot].hd)
			{
				hd++;
				continue;
			}

			const char* name = HDSlotName(slot);

			used += sprintf(classic + used, "%s%s", used ? "," : "",
				name ? name : "?");

			if (used > (int)sizeof(classic) - 40)
			{
				used += sprintf(classic + used, ",...");
				break;
			}
		}

		Log("HDLOG pickupdisplay level=%s slots=%d hd=%d classic=%s", g_stem,
			seen, hd, used ? classic : "-");
	}

	/* LARA'S VERDICT (#123, stage S4-2), after the objects and out of the
	 * models that were just built: which parts arrived, what they cover, how
	 * far they drift. It writes its own HDLOG lines. */
	HDLaraDrawBuild(g_stem);

	/* Released only when NEITHER side has anything to draw. A level whose
	 * every object stayed classic can still have an HD Lara - the two
	 * verdicts are separate and the models are one buffer. */
	if (!g_active && !HDLaraDrawActive())
	{
		Log("HDLOG level=%s no slot and no wardrobe part draws HD - "
			"the models are released again", g_stem);
		HDModelLevelEnd();
	}
}

void HDObjectRelease()
{
	/* Device work: buffers and the models' share of the page cache. It runs
	 * on the main thread and only there, immediately before the room path
	 * empties the cache these models' page slots point into. */
	g_active = false;
	g_count = 0;
	g_open = false;
	HDLaraDrawEnd();
	HDModelLevelEnd();
}

void HDObjectLevelEnd()
{
	/* NOT A SINGLE DIRECT3D CALL. HDLevelUnload runs at the top of LoadLevel
	 * on the LOAD thread while the main thread presents the loading bar - the
	 * two-threads-one-device window that froze this game twice. The buffers
	 * go in HDObjectRelease, on the thread that owns the device. */
	HDLaraDrawEnd();	// flags only as well, and for the same reason
	HDDonorMapsFree();	// text, no device - the donor levels' .MAP files
	HDMapFree();		// and the level's own, which the build reads back
	memset(g_slot, 0, sizeof(g_slot));
	g_active = false;
	g_baked_any = false;
	g_baked_slots = 0;
	g_items_baked = 0;
	g_swap_plans = 0;
	g_swap_models = 0;
	g_swap_drawn = 0;
	g_count = 0;
	g_dropped = 0;
	g_open = false;
	g_checked = false;
	g_items_classic = 0;
	g_clipped_lights = 0;
	g_swap_ignored = 0;
	g_mask_used = 0;
	g_draw_calls = 0;
	g_frame_logged = false;
	g_hd_slots = 0;
	g_classic_slots = 0;
}

void HDObjectDeviceLost()
{
	HDObjectRelease();
	HDObjectLevelEnd();
	HDModelShutdown();
}

bool HDObjectActive()
{
	return g_active;
}

/* ----------------------------------------------------------- the frame -- */

void HDObjectFrameReset()
{
	g_count = 0;
	g_dropped = 0;
	g_open = false;
	g_items_classic = 0;
	g_swap_ignored = 0;
	g_swap_drawn = 0;
	g_mask_used = 0;
	g_items_baked = 0;

	/* THE RING (#123, fix round P). Its line is written HERE, of the frame
	 * that has just ended, and not in HDObjectFrameEnd: that one is reached
	 * only when HDFrameDraw found something to draw, and "the ring opened and
	 * not one item of it is HD" is exactly the report worth having. This
	 * function runs once per DRAWN frame whatever happened in it -
	 * S_InitialisePolyList calls HDFrameReset, and the ring's own loop
	 * (game\newinv2.cpp) calls S_InitialisePolyList once per present.
	 *
	 * That is also the answer to the sixty hertz question of this round: the
	 * ring's list is emptied per PICTURE and never per logic tick, and
	 * GameFrameMinTicks() (specific\output.cpp) holds the whole inventory at
	 * thirty presents in any case, so nothing here has two rates to keep
	 * apart. The ring's own spin stays where it always was, in
	 * draw_current_object_list.
	 *
	 * `g_ring_logged` is armed again on every frame that is not a ring frame,
	 * so the block is written once per OPENING of the inventory rather than
	 * thirty times a second. */
	if (gLogEnabled && g_ring_frame && !g_ring_logged)
	{
		g_ring_logged = true;

		Log("HDLOG ring items=%d hd=%d classic=%d",
			g_ring_hd + g_ring_classic, g_ring_hd, g_ring_classic);

		for (int i = 0; i < g_ring_log_count; i++)
		{
			const int slot = g_ring_log[i].slot;
			const bool known = slot >= 0 && slot < NUMBER_OBJECTS;

			Log("HDLOG ring slot=%d name=%s model=%s nodes=%d mask=%X z=%.0f via=%s",
				slot,
				(known && HDSlotName(slot)) ? HDSlotName(slot) : "?",
				g_ring_log[i].hd && HDModelName(slot) ? HDModelName(slot) : "-",
				known ? g_slot[slot].nodes : 0,
				g_ring_log[i].mask,
				(double)g_ring_log[i].z,
				g_ring_log[i].hd
					? (known && g_slot[slot].via ? g_slot[slot].via : "?")
					: (known && g_slot[slot].why ? g_slot[slot].why : "classic"));
		}
	}

	if (!InventoryActive)
		g_ring_logged = false;

	g_ring_open = false;
	g_ring_open_row = -1;
	g_ring_frame = false;
	g_ring_hd = 0;
	g_ring_classic = 0;
	g_ring_log_count = 0;
	/* P2: the display's walk begins and ends inside one S_OutputPolyList and
	 * cannot survive a picture - but g_open is cleared here for the same
	 * belt-and-braces reason and this is its partner. */
	g_pickup_open = false;

	HDLaraDrawFrameReset();
}

bool HDObjectBegin(ITEM_INFO* item, OBJECT_INFO* obj)
{
	// PROF_HD_OBJ is what this call, HDObjectNode and HDObjectEnd cost
	// together - the three of them never nest, so one accumulator holds the
	// whole per-item recording of the frame (#148).
	ProfScope prof(PROF_HD_OBJ);

	if (!item || !obj)
		return false;

	/* THE ART ALREADY HAS IT (#123, fix round B, B1). Before every other
	 * guard, and on purpose: this item is not drawn HD, it is not drawn at
	 * all, and the reason sits in the ROOM path rather than in the object
	 * path - the .TRG group the room draws carries the object's geometry
	 * whether hd_objects is on or off.
	 *
	 * Per ITEM and not per slot, because only a room that really draws HD has
	 * the baked copy: a room the sidecar marked classic, or one whose group
	 * is empty, still needs its lamp post drawn the old way. That is the same
	 * test DrawStaticObjects makes for a room's static meshes, one line up
	 * from here in the same rule.
	 *
	 * Returning TRUE with no node recorded is what makes it vanish:
	 * DrawAnimatingItem then puts no classic polygon in, HDObjectNode is
	 * never reached because g_open stays false, and HDObjectEnd sees nothing
	 * and drops the item. */
	if (g_baked_any)
	{
		const int baked_slot = (int)item->object_number;

		if (baked_slot >= 0 && baked_slot < NUMBER_OBJECTS &&
			g_slot[baked_slot].baked && HDRoomIsHD(item->room_number))
		{
			g_items_baked++;
			g_open = false;
			return true;
		}
	}

	// The guard. With hd_objects off, or before a level built its models,
	// this is where DrawAnimatingItem turns round and draws what it always
	// drew.
	if (!g_active || !tomb5.hd_objects)
		return false;

	const int slot = (int)item->object_number;

	if (slot < 0 || slot >= NUMBER_OBJECTS || !g_slot[slot].hd)
	{
		g_items_classic++;
		return false;
	}

	if (g_count >= HD_OBJ_ITEMS)
	{
		g_dropped++;
		return false;
	}

	// The frame's field of view, if no HD room has taken it yet - an item can
	// be the first HD thing in a frame whose rooms all stayed classic.
	HDFrameCaptureProjection();

	g_open = true;
	g_open_slot = slot;
	g_open_nodes = g_slot[slot].nodes;
	g_open_room = (int)item->room_number;	// group M: its portal window
	g_open_mask = (unsigned int)item->mesh_bits;
	g_seen = 0;

	/* THE MESH SWAP (#123, fix round B, B3).
	 *
	 * A swap replaces ONE node's mesh with the pair's second pointer - a
	 * guard's hands (bits 10 and 13, set for every soldier the moment he is
	 * initialised), the cyborg's eleven damage panels, the Mafia's pistols
	 * coming out of their holsters. Stage S3-2 ignored all of it, because a
	 * .TRM carries no second geometry for a node. What it missed is that the
	 * remaster does not swap a node: it ships a whole SECOND MODEL for the
	 * swapped look, and the table at the top of this file says which.
	 *
	 * Where such a model exists the item draws it instead of the plain one -
	 * one model, one draw, the same joints. Where none exists the old answer
	 * stands and the count goes into the frame line: the soldiers' hands need
	 * no second file because MAFIA.TRM and SWAT.TRM already hold the weapon,
	 * which is measured and not assumed (ModelProbe -name MAFIA). Drawing the
	 * classic swap mesh over the HD body stays out of the question - that is
	 * the road TROne took and reversed after user reports of classic patches
	 * on an HD model (game_renderer.cpp:1825-1830). */
	g_open_swap = swap_model_for(swap_plan_of(slot), item);

	if (g_open_swap < 0 && item->meshswap_meshbits &&
		(unsigned int)item->meshswap_meshbits & g_slot[slot].covered)
		g_swap_ignored++;
	else if (g_open_swap >= 0)
		g_swap_drawn++;

	if (g_open_mask != 0xFFFFFFFFu)
		g_mask_used++;

	return true;
}

void HDObjectNode(int node, bool interpolated)
{
	ProfScope prof(PROF_HD_OBJ);

	if (!g_open || node < 0 || node >= g_open_nodes || node >= HD_OBJ_NODES)
		return;

	if (interpolated)
	{
		/* The matrix that DRAWS in the frac branch is the interpolated one,
		 * which phd_PutPolygons_I makes inside a push of its own
		 * (game\draw.cpp:296-301). The same push, the same call, the same
		 * pop - so what is recorded is the matrix the classic mesh would
		 * have been drawn with, and not the one before the blend. */
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(g_raw + node * 12, aMXPtr, 48);
		phd_PopMatrix();
	}
	else
		memcpy(g_raw + node * 12, aMXPtr, 48);

	g_seen |= 1u << node;
}

void HDObjectEnd()
{
	ProfScope prof(PROF_HD_OBJ);

	if (!g_open)
		return;

	g_open = false;

	if (!g_seen || g_count >= HD_OBJ_ITEMS)
		return;

	FrameItem& entry = g_frame[g_count];

	entry.slot = g_open_slot;
	entry.drawSlot = g_open_swap >= 0 ? g_open_swap : g_open_slot;
	entry.head = g_open_slot >= 0 && g_open_slot < NUMBER_OBJECTS &&
		g_slot[g_open_slot].head != 0;
	entry.nodes = g_open_nodes;
	entry.mask = g_open_mask;
	entry.room = g_open_room;	// group M
	entry.view = ViewSerial;	// fix round R
	// P2: no off-centre projection in the world - only the inventory ring
	// draws about anything but the screen centre.
	entry.offCentreX = 0.0f;
	entry.offCentreY = 0.0f;

	// The engine's 3x4 into the row-major 4x4 the shader wants - the note and
	// the unseen-node rule are over store_matrices at the top of this file.
	store_matrices(entry);

	/* THE LIGHT, out of the classic path's own setup.
	 *
	 * These three lines are the first three of phd_PutPolygons with the draw
	 * left out: the view matrix of the node (aSetViewMatrix, minus the
	 * SetTransform that only the fixed-function pipe needs), the reset that
	 * builds aLightMatrix and the node's world position out of it, and the
	 * setup that fills SunLights / PointLights / SpotLights and leaves the
	 * ambient in aAmbientR/G/B.
	 *
	 * Node 0 and not each node in turn, because the only per-node part of the
	 * classic answer is the light DIRECTION and the falloff, and the shader
	 * works both out from the surface point instead. The ambient really is
	 * per object: the shadow lights that lower it read `light->Range`, which
	 * CreateLightList measured at the ITEM.
	 *
	 * current_item and not `item`: CalculateObjectLighting has just set it,
	 * and for an item with a shade value it points at the static-mesh light
	 * item instead - which is the classic path's own decision and not ours. */
	SetD3DMatrix(&D3DMView, g_raw);
	SuperResetLights();
	InitObjectLighting(current_item);

	for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
	{
		entry.light[node * 3 + 0] = (float)aAmbientR;
		entry.light[node * 3 + 1] = (float)aAmbientG;
		entry.light[node * 3 + 2] = (float)aAmbientB;
	}

	fill_lights(entry.lights, HD_MAX_OBJ_LIGHTS, &entry.lightCount,
		&entry.sunScale, current_item);

	// An ITEM is never cut. The neck cut of stage S8 exists because the first
	// person camera sits inside Lara's own head, and nothing else in the level
	// has a camera inside it.
	memset(&entry.cut, 0, sizeof(entry.cut));
	entry.cull = HD_MODEL_CULL_NONE;
	// #123 fix round D (D2): no sheen on an object - Lara's alone for now.
	entry.specStrength = 0.0f;
	entry.specPower = 1.0f;

	/* The two proofs, on the first HD item of the level and while the
	 * engine's own light arrays still hold THIS item's answer. */
	self_test(entry);

	/* And the same light question of a CREATURE, once a second (#123, fix
	 * round S, S4) - the play test's "enemies in dark rooms are pure black". */
	creature_light(entry);

	g_count++;
}

/* ------------------- THE INVENTORY RING (P) AND THE PICKUP DISPLAY (P2) -- */

/* THE PICKUP DISPLAY'S HALF OF HDInvBegin (#123, fix round P2).
 *
 * Every reason this item might NOT be drawn HD has to be settled HERE, before
 * the answer goes back to DrawInventoryItemMe, because a true means the caller
 * puts not one classic polygon in. The draw itself must therefore be able to
 * fail for nothing this function did not already test: the model, the
 * skeleton, the renderer, an open bracket, a scene to draw in and a projection
 * to draw through.
 *
 * `x`/`y` are the display's place on the screen - S_DrawPickup's
 * `phd_winwidth - GetFixedScale(80) + PickupX` and `phd_winheight -
 * GetFixedScale(75)`, plus INVOBJ::yoff in y - which is what the classic path
 * hands phd_PutPolygonsPickup as its temporary projection centre. So the
 * slide-in and slide-out are carried for free: PickupX moves x, x moves the
 * offset, the offset moves the projection, and the object walks out of the
 * picture exactly as it always did. There is no alpha fade in either path to
 * copy - DrawPickups (game\health.cpp) draws nothing at all and only moves
 * PickupX. */
static bool pickup_begin(ITEM_INFO* item, int slot, float x, float y,
	long color)
{
	const bool known = slot >= 0 && slot < NUMBER_OBJECTS;
	const bool hd = known && g_slot[slot].hd != 0;

	/* THE SEAM, once per slot per level - see g_pickup_logged. Written before
	 * the guards below rather than after, so that a slot the renderer refused
	 * is named too; `hd` is the slot's own verdict, which is the question the
	 * line exists to answer. */
	if (gLogEnabled && known && !g_pickup_logged[slot])
	{
		g_pickup_logged[slot] = true;

		Log("HDLOG pickupdisplay slot=%d(%s) hd=%d x=%d y=%d", slot,
			HDSlotName(slot) ? HDSlotName(slot) : "?", hd ? 1 : 0,
			(int)x, (int)y);
	}

	if (!hd)
		return false;

	/* A BRACKET TO DRAW IN. HDRenderFrameBegin refuses to nest and says
	 * nothing about it, and there is no scene to draw into before
	 * _BeginScene - so both are asked here, where the answer can still be
	 * "draw it classically". Neither can happen from S_DrawPickup as the
	 * engine stands (S_InitialisePolyList opens the scene, and the frame's own
	 * bracket was closed long before the depth clear), which is why they are
	 * tested rather than assumed. */
	if (!HDRenderReady() || HDRenderInFrame() || !App.dx.InScene)
		return false;

	/* THE DISPLAY'S OWN PROJECTION, read here and now. NOT
	 * HDFrameProjectionInfo: that is the field of view the WORLD was collected
	 * with, and AlterFOV moves f_persp inside a frame whenever the binoculars
	 * are up. The classic display projects with whatever f_mpersp holds at
	 * this moment (aTransformLightClipMesh, specific\output.cpp) and f_mpersp
	 * is f_persp to the bit (specific\3dmath.cpp, AlterFOV sets both from the
	 * same line), so this is that number and not a copy of it. */
	g_pickup_persp = f_persp;
	g_pickup_centrex = f_centerx;
	g_pickup_centrey = f_centery;
	g_pickup_za = f_a;
	g_pickup_zb = f_b;

	if (g_pickup_persp <= 0.0f)
		return false;

	/* THE OFF-CENTRE PROJECTION, the ring's own arithmetic and the same two
	 * numbers - the derivation stands over HDInvNode and in hd_model.h. */
	g_ring_kx = (x - g_pickup_centrex) / g_pickup_persp;
	g_ring_ky = (y - g_pickup_centrey) / g_pickup_persp;
	g_ring_color = color;

	g_pickup_open = true;

	g_open = true;
	g_open_slot = slot;
	g_open_nodes = g_slot[slot].nodes;
	g_open_room = -1;			// no room, as the ring has none
	g_open_mask = (unsigned int)item->mesh_bits;
	g_open_swap = -1;			// as on the ring: the stack ITEM_INFO
						// never filled meshswap_meshbits
	g_seen = 0;

	/* NOT ONE OF THE FRAME'S COUNTERS IS TOUCHED HERE - not g_items_classic,
	 * not g_mask_used, not g_count. They belong to the line HDObjectFrameEnd
	 * has already written by the time the display is drawn, and a number
	 * added to a line that is out of the door lands in the NEXT frame's.
	 * The display has a seam of its own, above. */
	return true;
}

/* THE IMMEDIATE DRAW (#123, fix round P2). One object, one bracket, and every
 * device state back where it was - HDFramePickupBegin is HDRenderStateSave and
 * HDFramePickupEnd is HDRenderStateRestore, the very pair the rooms and the
 * ring are drawn between, so there is no second list of states to keep in
 * step. The classic DrawSortList that follows this in S_OutputPolyList runs
 * unchanged; with the object drawn here its own list is empty, and
 * DrawSortList returns at its first line on an empty list without touching a
 * state. */
static void pickup_draw(const FrameItem& entry)
{
	/* No ProfScope of its own: HDInvEnd's PROF_HD_OBJ is still open around
	 * this call, and two accumulators over the same microseconds would report
	 * the frame's object work twice (#148). */
	if (!HDFramePickupBegin(g_pickup_persp, g_pickup_centrex, g_pickup_centrey,
		g_pickup_za, g_pickup_zb))
		return;

	HDModelLighting lighting;

	lighting.lights = entry.lights;
	lighting.lightCount = entry.lightCount;
	lighting.sunScale = entry.sunScale;
	lighting.specStrength = entry.specStrength;
	lighting.specPower = entry.specPower;

	// The display's place on the screen, in the PROJECTION and never in the
	// matrices - fix round P2's whole point, ring and display alike.
	HDModelOffCentre(entry.offCentreX, entry.offCentreY);

	HDModelDraw(entry.drawSlot, entry.matrix, entry.nodes, entry.light,
		entry.mask, &lighting, &entry.cut, entry.cull);

	if (entry.head)
		HDModelDrawHead(entry.slot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting, &entry.cut, entry.cull);

	/* Put back before the bracket closes, exactly as HDObjectFrameDraw does:
	 * the register file is shared with everything else that draws.
	 *
	 * g_draw_calls is NOT touched. It is the frame line's count of what the
	 * world cost, HDObjectFrameEnd has already written that line by the time
	 * this runs, and adding a display's one call to yesterday's number would
	 * only make the line wrong. */
	HDModelOffCentre(0.0f, 0.0f);
	HDFramePickupEnd();
}

bool HDInvBegin(ITEM_INFO* item, OBJECT_INFO* obj, float x, float y, long color)
{
	ProfScope prof(PROF_HD_OBJ);

	if (!item || !obj)
		return false;

	/* WHICH OF THE TWO CALLERS THIS IS, and it is still one line.
	 *
	 * DrawThreeDeeObject2D is reached from the ring, the ammo selector, the
	 * keypad and the examine screen - all four inside S_CallInventory2, where
	 * InventoryActive is 1 - and from S_DrawPickup (specific\output.cpp), the
	 * little turning object in the corner after a pickup, where it is 0. The
	 * two never overlap: the inventory calls S_OutputPolyList itself, but it
	 * has CreateMonoScreen'd first and MonoScreenOn blocks the display's whole
	 * block there (output.cpp).
	 *
	 * FIX ROUND P REFUSED THE SECOND ONE, and named the reason: S_DrawPickup
	 * runs INSIDE S_OutputPolyList, after HDFrameDraw has drawn this frame's
	 * HD list and after HDFrameDrawGlow has finished with it, so an item
	 * pushed onto the list would suppress its own classic polygons and be
	 * drawn by nobody. That is still true of the LIST. Fix round P2 does not
	 * put it on the list: it collects the same matrices through the same node
	 * walk and draws the item on the spot in HDInvEnd, in a bracket of its
	 * own. Everything below is shared by both roads except where it says
	 * otherwise. */
	const bool pickup = !InventoryActive;

	if (!g_active || !tomb5.hd_objects)
		return false;

	const int slot = (int)item->object_number;

	if (pickup)
		return pickup_begin(item, slot, x, y, color);

	g_ring_frame = true;

	/* One row for the log, taken before the answer is known and corrected
	 * below if the answer is yes - so an item that falls through any of the
	 * guards is still named in the line, which is what the line is for. */
	int logRow = -1;

	if (g_ring_log_count < 16)
	{
		logRow = g_ring_log_count++;
		g_ring_log[logRow].slot = slot;
		g_ring_log[logRow].mask = (unsigned int)item->mesh_bits;
		g_ring_log[logRow].hd = false;
		g_ring_log[logRow].z = 0.0f;
	}

	g_ring_open_row = logRow;

	if (slot < 0 || slot >= NUMBER_OBJECTS || !g_slot[slot].hd)
	{
		g_ring_classic++;
		g_items_classic++;
		return false;
	}

	if (g_count >= HD_OBJ_ITEMS)
	{
		g_dropped++;
		g_ring_classic++;
		return false;
	}

	/* The frame's projection, which in a ring frame nothing else has taken:
	 * there are no rooms in the inventory and no DrawAnimatingItem either. */
	HDFrameCaptureProjection();

	float persp = 0.0f, cx = 0.0f, cy = 0.0f;

	HDFrameProjectionInfo(&persp, &cx, &cy);

	if (persp <= 0.0f)
	{
		g_ring_classic++;
		return false;
	}

	/* THE ITEM'S OWN SCREEN CENTRE, worked out once per item and handed to
	 * HDModelOffCentre at draw time. The derivation is over HDInvNode and the
	 * arithmetic in hd_model.h.
	 *
	 * NO SCALE TERM. TROne multiplies its sheared row by f_mpersp/persp
	 * (hostglue_2d5.cpp:485-498) because its host builds a projection of its
	 * own out of the window's real width, while TR5 computes f_persp from a
	 * 4:3 width whatever the window is (specific\3dmath.cpp, AlterFOV:
	 * fov_width = phd_winheight * 320 / 240). Our HD projection is built from
	 * that very f_persp (hd_level.cpp, HDFrameCaptureProjection takes
	 * f_persp, HDRenderMakeProjection is fed it), so the factor is exactly
	 * one and the ring is the same size in HD as it is classic - which is the
	 * requirement, not an accident to be corrected. */
	g_ring_kx = (x - cx) / persp;
	g_ring_ky = (y - cy) / persp;
	g_ring_color = color;
	g_ring_open = true;
	g_ring_hd++;

	if (logRow >= 0)
		g_ring_log[logRow].hd = true;

	g_open = true;
	g_open_slot = slot;
	g_open_nodes = g_slot[slot].nodes;
	/* NO ROOM. A ring item stands in no room at all, and -1 is what the
	 * frame list already means by that - HDFrameScissorForRoom leaves the
	 * scissor off for it, which is the whole screen, which is what the ring
	 * has. */
	g_open_room = -1;
	/* THE NODE MASK, EXACTLY (#123, fix round P). item->mesh_bits here is
	 * INVOBJ::meshbits, which the ring uses on purpose and not by accident:
	 * the passport's three page states, the compass needle that turns on its
	 * own, the globe's markers. TROne needed a flag of its own for this
	 * (TRONE_LIGHT_EXACT_NODES); our object path has taken the mask as a mask
	 * since stage S3-2, so an unnamed node draws nothing here for the same
	 * reason it draws nothing in the world. */
	g_open_mask = (unsigned int)item->mesh_bits;
	/* NO MESH SWAP, and it is not a decision that could go the other way.
	 * DrawThreeDeeObject2D (game\newinv2.cpp) builds its ITEM_INFO on the
	 * STACK and fills in eleven fields; meshswap_meshbits is not one of them,
	 * so the field holds whatever was on that stack a moment ago. The classic
	 * ring never reads it either - DrawInventoryItemMe tests mesh_bits alone,
	 * where DrawAnimatingItem tests both. Forced to "no alternative model"
	 * here so that nothing downstream can be tempted to look. */
	g_open_swap = -1;
	g_seen = 0;

	if (g_open_mask != 0xFFFFFFFFu)
		g_mask_used++;

	return true;
}

/* ONE NODE OF A RING ITEM. The engine's matrix stack, straight off, exactly as
 * HDObjectNode takes it in the world - and that is the point of fix round P2.
 *
 * THE OFF-CENTRE PROJECTION IS NOT IN HERE ANY MORE. The classic ring projects
 * an item about the item's own place on the screen: aTransformLightClipMesh
 * (specific\output.cpp) puts a vertex at
 *
 *     sx = vx * p / vz + f_centerx,   p = f_mpersp
 *
 * and phd_PutPolygonsPickup sets f_centerx = X, f_centery = Y for the length of
 * one mesh. The HD path builds ONE projection for the whole frame, about the
 * screen centre Cx/Cy, so the difference - kx = (X - Cx) / p - has to be put
 * somewhere, and there are two places it can go.
 *
 * FIX ROUND P PUT IT HERE, as a shear of the node matrix: row 0 += kx * row 2.
 * That is exact for the POSITION, because the projection divides by vz and
 * anything added to vx in proportion to vz survives as a constant pixel
 * offset. It is wrong for the LIGHT, and that is what the round was not
 * allowed to keep: hd_model.hlsl skins the vertex NORMAL through the very same
 * matrix and renormalises it (Skin(normal, node), then rsqrt), so a sheared
 * node matrix tilts and rescales every normal of the item and the ring's one
 * sun lights it by an amount that depends on where it stands on the screen.
 * Measured on the shader itself - ModelProbe -ringlight over the object's own
 * pixels - the middle of the inventory row came out 28 % too dark and the ammo
 * row 16 % too bright, two rings that stand at once and 45 % apart.
 *
 * FIX ROUND P2 PUTS IT IN THE PROJECTION, where the classic path has it: the
 * item carries kx / ky to HDModelOffCentre (hd_model.h), which writes _31 and
 * _32 of a copy of the frame's projection for that one draw. A normal never
 * goes through a projection, so it cannot be touched by one. The node matrix
 * that reaches the card is now the engine's own, and the ring's light is the
 * classic ring's light to the bit.
 *
 * The idea of folding the ring's centre into the item at all is TROne's
 * (Copyright 2026 Todesengel, GPLv3, games\tr5\glue\hostglue_2d5.cpp:502-506,
 * which shears the matrix); the road taken here and the arithmetic in
 * HDModelOffCentreMatrix were derived from TR5's own projection line.
 */
void HDInvNode(int node)
{
	ProfScope prof(PROF_HD_OBJ);

	// P2: the ring and the pickup display collect a node in exactly the same
	// way - the engine's matrix stack, straight off. Which of the two is open
	// only decides what HDInvEnd does with the item afterwards.
	if (!g_open || (!g_ring_open && !g_pickup_open) || node < 0 ||
		node >= g_open_nodes || node >= HD_OBJ_NODES)
		return;

	memcpy(g_raw + node * 12, aMXPtr, 48);

	g_seen |= 1u << node;
}

void HDInvEnd()
{
	ProfScope prof(PROF_HD_OBJ);

	if (!g_open || (!g_ring_open && !g_pickup_open))
		return;

	/* P2: WHICH OF THE TWO WAS OPEN, taken before both are cleared. The ring
	 * puts the item on the frame's list and the display draws it here and
	 * now; everything between this line and the light below is the same for
	 * both, because it IS the same item. */
	const bool pickup = g_pickup_open;

	g_open = false;
	g_ring_open = false;
	g_pickup_open = false;

	if (!g_seen)
		return;

	if (!pickup && g_count >= HD_OBJ_ITEMS)
		return;

	/* The display's item never goes on the list and never takes a place on it:
	 * it lives for the length of this function and is drawn at the bottom of
	 * it. g_count is left exactly where the world left it. */
	FrameItem& entry = pickup ? g_pickup_item : g_frame[g_count];

	entry.slot = g_open_slot;
	entry.drawSlot = g_open_slot;
	entry.head = g_open_slot >= 0 && g_open_slot < NUMBER_OBJECTS &&
		g_slot[g_open_slot].head != 0;
	entry.nodes = g_open_nodes;
	entry.mask = g_open_mask;
	entry.room = g_open_room;
	entry.view = ViewSerial;
	// P2: the ring's own screen centre, carried to the draw and put into the
	// PROJECTION there - never into these matrices.
	entry.offCentreX = g_ring_kx;
	entry.offCentreY = g_ring_ky;

	store_matrices(entry);

	// Node 0's view z, for the log line - see RingLogItem::z.
	if (!pickup && g_ring_open_row >= 0 && g_ring_open_row < 16)
		g_ring_log[g_ring_open_row].z = entry.matrix[14];

	/* THE RING'S LIGHT - AND THE DISPLAY'S, which is the same light with ONE
	 * number different. Both are the classic path's own and nothing else.
	 *
	 * phd_PutPolygonsPickup (specific\output.cpp) throws the whole level
	 * lighting away for an inventory object and puts up two things: an
	 * ambient of 8/8/8, and ONE sun whose colour is the ring's pcbright for
	 * this item and whose direction is the fixed (0, -50, -100) - up and
	 * towards the camera - normalised over sqrt(12500).
	 *
	 * The classic line reads that direction out of D3DMView, which is the
	 * NODE's matrix at that moment (aSetViewMatrix is called at the top of
	 * the same function), so what it computes is the direction expressed in
	 * the node's own space: vec = R^T d. The shading term is then
	 * dot(n_model, R^T d) = dot(R n_model, d) = dot(n_view, d) - so in VIEW
	 * space, which is where HD_OBJLIGHT_SUN lives, the direction is simply d
	 * and no per-node work is left.
	 *
	 * THE ONE NUMBER THAT IS NOT THE SAME FOR BOTH CALLERS (#123, fix round
	 * P2), and it was measured rather than carried over: the sun's SCALE.
	 * aTransformLightClipMesh has one line for it -
	 *
	 *     if (!InventoryActive) val *= 0.75F; else val += val;
	 *
	 * ("fucking shit", says Core's own comment beside it) - and the pickup
	 * display is drawn with InventoryActive 0. So the ring's sun is DOUBLED
	 * and the display's is three quarters, a factor of 2.667 between them, and
	 * a display drawn with the ring's 2.0 would have been visibly brighter
	 * than the classic one it replaces. It is the same test the same file
	 * makes for an item in the world (fill_lights, at the top of this file),
	 * which is the other thing the display shares with the world and the ring
	 * does not.
	 *
	 * fill_lights would arrive at the same numbers - it is not called here
	 * because the item's il lists are empty by construction
	 * (DrawThreeDeeObject2D sets nCurrentLights and nPrevLights to zero) and
	 * calling it would only be a walk over two empty arrays and the level's
	 * dynamic lights, which have no business in either the ring or the corner
	 * of the screen.
	 *
	 * No room cube, no point lights, no spots, no dynamics - and no fog bulbs,
	 * which hd_level.cpp takes off a ring frame and never puts into the
	 * display's own bracket. The distance fog is left exactly as the classic
	 * path has it: the shader runs the same subtraction from the same
	 * tomb5.distance_fog, and both stand a few hundred units from the camera
	 * where neither path reaches the threshold. */
	static const float kRingSun[3] =
	{
		0.0f,
		-50.0f / 111.80339887f,		// sqrt(12500)
		-100.0f / 111.80339887f
	};

	entry.lights[0].x = kRingSun[0];
	entry.lights[0].y = kRingSun[1];
	entry.lights[0].z = kRingSun[2];
	entry.lights[0].kind = (float)HD_OBJLIGHT_SUN;
	entry.lights[0].r = (float)CLRR(g_ring_color);
	entry.lights[0].g = (float)CLRG(g_ring_color);
	entry.lights[0].b = (float)CLRB(g_ring_color);
	entry.lights[0].invRange = 0.0f;
	entry.lightCount = 1;
	entry.sunScale = pickup ? 0.75f : 2.0f;

	for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
	{
		entry.light[node * 3 + 0] = 8.0f;
		entry.light[node * 3 + 1] = 8.0f;
		entry.light[node * 3 + 2] = 8.0f;
	}

	memset(&entry.cut, 0, sizeof(entry.cut));
	entry.cull = HD_MODEL_CULL_NONE;
	/* No sheen, which is what an ITEM has in the level too (fix round D, D2 -
	 * the specular term is Lara's alone for now). A pistol on the ring
	 * therefore shines exactly as the same pistol shines lying on the floor,
	 * which is the answer the round was asked for. */
	entry.specStrength = 0.0f;
	entry.specPower = 1.0f;

	/* AND HERE THE TWO PART (#123, fix round P2). The ring's item takes its
	 * place on the frame's list and waits for HDObjectFrameDraw; the display's
	 * is drawn on the spot, because by the time it gets here the frame's own
	 * bracket has been opened, drawn and closed, and the list it would join is
	 * a list nobody will read again. */
	if (pickup)
	{
		pickup_draw(entry);
		return;
	}

	g_count++;
}

bool HDObjectRingFrame()
{
	return g_ring_frame;
}

int HDObjectFrameCount()
{
	return g_count;
}

int HDObjectDrawCalls()
{
	return g_draw_calls;
}

void HDObjectFrameDraw(const void* projection, int bulbs, unsigned int passes)
{
	ProfScope prof(PROF_HD_OBJDRAW);

	if (!g_count)
		return;

	HDModelFrameBegin((const D3DMATRIX*)projection, bulbs, passes);

	for (int i = 0; i < g_count; i++)
	{
		const FrameItem& entry = g_frame[i];
		HDModelLighting lighting;

		/* GROUP M, WITHDRAWN FOR ITEMS IN FIX ROUND R. The call switches the
		 * scissor test OFF for every item and takes a count on the way past;
		 * the whole argument is over HDFrameScissorForRoom in hd_level.cpp.
		 * In one line: the classic path hands every item the whole screen
		 * (game\draw.cpp, PrintObjects), round M gave it its room's portal
		 * window instead, and the play test found enemies cut in half at a
		 * doorway's edge and soldiers not drawn at all. */
		HDFrameScissorForRoom(entry.room);

		lighting.lights = entry.lights;
		lighting.lightCount = entry.lightCount;
		lighting.sunScale = entry.sunScale;
		lighting.specStrength = entry.specStrength;
		lighting.specPower = entry.specPower;

		/* THE INVENTORY RING'S OWN SCREEN CENTRE (fix round P2). Zero for
		 * every item in the world, and then HDModelOffCentre writes nothing
		 * at all - it only touches the projection register when the value
		 * really changes, so a ring frame costs one 4-register upload per
		 * item and an ordinary frame costs none. Handed over here rather
		 * than folded into entry.matrix, because the shader takes the vertex
		 * NORMAL through those matrices as well; see hd_model.h. */
		HDModelOffCentre(entry.offCentreX, entry.offCentreY);

		/* drawSlot is the SWAPPED model when the item asked for one and it
		 * arrived (B3), and the item's own slot otherwise. It is the same
		 * skeleton, the same node count and the same bind pose, so nothing
		 * else in this call changes. */
		HDModelDraw(entry.drawSlot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting, &entry.cut, entry.cull);

		/* THE SEPARATE HD FACE, right after the body and on the same
		 * matrices. VON_CROY.TRM is a body with an empty collar and
		 * VON_CROY_HEAD.TRM is the face that fills it, so the two are one
		 * object and the second is not optional.
		 *
		 * Only for a slot that HAS one (#123, fix round B, B7): the call
		 * used to be made for every entry of every frame and to end in a map
		 * lookup that found nothing, which is a lookup per item per frame
		 * for a thing five slots in a level own. The head belongs to the
		 * item's own slot, not to the swapped model - a damage stage or a
		 * drawn pistol does not change a face. */
		if (entry.head)
			HDModelDrawHead(entry.slot, entry.matrix, entry.nodes, entry.light,
				entry.mask, &lighting, &entry.cut, entry.cull);
	}

	// GROUP M: the objects are done and the window of the last one must not
	// outlive them - the caller closes the bracket next, and the bracket is
	// what the classic path gets back. P2: and neither must the last item's
	// screen centre, for the same reason - the glow phase opens a second
	// bracket on the same list and the classic path shares the register file.
	HDModelOffCentre(0.0f, 0.0f);
	HDRenderScissorOff();

	/* E6: the phases add up. HDModelFrameBegin zeroes the model file's own
	 * counter per bracket, so a frame drawn in two phases would otherwise
	 * report the second phase alone - and the second phase is the small one.
	 * The solid phase, which is the one that carries HD_PASS_OPAQUE, starts
	 * the count over. */
	if (passes & HD_PASS_OPAQUE)
		g_draw_calls = 0;

	g_draw_calls += HDModelDrawCalls();
}

void HDObjectFrameEnd()
{
	if (gLogEnabled && !g_frame_logged && g_count)
	{
		g_frame_logged = true;
		Log("HDLOG frame objects hd=%d classic=%d baked=%d dropped=%d "
			"calls=%d swap_drawn=%d swap_ignored=%d meshbits=%d "
			"clipped_lights=%d(level)",
			g_count, g_items_classic, g_items_baked, g_dropped, g_draw_calls,
			g_swap_drawn, g_swap_ignored, g_mask_used, g_clipped_lights);
	}

	g_count = 0;
}

/* ------------------------------------------------- what Lara borrows -- */

/* The three calls hd_laradraw.cpp needs out of this file, and nothing more.
 * They exist so that Lara's composite rides the machinery the objects
 * already have instead of a second copy of it: one frame list, one light
 * conversion, one bind walk, one draw. See the block at the bottom of
 * hd_object.h for the argument. */

void HDObjectBindPose(int slot, float* out, int nodes)
{
	if (!out || nodes <= 0 || slot < 0 || slot >= NUMBER_OBJECTS)
		return;

	bind_pose_of(slot, out, nodes < HD_OBJ_NODES ? nodes : HD_OBJ_NODES);
}

int HDObjectLights(ITEM_INFO* lit, HDObjLight* out, int max, float* sunScale)
{
	int count = 0;

	if (!out || max <= 0)
		return 0;

	fill_lights(out, max, &count, sunScale, lit);
	return count;
}

bool HDObjectPush(int slot, const float* matrix, int nodes, const float* light,
	unsigned int mask, const HDObjLight* lights, int lightCount, float sunScale,
	const HDModelNeckCut* cut, int cull, float specStrength, float specPower)
{
	/* NOT behind g_active. That flag says "this level has HD OBJECTS", and a
	 * level can have an HD Lara and not one HD object - JOBY4's diving suit
	 * in a level whose creatures the randomizer moved, say. What must be up
	 * is the model path itself and the part being asked for. */
	if (!tomb5.hd_objects || !matrix || nodes <= 0 || !HDModelReady() ||
		!HDModelHas(slot))
		return false;

	if (g_count >= HD_OBJ_ITEMS)
	{
		g_dropped++;
		return false;
	}

	// The frame's field of view, if nothing HD has taken it yet - Lara can be
	// the first HD thing in a frame whose rooms all stayed classic.
	HDFrameCaptureProjection();

	FrameItem& entry = g_frame[g_count];

	entry.slot = slot;
	entry.drawSlot = slot;
	entry.head = HDModelHasHead(slot);
	entry.nodes = nodes < HD_OBJ_NODES ? nodes : HD_OBJ_NODES;
	entry.mask = mask;

	memcpy(entry.matrix, matrix, sizeof(float) * 16 * entry.nodes);

	for (int node = 0; node < entry.nodes; node++)
	{
		entry.light[node * 3 + 0] = light ? light[node * 3 + 0] : 128.0f;
		entry.light[node * 3 + 1] = light ? light[node * 3 + 1] : 128.0f;
		entry.light[node * 3 + 2] = light ? light[node * 3 + 2] : 128.0f;
	}

	entry.lightCount = 0;

	if (lights && lightCount > 0)
	{
		entry.lightCount = lightCount < HD_MAX_OBJ_LIGHTS
			? lightCount : HD_MAX_OBJ_LIGHTS;

		for (int i = 0; i < entry.lightCount; i++)
			entry.lights[i] = lights[i];
	}

	entry.sunScale = sunScale;

	if (cut)
		entry.cut = *cut;
	else
		memset(&entry.cut, 0, sizeof(entry.cut));

	entry.cull = cull;
	entry.specStrength = specStrength;
	entry.specPower = specPower;
	/* GROUP M: no portal window. Everything that comes through here is a
	 * piece of Lara, and Lara is in the camera's own room, whose window is
	 * the whole screen - and a piece of her that reaches out of that room,
	 * an arm through a doorway in third person, is the one thing in the
	 * frame that must never be cut. */
	entry.room = -1;
	entry.view = ViewSerial;	// fix round R
	entry.offCentreX = 0.0f;	// P2: Lara is never on the ring
	entry.offCentreY = 0.0f;

	g_count++;
	return true;
}

/* THE CAMERA THE OBJECTS OF THIS FRAME WERE COLLECTED WITH (fix round R).
 * Lowest and highest ViewSerial over the frame list; equal numbers mean one
 * camera, a spread means the frame was collected across a view rebuild. Both
 * are 0 for a frame with no HD object at all. */
void HDObjectViewSerials(long* lo, long* hi)
{
	long a = 0, b = 0;

	for (int i = 0; i < g_count; i++)
	{
		if (!i || g_frame[i].view < a) a = g_frame[i].view;
		if (!i || g_frame[i].view > b) b = g_frame[i].view;
	}

	if (lo) *lo = a;
	if (hi) *hi = b;
}

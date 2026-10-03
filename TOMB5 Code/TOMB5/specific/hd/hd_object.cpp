/*
 * Modified 2026 by ChrisChanSan.
 *
 * The routing rules are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3) - see hd_object.h for the list, function by function. The seam under
 * them, the skeleton test and the light conversion are new. So is the
 * inventory ring at the bottom of this file, except for one idea that is
 * TROne's: that the ring's off-centre projection can be folded into the item
 * for the length of one draw (games\tr5\glue\hostglue_2d5.cpp:502-506), which
 * is what lets the HD path keep one projection for the whole frame. TROne
 * folds it into the item's matrix as a shear; this engine folds it into the
 * projection, because the shader takes the vertex normal through those
 * matrices - the whole argument stands over HDInvNode. The arithmetic is
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
#include "hd_classic.h"			// the donor game's frame 0
#include "hd_install.h"
#include "hd_markers.h"			// the .hdm sidecar - the donor slots
#include "hd_mods.h"			// HDModsScan - the mods folder
#include "hd_pagecache.h"		// HDPageCacheAltRoot, for a guest's own pages
#include "hd_render.h"
#include "hd_level.h"
#include "hd_objlight_dark.h"		// which engine lights give nothing
#include "hd_shadow.h"			// HDShadowLight - the projected shadow's key light
#include "hd_trl.h"			// the lips' sources, for LIPLOG
#include "../function_stubs.h"
#include "../profile.h"			// PROFLOG phases
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
#include "../../game/deltapak.h"		// GLOBAL_playing_cutseq, cutseq_num - the lips
#include "../../game/gameflow.h"		// Gameflow - the language LIPLOG names
#include "../drawroom.h"		// mesh_vtxbuf - the classic mesh's own box

#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

/* A classic TR5 moveable has at most 32 meshes and the joint palette in the
 * shader is 32 registers wide, so 32 is the number everywhere. */
#define HD_OBJ_NODES	32

/* How many HD items one frame can hold. draw_rooms holds as many rooms as a
 * level may have (256) and every one of them can hold items, so this is not
 * a bound that follows from the level data - it is a fixed array with a
 * counter, and the counter goes into the log if it ever bites. 128 items is
 * 1.1 MiB of static, the price of a frame path that cannot fail. */
#define HD_OBJ_ITEMS	128

/* Pseudo slots for the alternative models. Chosen clear of the engine's 471
 * slots and of Lara's wardrobe block (HD_LARA_SLOT_BASE, 0x5A00), because
 * hd_model keeps its models in one map keyed by slot number and two of them
 * must never collide. */
#define HD_SWAP_SLOT_BASE	0x6000
#define HD_SWAP_NODE_BASE	0x8000

/* And one for a mod's inventory model - the <BASE>_OPTION file the ring and
 * the pickup display draw instead of the slot's own EXTRA model. Between the
 * two blocks above and clear of both: the swap block reaches 0x6000 + 470 and
 * the node block starts at 0x8000. */
#define HD_RING_SLOT_BASE	0x7000

namespace
{
	// A mod's inventory model for one slot - the plan is below, next to the
	// ring; the verdict only carries a pointer to it.
	struct RingModel;

	/* What one slot is allowed to do, decided once at level build. */
	struct Verdict
	{
		unsigned char hd;	// 1 = this slot's items draw HD
		unsigned char baked;	// 1 = the art already has it, draw nothing
		unsigned char head;	// 1 = the slot has its own <NAME>_HEAD.TRM
		const char* why;	// why not, for the log
		/* Which rule named the model: the very string the HDLOG bind line
		 * printed at level build, kept so that the ring's own line can say it
		 * again without asking the disk a second time. A static literal out
		 * of via_label() or "donor". */
		const char* via;
		int nodes;		// the classic mesh tree's node count
		int maxBone;		// the highest node the .TRM names
		float drift;		// the bind drift, mean over the covered nodes
		float worst;
		unsigned int covered;
		/* The "this item shows nothing" line is written once per slot per
		 * level and not thirty times a second. */
		unsigned char maskSaid;
		/* The floor lift, in world units, and zero for every slot that is not
		 * a pickup or whose model does not sink. Measured once at level build
		 * and applied in the object path - see the floor rule at the plan
		 * loop. */
		float lift;
		/* The ring plan of this slot, or null. Kept here so the frame path
		 * reads level data instead of walking the plan per HD item per
		 * frame. */
		const RingModel* ring;
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
		int drawSlot;			// the alternative model, or == slot
		bool head;			// this slot has a separate HD face
		int nodes;
		unsigned int mask;		// item->mesh_bits
		float matrix[HD_OBJ_NODES * 16];
		float light[HD_OBJ_NODES * 3];
		HDObjLight lights[HD_MAX_OBJ_LIGHTS];
		int lightCount;
		float sunScale;
		HDModelNeckCut cut;	// first-person neck cut: Lara's body alone
		int cull;		// and the one-sided draw that goes with it
		HDModelSoft soft;	// the chest's secondary motion: zeros but for Lara's body
		/* The projected shadow's mark (HD_OBJ_CAST_*) and the node mask the
		 * map draws the entry with: her parts for her map, an item whose blob
		 * gave way for the second map (HD_OBJ_CAST_OTHERS), and
		 * HD_OBJ_CAST_NONE for every other entry and in every frame the
		 * projected shadow does not run in. */
		int cast;
		unsigned int castMask;
		/* The sheen. 0 for an item - the specular term is Lara's alone. */
		float specStrength;
		float specPower;
		/* The room this thing stands in, so that it is drawn
		 * inside the same portal window its room is - `item->room_number`
		 * for an item, and -1 for anything pushed without one, which is
		 * Lara's whole composite (she is in the camera's room and the
		 * camera's room is the whole screen). */
		int room;
		/* Which camera this was collected with: ViewSerial at the moment
		 * the matrices were taken; see the note over it in
		 * specific\3dmath.cpp. Read only by the log. */
		long view;
		/* The inventory ring's own screen centre, as the two numbers
		 * HDModelOffCentre wants: (X - centreX) / persp and the same in y.
		 * Zero for everything in the world, which is every item outside the
		 * inventory, and then not a register is written.
		 *
		 * It rides on the item and not on the matrices - see the note over
		 * HDModelOffCentre in hd_model.h. The same offset put into the node
		 * matrices as a shear would move the item's normals with it and light
		 * the ring by screen position. */
		float offCentreX;
		float offCentreY;
		/* A speaking face's cutscene track and the frame of it to show -
		 * see HDObjectLips. -1 and -1 for everything that does not speak. */
		int lipsTrack;
		int lipsFrame;
		/* The mirror's three clip planes in view space, n.xyz and d each,
		 * for HDModelClip - (0, 0, 0, 1) three times for everything that is
		 * not a reflection, which keeps every pixel. */
		float clip[12];
	};

	FrameItem g_frame[HD_OBJ_ITEMS];
	int g_count = 0;
	int g_dropped = 0;

	/* The item the engine is walking right now. */
	bool g_open = false;
	int g_open_slot = -1;
	int g_open_swap = -1;		// the alternative model this item wants
	int g_open_nodes = 0;
	// The room of the item being walked, kept from
	// HDObjectBegin (which has the ITEM_INFO) to HDObjectEnd (which has not).
	int g_open_room = -1;
	unsigned int g_open_mask = 0;
	float g_raw[HD_OBJ_NODES * 12];		// the engine's own 3x4, per node
	unsigned int g_seen = 0;
	// The lips of the item being walked (HDObjectLips), -1 for none.
	int g_open_lips_track = -1;
	int g_open_lips_frame = -1;
	/* The items being walked are a mirror's reflection (HDObjectReflection),
	 * how many of them were recorded since the mirror's log line last asked,
	 * and the clip planes the reflection is stamped with. */
	bool g_reflection = false;
	int g_reflected = 0;
	float g_clip[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };

	/* The second map's casters. game\draw.cpp asks HDObjectBlobGivesWay
	 * right before an item's blob and then opens the same item with
	 * HDObjectBegin: `g_gave_way` is the item whose blob gave way, which
	 * HDObjectBegin takes over into `g_open_casts` and clears, and
	 * `g_open_place` is the item's world position, the place the second box
	 * is asked about in HDObjectEnd. */
	const ITEM_INFO* g_gave_way = 0;
	bool g_open_casts = false;
	float g_open_place[3] = { 0.0f, 0.0f, 0.0f };

	/* The entry's clip planes: the mirror's while a reflection is recorded,
	 * planes that keep every pixel otherwise. */
	void stamp_clip(float* clip, bool reflected)
	{
		static const float kKeepAll[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };

		memcpy(clip, reflected ? g_clip : kKeepAll, sizeof(kKeepAll));
	}
	/* A clip is open somewhere - set by the first face that speaks, and what
	 * lets HDObjectFrameReset close them all once the cut is over without
	 * asking the model file every frame. */
	bool g_lips_playing = false;

	/* Per frame, emptied by HDObjectFrameReset - they go into a line that
	 * says "frame", so they have to mean one. */
	int g_items_classic = 0;
	int g_swap_ignored = 0;
	int g_swap_drawn = 0;
	int g_mask_used = 0;
	int g_items_baked = 0;

	/* ------------------------------------------------ the inventory ring -- */

	/* The item being walked came out of newinv2.cpp and carries the ring's
	 * own screen centre. Kept apart from g_open so that an ordinary item can
	 * never pick up a ring's centre and a ring item can never pick up a
	 * room. */
	bool g_ring_open = false;
	/* The three the pickup display borrows. The off-centre pair and the
	 * light colour are a property of the call rather than of the ring,
	 * and the two callers cannot overlap: every place that sets
	 * InventoryActive puts a mono screen up first, and S_OutputPolyList
	 * blocks the display's whole block while one is up
	 * (specific\output.cpp). One pair, not two that have to be kept in
	 * step. */
	float g_ring_kx = 0.0f;
	float g_ring_ky = 0.0f;
	long g_ring_color = 0;

	/* Per frame. `g_ring_frame` is read by hd_level.cpp (the fog bulbs); the
	 * two counters and the little list are the log line, which is written
	 * once per opening of the ring rather than once a frame - the ring is
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
		/* The item's depth: node 0's view z, which is the push down +z the
		 * ring gives the item - INVOBJ::scale1, plus the item's own root
		 * offset out of the animation frame.
		 *
		 * The table in game\newinv2.cpp (inventry_objects_list) holds the
		 * built-in scale1 - 0x3E8 for the pistols, 0x898 for the passport
		 * pages - and init_new_inventry moves the binoculars in code. But it
		 * is not fixed: LoadGameflow (game\gameflow.cpp) overwrites objname,
		 * yoff, scale1 and the three rotations out of SCRIPT.DAT for every
		 * slot the level names, which is every key, puzzle, pickup and
		 * examine item. So the number is per level, and the log line says it.
		 *
		 * It also decides whether a ring item stands in front of the frozen
		 * backdrop, which S_DrawTile puts at sz 0.995 - the far plane, since
		 * the engine runs with znear 20 and zfar 20480
		 * (specific\gamemain.cpp, InitWindow) and sz is f_a - f_b/z. */
		float z;
	};

	/* Six on the ring, three in the ammo selector, one in the keypad and one
	 * in the examine screen - and the two rings can stand at once. Sixteen is
	 * four times what a frame has been seen to hold. */
	RingLogItem g_ring_log[16];
	int g_ring_log_count = 0;

	/* ------------------------------------------------ the pickup display -- */

	/* The second caller of DrawInventoryItemMe.
	 *
	 * S_DrawPickup (specific\output.cpp) walks the very same mesh tree through
	 * the very same four call sites; what is different is when. It runs inside
	 * S_OutputPolyList, after HDFrameDraw has drawn this frame's HD list and
	 * after HDFrameDrawGlow has finished with it, so an item pushed onto that
	 * list would suppress its own classic polygons and be drawn by nobody.
	 *
	 * So it is drawn on the spot: the node walk collects the matrices
	 * exactly as the ring's does, and HDInvEnd opens a bracket of its own
	 * (hd_level.cpp, HDFramePickupBegin), draws the one object and closes it
	 * again. Nothing reaches the frame list and g_count is not touched, so the
	 * list that HDObjectFrameReset empties on the next picture is still the
	 * world's alone.
	 *
	 * `g_pickup_open` is the mode the walk is in. The numbers below it are
	 * the display's own projection, read at HDInvBegin out of f_persp /
	 * f_centerx / f_centery / f_a / f_b - not out of HDFrameCaptureProjection,
	 * which took the field of view the world was collected with and can be a
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

	/* One line per slot per level. The display can show the same medipack two
	 * hundred times in a level and the report is the same line every time, so
	 * the slot is struck off the first time it is drawn. Cleared where the
	 * level's own line is written, in HDObjectLevelBuild. */
	bool g_pickup_logged[NUMBER_OBJECTS];

	/* The engine's 3x4 into the row-major 4x4 the shader wants, once per node
	 * of the item just walked - the same conversion SetD3DMatrix does for the
	 * fixed-function pipe, and the one the rooms use.
	 *
	 * A node the walk never reached keeps the last one that was: TROne's
	 * fill_unseen_joints (game_renderer.cpp:1391), and the reason is that a
	 * slightly wrong limb beats a limb collapsed onto the camera. It cannot
	 * happen from DrawAnimatingItem or DrawInventoryItemMe, both of which walk
	 * every node - but HDObjectNode and HDInvNode are public calls and a zero
	 * matrix is a divide by zero.
	 *
	 * Shared by the world and the ring, so the matrix convention exists once. */
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

	/* The baked slots. Set per level and read in HDObjectBegin before the
	 * object path's own guard, because an object
	 * that is already in the room art is drawn twice whether hd_objects is on
	 * or off - the second copy comes out of the .TRG either way. */
	bool g_baked_any = false;
	int g_baked_slots = 0;

	/* ------------------------------------------ a mod's inventory model --
	 *
	 * The ring and the pickup display draw the <BASE>_OPTION file a mod ships
	 * instead of the slot's own EXTRA model; hd_bind.h says when and why. The
	 * pickup on the floor draws it too - see the floor rule at the plan loop
	 * for what that takes.
	 *
	 * The model is another game's, so it stands in another frame: the I-III
	 * inventory models are built upright and IV-VI's lie flat, and the origin
	 * is wherever that artist put it. `fix` is what puts one into the frame
	 * the ring's matrices are made for - its own bounding box centred and
	 * turned so that its longest axis lies where the EXTRA model's longest
	 * lies, then scaled by the two longest axes' ratio. It is measured off the
	 * two boxes at level build, per slot, and no rotation is written down
	 * anywhere: the two files say which way round they are.
	 *
	 * A short fixed list rather than a per-slot array: a level has one or two
	 * of these at the outside, and the lookup runs once per ring item. */
	struct RingModel
	{
		int slot;		// the level's own slot, e.g. UZI_ITEM
		int model;		// the pseudo slot the OPTION file sits in
		float fix[16];
		/* And what the floor costs it. The fit centres the model on the
		 * item's box, which puts part of a model thicker than the item below
		 * the item's own lowest point - under the floor, for a thing lying on
		 * the ground. This is how far it has to come back up, measured on the
		 * fitted vertices against the classic mesh's own deepest point; zero
		 * for a model that already lies on it.
		 *
		 * It is applied on the floor and nowhere else: the ring and the
		 * corner display have no floor and draw `fix` unchanged.
		 *
		 * It is not folded into a copy of the fit: a lift is a move in the
		 * world and the fit's translation row is the item's frame - see the
		 * lift in HDObjectEnd for a turned item, where the two differ. */
		float lift;
		float sank;		// what it stood at before the lift, for the log
	};

	RingModel g_ring_model[16];
	int g_ring_models = 0;

	/* The frame path asks this of every HD item, and the answer is level
	 * data: g_slot[].ring holds it, set where the plan is built. The walk
	 * over the list (at most sixteen, usually empty) is the fallback for a
	 * slot number outside the array. */
	const RingModel* ring_model_of(int slot)
	{
		if (slot >= 0 && slot < NUMBER_OBJECTS)
			return g_slot[slot].ring;

		for (int i = 0; i < g_ring_models; i++)
			if (g_ring_model[i].slot == slot)
				return &g_ring_model[i];

		return 0;
	}

	/* ------------------------------------------ the floor a pickup lies on --
	 *
	 * The reference is the classic mesh and not "the floor": a pickup is
	 * drawn at its frame's own offset, not at the item origin, and the offsets
	 * are large - ANDREA3's classic PISTOLS_ITEM mesh has its deepest point 59
	 * units below its node and its CROWBAR_ITEM one unit above. So the ground
	 * is wherever the level's own picture ends, and that is what an HD model
	 * has to meet. The frame offset reaches both meshes through the same
	 * matrix, so the difference below is free of it - and a rule stated on
	 * that difference holds for a pedestal and for a wall place (OCB 2/9) with
	 * no case of its own, because those move the item and both meshes with it.
	 *
	 * In the frame the level draws it in, which is `pose` - HDFramePose's
	 * twelve floats a node. The animation frame turns node 0 before the first
	 * mesh is drawn, and a rule measured in the bind pose is a rule about a
	 * picture nobody sees. The bbox ProcessMeshData took at level load cannot
	 * answer that: a box under rotation is a bound and not a mesh. So this
	 * walks the mesh's own vertices - the engine keeps them, ACMESHVERTEX by
	 * ACMESHVERTEX, in MESH_DATA::aVtx (specific\drawroom.cpp) - and with an
	 * identity pose it comes out at bbox[4] again. y is down, so the deepest
	 * point is the largest y.
	 *
	 * Over the whole object and not node 0 alone, each node's mesh at the
	 * place the mesh tree puts it: a two-mesh pickup (HK_ITEM carries the
	 * laser sight on node 1) would otherwise be measured against half of
	 * itself. `mesh_index` counts in the doubled array LoadObjects builds -
	 * node n is at mesh_index + 2n, the odd entry beside it being the mesh
	 * swap - which is exactly how DrawAnimatingItem walks it.
	 *
	 * And over nodes the item may not show, a known gap. InitialiseItem gives
	 * HK_ITEM, HK_AMMO_ITEM, CROSSBOW_ITEM and REVOLVER_ITEM mesh_bits = 1
	 * (game\items.cpp), so the level draws node 0 of those four and nothing
	 * else - and both sides of this rule sweep every node anyway. The two
	 * sides are consistent, and the rule is a difference, so a node hidden on
	 * both only matters where the hidden nodes of the one reach deeper than
	 * those of the other.
	 *
	 * Measured over the fifteen shipped levels, node 0 alone against the whole
	 * object: HK_ITEM -4.1 either way, HK_AMMO_ITEM -2.7 either way,
	 * CROSSBOW_ITEM -0.9 (one node, nothing to hide), REVOLVER_ITEM in
	 * ANDREA3 -10.4 either way - and one row that does change, REVOLVER_ITEM
	 * in JOBY2 and JOBY5: the whole object reaches 15.3 against the classic
	 * mesh's 14.3 and is lifted 1.1 units, while node 0 alone reaches 12.5 and
	 * would not be lifted at all. The deepest 1.1 units of that model are on
	 * nodes 1 and 2, which the item does not draw: one millimetre at 1024
	 * units to a metre.
	 *
	 * Closing the gap is not free: the mask is the item's and this rule is per
	 * slot, so an item with a mask of its own would want a lift of its own,
	 * and the only other place in TR5 that touches a pickup's mask sets it to
	 * zero (InitialisePickup, for an item whose trigger_flags carry 0x100,
	 * game\init.cpp), which draws nothing at all and so has no floor to meet. */
	bool classic_lowest_of(int slot, const float* pose, int nodes, float* out)
	{
		/* The pose is required. Its translation is where the mesh tree and
		 * the frame together put the node, so without it a node past the
		 * first would be measured in its own frame and not the item's. */
		if (!pose || !mesh_vtxbuf || slot < 0 || slot >= NUMBER_OBJECTS ||
			!objects[slot].loaded)
			return false;

		const OBJECT_INFO& object = objects[slot];
		const int count = nodes < object.nmeshes ? nodes : object.nmeshes;
		bool any = false;
		float deepest = 0.0f;

		for (int node = 0; node < count; node++)
		{
			const int at = object.mesh_index + node * 2;

			if (at < 0 || at >= num_level_meshes)
				continue;

			const MESH_DATA* mesh = mesh_vtxbuf[at];

			// A mesh with no vertices at all has nothing to measure:
			// ProcessMeshData allocates no points for it.
			if (!mesh || !mesh->nVerts || !mesh->aVtx)
				continue;

			const float* m = pose + (size_t)node * 12;

			for (int v = 0; v < mesh->nVerts; v++)
			{
				const ACMESHVERTEX& one = mesh->aVtx[v];
				const float y = m[4] * one.x + m[5] * one.y +
					m[6] * one.z + m[7];

				if (!any || y > deepest)
					deepest = y;

				any = true;
			}
		}

		if (any && out)
			*out = deepest;

		return any;
	}

	/* And the frame itself, per slot. HDFramePose does the walk; this is
	 * where the engine's own three pieces of it are - the frame the draw
	 * reads, the object's slice of the bone array, and its node count.
	 *
	 * The animation's first frame is the one a pickup is drawn at, and that
	 * is the anim's pointer and not the object's. InitialiseItem sets
	 * anim_number = objects[slot].anim_index and frame_number =
	 * anims[anim_number].frame_base (game\items.cpp); no pickup has a control
	 * routine that advances its frame; GetFrames then starts at
	 * anims[anim_number].frame_ptr and adds size * ((frame_number -
	 * frame_base) / rate), which is zero frames (game\draw.cpp). So frame_ptr
	 * is the pointer DrawAnimatingItem unpacks its rotations from.
	 *
	 * OBJECT_INFO::frame_base is the same pointer on all fifteen shipped
	 * levels; where the two differ, the difference is logged and the anim's
	 * frame - the one the engine draws - is measured. */
	const short* first_frame_of(int slot)
	{
		const OBJECT_INFO& object = objects[slot];

		/* A slot with no animation has nothing but its rest pose, and that is
		 * what the bind walk was: HDFramePose takes a null frame as identity
		 * either way. */
		if (!anims || object.anim_index < 0)
			return object.frame_base;

		const short* animFrame = anims[object.anim_index].frame_ptr;

		if (!animFrame)
			return object.frame_base;

		if (animFrame != object.frame_base)
		{
			/* Does not happen on the fifteen shipped levels; if it ever
			 * does, the line says which slot, and the numbers under it are
			 * the animation's frame - the level's picture - while the
			 * object's rest pose is another one. */
			Log("HDLOG level=%s pickup floor slot=%d name=%s the object's own "
				"first frame and anim %d's differ - the rule measures the "
				"anim's, which is the one the draw reads", g_stem, slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?",
				(int)object.anim_index);
		}

		return animFrame;
	}

	int frame_pose_of(int slot, float* out /* nodes * 12 */, int nodes)
	{
		return HDFramePose(first_frame_of(slot),
			bones ? &bones[objects[slot].bone_index] : 0, nodes, out);
	}

	/* Which way up the level draws this pickup, one line per measured slot.
	 * The floor rule's own two lines print numbers that only mean anything in
	 * a stated frame, and this is the frame: where node 0's own y axis points
	 * once the item's animation frame has turned it, in the item's frame,
	 * taken out of the very matrix the rule measured through. (0 1 0) is a
	 * frame that turns nothing or turns only about y, and then the numbers
	 * are the bind pose's; (0 -1 0) is a model standing on its head, which is
	 * what ANDREA3's UZI_ITEM does - measured in the bind pose it would be
	 * lifted the wrong way.
	 *
	 * `blended` counts the model's vertices that ride on more than one node -
	 * zero on every pickup file measured, and a number here says the sweep
	 * read that model off its heaviest bone alone. */
	void frame_turn_line(int slot, const float* pose, int model)
	{
		int kept = 0, blended = 0;

		HDModelPointsInfo(model, &kept, &blended);

		Log("HDLOG level=%s pickup floor slot=%d name=%s drawn frame: node 0's "
			"y axis points (%.3f %.3f %.3f) in the item's frame, %d vertices "
			"swept, %d of them blended", g_stem, slot,
			HDSlotName(slot) ? HDSlotName(slot) : "?",
			pose[1], pose[5], pose[9], kept, blended);
	}

	/* What HDModelRingFitOverflow says, in words. Below 1.05 the model is in
	 * the box for any purpose the ring has, and the number itself is printed
	 * as soon as it is not. The buffer is consumed by the Log call on the
	 * next line and nowhere else. */
	const char* box_verdict(float over, int axis)
	{
		/* snprintf and not sprintf: `over` is a ratio of two measured extents
		 * and its printed width is not bounded by anything this file knows -
		 * a target axis of a thousandth of a unit would write a number longer
		 * than the sentence around it. */
		static char said[128];
		/* The target's own axes, longest first, and not x/y/z: the fit pairs
		 * the two models' shapes, so the axis that is overflowed is one of
		 * the shape's, and naming it x would name a direction the rule never
		 * used. */
		static const char* kAxis[3] =
		{
			"its longest axis", "its middle axis", "its flattest axis"
		};

		if (over <= 0.0f)
			std::snprintf(said, sizeof(said), "how far it leaves that room "
				"could not be measured");
		else if (over < 1.05f)
			std::snprintf(said, sizeof(said),
				"and it stays inside that room (%.2fx)", over);
		else
			std::snprintf(said, sizeof(said), "and it stands %.2fx out of "
				"the room the ring keeps for it, on %s - which the fit"
				" allows, see HDModelRingFit",
				over, axis >= 0 && axis < 3 ? kAxis[axis] : "an axis");

		return said;
	}

	// out = a then b, both row-major with the translation in row 3 - the
	// convention every matrix in this file and in hd_model is written in.
	void ring_mul(const float* a, const float* b, float* out)
	{
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
			{
				float sum = 0.0f;

				for (int k = 0; k < 4; k++)
					sum += a[r * 4 + k] * b[k * 4 + c];

				out[r * 4 + c] = sum;
			}
	}

	/* ------------------------------------------------------ the mesh swaps --
	 *
	 * A .TRM is one whole model and carries no second geometry for a node,
	 * but the remaster ships whole alternative models; which they are is
	 * measured over 5\ITEM against the engine's own swap code.
	 *
	 *   MAFIA2_SWAP.TRM   MAFIA2.TRM has his pistols in the shoulder holsters
	 *                     and empty fists - which is his initial state,
	 *                     meshswap_meshbits 0x2400 (game\mafia2.cpp, the
	 *                     hands swapped out of MESHSWAP2). When he draws them
	 *                     the engine writes 0x80 instead, the torso swap, and
	 *                     MAFIA2_SWAP.TRM is the same man with the guns in
	 *                     his hands and the holsters empty. Two pictures,
	 *                     one bit; without it he aims with a bare fist.
	 *   HITMAN_<n+1>.TRM  the cyborg's eleven damage stages, cumulative. The
	 *                     file numbers are 5,6,7,8,11,12,13,14,15,16,20 and
	 *                     the panel nodes are 4,5,6,7,10,11,12,13,14,15,19 -
	 *                     the same eleven, off by one, and the same eleven
	 *                     game\hitman.cpp opens. Their vertex counts rise
	 *                     along the opening order (16,15,14,7,6,13,8,5,11,
	 *                     12,20 -> 4115..5548 against the closed body's
	 *                     4035), which is what "cumulative" means: the right
	 *                     file is the one for the panel opened last.
	 *
	 * Every other swap in the game is a soldier's hands out of MESHSWAP1
	 * (setup.cpp), and there the remaster has no second file because it does
	 * not need one - MAFIA.TRM already holds the pistol in the hand, and so
	 * do SWAT, SWAT_PLUS and the guards. Those swaps are ignored.
	 *
	 * The table is keyed on the model name, not on the slot: the randomizer's
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

	/* Which alternative model this item wants, or -1 for the plain one. */
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

	/* Per level: how often a light had to be turned away at the shader's
	 * twelve. Once is worth knowing, every frame is noise. */
	int g_clipped_lights = 0;
	int g_draw_calls = 0;
	bool g_checked = false;
	bool g_frame_logged = false;

	/* The threshold of the bind drift lives in hd_model.h, because the
	 * diagnostic tool has to judge a slot by the same number the game does.
	 * docs\Engine-Manual.md describes the measurement: ModelProbe -all over
	 * all fourteen randomized levels and their vanilla twins, 700-odd bound
	 * slots. The highest drift of a model that belongs where it stands is
	 * 239 and the lowest of one that does not is 308, and 256 sits in that
	 * gap - nearer the accepted end, because refusing a good model costs a
	 * classic-looking door and accepting a bad one costs a torn body.
	 *
	 * It is only the second test. The first - does the .TRM name a node the
	 * slot does not have - is a certainty rather than a number, and it is the
	 * one that catches FIREROPE in eleven of the fourteen. */
	const float kDriftLimit = HD_SKELETON_DRIFT_LIMIT;

	/* ------------------------------------------------------ the bind pose -- */

	/* The slot's bind pose, out of the engine's own mesh tree. Four longs a
	 * node after the first - flags, x, y, z - and the flags carry the matrix
	 * stack: bit 0 pops, bit 1 pushes, and the push happens before the
	 * translation, so what is pushed is the parent's frame. It is
	 * DrawAnimatingItem's walk with every rotation left out, which is what a
	 * .TRM's vertices are measured against.
	 *
	 * The same walk stands in ModelProbe (ReadClassicSlot) against the .TRC
	 * on disk; this one reads the loaded level, so what the game binds is
	 * what the tool measured. */
	/* `tree`, when given, comes back as the parent of every node - the node
	 * whose frame this one's offset was added to, and -1 for the root. It
	 * falls out of the same walk: the frame `current` holds at any moment
	 * belongs to exactly one node, and the push/pop stack carries that
	 * number along with the position. A separate HD face needs it, because
	 * its neck belongs to the node the head node hangs off. */
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

	/* World -> view. aW2VMx holds the camera's rotation in M00..M22 and its
	 * world position in M03 / M13 / M23, so a point is R * (p - c) - which is
	 * the very line phd_TranslateAbs computes (specific\3dmath.cpp).
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

	/* World up, in view space - the direction a lifted pickup moves in. Not
	 * the node matrix's own y row, which is the image of the model's y axis:
	 * for a slot whose frame turns node 0 upside down - UZI_ITEM's turns it
	 * 180 degrees about z - that would push the model further into the floor
	 * by exactly the amount it is meant to come out of it.
	 *
	 * The node matrices are model space to view space, so the displacement has
	 * to be a view-space vector, and world up is (0, -1, 0) because y is down.
	 * One vector per camera: the same answer for every node of every item, so
	 * the frame path adds three mul-adds a node and nothing else. Whatever the
	 * item's rotation or its frame's, the lift is straight up by construction. */
	long g_up_view_serial = -1;
	float g_up_view[3] = { 0.0f, -1.0f, 0.0f };

	const float* world_up_in_view()
	{
		if (g_up_view_serial != ViewSerial)
		{
			world_to_view(0.0f, -1.0f, 0.0f, true, g_up_view);
			g_up_view_serial = ViewSerial;
		}

		return g_up_view;
	}

	/* Where the lights go. A sink rather than the FrameItem itself, so that
	 * Lara's composite can ask for exactly this list without being an item -
	 * see HDObjectLights at the bottom of this file. */
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

	/* The object's lights, from the very lists the classic path reads.
	 *
	 * SuperSetupLight (specific\lighting.cpp) turns each of them into a
	 * direction in the node's model space and a falloff measured from the
	 * node's world position. Both of those are per node and the shader can do
	 * better than either, so what travels here is the light itself: a sun's
	 * direction, everything else's position, all in view space. The fade and
	 * the range test are not redone - `Active` and the faded r/g/b are the
	 * engine's own answer, worked out in CreateLightList a moment ago inside
	 * CalculateObjectLighting. One thing is added to `Active`: an entry whose
	 * colour has faded below HD_OBJLIGHT_DARK_BOUND in every channel is left
	 * out (hd_objlight_dark.h). FadeLightList leaves such a remainder Active
	 * after a fade-out more often than not, it lights nothing, and counted it
	 * would keep the list from being empty - the one case in which the model
	 * shader takes the eye as the lamp of the sheen. The classic path keeps
	 * the entry and adds its nothing; the light a vertex gets is the same.
	 *
	 * The order matters. Suns first because there are at most a handful
	 * and they light the whole body; then the dynamics, because a torch, a
	 * flare and a gun flash are what a player watches for; then the room's own
	 * points and spots. Only past twelve does anything fall off the end, and
	 * then it is the least of them.
	 *
	 * `report` is for the log: when it is given, it comes back with what the
	 * two engine lists held, what went on and what was left out. Lara's draw
	 * asks for it with -log only; the item path never does.
	 */
	float brightest(const PCLIGHT& light)
	{
		const float rg = light.r > light.g ? light.r : light.g;

		return rg > light.b ? rg : light.b;
	}

	void report_dark(HDObjLightReport* report, const PCLIGHT& light)
	{
		if (!report)
			return;

		const float top = brightest(light);

		if (!report->dark || top > report->maxDark)
			report->maxDark = top;

		report->dark++;
	}

	void report_lit(HDObjLightReport* report, const PCLIGHT& light, bool handed)
	{
		if (!report || !handed)
			return;

		const float top = brightest(light);

		if (report->minLit <= 0.0f || top < report->minLit)
			report->minLit = top;
	}

	void fill_lights(HDObjLight* list, int max, int* countOut, float* sunScale,
		ITEM_INFO* lit, HDObjLightReport* report = 0)
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

		if (report)
		{
			memset(report, 0, sizeof(*report));
			report->curCount = (int)counts[0];
			report->prevCount = (int)counts[1];

			for (int list = 0; list < 2; list++)
			{
				PCLIGHT* source = (PCLIGHT*)lists[list];

				for (long i = 0; source && i < counts[list]; i++)
					if (source[i].Active)
						(list ? report->prevActive : report->curActive)++;
			}
		}

		/* --- the suns ---------------------------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active || source[i].Type != LIGHT_SUN)
					continue;

				// A faded remainder: nothing to give, see above.
				if (HDObjLightIsDark(source[i].r, source[i].g, source[i].b))
				{
					report_dark(report, source[i]);
					continue;
				}

				/* Minus one, because the level files carry the direction
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

				report_lit(report, source[i], add_light(out, light));
			}
		}

		const int beforeDynamics = out.count;

		/* --- the dynamics ------------------------------------------------ */
		for (int i = 0; i < 32; i++)
		{
			DYNAMIC* dynamic = &dynamics[i];

			if (!dynamic->on)
				continue;

			/* SuperSetupDynamicLight's own falloff, which is not the room
			 * path's: an object uses falloff - (falloff >> 3), a roomlet
			 * (falloff >> 1) + (falloff >> 3). */
			const float falloff = (float)(dynamic->falloff - (dynamic->falloff >> 3));

			if (falloff <= 0.0f)
				continue;

			/* The engine tests the distance at each node and drops the light
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

		if (report)
			report->dynamics = out.count - beforeDynamics;

		/* --- the room's points and spots --------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active)
					continue;

				/* A shadow light darkens the ambient and casts nothing; the
				 * engine has already taken it off aAmbient. */
				if (source[i].Type != LIGHT_POINT && source[i].Type != LIGHT_SPOT)
					continue;

				// A faded remainder: nothing to give, see above.
				if (HDObjLightIsDark(source[i].r, source[i].g, source[i].b))
				{
					report_dark(report, source[i]);
					continue;
				}

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

				report_lit(report, source[i], add_light(out, light));
			}
		}

		if (report)
			report->handed = out.count;

		if (countOut)
			*countOut = out.count;
	}

	/* ------------------------------------------------------- the self-test -- */

	/* Two checks, run on the first HD item of a level and written into the
	 * log. Neither of them is a self-test of a formula: both run the engine's
	 * own code on one side.
	 *
	 * itemcheck - a node's origin lands in the same pixel on both paths. The
	 *   node matrix already carries the origin into view space (its
	 *   translation is the view position of the node), so the classic side is
	 *   the three lines of aTransformLightClipMesh and the HD side is the
	 *   projection matrix the card is handed, through the viewport rule the
	 *   card really applies.
	 *
	 * lightcheck - the shader's light arithmetic gives the classic answer. The
	 *   classic side is aTransformLightClipMesh evaluated on the engine's own
	 *   SunLights / PointLights / SpotLights, which InitObjectLighting filled
	 *   a moment ago for this item at node 0; the HD side is the same
	 *   arithmetic on the view-space list this file just built, with the
	 *   sample normal turned into view space by the node's own matrix. If the
	 *   two disagree, the conversion is wrong - which is the one thing in the
	 *   whole object path that cannot be seen in a picture, only measured.
	 */
	/* The light, both ways, as one call - shared by self_test and the
	 * creature probe below, so both ask the same question with one
	 * arithmetic.
	 *
	 * `classic` is aTransformLightClipMesh evaluated on the engine's own
	 * SunLights / PointLights / SpotLights, which InitObjectLighting filled
	 * for this item a moment ago, plus the ambient it left in aAmbientR/G/B.
	 * `hd` is the same arithmetic on the view-space list this file built for
	 * the shader, with the sample normal turned into view space by the
	 * node's own matrix. */
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

	/* And the same question of a creature in a dark room.
	 *
	 * Whether an HD body gets less light than the classic item would have is
	 * not a question a picture can settle, because a dark room is dark in the
	 * classic engine too. So it is measured, on the creature itself and in the
	 * room it is standing in: the very comparison the level's own lightcheck
	 * makes, run once a second of game time for up to HD_LIGHT_PROBE_ITEMS
	 * creatures of the frame. `classic` is what aTransformLightClipMesh would
	 * give this item out of the engine's own light arrays; `hd` is what the
	 * shader gets. Two numbers that agree say the HD path is as dark as the
	 * classic one, and a black guard is the original level's dark room; two
	 * that disagree name the term that is missing.
	 *
	 * `dist` is how far the creature stands from the lens, since distant
	 * enemies are the ones in question, and `lit` is the brightest of the
	 * three channels so that a line can be read without adding them up.
	 *
	 * A creature and not every item: objects[].intelligent is the engine's own
	 * word for the things that walk about and shoot, and a level full of
	 * static meshes would otherwise bury the line. With -log only, and nothing
	 * branches on it. */
	/* How many creatures of one second the probe is allowed to name. Six is
	 * a busy room's worth and a line that reads at a glance; more
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

	/* For the "HDLOG bind" line - which of bind_against's
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

	/* The level's own .MAP. Without it everything below would bind by the
	 * slot's own name, which is the last of the three rules in hd_bind.h and
	 * the wrong one for every slot the map names: ANIMATING10.TRM is in no
	 * ITEM folder, while the map says ANIMATING10_ANDREA1; the lantern, the
	 * lamp post and the wall arm would ask for names that do not exist and
	 * draw classic on top of the art that already has them.
	 *
	 * Loaded here rather than kept from hd_scan's level scan because that
	 * scan frees it, runs on another call and may not have run at all. Freed
	 * in HDObjectLevelEnd, which the next level's build calls first. */
	const bool haveMap = HDMapLoadForLevel(installRoot, g_stem);

	/* Which slots the art already has.
	 *
	 * A .MAP entry of DUMMY is not "no model for this slot" - it is the
	 * remaster saying it draws nothing here, and the reason it can is that
	 * the object is baked into the .TRG room art. Measured with BakeProbe
	 * over ANDREA1: every one of the 14 DUMMY slots that has items has the
	 * remaster's own triangles standing inside the item's own bounding box -
	 * the wall lantern (ANIMATING12) 1250 of them in 633x417x132 units, the
	 * lamp post (ANIMATING11) 730, the alcove (SEARCH_OBJECT3) 5001 - and no
	 * slot the map does not call DUMMY has more than 18, which is the room's
	 * own floor and wall inside a big enemy's box.
	 *
	 * So they must be drawn by nobody, exactly as a room's static meshes are.
	 * This is TROne's baked-mesh rule (game_renderer.cpp:3722-3736) reached
	 * from the other end: TROne keys on the mesh pointer of a static, this
	 * engine keys on the remaster's own word for it.
	 *
	 * A donor slot is never baked. Its model came out of another level file,
	 * so whatever this level's map says about the slot's vanilla content says
	 * nothing about what stands there now - and the art of this level has
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
	g_ring_models = 0;

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

	/* Where the other bundle is. Set on both modules that need it and always,
	 * not only when this level has a guest: they name an installation, and a
	 * level with no guest offers no page and no name that could reach it. An
	 * empty path is the answer for a machine without Tomb Raider I-III
	 * Remastered, and then a guest's model is "missing" with the file name in
	 * the log. */
	HDModelTr3Root(HDInstallRootTr3());
	HDPageCacheAltRoot(HDInstallRootTr3());

	/* The mods folder. Scanned once per session and here
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

	/* The donor slots. The sidecar says which of this level's slots hold a
	 * model that came out of another level file, and the name the Remaster
	 * would give such a slot here is the wrong one - ANIMATING10 is a
	 * different object in every one of the fifteen files.
	 *
	 * Resolved before the loop that fills `wanted`, because the answer is the
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
			/* A guest out of Tomb Raider III. It comes from no TR5 level
			 * at all, so no .MAP can name it: the name is this engine's own
			 * four-line table, and the model is in the other bundle. */
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
	/* The classic mesh tree beside the bind pose, and kept alive for exactly
	 * as long. */
	std::vector<int> trees(slots.size() * HD_OBJ_NODES, -1);

	/* The names of the alternative models. Kept alive next to `binds`
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
		/* A pickup's vertices are kept until the floor rule has measured.
		 * A frame that turns node 0 sends the box corners where no vertex
		 * is, so the sweep needs the points. It is the pickup family and not
		 * every slot - HDSlotIsPickup is the rule - and HDModelReleasePoints
		 * gives the lists back at the end of the plan, before the first
		 * frame. */
		one.keepPoints = HDSlotIsPickup(slot);

		/* A donor slot that resolved to a name gets it; one that did not is
		 * left out of the list entirely rather than bound by its own name.
		 * Binding it by name is the one answer that is certainly wrong: the
		 * slot is called SAS and holds a T-Rex, so SAS.TRM would draw a
		 * soldier with a dinosaur's skeleton, and the skeleton test is not
		 * certain to throw it out. */
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

		/* The alternative models of this slot.
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

		/* And only if the level really puts one down. The cyborg's eleven
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

	/* A mod's inventory model, planned in the same list and on a pseudo slot
	 * like every other extra model in this file.
	 *
	 * `nodes` is one and `rigidNode` is 0, which is the right statement about
	 * a file out of another game: its bone numbers are that game's and mean
	 * nothing to this slot's mesh tree, so every vertex rides on the item's own
	 * root - which is what an inventory object does anyway. No bind pose, for
	 * the same reason: the model comes in its own frame and `fix` below is what
	 * puts it into the ring's. */
	std::vector<std::string> optionNames;
	const DWORD ringBegan = GetTickCount();
	int ringAsked = 0;

	optionNames.reserve(sizeof(g_ring_model) / sizeof(g_ring_model[0]));

	for (size_t i = 0; i < slots.size(); i++)
	{
		char option[128];
		const int slot = slots[i];

		/* And it says so when it stops. The table holds sixteen and a
		 * level's slot list is over a hundred, so without this line a Mods
		 * folder with seventeen inventory models would have the seventeenth
		 * quietly draw the install's own, with nothing in the log to point
		 * at. */
		if (g_ring_models >= (int)(sizeof(g_ring_model) / sizeof(g_ring_model[0])))
		{
			Log("HDLOG level=%s ring plan full at %d models - slot %d (%s) "
				"and everything after it keep the install's own inventory "
				"model", g_stem, g_ring_models, slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?");
			break;
		}

		ringAsked++;

		if (!*HDBindModOption(slot, option, sizeof(option)))
			continue;

		optionNames.push_back(option);

		HDModelSlot one;

		one.slot = HD_RING_SLOT_BASE + slot;
		one.nodes = 1;
		one.name = optionNames.back().c_str();
		one.rigidNode = 0;
		/* And posed with the donor game's own frame 0. The file is stored
		 * in the classic bind pose, which is not a pose at all: a pair of
		 * Uzis stands side by side and parallel in it, and the cross shown
		 * in the game the file came from is frame 0 of that game's own
		 * object. hd_classic.h reads it; HDModelSlot::classicPose says what
		 * it needs and what happens when it has not got it. */
		one.classicPose = true;
		/* And its vertices kept until the floor rule has pushed them through
		 * the fit - as every pickup model's are, for the reason at the slot
		 * plan above. HDModelReleasePoints gives them back at the end of the
		 * plan. */
		one.keepPoints = true;

		wanted.push_back(one);

		g_ring_model[g_ring_models].slot = slot;
		g_ring_model[g_ring_models].model = one.slot;
		g_ring_model[g_ring_models].lift = 0.0f;
		g_ring_model[g_ring_models].sank = 0.0f;
		g_ring_models++;
	}

	/* What the question cost. It is asked of every loaded slot and it walks
	 * the mods for a slot whose name ends in _ITEM - four names each, three
	 * EXTRA prefixes and the OPTION - which is the kind of per-slot disk walk
	 * that can blow a level build up to seconds. It stands outside the models
	 * timer below, so it gets a line of its own. Measured with ModelProbe
	 * -inventory over ANDREA1's 130 slots and nine mods: 7.7 ms, and 0.11 ms
	 * with no Mods folder. */
	Log("HDLOG level=%s ring asked=%d of %d slots, found=%d, ms=%d", g_stem,
		ringAsked, (int)slots.size(), g_ring_models,
		(int)(GetTickCount() - ringBegan));

	/* Lara's wardrobe, in the same HDModelLevelBegin. Her parts are models
	 * like any other - the same page cache, the same buffers, the same
	 * measurement - and the level begin call takes the whole list in one go,
	 * so they have to be in this list and not in a second one. hd_laradraw
	 * keeps the bind poses it hands over alive until the level ends, exactly
	 * as the `binds` block above does.
	 *
	 * Before the log line below, so that one line counts everything the level
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

	/* The phases at the end of the line split the models time, so a slow
	 * level build names the part that grew (key_ms: see HDPageCacheKeyOf).
	 * The three .TRM counters beside them say how much of the wardrobe a
	 * level change reads. cut_ranges and cut_models are the opaque draw
	 * ranges the alpha test runs on and the models they belong to
	 * (hd_model_cut.h) - 0 on a level without such a model. */
	Log("HDLOG level=%s models ms=%d slots=%d loaded=%d (5:%d 4:%d) "
		"mod:%d heads=%d lara=%d dummy=%d missing=%d refused=%d verts=%d idx=%d "
		"ranges=%d pages=%d vb_kib=%.0f ib_kib=%.0f read_ms=%d build_ms=%d "
		"find_ms=%d key_ms=%d page_ms=%d buf_ms=%d trm_open=%d trm_load=%d trm_kib=%.0f "
		"cut_ranges=%d cut_models=%d",
		g_stem, (int)(GetTickCount() - started), stats.slots, stats.loaded,
		stats.fromGame5, stats.fromGame4, stats.fromMod, stats.heads,
		stats.lara, stats.dummy, stats.missing, stats.refused, stats.vertices,
		stats.indices, stats.ranges, stats.pages, stats.vertexBytes / 1024.0,
		stats.indexBytes / 1024.0, (int)(stats.readMs + 0.5),
		(int)(stats.buildMs + 0.5), (int)(stats.findMs + 0.5),
		(int)(stats.keyMs + 0.5),
		(int)(stats.pageMs + 0.5), (int)(stats.bufferMs + 0.5),
		(int)stats.trmOpens, (int)stats.trmLoads, stats.trmBytes / 1024.0,
		stats.cutRanges, stats.cutModels);

	/* The models' half of the texture budget. They arrive
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

	/* The speaking faces' track files, one line each - see HDModelLipsLine. */
	for (int i = 0; i < HDModelLipsLineCount(); i++)
		Log("HDLOG level=%s lips %s", g_stem, HDModelLipsLine(i));

	/* The verdict per slot. One line each, so that a wrong picture in the game
	 * can be looked up rather than guessed at. */
	g_hd_slots = 0;
	g_classic_slots = 0;

	/* The bind line - which of the four rules answered each slot, next to
	 * the verdict line below rather than folded into it: it answers why a
	 * name was tried, the verdict line whether it drew. A donor slot never
	 * calls HDBindSlot at all
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
			verdict.via = viaStr;		// for the ring's log line

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
		verdict.lift = 0;	// the floor rule below fills it
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

			/* A line per slot, including the ones that never reached the
			 * card, so the level line's "classic" number can be taken
			 * apart. The model note above names the file that is missing;
			 * this names the slot and the verdict. */
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

		/* The skeleton test, in the order of certainty.
		 *
		 * 1. A bone number the slot does not have. The randomizer puts
		 *    foreign creatures into slots - ANDREA1's SAS can hold a 25 node
		 *    tree while SAS.TRM is drawn for 15 - and a vertex that names a
		 *    node the tree does not have is not this skeleton's.
		 * 2. No geometry at all. Cannot happen with a model the reader
		 *    accepted; a guard.
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

	/* The alternative models, after the build because only now is it known
	 * which of them really arrived. One that did not
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

	/* A mod's inventory model, measured. After the verdicts,
	 * because both boxes have to be on the card before either can be measured
	 * - and because a slot whose own model did not come out HD draws classic in
	 * the ring too, so there is nothing to put the OPTION model into.
	 *
	 * A plan that does not survive this is struck out rather than left half
	 * made, exactly as a missing swap model is above: the ring then shows what
	 * it showed before the mod, which is a picture. */
	{
		int kept = 0;

		for (int i = 0; i < g_ring_models; i++)
		{
			RingModel& ring = g_ring_model[i];
			const int slot = ring.slot;
			const bool hd = slot >= 0 && slot < NUMBER_OBJECTS &&
				g_slot[slot].hd != 0 && HDModelHas(slot);
			float srcMin[3], srcMax[3], dstMin[3], dstMax[3];

			/* The donor game's own frame 0, two lines and before the
			 * verdict below - a plan that is struck out here was still posed
			 * or refused a pose while the file was open, and a log that says
			 * nothing about it cannot tell the two apart.
			 *
			 * The classic object is printed out of what the loader read, not
			 * out of a second look at the disk (HDClassicCached), so the line
			 * can only ever describe the frame that was really used. */
			{
				HDModelClassicPose pose;

				if (HDModelClassicPoseOf(ring.model, &pose))
				{
					HDClassicObject object;

					if (pose.object && HDClassicCached(pose.game, pose.object, &object))
					{
						char bones[192] = "", rots[192] = "";
						int atBone = 0, atRot = 0;

						/* Eight nodes of numbers is the whole of every
						 * inventory object the table knows - the deepest is
						 * three - and it keeps one log line one log line. */
						for (int n = 0; n < object.meshes && n < 8; n++)
						{
							if (n && atBone < (int)sizeof(bones) - 1)
								atBone += std::snprintf(bones + atBone,
									sizeof(bones) - atBone, " %d:%d/%d/%d/%d", n,
									object.bone[n].flags, object.bone[n].x,
									object.bone[n].y, object.bone[n].z);

							if (atRot < (int)sizeof(rots) - 1)
								atRot += std::snprintf(rots + atRot,
									sizeof(rots) - atRot, " %d:%d/%d/%d", n,
									object.rot[n][0], object.rot[n][1],
									object.rot[n][2]);
						}

						Log("HDLOG classic %d/%s object %d meshes=%d bones=%s"
							" frame0 offset=%d/%d/%d bounds=%d..%d/%d..%d/%d..%d"
							" rot=%s (1024ths of a turn)",
							object.game, object.level, object.id, object.meshes,
							*bones ? bones : " none", object.offset[0],
							object.offset[1], object.offset[2], object.bounds[0],
							object.bounds[1], object.bounds[2], object.bounds[3],
							object.bounds[4], object.bounds[5], rots);
					}
					else if (pose.game && pose.object)
						Log("HDLOG classic %d object %d: %s", pose.game,
							pose.object, pose.why);

					if (pose.applied)
						Log("HDLOG level=%s ring pose slot=%d model=%s classic="
							"%d/%s#%d nodes=%d applied (box %.0fx%.0fx%.0f -> "
							"%.0fx%.0fx%.0f)", g_stem, slot,
							HDModelName(ring.model) ? HDModelName(ring.model) : "-",
							pose.game, pose.level, pose.object, pose.nodes,
							pose.before[0], pose.before[1], pose.before[2],
							pose.after[0], pose.after[1], pose.after[2]);
					else
						Log("HDLOG level=%s ring pose slot=%d model=%s not "
							"posed: %s - it is drawn as the file stands",
							g_stem, slot,
							HDModelName(ring.model) ? HDModelName(ring.model) : "-",
							pose.why);
				}
			}

			if (!hd || !HDModelHas(ring.model) ||
				!HDModelRingFit(ring.model, slot, ring.fix))
			{
				Log("HDLOG level=%s ring slot=%d name=%s model=%s - not drawn: "
					"%s", g_stem, slot,
					HDSlotName(slot) ? HDSlotName(slot) : "?",
					HDModelName(ring.model) ? HDModelName(ring.model) : "-",
					!HDModelHas(ring.model) ? "the .TRM did not load"
						: !hd ? "the slot itself is classic here"
						: "one of the two models has no bounding box");
				continue;
			}

			HDModelBounds(ring.model, srcMin, srcMax);
			HDModelBounds(slot, dstMin, dstMax);

			/* The shape's own extents beside the box. The fit reads these and
			 * not the box, so a wrong picture has to be traceable to the
			 * numbers the rule really used - and the box is kept beside them
			 * because it is the room the ring keeps. */
			float srcExt[3] = { 0, 0, 0 }, dstExt[3] = { 0, 0, 0 };

			HDModelPrincipalAxes(ring.model, 0, srcExt);
			HDModelPrincipalAxes(slot, 0, dstExt);

			/* And whether it stays in the box. The scale matches the two
			 * longest extents and nothing else, so a model built in another
			 * game's proportions can stand well out of the room the ring keeps
			 * for this item - which is intended (see HDModelRingFit), and the
			 * line says so, so that a crowded-looking ring can be explained. */
			int outAxis = -1;
			const float over = HDModelRingFitOverflow(ring.model, slot,
				&outAxis);

			Log("HDLOG level=%s ring slot=%d name=%s inventory=%s instead of "
				"%s - its box %.0fx%.0fx%.0f against %.0fx%.0fx%.0f, its shape "
				"%.0fx%.0fx%.0f against %.0fx%.0fx%.0f, scale "
				"%.2f, centre moved by %.0f/%.0f/%.0f, %s", g_stem, slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?",
				HDModelName(ring.model) ? HDModelName(ring.model) : "-",
				HDModelName(slot) ? HDModelName(slot) : "-",
				srcMax[0] - srcMin[0], srcMax[1] - srcMin[1],
				srcMax[2] - srcMin[2], dstMax[0] - dstMin[0],
				dstMax[1] - dstMin[1], dstMax[2] - dstMin[2],
				srcExt[0], srcExt[1], srcExt[2],
				dstExt[0], dstExt[1], dstExt[2],
				std::sqrt(ring.fix[0] * ring.fix[0] + ring.fix[1] * ring.fix[1] +
					ring.fix[2] * ring.fix[2]),
				ring.fix[12], ring.fix[13], ring.fix[14],
				box_verdict(over, outAxis));

			/* The floor is measured below, not here: the pickup lying on the
			 * ground takes this very model, and how far it has to be lifted
			 * to lie on the floor rather than in it is the same rule every
			 * other pickup model goes through. */

			if (kept != i)
				g_ring_model[kept] = ring;

			kept++;
		}

		g_ring_models = kept;

		/* And the frame path's own pointer to the plan, set here and not
		 * where the plan was written down: the loop above compacts the array,
		 * so a pointer taken before it could name another slot's entry. It is
		 * what ring_model_of hands out, without a walk per item per frame. */
		for (int i = 0; i < g_ring_models; i++)
		{
			const int slot = g_ring_model[i].slot;

			if (slot >= 0 && slot < NUMBER_OBJECTS)
				g_slot[slot].ring = &g_ring_model[i];
		}
	}

	/* -------------------------------------------------------- the floor rule --
	 *
	 * One rule for every pickup model, measured against the classic mesh the
	 * model replaces - the reason is at classic_lowest_of above. An HD model
	 * that reaches deeper than that mesh stands in the ground where the
	 * level's own picture does not, so it is lifted by exactly the
	 * difference, straight up in the world. Never scaled, never turned, never
	 * moved sideways; a model that already lies on the mesh or above it is not
	 * touched at all.
	 *
	 * Lifting rather than refusing: keeping the vanilla model when the
	 * replacement sinks would hide a mod the player installed, by a number he
	 * cannot see or fix. The lift shows it, in the place the item really
	 * lies: the model's lowest point ends up exactly where the classic mesh's
	 * was.
	 *
	 * It is measured on the geometry - HDModelFloorLift works on the
	 * vertices, since the box corners pushed through the fit are a bound and
	 * not the model.
	 *
	 * And in the frame the level draws the item in. The item's animation
	 * frame turns node 0 before the first mesh is drawn, so both lowest points
	 * are taken through HDFramePose's matrices; the bind pose is not the
	 * picture the player sees for the six of ANDREA3's eighteen pickup slots
	 * whose frame carries a real turn. The lift itself goes straight up in
	 * the world - see world_up_in_view - so the rule is one measurement and
	 * one direction and neither of them depends on where the camera is.
	 *
	 * Once, here. The lift is a property of two models, of the mesh tree and
	 * of one animation frame, all of them level data, so nothing of this is
	 * left for a frame to work out; the draw adds one translation to a matrix
	 * it already has. */
	{
		float pose[HD_OBJ_NODES * 12];

		for (int i = 0; i < g_ring_models; i++)
		{
			RingModel& ring = g_ring_model[i];
			const int slot = ring.slot;
			const int nodes = slot >= 0 && slot < NUMBER_OBJECTS &&
				objects[slot].nmeshes < HD_OBJ_NODES
				? objects[slot].nmeshes : HD_OBJ_NODES;
			float classic = 0.0f, lowest = 0.0f, lift = 0.0f;

			frame_pose_of(slot, pose, nodes);
			frame_turn_line(slot, pose, ring.model);

			const bool haveMesh = classic_lowest_of(slot, pose, nodes, &classic);

			if (!haveMesh ||
				!HDModelFloorLift(ring.model, ring.fix, pose, nodes, classic,
					&lowest, &lift))
			{
				Log("HDLOG level=%s ring floor slot=%d name=%s not measured: "
					"%s - it is drawn where the fit puts it", g_stem, slot,
					HDSlotName(slot) ? HDSlotName(slot) : "?",
					haveMesh ? "the fitted model kept no vertices to measure"
						: "the slot has no classic mesh with vertices in it");
				continue;
			}

			ring.lift = lift;
			ring.sank = lowest - classic;

			Log("HDLOG level=%s ring floor slot=%d name=%s takes the fitted "
				"%s on the floor, lifted by %.1f units (its lowest point "
				"stood %.1f %s the classic mesh's)", g_stem, slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?",
				HDModelName(ring.model) ? HDModelName(ring.model) : "-",
				lift, ring.sank < 0.0f ? -ring.sank : ring.sank,
				ring.sank > 0.0f ? "under" : "above");
		}

		/* And every other pickup the level drew HD, the install's own files
		 * included. They are expected to read zero - the remaster modelled
		 * them over the classic mesh - and a line that says otherwise is a
		 * finding about the remaster's file and not about this rule, which is
		 * exactly why it is printed for all of them and not only for the ones
		 * that move. */
		for (int slot = 0; slot < NUMBER_OBJECTS; slot++)
		{
			if (!g_slot[slot].hd || !HDSlotIsPickup(slot) || ring_model_of(slot))
				continue;

			const int nodes = objects[slot].nmeshes < HD_OBJ_NODES
				? objects[slot].nmeshes : HD_OBJ_NODES;
			float classic = 0.0f, lowest = 0.0f, lift = 0.0f;

			frame_pose_of(slot, pose, nodes);
			frame_turn_line(slot, pose, slot);

			const bool haveMesh = classic_lowest_of(slot, pose, nodes, &classic);

			if (!haveMesh ||
				!HDModelFloorLift(slot, 0, pose, nodes, classic, &lowest, &lift))
			{
				/* A pickup the rule could not measure says so: the
				 * measurement needs the model's kept vertices, and a model
				 * without them should be visible in the log. */
				Log("HDLOG level=%s pickup floor slot=%d name=%s model=%s not "
					"measured: %s", g_stem, slot,
					HDSlotName(slot) ? HDSlotName(slot) : "?",
					HDModelName(slot) ? HDModelName(slot) : "-",
					haveMesh ? "the model kept no vertices to measure"
						: "the slot has no classic mesh with vertices in it");
				continue;
			}

			g_slot[slot].lift = lift;

			const float sank = lowest - classic;
			char verdict[64];

			if (lift > 0.0f)
				std::snprintf(verdict, sizeof(verdict),
					"lifted by %.1f units", lift);
			else
				std::snprintf(verdict, sizeof(verdict), "not lifted%s",
					sank > 0.0f ? ", which is inside the slack the rule leaves"
						: "");

			Log("HDLOG level=%s pickup floor slot=%d name=%s model=%s lowest "
				"point %.1f %s the classic mesh's - %s", g_stem, slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?",
				HDModelName(slot) ? HDModelName(slot) : "-",
				sank < 0.0f ? -sank : sank, sank > 0.0f ? "under" : "above",
				verdict);
		}

		/* And the vertex lists back. They were kept for this one measurement
		 * and nothing after this line reads them - a level build does not
		 * carry three quarters of a megabyte of points into its first frame.
		 * Thirteen bytes a vertex: three floats of position and the one byte
		 * that says which node the point rides on. ANDREA3 with four mods in
		 * the folder keeps 57,378 of them, which is 746 KB; the install's own
		 * eighteen models are 18,526 points and 241 KB. */
		HDModelReleasePoints();
	}

	/* The donor slots, one line each, after the verdicts because `hd` is
	 * only known once the model is either on the card or not. A slot that
	 * came out classic says here which of the four ways it got there - no
	 * name, the donor's DUMMY, no such file, or a skeleton the model does
	 * not fit. The last of those is the slot line above; this one
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

	/* The pickup display's own line, once per level.
	 *
	 * Which slots it counts is not a hand-written list: the display
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
	 * The table is read at build time and it is per level: LoadGameflow
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

	/* Lara's verdict, after the objects and out of the
	 * models that were just built: which parts arrived, what they cover, how
	 * far they drift. It writes its own HDLOG lines. */
	HDLaraDrawBuild(g_stem);

	/* Released only when neither side has anything to draw. A level whose
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
	/* Not a single Direct3D call. HDLevelUnload runs at the top of LoadLevel
	 * on the load thread while the main thread presents the loading bar, and
	 * two threads on one device freeze the game. The buffers go in
	 * HDObjectRelease, on the thread that owns the device. */
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
	g_ring_models = 0;	// a mod's inventory models
	/* And the cached up direction. ViewSerial only ever
	 * counts up, so this cannot go stale on its own - it is cleared here so
	 * that it could not if the serial were ever reset with a level. */
	g_up_view_serial = -1;
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
	// The clips go with the models, in HDObjectRelease on the device's thread.
	g_lips_playing = false;
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
	/* The cut is over - played out, skipped or left through the fade - and
	 * its clips close, which gives back their files and their buffers; the
	 * faces draw their static buffers again. A test of a flag in every other
	 * frame, and false throughout with hd_enable off, where no face opens a
	 * clip. */
	if (g_lips_playing && !GLOBAL_playing_cutseq)
	{
		g_lips_playing = false;
		HDModelLipsClose();
	}

	g_count = 0;
	g_dropped = 0;
	g_open = false;
	g_gave_way = 0;
	g_open_casts = false;
	g_items_classic = 0;
	g_swap_ignored = 0;
	g_swap_drawn = 0;
	g_mask_used = 0;
	g_items_baked = 0;

	/* The ring. Its line is written here, of the frame that has just ended,
	 * and not in HDObjectFrameEnd: that one is reached only when HDFrameDraw
	 * found something to draw, and "the ring opened and not one item of it is
	 * HD" is exactly the report worth having. This function runs once per
	 * drawn frame whatever happened in it - S_InitialisePolyList calls
	 * HDFrameReset, and the ring's own loop (game\newinv2.cpp) calls
	 * S_InitialisePolyList once per present.
	 *
	 * At sixty hertz the ring's list is still emptied per picture and never
	 * per logic tick, and GameFrameMinTicks() (specific\output.cpp) holds the
	 * whole inventory at thirty presents in any case, so nothing here has two
	 * rates to keep apart. The ring's own spin stays in
	 * draw_current_object_list.
	 *
	 * `g_ring_logged` is armed again on every frame that is not a ring frame,
	 * so the block is written once per opening of the inventory rather than
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

			/* Which model really stood there: a mod's own <BASE>_OPTION
			 * where there is one, and the slot's EXTRA model everywhere
			 * else. */
			const RingModel* ring = ring_model_of(slot);
			const int drew = ring ? ring->model : slot;

			Log("HDLOG ring slot=%d name=%s model=%s nodes=%d mask=%X z=%.0f "
				"via=%s%s",
				slot,
				(known && HDSlotName(slot)) ? HDSlotName(slot) : "?",
				g_ring_log[i].hd && HDModelName(drew) ? HDModelName(drew) : "-",
				known ? g_slot[slot].nodes : 0,
				g_ring_log[i].mask,
				(double)g_ring_log[i].z,
				g_ring_log[i].hd
					? (known && g_slot[slot].via ? g_slot[slot].via : "?")
					: (known && g_slot[slot].why ? g_slot[slot].why : "classic"),
				ring ? " (a mod's own inventory model, turned and scaled onto "
					"the box of the one it replaces)" : "");
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
	/* The display's walk begins and ends inside one S_OutputPolyList and
	 * cannot survive a picture - cleared here as a safeguard, like g_open. */
	g_pickup_open = false;

	HDLaraDrawFrameReset();
}

/* What HDObjectBegin decides for an item, without doing any of it: the one
 * decision HDObjectBegin acts on and HDObjectBlobGivesWay asks before the
 * item's blob, so the two can never answer differently. */
enum
{
	BEGIN_NONE = 0,		// no item, or the HD objects are off: drawn classic
	BEGIN_BAKED,		// the room's art already has it: drawn not at all
	BEGIN_CLASSIC,		// its slot has no HD model: drawn classic
	BEGIN_FULL,		// the frame's list is full: drawn classic
	BEGIN_HD		// drawn as an HD model
};

static int begin_decision(const ITEM_INFO* item, const OBJECT_INFO* obj)
{
	if (!item || !obj)
		return BEGIN_NONE;

	/* The art already has it. Before every other guard: this item is not
	 * drawn HD, it is not drawn at all, and the reason sits in the room path
	 * rather than in the object path - the .TRG group the room draws carries
	 * the object's geometry whether hd_objects is on or off.
	 *
	 * Per item and not per slot, because only a room that really draws HD has
	 * the baked copy: a room the sidecar marked classic, or one whose group
	 * is empty, still needs its lamp post drawn the old way. That is the same
	 * test DrawStaticObjects makes for a room's static meshes.
	 *
	 * HDObjectBegin returning true with no node recorded is what makes it
	 * vanish: DrawAnimatingItem then puts no classic polygon in, HDObjectNode is
	 * never reached because g_open stays false, and HDObjectEnd sees nothing
	 * and drops the item. */
	if (g_baked_any)
	{
		const int baked_slot = (int)item->object_number;

		if (baked_slot >= 0 && baked_slot < NUMBER_OBJECTS &&
			g_slot[baked_slot].baked && HDRoomIsHD(item->room_number))
			return BEGIN_BAKED;
	}

	// The guard. With hd_objects off, or before a level built its models,
	// this is where DrawAnimatingItem turns round and draws what it always
	// drew.
	if (!g_active || !tomb5.hd_objects)
		return BEGIN_NONE;

	const int slot = (int)item->object_number;

	if (slot < 0 || slot >= NUMBER_OBJECTS || !g_slot[slot].hd)
		return BEGIN_CLASSIC;

	if (g_count >= HD_OBJ_ITEMS)
		return BEGIN_FULL;

	return BEGIN_HD;
}

bool HDObjectBegin(ITEM_INFO* item, OBJECT_INFO* obj)
{
	// PROF_HD_OBJ is what this call, HDObjectNode and HDObjectEnd cost
	// together - the three of them never nest, so one accumulator holds the
	// whole per-item recording of the frame.
	ProfScope prof(PROF_HD_OBJ);

	/* Whether this item's blob gave way to the second map
	 * (HDObjectBlobGivesWay, asked right before for the same item): taken
	 * over for this item, and asked of no later one. */
	g_open_casts = item && item == g_gave_way;
	g_gave_way = 0;

	switch (begin_decision(item, obj))
	{
	case BEGIN_BAKED:
		g_items_baked++;
		g_open = false;
		return true;

	case BEGIN_CLASSIC:
		g_items_classic++;
		return false;

	case BEGIN_FULL:
		g_dropped++;
		return false;

	case BEGIN_HD:
		break;

	default:
		return false;
	}

	const int slot = (int)item->object_number;

	// The frame's field of view, if no HD room has taken it yet - an item can
	// be the first HD thing in a frame whose rooms all stayed classic.
	HDFrameCaptureProjection();

	g_open = true;
	g_open_slot = slot;
	g_open_nodes = g_slot[slot].nodes;
	g_open_room = (int)item->room_number;	// its portal window
	g_open_mask = (unsigned int)item->mesh_bits;
	// Its place, the world position its blob would be drawn under.
	g_open_place[0] = (float)item->pos.x_pos;
	g_open_place[1] = (float)item->pos.y_pos;
	g_open_place[2] = (float)item->pos.z_pos;
	g_seen = 0;
	g_open_lips_track = -1;	// only a cutscene's actor speaks - HDObjectLips
	g_open_lips_frame = -1;

	/* The mesh swap.
	 *
	 * A swap replaces one node's mesh with the pair's second pointer - a
	 * guard's hands (bits 10 and 13, set for every soldier the moment he is
	 * initialised), the cyborg's eleven damage panels, the Mafia's pistols
	 * coming out of their holsters. A .TRM carries no second geometry for a
	 * node; the remaster does not swap a node but ships a whole second model
	 * for the swapped look, and the table at the top of this file says which.
	 *
	 * Where such a model exists the item draws it instead of the plain one -
	 * one model, one draw, the same joints. Where none exists the swap is
	 * ignored and counted in the frame line: the soldiers' hands need no
	 * second file because MAFIA.TRM and SWAT.TRM already hold the weapon
	 * (measured with ModelProbe -name MAFIA). The classic swap mesh is never
	 * drawn over the HD body - TROne took that approach and reversed it after
	 * reports of classic patches on an HD model (game_renderer.cpp:1825-1830). */
	g_open_swap = swap_model_for(swap_plan_of(slot), item);

	if (g_open_swap < 0 && item->meshswap_meshbits &&
		(unsigned int)item->meshswap_meshbits & g_slot[slot].covered)
		g_swap_ignored++;
	else if (g_open_swap >= 0)
		g_swap_drawn++;

	if (g_open_mask != 0xFFFFFFFFu)
		g_mask_used++;

	/* And when the mask hides the whole model, say so.
	 *
	 * An item's mesh_bits names classic meshes, and a .TRM's bone numbers are
	 * classic mesh numbers too, so the mask reaches the model as a node mask
	 * and a node it hides is a node whose triangles are not drawn. That is
	 * right and it is what vanilla does - HK_ITEM, HK_AMMO_ITEM, CROSSBOW_ITEM
	 * and REVOLVER_ITEM are initialised with mesh_bits 1 (game\items.cpp), so
	 * only mesh 0 is ever shown and what sits on mesh 1 is not.
	 *
	 * A model whose geometry sits entirely on hidden nodes therefore draws
	 * nothing at all while the level build reports it bound, loaded and
	 * textured: an MP5 mod that puts all 5478 of its vertices on node 1 of
	 * HK_ITEM is invisible on the floor. The fault is the file's - the
	 * remaster's own EXTRA5_HK_ITEM has 2426 vertices on node 0 and 73 on
	 * node 1 - and this line tells a mod author which node the geometry has
	 * to be on.
	 *
	 * Once per slot per level, behind -log, and never for a slot the ring plan
	 * draws through its own one-node model: that one is given the full mask in
	 * HDObjectEnd and the classic numbering does not apply to it. */
	if (gLogEnabled && !g_slot[slot].maskSaid && !ring_model_of(slot) &&
		!(g_slot[slot].covered & g_open_mask))
	{
		g_slot[slot].maskSaid = 1;
		Log("HDLOG level=%s item slot=%d name=%s model=%s draws nothing: its "
			"geometry sits on node mask %X and this item shows %X - the "
			"model's vertices name a classic mesh this slot never draws",
			g_stem, slot, HDSlotName(slot) ? HDSlotName(slot) : "?",
			HDModelName(slot) ? HDModelName(slot) : "-",
			g_slot[slot].covered, g_open_mask);
	}

	return true;
}

void HDObjectNode(int node, bool interpolated)
{
	ProfScope prof(PROF_HD_OBJ);

	if (!g_open || node < 0 || node >= g_open_nodes || node >= HD_OBJ_NODES)
		return;

	if (interpolated)
	{
		/* The matrix that draws in the frac branch is the interpolated one,
		 * which phd_PutPolygons_I makes inside a push of its own
		 * (game\draw.cpp). The same push, the same call, the same
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

int HDObjectLips(long track, long frame)
{
	if (!g_open || track < 0 || g_open_slot < 0 || g_open_slot >= NUMBER_OBJECTS ||
		!g_slot[g_open_slot].head)
		return 0;

	/* The face opens its file at the cut's first frame and answers from
	 * then on without looking again; a face with no file for the track
	 * answers 0 every time and is drawn as it was built. */
	const int frames = HDModelLipsOpen(g_open_slot, true, (int)track);

	if (frames <= 0)
		return 0;

	g_open_lips_track = (int)track;
	g_open_lips_frame = frame < 0 ? 0 : (int)frame;
	g_lips_playing = true;
	return frames;
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
	entry.room = g_open_room;
	entry.view = ViewSerial;
	// No off-centre projection in the world - only the inventory ring
	// draws about anything but the screen centre.
	entry.offCentreX = 0.0f;
	entry.offCentreY = 0.0f;
	entry.lipsTrack = g_open_lips_track;
	entry.lipsFrame = g_open_lips_frame;
	stamp_clip(entry.clip, g_reflection);

	/* And the pickup on the floor takes the mod's own inventory model.
	 *
	 * The fitted <BASE>_OPTION model can reach below the item's lowest point;
	 * the floor rule at the plan loop measures that sinking on the geometry
	 * and lifts the model out of it, so a mod's Uzis lie on the ground as the
	 * same model the ring and the corner display show.
	 *
	 * The same lines the ring's own path writes (HDInvEnd): the file is
	 * another game's and stands on one node, so the item's node mask - which
	 * names nodes of the classic model - cannot be applied to it. */
	const RingModel* floorRing = ring_model_of(g_open_slot);

	if (floorRing)
	{
		entry.drawSlot = floorRing->model;
		entry.head = false;
		entry.nodes = 1;
		entry.mask = ~0u;
	}

	// The engine's 3x4 into the row-major 4x4 the shader wants - the note and
	// the unseen-node rule are over store_matrices at the top of this file.
	store_matrices(entry);

	/* The fit, in front of the item's own matrix, which is where the
	 * ring puts it for the reason given there: the model is moved into this
	 * slot's frame first and the engine's matrix then does with it what it
	 * does with the EXTRA model. */
	if (floorRing)
	{
		float placed[16];

		ring_mul(floorRing->fix, entry.matrix, placed);
		memcpy(entry.matrix, placed, sizeof(placed));
	}

	/* And the lift, straight up in the world. One site for both cases - the
	 * fitted OPTION model and a slot's own - because it is one rule and the
	 * direction has nothing to do with which of the two is being drawn.
	 *
	 * Not along the model's own y axis (the node matrix's row 1): that is
	 * wrong wherever the item's animation frame turns the model. ANDREA3's
	 * UZI_ITEM frame turns node 0 by 180 degrees about z, so the model's y
	 * axis points at the sky and a lift along it drives a mod's Uzis 11.5
	 * units deeper into the ground.
	 *
	 * The matrices are model space to view space, so this adds the world's up
	 * direction expressed in view space (world_up_in_view, one vector per
	 * camera) to each node's translation row. The 3x3 is untouched: nothing
	 * is scaled, nothing is turned, and nothing moves sideways whatever the
	 * item's rotation or its frame's. */
	const float lift = floorRing ? floorRing->lift
		: (g_open_slot >= 0 && g_open_slot < NUMBER_OBJECTS
			? g_slot[g_open_slot].lift : 0.0f);

	if (lift > 0.0f)
	{
		const float* up = world_up_in_view();

		for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
		{
			float* m = entry.matrix + node * 16;

			m[12] += lift * up[0];
			m[13] += lift * up[1];
			m[14] += lift * up[2];
		}
	}

	/* The light, out of the classic path's own setup.
	 *
	 * These three lines are the first three of phd_PutPolygons with the draw
	 * left out: the view matrix of the node (aSetViewMatrix, minus the
	 * SetTransform that only the fixed-function pipe needs), the reset that
	 * builds aLightMatrix and the node's world position out of it, and the
	 * setup that fills SunLights / PointLights / SpotLights and leaves the
	 * ambient in aAmbientR/G/B.
	 *
	 * Node 0 and not each node in turn, because the only per-node part of the
	 * classic answer is the light direction and the falloff, and the shader
	 * works both out from the surface point instead. The ambient really is
	 * per object: the shadow lights that lower it read `light->Range`, which
	 * CreateLightList measured at the item.
	 *
	 * current_item and not `item`: CalculateObjectLighting has just set it,
	 * and for an item with a shade value it points at the static-mesh light
	 * item instead - the classic path's own decision. */
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

	// An item is never cut. The neck cut exists because the first-person
	// camera sits inside Lara's own head, and nothing else in the level has a
	// camera inside it.
	memset(&entry.cut, 0, sizeof(entry.cut));
	entry.cull = HD_MODEL_CULL_NONE;
	memset(&entry.soft, 0, sizeof(entry.soft));

	/* The second map: an item whose blob gave way is drawn into it when its
	 * place lies inside the second box grown across the light by
	 * OTHERS_MARGIN (hd_shadow.h, HDShadowOtherCasts - the blob, the frame,
	 * her step, the target, HD and not mirrored were all asked when the blob
	 * gave way), with the node mask it is drawn with here. The margin keeps
	 * an item whose feet have left the box casting while its upper body
	 * still reaches into it, so its shadow fades out at the box's edge on
	 * every side. Every other item casts nothing. */
	const HDShadowFrame* shadow = HDShadowFrameState();
	const bool inside = g_open_casts &&
		HDShadowBoxHolds(&shadow->others, g_open_place, HD_SHADOW_OTHERS_MARGIN);

	entry.cast = inside ? HD_OBJ_CAST_OTHERS : HD_OBJ_CAST_NONE;
	entry.castMask = inside ? entry.mask : 0;
	g_open_casts = false;
	// No sheen on an object - the specular term is Lara's alone.
	entry.specStrength = 0.0f;
	entry.specPower = 1.0f;

	/* The two checks, on the first HD item of the level and while the
	 * engine's own light arrays still hold this item's answer. Not on a
	 * reflection: its lights went through the mirrored view and the
	 * classic answer they are held against did not. */
	if (!g_reflection)
	{
		self_test(entry);

		/* And the same light question of a creature, once a second - whether
		 * an enemy in a dark room gets less light HD than classic. */
		creature_light(entry);
	}
	else
		g_reflected++;

	g_count++;
}

void HDObjectReflection(bool on, const float* planes)
{
	g_reflection = on;

	if (on && planes)
		memcpy(g_clip, planes, sizeof(g_clip));
	else
		stamp_clip(g_clip, false);
}

int HDObjectReflectedTake()
{
	const int n = g_reflected;

	g_reflected = 0;
	return n;
}

/* ------------------------- the inventory ring and the pickup display -- */

/* The pickup display's half of HDInvBegin.
 *
 * Every reason this item might not be drawn HD has to be settled here, before
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

	/* The seam, once per slot per level - see g_pickup_logged. Written before
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

	/* A bracket to draw in. HDRenderFrameBegin refuses to nest and says
	 * nothing about it, and there is no scene to draw into before
	 * _BeginScene - so both are asked here, where the answer can still be
	 * "draw it classically". Neither can happen from S_DrawPickup as the
	 * engine stands (S_InitialisePolyList opens the scene, and the frame's own
	 * bracket was closed long before the depth clear), which is why they are
	 * tested rather than assumed. */
	if (!HDRenderReady() || HDRenderInFrame() || !App.dx.InScene)
		return false;

	/* The display's own projection, read here and now. Not
	 * HDFrameProjectionInfo: that is the field of view the world was collected
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

	/* The off-centre projection, the ring's own arithmetic and the same two
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

	/* None of the frame's counters is touched here - not g_items_classic,
	 * not g_mask_used, not g_count. They belong to the line HDObjectFrameEnd
	 * has already written by the time the display is drawn, and a number
	 * added now would land in the next frame's. The display has a line of
	 * its own, above. */
	return true;
}

/* The immediate draw. One object, one bracket, and every
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
	 * the frame's object work twice. */
	if (!HDFramePickupBegin(g_pickup_persp, g_pickup_centrex, g_pickup_centrey,
		g_pickup_za, g_pickup_zb))
		return;

	HDModelLighting lighting;

	lighting.lights = entry.lights;
	lighting.lightCount = entry.lightCount;
	lighting.sunScale = entry.sunScale;
	lighting.specStrength = entry.specStrength;
	lighting.specPower = entry.specPower;

	// The display's place on the screen, in the projection and never in the
	// matrices - ring and display alike; see HDInvNode.
	HDModelOffCentre(entry.offCentreX, entry.offCentreY);

	HDModelDraw(entry.drawSlot, entry.matrix, entry.nodes, entry.light,
		entry.mask, &lighting, &entry.cut, entry.cull);

	if (entry.head)
		HDModelDrawHead(entry.slot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting, &entry.cut, entry.cull);

	/* Put back before the bracket closes, exactly as HDObjectFrameDraw does:
	 * the register file is shared with everything else that draws.
	 *
	 * g_draw_calls is not touched. It is the frame line's count of what the
	 * world cost, and HDObjectFrameEnd has already written that line by the
	 * time this runs. */
	HDModelOffCentre(0.0f, 0.0f);
	HDFramePickupEnd();
}

bool HDInvBegin(ITEM_INFO* item, OBJECT_INFO* obj, float x, float y, long color)
{
	ProfScope prof(PROF_HD_OBJ);

	if (!item || !obj)
		return false;

	/* Which of the two callers this is.
	 *
	 * DrawThreeDeeObject2D is reached from the ring, the ammo selector, the
	 * keypad and the examine screen - all four inside S_CallInventory2, where
	 * InventoryActive is 1 - and from S_DrawPickup (specific\output.cpp), the
	 * little turning object in the corner after a pickup, where it is 0. The
	 * two never overlap: the inventory calls S_OutputPolyList itself, but it
	 * has CreateMonoScreen'd first and MonoScreenOn blocks the display's whole
	 * block there (output.cpp).
	 *
	 * S_DrawPickup runs inside S_OutputPolyList, after HDFrameDraw has drawn
	 * this frame's HD list and after HDFrameDrawGlow has finished with it, so
	 * an item pushed onto the list would suppress its own classic polygons and
	 * be drawn by nobody. So the display's item does not go on the list: it
	 * collects the same matrices through the same node walk and is drawn on
	 * the spot in HDInvEnd, in a bracket of its own. Everything below is
	 * shared by both callers except where it says otherwise. */
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

	/* The item's own screen centre, worked out once per item and handed to
	 * HDModelOffCentre at draw time. The derivation is over HDInvNode and the
	 * arithmetic in hd_model.h.
	 *
	 * No scale term. TROne multiplies its sheared row by f_mpersp/persp
	 * (hostglue_2d5.cpp:485-498) because its host builds a projection of its
	 * own out of the window's real width, while TR5 computes f_persp from a
	 * 4:3 width whatever the window is (specific\3dmath.cpp, AlterFOV:
	 * fov_width = phd_winheight * 320 / 240). This engine's HD projection is
	 * built from that very f_persp (hd_level.cpp, HDFrameCaptureProjection
	 * takes f_persp, HDRenderMakeProjection is fed it), so the factor is
	 * exactly one and the ring is the same size in HD as it is classic. */
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
	/* No room. A ring item stands in no room at all, and -1 is what the
	 * frame list already means by that - HDFrameScissorForRoom leaves the
	 * scissor off for it, which is the whole screen, which is what the ring
	 * has. */
	g_open_room = -1;
	/* The node mask, exactly. item->mesh_bits here is INVOBJ::meshbits,
	 * which the ring uses on purpose: the passport's three page states, the
	 * compass needle that turns on its own, the globe's markers. TROne needs
	 * a flag of its own for this (TRONE_LIGHT_EXACT_NODES); this object path
	 * takes the mask as a mask everywhere, so an unnamed node draws nothing
	 * here for the same reason it draws nothing in the world. */
	g_open_mask = (unsigned int)item->mesh_bits;
	/* No mesh swap. DrawThreeDeeObject2D (game\newinv2.cpp) builds its
	 * ITEM_INFO on the stack and fills in eleven fields; meshswap_meshbits is
	 * not one of them,
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

/* One node of a ring item. The engine's matrix stack, straight off, exactly as
 * HDObjectNode takes it in the world.
 *
 * The off-centre projection is not applied here. The classic ring projects
 * an item about the item's own place on the screen: aTransformLightClipMesh
 * (specific\output.cpp) puts a vertex at
 *
 *     sx = vx * p / vz + f_centerx,   p = f_mpersp
 *
 * and phd_PutPolygonsPickup sets f_centerx = X, f_centery = Y for the length of
 * one mesh. The HD path builds one projection for the whole frame, about the
 * screen centre Cx/Cy, so the difference - kx = (X - Cx) / p - has to be put
 * somewhere, and there are two places it can go.
 *
 * A shear of the node matrix (row 0 += kx * row 2) is exact for the position,
 * because the projection divides by vz and anything added to vx in proportion
 * to vz survives as a constant pixel offset. It is wrong for the light:
 * hd_model.hlsl skins the vertex normal through the very same matrix and
 * renormalises it (Skin(normal, node), then rsqrt), so a sheared node matrix
 * tilts and rescales every normal of the item and the ring's one sun lights it
 * by an amount that depends on where it stands on the screen. Measured on the
 * shader itself - ModelProbe -ringlight over the object's own pixels - a shear
 * leaves the middle of the inventory row 28 % too dark and the ammo row 16 %
 * too bright, two rings that stand at once and 45 % apart.
 *
 * So it goes into the projection, where the classic path has it: the item
 * carries kx / ky to HDModelOffCentre (hd_model.h), which writes _31 and _32
 * of a copy of the frame's projection for that one draw. A normal never goes
 * through a projection, so it cannot be touched by one. The node matrix that
 * reaches the card is the engine's own, and the ring's light is the classic
 * ring's light to the bit.
 *
 * The idea of folding the ring's centre into the item at all is TROne's
 * (Copyright 2026 Todesengel, GPLv3, games\tr5\glue\hostglue_2d5.cpp:502-506,
 * which shears the matrix); the way taken here and the arithmetic in
 * HDModelOffCentreMatrix are derived from TR5's own projection line.
 */
void HDInvNode(int node)
{
	ProfScope prof(PROF_HD_OBJ);

	// The ring and the pickup display collect a node in exactly the same
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

	/* Which of the two was open, taken before both are cleared. The ring
	 * puts the item on the frame's list and the display draws it here and
	 * now; everything between this line and the light below is the same for
	 * both. */
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
	entry.lipsTrack = -1;	// nothing on the ring speaks
	entry.lipsFrame = -1;

	/* A mod's own inventory model, for the ring and the display in the
	 * corner (and the floor, see HDObjectEnd). It is a file out of another
	 * game's conventions, so it is drawn rigidly on the item's root node and
	 * put into this slot's frame by the correction measured at level build.
	 *
	 * The node mask goes with it. The mask the ring hands in is INVOBJ's own -
	 * the passport's pages, the compass needle - and it names nodes of the
	 * classic model, which this file has none of, so the one node it has is
	 * drawn. A moving part in a mod's ring model is not supported; the objects
	 * that have one (the passport, the compass, the globe) have no _OPTION
	 * model in the mods folder. */
	const RingModel* ring = ring_model_of(g_open_slot);

	if (ring)
	{
		entry.drawSlot = ring->model;
		entry.head = false;
		entry.nodes = 1;
		entry.mask = ~0u;
	}

	entry.room = g_open_room;
	entry.view = ViewSerial;
	// The ring's own screen centre, carried to the draw and put into the
	// projection there - never into these matrices.
	entry.offCentreX = g_ring_kx;
	entry.offCentreY = g_ring_ky;
	// The ring and the display are never a reflection: no pixel is clipped.
	stamp_clip(entry.clip, false);

	store_matrices(entry);

	/* And the correction goes in front of the item's own matrix: the model
	 * is moved into this slot's frame first and the engine's
	 * matrix then does with it exactly what it does with the EXTRA model. It
	 * is not folded into the projection like the ring's off-centre term - that
	 * one must not touch a normal and this one must, because the model is
	 * turned. */
	if (ring)
	{
		float placed[16];

		ring_mul(ring->fix, entry.matrix, placed);
		memcpy(entry.matrix, placed, sizeof(placed));
	}

	// Node 0's view z, for the log line - see RingLogItem::z.
	if (!pickup && g_ring_open_row >= 0 && g_ring_open_row < 16)
		g_ring_log[g_ring_open_row].z = entry.matrix[14];

	/* The ring's light - and the display's, which is the same light with one
	 * number different. Both are the classic path's own and nothing else.
	 *
	 * phd_PutPolygonsPickup (specific\output.cpp) throws the whole level
	 * lighting away for an inventory object and puts up two things: an
	 * ambient of 8/8/8, and one sun whose colour is the ring's pcbright for
	 * this item and whose direction is the fixed (0, -50, -100) - up and
	 * towards the camera - normalised over sqrt(12500).
	 *
	 * The classic line reads that direction out of D3DMView, which is the
	 * node's matrix at that moment (aSetViewMatrix is called at the top of
	 * the same function), so what it computes is the direction expressed in
	 * the node's own space: vec = R^T d. The shading term is then
	 * dot(n_model, R^T d) = dot(R n_model, d) = dot(n_view, d) - so in view
	 * space, which is where HD_OBJLIGHT_SUN lives, the direction is simply d
	 * and no per-node work is left.
	 *
	 * The one number that differs between the two callers is the sun's
	 * scale. aTransformLightClipMesh has one line for it -
	 *
	 *     if (!InventoryActive) val *= 0.75F; else val += val;
	 *
	 * - and the pickup display is drawn with InventoryActive 0. So the ring's
	 * sun is doubled and the display's is three quarters, a factor of 2.667
	 * between them; a display drawn with the ring's 2.0 would be visibly
	 * brighter than the classic one it replaces. It is the same test this
	 * file makes for an item in the world (fill_lights, at the top of this
	 * file).
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
	memset(&entry.soft, 0, sizeof(entry.soft));
	entry.cast = HD_OBJ_CAST_NONE;
	entry.castMask = 0;
	/* No sheen, which is what an item has in the level too - the specular
	 * term is Lara's alone. A pistol on the ring therefore shines exactly as
	 * the same pistol shines lying on the floor. */
	entry.specStrength = 0.0f;
	entry.specPower = 1.0f;

	/* And here the two part. The ring's item takes its
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

		// A part of Lara pushed for the projected shadow's map alone.
		if (entry.cast == HD_OBJ_CAST_ONLY)
			continue;

		/* The call switches the scissor test off for every item and takes a
		 * count on the way past; the argument is over HDFrameScissorForRoom
		 * in hd_level.cpp. In short: the classic path hands every item the
		 * whole screen (game\draw.cpp, PrintObjects), and clipping an item to
		 * its room's portal window cuts enemies in half at a doorway's edge
		 * and hides some soldiers entirely. */
		HDFrameScissorForRoom(entry.room);

		lighting.lights = entry.lights;
		lighting.lightCount = entry.lightCount;
		lighting.sunScale = entry.sunScale;
		lighting.specStrength = entry.specStrength;
		lighting.specPower = entry.specPower;

		/* The inventory ring's own screen centre. Zero for
		 * every item in the world, and then HDModelOffCentre writes nothing
		 * at all - it only touches the projection register when the value
		 * really changes, so a ring frame costs one 4-register upload per
		 * item and an ordinary frame costs none. Handed over here rather
		 * than folded into entry.matrix, because the shader takes the vertex
		 * normal through those matrices as well; see hd_model.h. */
		HDModelOffCentre(entry.offCentreX, entry.offCentreY);

		/* A mirror's reflection ends where the copy behind the glass ends:
		 * its three planes, uploaded only when they change - a mirror's items
		 * and Lara's pieces follow each other on the list, and everything
		 * else carries the planes that keep every pixel. */
		HDModelClip(entry.clip);

		/* drawSlot is the swapped model when the item asked for one and it
		 * arrived, and the item's own slot otherwise. It is the same
		 * skeleton, the same node count and the same bind pose, so nothing
		 * else in this call changes. */
		/* The lips go with the draw that holds the face: Lara's face is a
		 * model of its own (HDObjectPush), an actor's is his separate face
		 * below, and a model with no lips ignores them. */
		HDModelDraw(entry.drawSlot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting, &entry.cut, entry.cull, entry.lipsTrack,
			entry.lipsFrame, &entry.soft);

		/* The separate HD face, right after the body and on the same
		 * matrices. VON_CROY.TRM is a body with an empty collar and
		 * VON_CROY_HEAD.TRM is the face that fills it, so the two are one
		 * object and the second is not optional.
		 *
		 * Only for a slot that has one, which saves a map lookup per item
		 * per frame for a thing about five slots in a level own. The head
		 * belongs to the item's own slot, not to the swapped model - a
		 * damage stage or a drawn pistol does not change a face. */
		if (entry.head)
			HDModelDrawHead(entry.slot, entry.matrix, entry.nodes, entry.light,
				entry.mask, &lighting, &entry.cut, entry.cull, entry.lipsTrack,
				entry.lipsFrame);
	}

	/* LIPLOG: one line per frame change of a speaking face, out of the model
	 * file's own record of what the draws above did - the frame counter, the
	 * cutscene, the frame and the track, which of the clip, the ease or the
	 * bind was shown and how far the farthest vertex stood from the bind.
	 * `lang` is the language the audio path looks for first (specific\
	 * audio.cpp, ACMEmulateCDPlay); the track files are the same for all of
	 * them. The record is emptied whether or not it was written. */
	if (HDModelLipsEventCount())
	{
		if (gLogEnabled)
		{
			const char* lang = "EN";

			switch (Gameflow ? Gameflow->Language : 0)
			{
			case GERMAN: lang = "DE"; break;
			case FRENCH: lang = "FR"; break;
			case ITALIAN: lang = "IT"; break;
			case SPANISH: lang = "ES"; break;
			case JAPAN: lang = "JP"; break;
			default: break;
			}

			for (int i = 0; i < HDModelLipsEventCount(); i++)
			{
				const HDModelLipsEvent* e = HDModelLipsEventAt(i);

				/* The loop's and the firing face's frame changes are once a
				 * tick; hd_laradraw.cpp writes their changes of face and
				 * their blinks instead. */
				if (e->track >= HD_LIPS_TRACK_LOOP)
					continue;

				Log("LIPLOG f=%d cut=%ld frame=%d track=%d slot=%d src=%s "
					"jaw=%.1f lang=%s", (int)GlobalCounter, cutseq_num, e->frame,
					e->track, e->slot, HDLipsSourceName(e->source), e->jaw, lang);
			}
		}

		HDModelLipsEventsClear();
	}

	// The objects are done and the scissor window of the last one must not
	// outlive them - the caller closes the bracket next, and the bracket is
	// what the classic path gets back. Neither must the last item's screen
	// centre, for the same reason - the glow phase opens a second bracket on
	// the same list and the classic path shares the register file. Nor the
	// last item's clip planes.
	HDModelOffCentre(0.0f, 0.0f);
	HDModelClip(0);
	HDRenderScissorOff();

	/* The phases add up. HDModelFrameBegin zeroes the model file's own
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

int HDObjectLights(ITEM_INFO* lit, HDObjLight* out, int max, float* sunScale,
	HDObjLightReport* report)
{
	int count = 0;

	if (!out || max <= 0)
		return 0;

	fill_lights(out, max, &count, sunScale, lit, report);
	return count;
}

/* The same two lists and the same tests as fill_lights' suns and its points
 * and spots, in world space and without the conversion to view space; the
 * dynamics are a third list fill_lights adds between the two, and are not
 * read here. A sun's file direction is the way its light travels - the
 * model shader's "towards the light" is its negation. */
int HDObjectShadowLights(ITEM_INFO* lit, HDShadowLight* out, int max)
{
	int count = 0;

	if (!lit || !out || max <= 0)
		return 0;

	void* lists[2] = { lit->il.pCurrentLights, lit->il.pPrevLights };
	const long counts[2] = { lit->il.nCurrentLights, lit->il.nPrevLights };

	for (int list = 0; list < 2; list++)
	{
		PCLIGHT* source = (PCLIGHT*)lists[list];

		for (long i = 0; source && i < counts[list] && count < max; i++)
		{
			const PCLIGHT& l = source[i];

			if (!l.Active || HDObjLightIsDark(l.r, l.g, l.b))
				continue;

			HDShadowLight& to = out[count];

			to.r = l.r;
			to.g = l.g;
			to.b = l.b;
			to.range = 0.0f;

			if (l.Type == LIGHT_SUN)
			{
				to.kind = HD_SHADOW_SUN;
				to.x = l.nx;
				to.y = l.ny;
				to.z = l.nz;
			}
			else if (l.Type == LIGHT_POINT || l.Type == LIGHT_SPOT)
			{
				const bool spot = l.Type == LIGHT_SPOT;

				to.kind = spot ? HD_SHADOW_SPOT : HD_SHADOW_POINT;
				to.x = l.x;
				to.y = l.y;
				to.z = l.z;
				to.range = spot ? l.Cutoff : l.Outer;

				if (to.range <= 0.0f)
					continue;
			}
			else
				continue;

			count++;
		}
	}

	return count;
}

int HDObjectCastFrame()
{
	int drawn = 0;

	for (int i = 0; i < g_count; i++)
	{
		const FrameItem& entry = g_frame[i];

		if (entry.cast != HD_OBJ_CAST_ALSO && entry.cast != HD_OBJ_CAST_ONLY)
			continue;

		if (HDModelCastDraw(entry.drawSlot, false, entry.matrix, entry.nodes,
			entry.castMask, entry.lipsTrack, entry.lipsFrame, &entry.soft))
			drawn++;

		if (entry.head && HDModelCastDraw(entry.slot, true, entry.matrix, entry.nodes,
			entry.castMask, entry.lipsTrack, entry.lipsFrame, 0))
			drawn++;
	}

	return drawn;
}

int HDObjectCastCount()
{
	int marked = 0;

	for (int i = 0; i < g_count; i++)
		if (g_frame[i].cast == HD_OBJ_CAST_ALSO || g_frame[i].cast == HD_OBJ_CAST_ONLY)
			marked++;

	return marked;
}

int HDObjectCastOthersFrame()
{
	int drawn = 0;

	for (int i = 0; i < g_count; i++)
	{
		const FrameItem& entry = g_frame[i];

		if (entry.cast != HD_OBJ_CAST_OTHERS)
			continue;

		// The body - or the swapped model - and the separate face, as drawn.
		if (HDModelCastDraw(entry.drawSlot, false, entry.matrix, entry.nodes,
			entry.castMask, entry.lipsTrack, entry.lipsFrame, &entry.soft))
			drawn++;

		if (entry.head && HDModelCastDraw(entry.slot, true, entry.matrix, entry.nodes,
			entry.castMask, entry.lipsTrack, entry.lipsFrame, 0))
			drawn++;
	}

	return drawn;
}

int HDObjectCastOthersCount()
{
	int marked = 0;

	for (int i = 0; i < g_count; i++)
		if (g_frame[i].cast == HD_OBJ_CAST_OTHERS)
			marked++;

	return marked;
}

bool HDObjectBlobGivesWay(ITEM_INFO* item, OBJECT_INFO* obj)
{
	g_gave_way = 0;

	if (!item || !obj || !obj->shadow_size)
		return false;

	/* Her step first: it ran only in a frame the projected shadow runs in,
	 * so a frame without it asks nothing else - in particular not for the
	 * second target, which asking makes. */
	const HDShadowFrame* shadow = HDShadowFrameState();
	const bool herStep = shadow->stepped != 0;
	const bool runs = herStep && shadow->runs != 0;
	const bool hdModel = runs && begin_decision(item, obj) == BEGIN_HD;
	const bool target = hdModel && !g_reflection && HDRenderShadowReceive2Ready() &&
		HDModelCastReady() && HDRenderShadowOthersReady();

	if (!HDShadowBlobGivesWay(runs, herStep, target, hdModel, g_reflection))
		return false;

	g_gave_way = item;
	return true;
}

bool HDObjectPush(int slot, const float* matrix, int nodes, const float* light,
	unsigned int mask, const HDObjLight* lights, int lightCount, float sunScale,
	const HDModelNeckCut* cut, int cull, float specStrength, float specPower,
	int lipsTrack, int lipsFrame, const HDModelSoft* soft, int cast,
	unsigned int castMask)
{
	/* Not behind g_active. That flag says "this level has HD objects", and a
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

	if (soft)
		entry.soft = *soft;
	else
		memset(&entry.soft, 0, sizeof(entry.soft));

	entry.cast = cast == HD_OBJ_CAST_ALSO || cast == HD_OBJ_CAST_ONLY
		? cast : HD_OBJ_CAST_NONE;
	entry.castMask = castMask;
	entry.specStrength = specStrength;
	entry.specPower = specPower;
	/* No portal window. Everything that comes through here is a
	 * piece of Lara, and Lara is in the camera's own room, whose window is
	 * the whole screen - and a piece of her that reaches out of that room,
	 * an arm through a doorway in third person, is the one thing in the
	 * frame that must never be cut. */
	entry.room = -1;
	entry.view = ViewSerial;
	entry.offCentreX = 0.0f;	// Lara is never on the ring
	entry.offCentreY = 0.0f;
	// Her pieces and the braid in a mirror carry the mirror's planes.
	stamp_clip(entry.clip, g_reflection);

	/* A pushed model that is itself a face speaks the way an actor's face
	 * does - see HDObjectLips. Where it has no clip for the track it draws
	 * as it was built.
	 *
	 * Only a cut's track marks a cut's clip as playing. The loop between
	 * lines and the firing face (HD_LIPS_TRACK_LOOP, _FIRE) play outside
	 * any cut, and HDObjectFrameReset would otherwise close their clip and
	 * their buffer at the start of every frame; a cut's own track closes
	 * them when it opens (HDModelLipsOpen). */
	entry.lipsTrack = -1;
	entry.lipsFrame = -1;

	if (lipsTrack >= 0 && HDModelLipsOpen(slot, false, lipsTrack) > 0)
	{
		entry.lipsTrack = lipsTrack;
		entry.lipsFrame = lipsFrame < 0 ? 0 : lipsFrame;

		if (lipsTrack < HD_LIPS_TRACK_LOOP)
			g_lips_playing = true;
	}

	g_count++;
	return true;
}

/* The camera the objects of this frame were collected with.
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

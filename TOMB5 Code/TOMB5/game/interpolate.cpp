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
#include "interpolate.h"
#include "interpolate_math.h"		// the lerps and the thresholds - InterpTest.cpp checks these
#include "control.h"			// items, level_items, framecount, GlobalCounter
#include "camera.h"			// camera, FirstPersonActive, GetFirstPersonRoll
#include "effects.h"			// effects[24]
#include "hair.h"			// hairs[2][7]
#include "deltapak.h"			// GLOBAL_playing_cutseq
#include "spotcam.h"			// bUseSpotCam
#include "gameflow.h"			// gfLevelId, for the log line
#include "lara.h"			// lara.poisoned - the poison sway guard
#include "delstuff.h"			// lara_matrices - ARMLOG reads the view set
#include "tomb4fx.h"			// Gunshells, Bubbles

// interpolate_math.h mirrors camera_type by value, so InterpTest can ask the
// same question without the engine.
static_assert(CHASE_CAMERA == INTERP_CAM_CHASE && LOOK_CAMERA == INTERP_CAM_LOOK &&
	COMBAT_CAMERA == INTERP_CAM_COMBAT, "camera_type moved - update interpolate_math.h");
#include "rat.h"			// Rats
#include "bat.h"			// Bats
#include "spider.h"			// Spiders
#include "rope.h"			// RopeList
#include "../specific/3dmath.h"		// phd_LookAt, CamRoll
#include "../specific/function_stubs.h"	// Log, gLogEnabled
#include "../specific/output.h"		// ActiveFpsMode, GameFrameMinTicks
#include "../specific/time.h"		// TIME_SubTick
#include "../specific/file.h"		// commands - where turn180_effect fires (InterpTurnOf)
#include "../tomb5/tomb5.h"		// tomb5.fps_mode

namespace
{
	/* InitialiseFXArray (game\items.cpp) allocates and walks 24 FX_INFO
	 * slots; the count is a literal there, so it is repeated here. */
	const int FX_COUNT = 24;

	// hairs[2][7] (game\hair.cpp) - two pigtails of seven points.
	const int HAIR_TAILS = 2;
	const int HAIR_LINKS = 7;

	/* Things that move without a usable velocity, and so go through the
	 * snapshot instead of InterpTrailLong:
	 *
	 *   Gunshells   the fall speed is negated and the position written back
	 *               on a bounce (UpdateGunShells, game\tomb4fx.cpp), so the
	 *               stored speed is not the step taken on that tick.
	 *   Rats        the step is `sin(y_rot) * speed`, and angle and speed are
	 *   Bats        rewritten after the position moves; pos + speed, the
	 *   Spiders     same shape as an items[] entry.
	 *   Bubbles     the drift is a sine of an already advanced rotation, and
	 *               the impulse decays after it is spent.
	 *   RopeList    a constraint solve over 24 links with no velocity; Lara
	 *               hangs on it, so it must blend with her or her hands leave
	 *               it every second frame.
	 *
	 * 16.7 KB of statics (10.1 for the 184 movers with their pointer table,
	 * 1.2 for the bubbles, 5.6 for the ropes). Their drawing routines are
	 * unchanged and find the blended values in the same globals. */
	const int SHELL_COUNT = 24;	// UpdateGunShells / DrawGunshells walk 24
	const int RAT_COUNT = 32;	// UpdateRats walks 32
	const int BAT_COUNT = 64;	// UpdateBats walks 64
	const int SPIDER_COUNT = 64;	// UpdateSpiders walks 64
	const int MOVER_COUNT = SHELL_COUNT + RAT_COUNT + BAT_COUNT + SPIDER_COUNT;

	const int BUBBLE_COUNT = 40;	// UpdateBubbles / DrawBubbles walk 40
	const int ROPE_COUNT = 8;	// RopeList[8] (game\rope.h)
	const int ROPE_LINKS = 24;	// MeshSegment[24], and DrawRope draws 24

	// The thresholds and lerps live in interpolate_math.h (no engine
	// dependency, so the offline test includes the real file); the reasoning
	// for the numbers is there.
	const long ITEM_SNAP = INTERP_ITEM_SNAP;
	const long CAM_SNAP = INTERP_CAM_SNAP;

	/* The item snap in a rope's units: its drawn links are 16.16 fixed point.
	 * It catches SetPendulumPoint (game\rope.cpp), which moves the whole rope
	 * in one tick when Lara takes hold of it. */
	const long ROPE_SNAP = INTERP_ITEM_SNAP << 16;

	/* One thing's state before the last tick. room, object, anim and frame
	 * are not interpolated: they tell this state from a different thing that
	 * took over the slot (otherwise drawn sliding from the old one's place to
	 * the new one's), and give the pose blend its previous frame. */
	struct InterpNode
	{
		long x, y, z;
		short xr, yr, zr;
		short room;
		short object;
		short anim, frame;
		unsigned char valid;
	};

	// What InterpBeginFrame overwrote, so InterpEndFrame can put it back.
	struct SavedNode
	{
		long x, y, z;
		short xr, yr, zr;
	};

	/* The same pair for a thing with a place and no facing (a bubble, a rope
	 * mesh segment: PHD_VECTORs). `valid` is also the identity test: a freed
	 * bubble slot (size 0) is handed straight to the next bubble, so "alive
	 * on the last tick" is all that can be known. */
	struct InterpVec
	{
		long x, y, z;
		unsigned char valid;
	};

	struct SavedVec
	{
		long x, y, z;
	};

	/* Lara's nine aim angles: torso twist, head turn and both arms' aim. They
	 * do not come from her animation; LookLeftRight, the weapon aim and the
	 * look camera write them once per tick and both halves of
	 * CalcLaraMatrices read them through phd_RotYXZ. So they are blended in
	 * the snapshot like a position, eighteen shorts and no draw code, the
	 * short way round. The arm's own animation frame is handled separately
	 * (InterpArmAnim, InterpArmPrev). */
	struct InterpLaraAim
	{
		short head[3];		// y, x, z
		short torso[3];
		short right[3];		// lara.right_arm.y_rot / x_rot / z_rot
		short left[3];
		unsigned char valid;
	};

	struct InterpCamera
	{
		long px, py, pz;
		long tx, ty, tz;
		short room;
		short type, oldType;
		short fixed;
		short spot;		// bUseSpotCam - entering a flyby is a cut
		short roll;
		unsigned char valid;
	};

	/* Memory: 27 KB of statics, nothing allocated. InterpNode is 28 bytes
	 * after alignment, so the previous state is 15.0 KB; the copy of what a
	 * frame overwrote is 10.7 KB (the restore is exact) and its index lists
	 * 1.1 KB. */
	InterpNode g_prevItem[MAX_ITEMS];
	InterpNode g_prevFx[FX_COUNT];
	InterpNode g_prevHair[HAIR_TAILS * HAIR_LINKS];
	InterpCamera g_prevCam;
	InterpLaraAim g_prevAim;
	InterpLaraAim g_savedAim;
	bool g_aimTouched = false;

	/* May this slot's pose be blended in time? Decided in InterpBeginFrame's
	 * walk before the blended position overwrites the live one (afterwards
	 * the snap test would measure half the distance).
	 *
	 * POSE_ROOT is the side channel of the root rule: GetFramesNow sets it on
	 * a slot whose pose the neighbour rule refused, and the draw then blends
	 * the slot's root offset in time. One byte per slot, cleared every drawn
	 * frame, and zero for a snapped slot, a frame carrying the tick and all of
	 * fps_mode 0. */
	enum { POSE_NO = 0, POSE_OK = 1, POSE_ROOT = 2 };

	unsigned char g_poseOk[MAX_ITEMS];

	// The offset the PREVIOUS tick drew, for the slots at POSE_ROOT. Never
	// read without it, so it needs no clearing of its own.
	long g_rootPrev[MAX_ITEMS][3];

	/* Whether the previous offset must be turned by a half turn into this
	 * tick's frame (InterpRootFlip). Decided in the same walk as g_poseOk,
	 * where both ticks' rotations are still known, and written for every slot
	 * the walk lets through, so a slot at POSE_ROOT never reads it stale. */
	unsigned char g_rootFlip[MAX_ITEMS];

	/* Whether the previous tick drew its frame turned by half a turn itself
	 * (InterpRootRefused's `turned`), so its offset needs the same turn;
	 * written with g_rootPrev and read only with it. */
	unsigned char g_rootTurned[MAX_ITEMS];

	/* Where turn180_effect fires in each animation (InterpTurnOf), indexed
	 * by the animation number's low bits and checked against the number. */
	struct TurnSlot
	{
		short anim;
		short flip;
		short pair;
		unsigned char kind;
		unsigned char valid;
	};

	TurnSlot g_turn[512];

	/* The tick's position step in the item's own y_rot frame (the frame of
	 * the offsets and of TranslateItem), measured before ApplyNode overwrites
	 * the live position. See InterpRootNarrow. */
	long g_rootStep[MAX_ITEMS][3];

	// Lara's pose is built two or three times a frame and counted once.
	bool g_laraPosed = false;

	// Her root blend, latched the same way.
	bool g_laraRooted = false;

	/* The weapon arms' animation frame at the previous tick. The draw needs
	 * both ticks' frames at once, so this is read back through InterpArmPrev
	 * rather than written into the live structure. The frame_base pointer is
	 * kept as well as the numbers: it is derived four different ways
	 * (larafire.cpp, lara1gun.cpp, lara2gun.cpp, laraflar.cpp), and comparing
	 * it proves both frames come from the same animation data. */
	struct InterpArmAnim
	{
		short* base[2];
		short anim[2];
		short frame[2];
		unsigned char valid;
	};

	InterpArmAnim g_prevArm;

	// Both arms are asked two or three times a frame; counted once, and by
	// frame, exactly as g_laraPosed is.
	bool g_armBlended = false;
	bool g_armRefused = false;

	SavedNode g_savedItem[MAX_ITEMS];
	SavedNode g_savedFx[FX_COUNT];
	SavedNode g_savedHair[HAIR_TAILS * HAIR_LINKS];
	short g_touchedItem[MAX_ITEMS];
	short g_touchedFx[FX_COUNT];
	short g_touchedHair[HAIR_TAILS * HAIR_LINKS];
	int g_touchedItems = 0;
	int g_touchedFxs = 0;
	int g_touchedHairs = 0;

	/* The particle snapshot, the same three arrays as for items: the state
	 * before the tick, a copy of what the frame overwrote, and the list of
	 * touched entries the restore walks. 16.7 KB, bringing the file to about
	 * 44 KB of statics. The four simple movers share one index space;
	 * GatherMovers alone knows its order. */
	PHD_3DPOS* g_moverPos[MOVER_COUNT];
	unsigned char g_moverAlive[MOVER_COUNT];
	InterpNode g_prevMover[MOVER_COUNT];
	SavedNode g_savedMover[MOVER_COUNT];
	short g_touchedMover[MOVER_COUNT];
	int g_touchedMovers = 0;

	InterpVec g_prevBubble[BUBBLE_COUNT];
	SavedVec g_savedBubble[BUBBLE_COUNT];
	short g_touchedBubble[BUBBLE_COUNT];
	int g_touchedBubbles = 0;

	InterpVec g_prevRope[ROPE_COUNT * ROPE_LINKS];
	SavedVec g_savedRope[ROPE_COUNT * ROPE_LINKS];
	short g_touchedRope[ROPE_COUNT * ROPE_LINKS];
	int g_touchedRopes = 0;

	// The camera is restored by rebuilding the view matrix, not by writing a
	// field back.
	InterpCamera g_savedCam;
	bool g_camTouched = false;

	bool g_applied = false;		// this frame wrote into the live state
	float g_alpha = 1.0F;		// what this frame is being drawn at

	/* The frame being drawn carries no logic tick. Latched at the top of the
	 * frame and held until the next one, because the counters that ask it
	 * sit on both sides of DrawPhaseGame (see interpolate.h). */
	bool g_tween = false;

	/* The tween latch at fps_mode 2. At 60 Hz there are two frames per tick;
	 * bound to e.g. a 144 Hz display there are several at no fixed phase and
	 * `alpha < 1` would stop every counter. So g_tickSerial steps once per
	 * logic tick (InterpCapturePrev), and a drawn frame carries a tick when
	 * the serial has moved since the last frame that claimed one. Exactly one
	 * frame per tick steps the counters, and a hitch running three ticks in
	 * one frame steps them once, as at 30 Hz. fps_mode 1 keeps the parity
	 * rule. */
	unsigned long g_tickSerial = 0;
	unsigned long g_steppedSerial = 0;

	// The logic RNG over a tween frame - see the header's note on rand_1.
	long g_seed = 0;
	bool g_seedSaved = false;

	// TICKLOG. What stepped this second, by kind.
	int g_step[INTERP_STEP_COUNT] = { 0 };

	/* INTERPLOG. Counted at every fps_mode, because a fps_mode 0 run is the
	 * baseline the per-frame movement is compared against; written only with
	 * -log. */
	int g_secFrames = 0;
	int g_secTween = 0;		// frames drawn at an alpha other than 1

	/* The blend factor as a distribution (min / mean / max). At 144 Hz a
	 * healthy second reads about 0.10 / 0.50 / 0.98, the interval walked
	 * evenly; a maximum pinned at 1.00 with the mean near it is the clamp
	 * firing, frames arriving faster than the logic has states. */
	double g_secAlphaSum = 0;
	float g_secAlphaMin = 1.0F;
	float g_secAlphaMax = 0.0F;
	int g_secItems = 0;		// interpolated items, added over the second
	double g_secLaraStep = 0;	// |dpos| of lara_item per DRAWN frame
	int g_secLaraFrames = 0;
	int g_snapTeleport = 0;
	int g_snapRoomJump = 0;
	int g_snapObject = 0;
	int g_snapInvalid = 0;
	int g_snapHalfTurn = 0;	// rotations that turned half a turn in one tick and kept the live value
	int g_snapCamCut = 0;
	int g_snapCutscene = 0;

	/* One counter per camera cut reason, so the log names which rule fired
	 * and not only how often. */
	int g_cutType = 0;		// camera.type / old_type / fixed_camera
	int g_cutHandover = 0;	// type changes inside the smooth family - not a cut
	int g_cutSpot = 0;		// a flyby beginning or ending
	int g_cutEye = 0;		// the eye jumped further than CAM_SNAP
	int g_cutAim = 0;		// the view DIRECTION jumped (see InterpCamAimCut)
	int g_secPose = 0;		// items drawn with a blended pose
	int g_secPoseSkip = 0;		// ... and refused one by the neighbour rule
	int g_secPoseFar = 0;		// ... refused because the pair is too far apart
	int g_secPoseHD = 0;		// ... of which went through the HD path
	int g_secLaraPose = 0;		// frames on which LARA's own pose was blended
	int g_secPoseBack = 0;		// frames on which Lara's pose was blended walking backwards
	/* Refused tween frames whose root offset was blended in time. A count,
	 * not a mean per frame: once per climb cycle would round to 0.0. */
	int g_secRootFix = 0;
	int g_secRootLines = 0;		// ... and how many of them wrote Lara's line
	/* ... of those, the ones whose previous offset was turned by a half turn
	 * first (turn180_effect fires once per roll). */
	int g_secRootFlip = 0;
	/* The worst bone length the pose blend left this second, in hundredths,
	 * with node, animation and frame. 100 is a rigid pose, and what a second
	 * without tween frames reads. */
	int g_secStretch = 100;
	int g_secStretchNode = -1;
	int g_secStretchAnim = -1;
	int g_secStretchFrame = -1;

	int g_secArm = 0;		// frames with an arm blended in time
	int g_secArmSkip = 0;		// ... and frames on which the rule refused
	int g_secParticles = 0;		// particles moved, added over the second
	unsigned long g_secPartKinds = 0;	// ... and which kinds they were, as a mask
	unsigned short g_secCounter = 0;	// GlobalCounter when this second began
	int g_at = 0;			// seconds since the last invalidation
	long g_lastDrawnX = 0, g_lastDrawnY = 0, g_lastDrawnZ = 0;
	bool g_lastDrawnValid = false;

	// Short names for the shared arithmetic.
	inline short LerpAngle(short a, short b, float alpha) { return InterpLerpAngle(a, b, alpha); }
	inline long LerpLong(long a, long b, float alpha) { return InterpLerpLong(a, b, alpha); }
	inline long Manhattan(long dx, long dy, long dz) { return InterpManhattan(dx, dy, dz); }

	/* The roll the view is rebuilt with. CamRoll (specific\3dmath.cpp) is
	 * what the last phd_LookAt was handed, for all three rolling cameras; in
	 * first person it is read through the camera's own getter so this stays
	 * right if the head camera ever bypasses phd_LookAt. */
	short CameraRollNow()
	{
		if (FirstPersonActive())
			return GetFirstPersonRoll();

		return CamRoll;
	}

	void CaptureNode(InterpNode* n, const PHD_3DPOS* pos, short room, short object,
		short anim, short frame)
	{
		n->x = pos->x_pos;
		n->y = pos->y_pos;
		n->z = pos->z_pos;
		n->xr = pos->x_rot;
		n->yr = pos->y_rot;
		n->zr = pos->z_rot;
		n->room = room;
		n->object = object;
		n->anim = anim;
		n->frame = frame;
		n->valid = 1;
	}

	/* Returns SNAP_NONE to blend this node from `n` to `pos`, or the reason
	 * not to. The reasons are counted: a snap rule firing too often only
	 * looks like 30 Hz in the picture but is obvious in the log. */
	enum SnapWhy { SNAP_NONE = 0, SNAP_INVALID, SNAP_OBJECT, SNAP_TELEPORT, SNAP_ROOMJUMP };

	SnapWhy NodeSnap(const InterpNode* n, const PHD_3DPOS* pos, short room, short object)
	{
		if (!n->valid)
			return SNAP_INVALID;

		if (n->object != object)
			return SNAP_OBJECT;

		if (Manhattan(pos->x_pos - n->x, pos->y_pos - n->y, pos->z_pos - n->z) > ITEM_SNAP)
			return n->room != room ? SNAP_ROOMJUMP : SNAP_TELEPORT;

		return SNAP_NONE;
	}

	/* Did it move at all? Of up to 512 item slots only a handful travel in a
	 * tick; the rest are skipped in one comparison, with nothing to blend or
	 * restore. The log's `items=` therefore counts things that really moved. */
	bool NodeMoved(const InterpNode* n, const PHD_3DPOS* pos)
	{
		return n->x != pos->x_pos || n->y != pos->y_pos || n->z != pos->z_pos ||
			n->xr != pos->x_rot || n->yr != pos->y_rot || n->zr != pos->z_rot;
	}

	// One drawn frame and its blend factor. All three exits of
	// InterpBeginFrame pass through here, so live frames count too and a
	// second full of snaps reads 1.00/1.00/1.00.
	void NoteFrameAlpha(float alpha)
	{
		g_secFrames++;
		g_secAlphaSum += alpha;

		if (alpha < g_secAlphaMin)
			g_secAlphaMin = alpha;

		if (alpha > g_secAlphaMax)
			g_secAlphaMax = alpha;
	}

	void CountSnap(SnapWhy why)
	{
		switch (why)
		{
		case SNAP_INVALID:	g_snapInvalid++; break;
		case SNAP_OBJECT:	g_snapObject++; break;
		case SNAP_TELEPORT:	g_snapTeleport++; break;
		case SNAP_ROOMJUMP:	g_snapRoomJump++; break;
		default: break;
		}
	}

	// Writes the blended state into one live position, remembering what stood
	// there. Never called for a node that snapped; a rotation that turned
	// half a turn keeps its live value here.
	void ApplyNode(PHD_3DPOS* pos, const InterpNode* n, SavedNode* save, float alpha)
	{
		save->x = pos->x_pos;
		save->y = pos->y_pos;
		save->z = pos->z_pos;
		save->xr = pos->x_rot;
		save->yr = pos->y_rot;
		save->zr = pos->z_rot;

		pos->x_pos = LerpLong(n->x, save->x, alpha);
		pos->y_pos = LerpLong(n->y, save->y, alpha);
		pos->z_pos = LerpLong(n->z, save->z, alpha);

		/* A half turn is a snap: turn180_effect turned the node by half and
		 * the animation's root bone the other half, and blended apart the
		 * body would stand sideways for one frame. The whole rotation keeps
		 * the live value (the effect also negates x_rot); only the position
		 * blends. See InterpHalfTurn. */
		if (InterpHalfTurn(n->yr, save->yr))
		{
			g_snapHalfTurn++;
			return;
		}

		pos->x_rot = LerpAngle(n->xr, save->xr, alpha);
		pos->y_rot = LerpAngle(n->yr, save->yr, alpha);
		pos->z_rot = LerpAngle(n->zr, save->zr, alpha);
	}

	/* The tick's position step in the item's own frame. The offsets are in
	 * the item's frame and the positions in the world, so the step is turned
	 * back by the current y_rot with TranslateItem's own phd_sin/phd_cos and
	 * shift (game\control.cpp): a command of (0,-512,0) comes back as
	 * (0,-512,0). The current tick's rotation is the frame of the drawn
	 * offset, and still live at the only call site. x_rot and z_rot are not
	 * undone: TranslateItem and the state routines that move her by hand
	 * ignore them. See InterpRootNarrow. */
	void RootStep(const InterpNode* n, const PHD_3DPOS* pos, long out[3])
	{
		const long dx = pos->x_pos - n->x;
		const long dz = pos->z_pos - n->z;
		const long s = phd_sin(pos->y_rot);
		const long c = phd_cos(pos->y_rot);

		out[0] = (dx * c - dz * s) >> W2V_SHIFT;
		out[1] = pos->y_pos - n->y;
		out[2] = (dx * s + dz * c) >> W2V_SHIFT;
	}

	void RestoreNode(PHD_3DPOS* pos, const SavedNode* save)
	{
		pos->x_pos = save->x;
		pos->y_pos = save->y;
		pos->z_pos = save->z;
		pos->x_rot = save->xr;
		pos->y_rot = save->yr;
		pos->z_rot = save->zr;
	}

	/* ------------------------------------------------------- particles --
	 *
	 * Collects the four simple movers in one fixed order. Called from the
	 * capture and from the frame that applies the blend; the restore uses the
	 * pointers left here, so the set put back is the set that was blended. A
	 * null pointer is a set the level did not load (Rats, Bats and Spiders
	 * are game_malloc'd and absent in most levels); the static shell array is
	 * always there and an empty slot is marked by `alive`. */
	void GatherMovers()
	{
		int n = 0;

		for (int i = 0; i < SHELL_COUNT; i++, n++)
		{
			g_moverPos[n] = &Gunshells[i].pos;
			g_moverAlive[n] = (unsigned char)(Gunshells[i].counter ? 1 : 0);
		}

		for (int i = 0; i < RAT_COUNT; i++, n++)
		{
			g_moverPos[n] = Rats ? &Rats[i].pos : 0;
			g_moverAlive[n] = (unsigned char)(Rats && Rats[i].On ? 1 : 0);
		}

		for (int i = 0; i < BAT_COUNT; i++, n++)
		{
			g_moverPos[n] = Bats ? &Bats[i].pos : 0;
			g_moverAlive[n] = (unsigned char)(Bats && Bats[i].On ? 1 : 0);
		}

		for (int i = 0; i < SPIDER_COUNT; i++, n++)
		{
			g_moverPos[n] = Spiders ? &Spiders[i].pos : 0;
			g_moverAlive[n] = (unsigned char)(Spiders && Spiders[i].On ? 1 : 0);
		}
	}

	/* Snap and move tests for a thing that is only a place. `snap` is an
	 * argument because a bubble is in world units and a rope mesh segment in
	 * 16.16 fixed point (ProjectRopePoints, game\rope.cpp, shifts it by 16). */
	bool VecSnap(const InterpVec* p, const PHD_VECTOR* v, long snap)
	{
		if (!p->valid)
			return true;

		return Manhattan(v->x - p->x, v->y - p->y, v->z - p->z) > snap;
	}

	bool VecMoved(const InterpVec* p, const PHD_VECTOR* v)
	{
		return p->x != v->x || p->y != v->y || p->z != v->z;
	}

	/* The camera cut test. A cut (fixed camera taking over, spot cam node
	 * step, flyby start or end, binoculars) must jump, not drag the eye
	 * through the level for half a tick.
	 *
	 * camera.type is tested together with camera.old_type: CalculateCamera
	 * resets type to CHASE_CAMERA at its end unless a heavy camera's timer
	 * runs, and old_type is what the view was built as. A change of the pair
	 * is a cut only when it leaves the smooth family (InterpCamTypeCut); the
	 * hand-overs let through are logged as `handover=`. The target is
	 * measured as an aim, not a place (InterpCamAimCut). Each reason is
	 * counted for INTERPLOG. */
	bool CameraCut()
	{
		bool cut = false;

		const bool typeMoved = g_prevCam.type != (short)camera.type ||
			g_prevCam.oldType != (short)camera.old_type;

		if ((typeMoved && InterpCamTypeCut(g_prevCam.type, camera.type,
				g_prevCam.oldType, camera.old_type)) ||
			g_prevCam.fixed != (short)camera.fixed_camera)
		{
			cut = true;

			if (gLogEnabled)
				g_cutType++;
		}
		else if (typeMoved && gLogEnabled)
			g_cutHandover++;

		if (g_prevCam.spot != (short)bUseSpotCam)
		{
			cut = true;

			if (gLogEnabled)
				g_cutSpot++;
		}

		if (Manhattan(camera.pos.x - g_prevCam.px, camera.pos.y - g_prevCam.py,
			camera.pos.z - g_prevCam.pz) > CAM_SNAP)
		{
			cut = true;

			if (gLogEnabled)
				g_cutEye++;
		}

		if (InterpCamAimCut(g_prevCam.px, g_prevCam.py, g_prevCam.pz,
			g_prevCam.tx, g_prevCam.ty, g_prevCam.tz,
			camera.pos.x, camera.pos.y, camera.pos.z,
			camera.target.x, camera.target.y, camera.target.z, CAM_SNAP))
		{
			cut = true;

			if (gLogEnabled)
				g_cutAim++;
		}

		return cut;
	}

	void CaptureVec(InterpVec* p, const PHD_VECTOR* v, bool alive)
	{
		p->x = v->x;
		p->y = v->y;
		p->z = v->z;
		p->valid = (unsigned char)(alive ? 1 : 0);
	}

	void ApplyVec(PHD_VECTOR* v, const InterpVec* p, SavedVec* save, float alpha)
	{
		save->x = v->x;
		save->y = v->y;
		save->z = v->z;

		v->x = LerpLong(p->x, save->x, alpha);
		v->y = LerpLong(p->y, save->y, alpha);
		v->z = LerpLong(p->z, save->z, alpha);
	}

	void RestoreVec(PHD_VECTOR* v, const SavedVec* save)
	{
		v->x = save->x;
		v->y = save->y;
		v->z = save->z;
	}

	/* Rebuilds the view matrix from where the camera stands now. The
	 * world-to-view matrix, CamPos, CamRot and the device's view transform
	 * are built by phd_LookAt once per logic tick, so moving camera.pos alone
	 * changes nothing. It is called with the blended eye, and again in
	 * InterpEndFrame with the live eye, which restores those globals exactly.
	 *
	 * phd_GenerateW2V (called by phd_LookAt) steps game state:
	 *
	 *     lara.poisoned += (lara.dpoisoned - lara.poisoned) >> 4;
	 *
	 * Two extra calls per frame would ramp the poison four times as fast, so
	 * it is saved and restored around them; the ramp steps once per tick in
	 * CalculateCamera. specific\output.cpp guards the load screen's fade loop
	 * the same way. The tween frame draws the sway of the tick's own level. */
	void RebuildView(long px, long py, long pz, long tx, long ty, long tz, short roll)
	{
		const short poisoned = lara.poisoned;

		phd_LookAt(px, py, pz, tx, ty, tz, roll);

		lara.poisoned = poisoned;
	}

	/* INTERPLOG, once per second. Fields beyond the snap and camera counts:
	 *
	 *   halfturn     rotations that turned exactly half a turn in one tick and
	 *                kept the live value (InterpHalfTurn): turn180_effect,
	 *                once per roll. A large number means something turns half
	 *                a turn that should not.
	 *   handover     camera type changes inside the smooth family (chase,
	 *                look, combat) not taken for a cut; `type=` counts only
	 *                changes that leave the family.
	 *   pose_items   items whose pose was blended, per drawn frame; zero at
	 *                fps_mode 0.
	 *   pose_skips   blends the neighbour rule refused (animation changed or
	 *                frame did not step by one), and Lara's tweens across a far
	 *                key pair held on its first key (InterpKeyHold: the
	 *                tightrope turn). A few per second is normal; near
	 *                pose_items means snapshot and animation clock are out of
	 *                step.
	 *   pose_far     rate 1 pairs refused because a node turns more than 90
	 *                degrees (InterpPoseFar); in ANDREA1 one animation of 319
	 *                (205, the hips), so a big number is an unexamined one.
	 *   pose_hd      of those blended, the ones drawn by the HD object path
	 *                (HDObjectNode's interpolated branch).
	 *   lara_pose    tween frames on which Lara's own pose was blended, over
	 *                the tween count; one fewer per animation change.
	 *   root_fix     refused frames whose root offset the narrowed blend
	 *                (InterpRootNarrow) actually moved, a count over the
	 *                second. A small subset of pose_skips: about one per climb
	 *                cycle on a ladder, zero in a still room.
	 *   arms         blended / refused / tween frames for the weapon arms. With
	 *                a weapon drawn the first should equal the tween count and
	 *                the second be one per weapon animation change; empty
	 *                hands, a flare or a torch count nothing (the arms then
	 *                come from her own key frames). Counted where the weapon
	 *                is known (delstuff.cpp, after `gun`).
	 *   stretch      the worst bone length the pose blend left this second,
	 *                with node, animation and frame (cos(D/2) for a bone
	 *                turning D). 1.00 is rigid; below about 0.85 a limb is
	 *                visibly out of place.
	 *   particles    particles drawn between two ticks, per drawn frame:
	 *                hundreds in rain, zero in a still room and at fps_mode 0.
	 *   particle_kinds  the kinds, one bit each in InterpParticleKind order:
	 *                0001 sparks, 0002 fire, 0004 smoke, 0008 blood, 0010
	 *                drips, 0020 debris, 0040 rain, 0080 snow, 0100 underwater
	 *                dust, 0200 shockwaves, 0400 shells, 0800 swarms, 1000
	 *                bubbles, 2000 ropes. A kind on screen but missing here is
	 *                a draw site whose guard never fires.
	 *   root_flip    of root_fix, the frames whose previous offset was turned
	 *                by a half turn first: the item turned between the two
	 *                ticks and the tween was refused (the roll 147 fires
	 *                turn180_effect on its first frame; the underwater roll
	 *                205 and the tightrope turn 451 on a key frame made for
	 *                the new yaw, whose tween the far-pair rules refuse), or
	 *                the previous tick drew a frame turned by half a turn (the
	 *                change after the last frame of 48, 207, 210 and 212,
	 *                INTERP_TURN_EARLY). At most one per roll, turn or twist,
	 *                and only where the blend moves the root. halfturn without
	 *                root_flip is a turn whose tween blends: the tick an early
	 *                turn180_effect fires on, whose frame is drawn turned
	 *                (InterpKeyFrameTurn). Lara's `root` line prints the turned
	 *                offset as `turned=`.
	 *   pose_back    tween frames on which Lara's pose was blended by the
	 *                backwards rule (InterpPoseBack), the ledge hang's lowering
	 *                played down the pull-up; counted in lara_pose as well.
	 *                One per drawn in-between frame, so a lowering counts its
	 *                25 ticks times the tween frames per tick: 25 at sixty
	 *                hertz, about 137 at 165 (fps_mode 2, where every frame
	 *                is an in-between one); zero everywhere else and zero at
	 *                fps_mode 0. */
	void LogSecond()
	{
		if (gLogEnabled && g_secFrames > 0)
			Log("INTERPLOG level=%d at=%ds frames=%d tween=%d "
				"alpha=%.2f/%.2f/%.2f "
				"lara_step=%.1f items=%.1f snaps=%d (teleport=%d roomjump=%d "
				"object=%d invalid=%d) halfturn=%d camcut=%d (type=%d spot=%d eye=%d aim=%d handover=%d) "
				"cutscene=%d "
				"pose_items=%.1f pose_skips=%.1f pose_far=%.1f pose_hd=%.1f lara_pose=%d/%d "
				"root_fix=%d "
				"arms=%d/%d/%d stretch=%.2f@node%d(anim%d f%d) "
				"particles=%.1f particle_kinds=%04lx root_flip=%d pose_back=%d",
				(int)gfLevelId, g_at, g_secFrames, g_secTween,
				(double)(g_secAlphaMin <= g_secAlphaMax ? g_secAlphaMin : 0.0F),
				g_secAlphaSum / g_secFrames,
				(double)g_secAlphaMax,
				g_secLaraFrames ? g_secLaraStep / g_secLaraFrames : 0.0,
				(double)g_secItems / g_secFrames,
				g_snapTeleport + g_snapRoomJump + g_snapObject + g_snapInvalid,
				g_snapTeleport, g_snapRoomJump, g_snapObject, g_snapInvalid,
				g_snapHalfTurn, g_snapCamCut, g_cutType, g_cutSpot, g_cutEye, g_cutAim,
				g_cutHandover,
				g_snapCutscene,
				(double)g_secPose / g_secFrames,
				(double)g_secPoseSkip / g_secFrames,
				(double)g_secPoseFar / g_secFrames,
				(double)g_secPoseHD / g_secFrames,
				g_secLaraPose, g_secTween,
				g_secRootFix,
				g_secArm, g_secArmSkip, g_secTween,
				(double)g_secStretch / 100.0, g_secStretchNode,
				g_secStretchAnim, g_secStretchFrame,
				(double)g_secParticles / g_secFrames, g_secPartKinds,
				g_secRootFlip, g_secPoseBack);

		/* TICKLOG: how often each tween-guarded counter stepped in the logic
		 * second that just ended (GlobalCounter, thirty in both modes). A 30
		 * and a 60 fps run of the same scene must print the same line: a
		 * number that doubles is an unguarded counter, one that halves is a
		 * guard that also stopped a tick frame. -log only. */
		if (gLogEnabled && g_secFrames > 0)
			Log("TICKLOG level=%d at=%ds fade=%d screenfade=%d weather=%d "
				"legend=%d after_death=%d",
				(int)gfLevelId, g_at,
				g_step[INTERP_STEP_FADE], g_step[INTERP_STEP_SCREENFADE],
				g_step[INTERP_STEP_WEATHER], g_step[INTERP_STEP_LEGEND],
				g_step[INTERP_STEP_AFTERDEATH]);

		for (int k = 0; k < INTERP_STEP_COUNT; k++)
			g_step[k] = 0;

		g_at++;
		g_secFrames = 0;
		g_secTween = 0;
		g_secAlphaSum = 0;
		g_secAlphaMin = 1.0F;
		g_secAlphaMax = 0.0F;
		g_secItems = 0;
		g_secLaraStep = 0;
		g_secLaraFrames = 0;
		g_snapTeleport = 0;
		g_snapRoomJump = 0;
		g_snapObject = 0;
		g_snapInvalid = 0;
		g_snapHalfTurn = 0;
		g_snapCamCut = 0;
		g_cutType = 0;
		g_cutHandover = 0;
		g_cutSpot = 0;
		g_cutEye = 0;
		g_cutAim = 0;
		g_snapCutscene = 0;
		g_secPose = 0;
		g_secPoseSkip = 0;
		g_secPoseFar = 0;
		g_secPoseBack = 0;
		g_secPoseHD = 0;
		g_secLaraPose = 0;
		g_secRootFix = 0;
		g_secRootLines = 0;
		g_secRootFlip = 0;
		g_secArm = 0;
		g_secArmSkip = 0;
		g_secStretch = 100;
		g_secStretchNode = -1;
		g_secStretchAnim = -1;
		g_secStretchFrame = -1;
		g_secParticles = 0;
		g_secPartKinds = 0;
		g_secCounter = GlobalCounter;
	}

	/* Lara's movement per drawn frame (`lara_step=`), measured on the drawn
	 * position, not the logic one: over the same walk a 60 fps run reads half
	 * of a 30 fps run, which shows the interpolation is really happening. */
	void NoteDrawnLara()
	{
		if (!lara_item)
		{
			g_lastDrawnValid = false;
			return;
		}

		if (g_lastDrawnValid)
		{
			const double step = (double)Manhattan(lara_item->pos.x_pos - g_lastDrawnX,
				lara_item->pos.y_pos - g_lastDrawnY,
				lara_item->pos.z_pos - g_lastDrawnZ);

			// A teleport would swamp the mean of the whole second.
			if (step <= ITEM_SNAP)
			{
				g_secLaraStep += step;
				g_secLaraFrames++;
			}
		}

		g_lastDrawnX = lara_item->pos.x_pos;
		g_lastDrawnY = lara_item->pos.y_pos;
		g_lastDrawnZ = lara_item->pos.z_pos;
		g_lastDrawnValid = true;
	}
}

/* The capture runs at every frame rate, the only part of this file active at
 * fps_mode 0. It writes only its own statics (15 KB, thirty times a second),
 * and the frame rate can be changed from the pause screen mid-level, so the
 * first frame after the switch already has a previous state. */
void InterpCapturePrev()
{
	// The logic tick counter: one step per tick at every frame rate, written
	// only here.
	g_tickSerial++;

	if (items)
	{
		for (int i = 0; i < MAX_ITEMS; i++)
			CaptureNode(&g_prevItem[i], &items[i].pos, items[i].room_number,
				items[i].object_number, items[i].anim_number, items[i].frame_number);
	}

	if (effects)
	{
		for (int i = 0; i < FX_COUNT; i++)
			CaptureNode(&g_prevFx[i], &effects[i].pos, effects[i].room_number,
				effects[i].object_number, 0, effects[i].frame_number);
	}

	/* The braid must blend with the head: HairControl derives every link from
	 * the head bone, and an unblended braid would hang half a step behind
	 * Lara's neck. */
	for (int t = 0; t < HAIR_TAILS; t++)
		for (int l = 0; l < HAIR_LINKS; l++)
			CaptureNode(&g_prevHair[t * HAIR_LINKS + l], &hairs[t][l].pos, 0, 0, 0, 0);

	/* Shells, the three swarms, bubbles and ropes. The 184 mover slots are
	 * walked whether or not they moved; a slot that is not alive gets
	 * `valid = 0` and is drawn live on the next frame, which is what a
	 * just-spawned bat needs. */
	GatherMovers();

	for (int i = 0; i < MOVER_COUNT; i++)
	{
		if (!g_moverPos[i] || !g_moverAlive[i])
		{
			g_prevMover[i].valid = 0;
			continue;
		}

		CaptureNode(&g_prevMover[i], g_moverPos[i], 0, 0, 0, 0);
	}

	for (int i = 0; i < BUBBLE_COUNT; i++)
		CaptureVec(&g_prevBubble[i], &Bubbles[i].pos, Bubbles[i].size != 0);

	/* All eight ropes, with `Active` as the identity test: nRope only grows,
	 * and an inactive rope keeps stale segments from its last use. */
	for (int r = 0; r < ROPE_COUNT; r++)
	{
		const bool active = RopeList[r].Active != 0;

		for (int l = 0; l < ROPE_LINKS; l++)
			CaptureVec(&g_prevRope[r * ROPE_LINKS + l], &RopeList[r].MeshSegment[l], active);
	}

	/* The two arms' own animation frames, read back through InterpArmPrev
	 * (see InterpArmAnim). */
	g_prevArm.base[INTERP_ARM_RIGHT] = lara.right_arm.frame_base;
	g_prevArm.anim[INTERP_ARM_RIGHT] = lara.right_arm.anim_number;
	g_prevArm.frame[INTERP_ARM_RIGHT] = lara.right_arm.frame_number;
	g_prevArm.base[INTERP_ARM_LEFT] = lara.left_arm.frame_base;
	g_prevArm.anim[INTERP_ARM_LEFT] = lara.left_arm.anim_number;
	g_prevArm.frame[INTERP_ARM_LEFT] = lara.left_arm.frame_number;
	g_prevArm.valid = 1;

	/* The nine aim angles (see InterpLaraAim). */
	g_prevAim.head[0] = lara.head_y_rot;
	g_prevAim.head[1] = lara.head_x_rot;
	g_prevAim.head[2] = lara.head_z_rot;
	g_prevAim.torso[0] = lara.torso_y_rot;
	g_prevAim.torso[1] = lara.torso_x_rot;
	g_prevAim.torso[2] = lara.torso_z_rot;
	g_prevAim.right[0] = lara.right_arm.y_rot;
	g_prevAim.right[1] = lara.right_arm.x_rot;
	g_prevAim.right[2] = lara.right_arm.z_rot;
	g_prevAim.left[0] = lara.left_arm.y_rot;
	g_prevAim.left[1] = lara.left_arm.x_rot;
	g_prevAim.left[2] = lara.left_arm.z_rot;
	g_prevAim.valid = 1;

	g_prevCam.px = camera.pos.x;
	g_prevCam.py = camera.pos.y;
	g_prevCam.pz = camera.pos.z;
	g_prevCam.tx = camera.target.x;
	g_prevCam.ty = camera.target.y;
	g_prevCam.tz = camera.target.z;
	g_prevCam.room = camera.pos.room_number;
	g_prevCam.type = (short)camera.type;
	g_prevCam.oldType = (short)camera.old_type;
	g_prevCam.fixed = (short)camera.fixed_camera;
	g_prevCam.spot = (short)bUseSpotCam;
	g_prevCam.roll = CameraRollNow();
	g_prevCam.valid = 1;
}

/* How far this frame stands past the last logic tick, as a clamped fraction
 * of a logic step; the blend factor of fps_mode 2. Built from ControlPhase's
 * accumulator and the clock's remainder; see InterpRefreshAlpha. */
float InterpTickFraction()
{
	return InterpRefreshAlpha(framecount, TIME_SubTick());
}

float InterpAlphaNow()
{
	/* fps_mode 2 (display-bound) is asked first; it alone needs a clock.
	 * ActiveFpsMode is 2 only with a device whose present really waits for
	 * the display, so this branch never runs uncapped. GameFrameMinTicks
	 * decides per frame: a frame not paced by the display (the inventory,
	 * specific\output.cpp) is drawn live, since a factor from a clock pacing
	 * something else would judder. */
	const long mode = ActiveFpsMode();

	if (mode == 2)
		return GameFrameMinTicks() == 0 ? InterpTickFraction() : 1.0F;

	/* fps_mode 0 is the engine as it was. ActiveFpsMode rather than
	 * tomb5.fps_mode, so a display setting that cannot be honoured falls back
	 * to the interpolated 60 Hz cadence rather than duplicated frames. */
	if (mode != 1)
		return 1.0F;

	/* A frame whose present waits two ticks lands on the logic ticks and has
	 * nothing to blend (the inventory's guard, GameFrameMinTicks). Without
	 * this, framecount - an accumulator that survives a level change - could
	 * enter such a loop at -1 and stay there: every frame would read 0.5 and
	 * count as a tween frame, and the guarded counters, DoScreenFade among
	 * them, would stand still (a black screen that swallows input, because
	 * TitleOptions zeroes `input` until DoFade reaches 2). DoTitle also
	 * clears framecount as DoLevel does; both guards are kept. */
	if (GameFrameMinTicks() >= 2)
		return 1.0F;

	/* The cadence without a clock. ControlPhase spends two 1/60 s ticks per
	 * logic step (`for (framecount += nframes; framecount > 0; framecount -=
	 * 2)`), so at one tick per present framecount is -1 when this frame's
	 * tick ran and 0 when it did not. With s(n) the state after tick n, the
	 * drawn states are
	 *
	 *   s(1), (s(1)+s(2))/2, s(2), (s(2)+s(3))/2, s(3), ...
	 *
	 * even half steps with no drift. The cost is half a tick of latency on
	 * every second frame, 8.3 ms on average.
	 *
	 * The frame right before a ControlPhase that runs a tick is always live:
	 *
	 *   framecount 0, frame at 1.0, +1 -> 1, TICK, -2 -> -1
	 *   framecount -1, frame at 0.5, +1 -> 0, no tick
	 *   framecount 0, frame at 1.0, +1 -> 1, TICK, ...
	 *
	 * So no logic tick reads what a tween frame left in lara_matrices and
	 * lara_joint_matrices (delstuff.cpp), from which the first person and
	 * look cameras take the head bone. Only a late frame (two ticks or more)
	 * can put a tick behind a tween frame, leaving the camera's head bone half
	 * a step stale for one frame; nothing in the simulation or the savegame
	 * depends on those matrices, and HairControl builds its own stack from
	 * lara_item->pos (hair.cpp), already restored by InterpEndFrame. */
	return framecount < 0 ? 0.5F : 1.0F;
}

float InterpFrameAlpha()
{
	return g_alpha;
}

/* The room the blended eye stands in (see interpolate.h). The gate is
 * g_applied, not g_tween: a cutscene frame or the first frame after a level
 * start is a tween frame drawn live, and asking GetFloor about the live camera
 * could return a room other than camera.pos.room_number (the camera code
 * walks there over several ticks), changing the 30 Hz picture too.
 *
 * GetFloor (control.cpp) writes nothing: it walks portals from the room it is
 * given and returns the room holding the point through the caller's short,
 * here a copy. */
short InterpEyeRoom(short logicRoom)
{
	if (!g_applied || !room || logicRoom < 0 || logicRoom >= number_rooms)
		return logicRoom;

	short at = logicRoom;

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &at);

	// The walk cannot leave the room array; checked anyway before the value
	// seeds draw_room_list.
	if (at < 0 || at >= number_rooms)
		return logicRoom;

	return at;
}

short InterpPrevCamRoom()
{
	return g_prevCam.valid ? g_prevCam.room : (short)-1;
}

bool InterpIsTweenFrame()
{
	return g_tween;
}

void InterpNoteClassicPresent()
{
	/* A present that waited its full two ticks is a 30 Hz one: the load/save
	 * screen, the pause menu, the death menu, the loading bar, the load
	 * screen's fade loop and the F4 outfit menu. Their counters step once per
	 * present, so the latch must be down. RenderLoadPic loops `do { ...
	 * S_DumpScreen(); } while (DoFade != 2)` and only DoScreenFade sets
	 * DoFade to 2; a latch left from the last game frame would never end it.
	 * The inventory ring and the title present at the frame rate at fps_mode
	 * 1 and 2 and set the latch themselves (InterpNoteMenuFrame,
	 * DrawPhaseGame); at fps_mode 0 they come through here. */
	g_tween = false;
}

/* The ring's own frames (see interpolate.h): the same assignment
 * InterpBeginFrame makes. The ring computes its cadence from its own
 * accumulator (game\inventory_interp.cpp); the latch lives here. */
void InterpNoteMenuFrame(bool tween)
{
	g_tween = tween;
}

unsigned long InterpTickSerial()
{
	return g_tickSerial;
}

void InterpNoteStep(int kind)
{
	if (gLogEnabled && kind >= 0 && kind < INTERP_STEP_COUNT)
		g_step[kind]++;
}

/* The particle blend factor, gated by g_applied: true only on frames that
 * wrote the blended state, so particles show the same instant as the bodies
 * and camera. On every live frame this returns 1 and the draw sites keep
 * vanilla's arithmetic exactly. */
float InterpParticleAlpha()
{
	return g_applied ? g_alpha : 1.0F;
}

void InterpNoteParticles(int kind, int n)
{
	if (!gLogEnabled || n <= 0 || kind < 0 || kind >= INTERP_PART_COUNT)
		return;

	g_secParticles += n;
	g_secPartKinds |= 1UL << kind;
}

void InterpBeginFrame(float alpha)
{
	/* The tween latch, set before every exit. A cutscene frame or one without
	 * previous state is drawn live but still carries no tick, so the latch
	 * reads `alpha`, not what the snapshot did; fps_mode 0 passes 1 and it
	 * stays false. At fps_mode 2 there are several frames per tick, so the
	 * frame carrying the tick claims it from the serial (see g_tickSerial). */
	if (ActiveFpsMode() == 2 && GameFrameMinTicks() == 0)
	{
		const bool carries = g_tickSerial != g_steppedSerial;

		g_steppedSerial = g_tickSerial;
		g_tween = !carries;
	}
	else
		g_tween = (alpha < 1.0F);

	/* The logic RNG is saved: several drawing routines call
	 * GetRandomControl, the simulation's generator. InterpEndFrame restores
	 * it, so the next tick finds the stream where a 30 Hz run leaves it. */
	g_seedSaved = false;

	if (g_tween)
	{
		g_seed = GetRandomControlSeed();
		g_seedSaved = true;
	}

	g_applied = false;
	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_touchedMovers = 0;
	g_touchedBubbles = 0;
	g_touchedRopes = 0;
	g_camTouched = false;
	g_aimTouched = false;

	/* Nothing is pose-blended until this frame's walk allows it slot by slot,
	 * so every early return below leaves InterpPoseTime answering 0. A stale
	 * byte would draw a teleported enemy with half its old pose. */
	memset(g_poseOk, 0, sizeof(g_poseOk));
	g_laraPosed = false;
	g_laraRooted = false;
	g_armBlended = false;
	g_armRefused = false;

	/* Frames drawn live, writing nothing:
	 *
	 *   alpha >= 1   fps_mode 0, or the frame that lands on a logic tick.
	 *   !items       before a level is loaded. The title loads a level with a
	 *                Lara item (DoTitle writes `lara_item->mesh_bits = 0`),
	 *                so its frames are blended.
	 *   !valid       the first frame after a level start, a savegame or a
	 *                menu: no previous state.
	 *   cutscene     the cutscene camera is decoded from a frame index
	 *                (game\deltapak.cpp) and frigup_lara moves Lara inside
	 *                DrawPhaseGame; a restore would undo it. Cutscenes run at
	 *                30 Hz and are counted in the log. */
	if (alpha >= 1.0F || !items || !g_prevCam.valid)
	{
		if (gLogEnabled)
		{
			NoteFrameAlpha(alpha);
			NoteDrawnLara();
		}

		return;
	}

	if (GLOBAL_playing_cutseq)
	{
		if (gLogEnabled)
		{
			NoteFrameAlpha(alpha);
			g_snapCutscene++;
			NoteDrawnLara();
		}

		return;
	}

	/* A camera cut is a cut of the whole picture: tested before anything is
	 * blended, because a live camera over a half-step-back world makes the
	 * level jump against Lara on every cut frame. The frame is drawn live,
	 * one 30 Hz frame at the moment the picture jumps anyway; the tween latch
	 * set above still stops the counters. */
	if (CameraCut())
	{
		if (gLogEnabled)
		{
			NoteFrameAlpha(alpha);
			g_snapCamCut++;
			NoteDrawnLara();
		}

		return;
	}

	g_applied = true;
	g_alpha = alpha;

	for (int i = 0; i < MAX_ITEMS; i++)
	{
		ITEM_INFO* item = &items[i];
		const InterpNode* n = &g_prevItem[i];
		const SnapWhy why = NodeSnap(n, &item->pos, item->room_number, item->object_number);

		if (why != SNAP_NONE)
		{
			if (gLogEnabled)
				CountSnap(why);

			continue;
		}

		/* Set before ApplyNode overwrites the position. The slot has a
		 * previous state, the same object and no jump, so its pose may blend;
		 * GetFramesAt still applies the neighbour rule. An item that did not
		 * move gets it too: a machine animating on the spot has only its pose to blend. */
		g_poseOk[i] = 1;

		/* ApplyNode's own half-turn test, so the two agree: a rotation that
		 * keeps its live value leaves the previous root offset half a turn
		 * behind. */
		g_rootFlip[i] = (unsigned char)InterpHalfTurn(n->yr, item->pos.y_rot);

		/* The tick's position step, before ApplyNode halves it. Above the
		 * NodeMoved exit, so a slot that did not move has a step of zero. */
		RootStep(n, &item->pos, g_rootStep[i]);

		if (!NodeMoved(n, &item->pos))
			continue;

		ApplyNode(&item->pos, n, &g_savedItem[g_touchedItems], alpha);
		g_touchedItem[g_touchedItems++] = (short)i;
	}

	if (effects)
	{
		for (int i = 0; i < FX_COUNT; i++)
		{
			FX_INFO* fx = &effects[i];
			const InterpNode* n = &g_prevFx[i];

			if (NodeSnap(n, &fx->pos, fx->room_number, fx->object_number) != SNAP_NONE ||
				!NodeMoved(n, &fx->pos))
				continue;

			ApplyNode(&fx->pos, n, &g_savedFx[g_touchedFxs], alpha);
			g_touchedFx[g_touchedFxs++] = (short)i;
		}
	}

	for (int t = 0; t < HAIR_TAILS; t++)
	{
		for (int l = 0; l < HAIR_LINKS; l++)
		{
			const int k = t * HAIR_LINKS + l;
			const InterpNode* n = &g_prevHair[k];

			if (NodeSnap(n, &hairs[t][l].pos, 0, 0) != SNAP_NONE ||
				!NodeMoved(n, &hairs[t][l].pos))
				continue;

			ApplyNode(&hairs[t][l].pos, n, &g_savedHair[g_touchedHairs], alpha);
			g_touchedHair[g_touchedHairs++] = (short)k;
		}
	}

	/* Shells, swarms, bubbles and ropes: the items[] walk, except that the
	 * identity test is "alive on the last tick" and there is no pose. */
	{
		int shells = 0;
		int swarm = 0;

		GatherMovers();

		for (int i = 0; i < MOVER_COUNT; i++)
		{
			const InterpNode* p = &g_prevMover[i];
			PHD_3DPOS* pos = g_moverPos[i];

			if (!pos || !g_moverAlive[i] || !p->valid)
				continue;

			if (Manhattan(pos->x_pos - p->x, pos->y_pos - p->y, pos->z_pos - p->z) > ITEM_SNAP)
				continue;

			if (!NodeMoved(p, pos))
				continue;

			ApplyNode(pos, p, &g_savedMover[g_touchedMovers], alpha);
			g_touchedMover[g_touchedMovers++] = (short)i;

			// The first SHELL_COUNT entries of the list are the shells and
			// the rest are the three swarms - see GatherMovers.
			if (i < SHELL_COUNT)
				shells++;
			else
				swarm++;
		}

		InterpNoteParticles(INTERP_PART_SHELL, shells);
		InterpNoteParticles(INTERP_PART_SWARM, swarm);
	}

	{
		int moved = 0;

		for (int i = 0; i < BUBBLE_COUNT; i++)
		{
			const InterpVec* p = &g_prevBubble[i];
			PHD_VECTOR* v = &Bubbles[i].pos;

			if (!Bubbles[i].size || VecSnap(p, v, ITEM_SNAP) || !VecMoved(p, v))
				continue;

			ApplyVec(v, p, &g_savedBubble[g_touchedBubbles], alpha);
			g_touchedBubble[g_touchedBubbles++] = (short)i;
			moved++;
		}

		InterpNoteParticles(INTERP_PART_BUBBLE, moved);
	}

	{
		int moved = 0;

		for (int r = 0; r < ROPE_COUNT; r++)
		{
			if (!RopeList[r].Active)
				continue;

			for (int l = 0; l < ROPE_LINKS; l++)
			{
				const int k = r * ROPE_LINKS + l;
				const InterpVec* p = &g_prevRope[k];
				PHD_VECTOR* v = &RopeList[r].MeshSegment[l];

				if (VecSnap(p, v, ROPE_SNAP) || !VecMoved(p, v))
					continue;

				ApplyVec(v, p, &g_savedRope[g_touchedRopes], alpha);
				g_touchedRope[g_touchedRopes++] = (short)k;
				moved++;
			}
		}

		InterpNoteParticles(INTERP_PART_ROPE, moved);
	}

	/* Lara's nine aim angles, blended and restored like a position whenever
	 * there is a previous state. No snap rule: angles blend the short way
	 * round, and the largest jump in one tick is a fast head turn, whose
	 * halfway point is what a turn looks like. */
	if (g_prevAim.valid)
	{
		g_savedAim.head[0] = lara.head_y_rot;
		g_savedAim.head[1] = lara.head_x_rot;
		g_savedAim.head[2] = lara.head_z_rot;
		g_savedAim.torso[0] = lara.torso_y_rot;
		g_savedAim.torso[1] = lara.torso_x_rot;
		g_savedAim.torso[2] = lara.torso_z_rot;
		g_savedAim.right[0] = lara.right_arm.y_rot;
		g_savedAim.right[1] = lara.right_arm.x_rot;
		g_savedAim.right[2] = lara.right_arm.z_rot;
		g_savedAim.left[0] = lara.left_arm.y_rot;
		g_savedAim.left[1] = lara.left_arm.x_rot;
		g_savedAim.left[2] = lara.left_arm.z_rot;
		g_aimTouched = true;

		lara.head_y_rot = LerpAngle(g_prevAim.head[0], g_savedAim.head[0], alpha);
		lara.head_x_rot = LerpAngle(g_prevAim.head[1], g_savedAim.head[1], alpha);
		lara.head_z_rot = LerpAngle(g_prevAim.head[2], g_savedAim.head[2], alpha);
		lara.torso_y_rot = LerpAngle(g_prevAim.torso[0], g_savedAim.torso[0], alpha);
		lara.torso_x_rot = LerpAngle(g_prevAim.torso[1], g_savedAim.torso[1], alpha);
		lara.torso_z_rot = LerpAngle(g_prevAim.torso[2], g_savedAim.torso[2], alpha);
		lara.right_arm.y_rot = LerpAngle(g_prevAim.right[0], g_savedAim.right[0], alpha);
		lara.right_arm.x_rot = LerpAngle(g_prevAim.right[1], g_savedAim.right[1], alpha);
		lara.right_arm.z_rot = LerpAngle(g_prevAim.right[2], g_savedAim.right[2], alpha);
		lara.left_arm.y_rot = LerpAngle(g_prevAim.left[0], g_savedAim.left[0], alpha);
		lara.left_arm.x_rot = LerpAngle(g_prevAim.left[1], g_savedAim.left[1], alpha);
		lara.left_arm.z_rot = LerpAngle(g_prevAim.left[2], g_savedAim.left[2], alpha);
	}

	/* The camera. A cutting camera returned at the top, so the blend here is
	 * unconditional. camera.pos.room_number is not interpolated: it is the
	 * simulation's camera room and is read outside the draw. The draw's
	 * portal walk is seeded from InterpEyeRoom instead, because a walk from
	 * the logic room projected from the blended eye can drop a neighbouring
	 * room for one frame (see interpolate.h). */
	{
		const short roll = CameraRollNow();

		g_savedCam.px = camera.pos.x;
		g_savedCam.py = camera.pos.y;
		g_savedCam.pz = camera.pos.z;
		g_savedCam.tx = camera.target.x;
		g_savedCam.ty = camera.target.y;
		g_savedCam.tz = camera.target.z;
		g_savedCam.roll = roll;
		g_camTouched = true;

		camera.pos.x = LerpLong(g_prevCam.px, g_savedCam.px, alpha);
		camera.pos.y = LerpLong(g_prevCam.py, g_savedCam.py, alpha);
		camera.pos.z = LerpLong(g_prevCam.pz, g_savedCam.pz, alpha);
		camera.target.x = LerpLong(g_prevCam.tx, g_savedCam.tx, alpha);
		camera.target.y = LerpLong(g_prevCam.ty, g_savedCam.ty, alpha);
		camera.target.z = LerpLong(g_prevCam.tz, g_savedCam.tz, alpha);

		RebuildView(camera.pos.x, camera.pos.y, camera.pos.z,
			camera.target.x, camera.target.y, camera.target.z,
			LerpAngle(g_prevCam.roll, roll, alpha));
	}

	if (gLogEnabled)
	{
		NoteFrameAlpha(alpha);
		g_secTween++;
		g_secItems += g_touchedItems;
		NoteDrawnLara();
	}
}

void InterpEndFrame()
{
	if (g_applied)
	{
		for (int k = 0; k < g_touchedItems; k++)
			RestoreNode(&items[g_touchedItem[k]].pos, &g_savedItem[k]);

		for (int k = 0; k < g_touchedFxs; k++)
			RestoreNode(&effects[g_touchedFx[k]].pos, &g_savedFx[k]);

		for (int k = 0; k < g_touchedHairs; k++)
		{
			const int j = g_touchedHair[k];

			RestoreNode(&hairs[j / HAIR_LINKS][j % HAIR_LINKS].pos, &g_savedHair[k]);
		}

		/* The pointers GatherMovers left when the frame began, not
		 * re-gathered, so each value goes back where it came from. */
		for (int k = 0; k < g_touchedMovers; k++)
			RestoreNode(g_moverPos[g_touchedMover[k]], &g_savedMover[k]);

		for (int k = 0; k < g_touchedBubbles; k++)
			RestoreVec(&Bubbles[g_touchedBubble[k]].pos, &g_savedBubble[k]);

		for (int k = 0; k < g_touchedRopes; k++)
		{
			const int j = g_touchedRope[k];

			RestoreVec(&RopeList[j / ROPE_LINKS].MeshSegment[j % ROPE_LINKS], &g_savedRope[k]);
		}

		if (g_aimTouched)
		{
			lara.head_y_rot = g_savedAim.head[0];
			lara.head_x_rot = g_savedAim.head[1];
			lara.head_z_rot = g_savedAim.head[2];
			lara.torso_y_rot = g_savedAim.torso[0];
			lara.torso_x_rot = g_savedAim.torso[1];
			lara.torso_z_rot = g_savedAim.torso[2];
			lara.right_arm.y_rot = g_savedAim.right[0];
			lara.right_arm.x_rot = g_savedAim.right[1];
			lara.right_arm.z_rot = g_savedAim.right[2];
			lara.left_arm.y_rot = g_savedAim.left[0];
			lara.left_arm.x_rot = g_savedAim.left[1];
			lara.left_arm.z_rot = g_savedAim.left[2];
		}

		if (g_camTouched)
		{
			camera.pos.x = g_savedCam.px;
			camera.pos.y = g_savedCam.py;
			camera.pos.z = g_savedCam.pz;
			camera.target.x = g_savedCam.tx;
			camera.target.y = g_savedCam.ty;
			camera.target.z = g_savedCam.tz;

			/* And the view matrix. Every second ControlPhase runs no tick
			 * and no camera routine, so the tween frame's view would carry
			 * into the next, live frame and put the world half a step ahead
			 * of the camera. phd_LookAt with the saved eye rebuilds
			 * w2v_matrix, the matrix stack's base, CamPos, CamRot,
			 * D3DMView and the device's view transform from the tick's own
			 * inputs, exactly. */
			RebuildView(g_savedCam.px, g_savedCam.py, g_savedCam.pz,
				g_savedCam.tx, g_savedCam.ty, g_savedCam.tz, g_savedCam.roll);
		}

		g_applied = false;
	}

	/* The logic RNG goes back whether or not the snapshot was applied: a
	 * cutscene frame draws live and frigup_lara still rolls Lara's speech
	 * head from GetRandomControl. The next tick reads the stream as a 30 Hz
	 * run leaves it. */
	if (g_seedSaved)
	{
		SeedRandomControl(g_seed);
		g_seedSaved = false;
	}

	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_touchedMovers = 0;
	g_touchedBubbles = 0;
	g_touchedRopes = 0;
	g_camTouched = false;
	g_aimTouched = false;

	/* One INTERPLOG line per logic second: GlobalCounter steps thirty times
	 * a second in every mode and stands still in the inventory, so lines
	 * compare directly between 30 and 60 fps runs. */
	if (gLogEnabled && (unsigned short)(GlobalCounter - g_secCounter) >= 30)
		LogSecond();
}

void InterpInvalidate(const char* why)
{
	for (int i = 0; i < MAX_ITEMS; i++)
		g_prevItem[i].valid = 0;

	for (int i = 0; i < FX_COUNT; i++)
		g_prevFx[i].valid = 0;

	for (int i = 0; i < HAIR_TAILS * HAIR_LINKS; i++)
		g_prevHair[i].valid = 0;

	// Shells, swarms, bubbles and ropes too; Rats, Bats and Spiders are freed
	// with the level, so their previous state would describe other slots.
	for (int i = 0; i < MOVER_COUNT; i++)
		g_prevMover[i].valid = 0;

	for (int i = 0; i < BUBBLE_COUNT; i++)
		g_prevBubble[i].valid = 0;

	for (int i = 0; i < ROPE_COUNT * ROPE_LINKS; i++)
		g_prevRope[i].valid = 0;

	g_prevCam.valid = 0;
	g_prevAim.valid = 0;

	/* The arms' snapshot too: g_prevArm.base points into the level's frame
	 * data, which FreeLevel returns to the game_malloc pool and the next level
	 * allocates over, so a stale g_prevArm.base would read the next level's
	 * data as frame data. */
	g_prevArm.valid = 0;
	g_lastDrawnValid = false;

	// No pose has a neighbour any more either.
	memset(g_poseOk, 0, sizeof(g_poseOk));

	/* Where turn180_effect fires is read again from the animation table the
	 * next draw finds: after FreeLevel the numbers name another level's
	 * animations. */
	memset(g_turn, 0, sizeof(g_turn));

	/* No caller runs inside DrawPhaseGame (all are logic ticks or level
	 * boundaries); the reset keeps a future one from leaving interpolated
	 * values in the live state. */
	g_applied = false;
	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_touchedMovers = 0;
	g_touchedBubbles = 0;
	g_touchedRopes = 0;
	g_camTouched = false;
	g_aimTouched = false;

	/* The frame is live again, so no counter is left standing still. */
	g_tween = false;
	g_seedSaved = false;

	/* The tick serial starts level with the ticks: the frame after an
	 * invalidation follows a 30 Hz present that already stepped the counters,
	 * and at worst one step (1/60 s) is dropped rather than repeated. */
	g_steppedSerial = g_tickSerial;

	if (gLogEnabled)
	{
		LogSecond();
		Log("INTERPLOG level=%d invalidated: %s", (int)gfLevelId, why ? why : "?");
		g_at = 0;
	}
}

/* ------------------------------------------------------------------ pose --
 *
 * Whether this frame blends the item's pose, and what it wore before the last
 * tick. Key frames, factors and refusals live in interpolate_math.h and
 * draw.cpp. The g_applied gate rules out every live frame in one test. */
float InterpPoseTime(const ITEM_INFO* item, short* prevAnim, short* prevFrame)
{
	if (!g_applied || !items || !item)
		return 0.0F;

	const long i = (long)(item - items);

	if (i < 0 || i >= MAX_ITEMS || !g_poseOk[i])
		return 0.0F;

	*prevAnim = g_prevItem[i].anim;
	*prevFrame = g_prevItem[i].frame;

	return g_alpha;
}

void InterpNotePose(const ITEM_INFO* item)
{
	if (!gLogEnabled)
		return;

	/* Lara is built more than once per frame (world matrices, joint set, a
	 * mirror room, the first person camera on the tick); her pose is counted
	 * once per frame. */
	if (item == lara_item)
	{
		if (g_laraPosed)
			return;

		g_laraPosed = true;
		g_secLaraPose++;
	}

	g_secPose++;
}

void InterpNotePoseSkip()
{
	if (gLogEnabled)
		g_secPoseSkip++;
}

void InterpNotePoseFar()
{
	if (gLogEnabled)
		g_secPoseFar++;
}

/* Counted once per frame by lara_pose's own latch: GetFramesAt notes this
 * before GetFramesNow notes the pose, so only the first build of her pose in
 * a frame finds g_laraPosed clear. */
void InterpNotePoseBack()
{
	if (gLogEnabled && !g_laraPosed)
		g_secPoseBack++;
}

/* The root rule's side channel (see interpolate.h). GetFramesNow raises the
 * latch on a slot it has just refused and the draw reads it two statements
 * later. InterpBeginFrame clears g_poseOk every drawn frame and every early
 * return leaves it at zero, as does a snapped slot, so a stale byte cannot
 * survive one frame. */
void InterpRootRefused(const ITEM_INFO* item, const long prev[3], bool turned)
{
	if (!g_applied || !items || !item)
		return;

	const long i = (long)(item - items);

	if (i < 0 || i >= MAX_ITEMS || !g_poseOk[i])
		return;

	if (!prev)
	{
		g_poseOk[i] = POSE_OK;
		return;
	}

	g_rootPrev[i][0] = prev[0];
	g_rootPrev[i][1] = prev[1];
	g_rootPrev[i][2] = prev[2];
	g_rootTurned[i] = turned ? 1 : 0;
	g_poseOk[i] = POSE_ROOT;
}

bool InterpRootNow(const ITEM_INFO* item, short* const frm[], long frac, float blend,
	long out[3])
{
	if (!g_applied || !items || !item || !frm || !frm[0])
		return false;

	const long i = (long)(item - items);

	if (i < 0 || i >= MAX_ITEMS || g_poseOk[i] != POSE_ROOT)
		return false;

	/* This tick's offset by vanilla's arithmetic from the pair GetFramesNow
	 * left, then the two ticks blended in time at the frame's factor. The
	 * previous offset is first put into this tick's frame (a copy except
	 * after a half turn, InterpRootFlip), then narrowed to what the position
	 * step pays for (InterpRootNarrow), which weighs this tick's frame. The
	 * half turn is the item's own between the two ticks (g_rootFlip) or the
	 * one the previous tick's frame was drawn with (g_rootTurned, the frame
	 * an early turn180_effect fires on); both at once cancel. */
	long prev[3], cur[3], start[3], blended[3];
	const unsigned char flip = (unsigned char)(g_rootFlip[i] ^ g_rootTurned[i]);

	if (flip)
		InterpRootFlip(g_rootPrev[i], prev);
	else
	{
		prev[0] = g_rootPrev[i][0];
		prev[1] = g_rootPrev[i][1];
		prev[2] = g_rootPrev[i][2];
	}

	InterpRootKey(frm[0], frm[1], frac, blend, cur);
	InterpRootNarrow(prev, cur, g_rootStep[i], start);
	InterpRootLerp(start, cur, g_alpha, blended);

	/* The draw is only told where the blend lands elsewhere. GetFramesNow
	 * raises the latch on every refusal by the neighbour rule, and every item
	 * standing still is refused on every tween frame (about twenty per drawn frame in an
	 * ordinary room). Their prev and cur come from the same (animation,
	 * frame) pair, so they blend to the offset they carry and go back to the
	 * draw site's own float path, unchanged by even one unit. root_fix
	 * counts only the frames this rule really changed. */
	if (blended[0] == cur[0] && blended[1] == cur[1] && blended[2] == cur[2])
		return false;

	out[0] = blended[0];
	out[1] = blended[1];
	out[2] = blended[2];

	if (gLogEnabled)
	{
		/* Lara is built two or three times per drawn frame (view set, joint
		 * set, a mirror) with the same root; counted once per frame, as in
		 * InterpNotePose. */
		if (item == lara_item)
		{
			if (g_laraRooted)
				return true;

			g_laraRooted = true;
		}

		g_secRootFix++;

		if (flip)
			g_secRootFlip++;

		/* INTERPLOG root line, Lara only, at most four a second: the numbers
		 * the rule used, to compare against RootProbe's table from the level
		 * file (they must agree to the unit). pos_y is the blended position,
		 * so pos_y + drawn.y must lie between the two ticks. `flip` and
		 * `turned` are the half-turn test (the item's own turn or the turned
		 * frame the previous tick drew) and the turned previous offset;
		 * `step` is the tick's position step in her frame and `kept` how much
		 * of the offset step it paid for. A transition that does not blend
		 * prints no line. */
		if (item == lara_item && g_secRootLines < 4)
		{
			g_secRootLines++;
			Log("INTERPLOG root lara anim %d->%d frame %d->%d "
				"offset_prev=(%d,%d,%d) offset_cur=(%d,%d,%d) drawn=(%d,%d,%d) pos_y=%d"
				" flip=%d turned=(%d,%d,%d) step=(%d,%d,%d) kept=(%d,%d,%d)",
				(int)g_prevItem[i].anim, (int)item->anim_number,
				(int)g_prevItem[i].frame, (int)item->frame_number,
				(int)g_rootPrev[i][0], (int)g_rootPrev[i][1], (int)g_rootPrev[i][2],
				(int)cur[0], (int)cur[1], (int)cur[2],
				(int)out[0], (int)out[1], (int)out[2],
				(int)item->pos.y_pos,
				(int)flip, (int)prev[0], (int)prev[1], (int)prev[2],
				(int)g_rootStep[i][0], (int)g_rootStep[i][1], (int)g_rootStep[i][2],
				(int)labs(cur[0] - start[0]), (int)labs(cur[1] - start[1]),
				(int)labs(cur[2] - start[2]));
		}
	}

	return true;
}

/* The bones of one drawn pose against their own lengths. The bone table is
 * walked as Rich_CalcLaraMatrices walks it (flag 1 pops, flag 2 pushes, the
 * current node is the parent), and the length between drawn matrices is
 * compared with the table's length, which no animation changes. A bone under
 * a hundredth of a unit has no ratio and is skipped. */
void InterpNoteLaraStretch(const float* matrices, const long* bone, int nodes,
	int animNumber, int frameNumber)
{
	if (!gLogEnabled || !matrices || !bone || nodes < 2)
		return;

	int stack[32];
	int top = 0;
	int cur = 0;

	for (int node = 1; node < nodes && node < 32; node++)
	{
		const long* b = bone + (node - 1) * 4;

		if ((b[0] & 1) && top > 0)
			cur = stack[--top];

		if (b[0] & 2)
		{
			stack[top] = cur;

			if (top < 31)
				++top;
		}

		const double want = sqrt((double)b[1] * (double)b[1] +
			(double)b[2] * (double)b[2] + (double)b[3] * (double)b[3]);

		if (want > 0.01)
		{
			const float* mp = matrices + cur * 12;
			const float* mn = matrices + node * 12;
			const double dx = (double)mn[M03] - (double)mp[M03];
			const double dy = (double)mn[M13] - (double)mp[M13];
			const double dz = (double)mn[M23] - (double)mp[M23];
			const int ratio = (int)(sqrt(dx * dx + dy * dy + dz * dz) /
				want * 100.0 + 0.5);

			if (ratio < g_secStretch)
			{
				g_secStretch = ratio;
				g_secStretchNode = node;
				g_secStretchAnim = animNumber;
				g_secStretchFrame = frameNumber;
			}
		}

		cur = node;
	}
}

/* KEYFARLOG (see interpolate.h):
 *   f       GlobalCounter, the tick
 *   anim, frame   the animation and frame the item stands on
 *   keys    the key pair that frame lies between
 *   rate    the animation's own rate
 *   hips    the turn of node 0 between the two key frames, in degrees
 *   drawn   the key frame drawn in place of the blend; -1 where a key frame
 *           is built between the two instead
 *   mode    hold (the first key, InterpKeyHold) or lerp (a key frame built
 *           at the tick's own weight, InterpKeyFrameLerp) */
void InterpNoteKeyFar(const ITEM_INFO* item, const short* frame0, const short* frame1, bool hold)
{
	static bool seen = false;
	static unsigned short lastCounter = 0;
	static short lastFrame = 0;

	if (!gLogEnabled || !item || !anims || !frame0 || !frame1)
		return;

	if (seen && lastCounter == GlobalCounter && lastFrame == item->frame_number)
		return;

	seen = true;
	lastCounter = GlobalCounter;
	lastFrame = item->frame_number;

	const ANIM_STRUCT* anim = &anims[item->anim_number];
	const long rate = anim->interpolation & 0xFF;
	const long key = rate > 0 ? (item->frame_number - anim->frame_base) / rate : 0;
	const short* a = frame0 + 9;
	const short* b = frame1 + 9;
	short ra[3], rb[3];
	float ma[9], mb[9];

	InterpUnpackRot(a, ra);
	InterpUnpackRot(b, rb);
	InterpPoseRot(ra, ma);
	InterpPoseRot(rb, mb);

	double c = (double)InterpPoseTurnCos(ma, mb);

	if (c > 1.0)
		c = 1.0;
	else if (c < -1.0)
		c = -1.0;

	Log("KEYFARLOG f=%d anim=%d frame=%d keys=%ld-%ld rate=%ld hips=%.1f drawn=%ld mode=%s",
		(int)GlobalCounter, (int)item->anim_number, (int)item->frame_number,
		key, key + 1, rate, acos(c) * 180.0 / 3.14159265358979323846, hold ? key : -1L,
		hold ? "hold" : "lerp");
}

/* TURNLOG (see interpolate.h):
 *   f       GlobalCounter, the tick
 *   anim, frame   the animation and frame the item stands on
 *   flip    the frame turn180_effect fires on
 *   keys    the key pair the drawn frame lies on, both drawn turned */
void InterpNoteTurn(const ITEM_INFO* item, long flip, long pair)
{
	static bool seen = false;
	static unsigned short lastCounter = 0;

	if (!gLogEnabled || !item)
		return;

	if (seen && lastCounter == GlobalCounter)
		return;

	seen = true;
	lastCounter = GlobalCounter;

	Log("TURNLOG f=%d anim=%d frame=%d flip=%ld keys=%ld-%ld drawn=turned",
		(int)GlobalCounter, (int)item->anim_number, (int)item->frame_number,
		flip, pair, pair + 1);
}

/* See interpolate.h. The command walk is AnimateLara's (laramisc.cpp):
 * TranslateItem carries three operands, the jump velocity, a sound and a
 * flip effect two each, everything else none. */
int InterpTurnOf(short animNumber, long* flip, long* pair)
{
	*flip = -1;
	*pair = -1;

	if (!anims || !commands || animNumber < 0)
		return INTERP_TURN_NONE;

	TurnSlot* s = &g_turn[animNumber & 511];

	if (!s->valid || s->anim != animNumber)
	{
		const ANIM_STRUCT* anim = &anims[animNumber];
		const short* cmd = &commands[anim->command_index];
		long f = -1;
		long k = -1;

		for (int n = anim->number_commands; n > 0 && f < 0; n--)
		{
			switch (*cmd++)
			{
			case 1:
				cmd += 3;
				break;

			case 2:
			case 5:
				cmd += 2;
				break;

			case 6:

				if ((cmd[1] & 0x3FFF) == 0)
					f = cmd[0];

				cmd += 2;
				break;
			}
		}

		const int kind = f < 0 ? INTERP_TURN_NONE
			: InterpTurnKind(anim->frame_ptr, anim->interpolation >> 8, anim->frame_base,
				anim->interpolation & 0xFF, f, &k);

		s->anim = animNumber;
		s->flip = (short)f;
		s->pair = (short)k;
		s->kind = (unsigned char)kind;
		s->valid = 1;
	}

	*flip = s->flip;
	*pair = s->pair;

	return s->kind;
}

/* The arm's previous frame (see interpolate.h). Gated by g_applied like the
 * body pose; the rest is the neighbour rule for the arm. A pistol arm counts
 * down as well as up (larafire.cpp draws and holsters through the same
 * table), so steps of -1, 0 and +1 are neighbours. */
bool InterpArmPrev(int which, InterpArmPose* prev, float* alpha)
{
	if (!prev || !alpha || which < 0 || which > 1)
		return false;

	if (!g_applied || !g_prevArm.valid)
		return false;

	const LARA_ARM* arm = which == INTERP_ARM_RIGHT ? &lara.right_arm
		: &lara.left_arm;

	if (g_prevArm.base[which] != arm->frame_base ||
		g_prevArm.anim[which] != arm->anim_number)
		return false;

	const int step = (int)arm->frame_number - (int)g_prevArm.frame[which];

	if (step < -1 || step > 1)
		return false;

	prev->frame_base = g_prevArm.base[which];
	prev->anim_number = g_prevArm.anim[which];
	prev->frame_number = g_prevArm.frame[which];
	*alpha = g_alpha;

	return true;
}

/* Latched per DRAWN frame, because CalcLaraMatrices runs two or three times
 * in one - the view set, the joint set and a mirror - and every one of them
 * asks both arms. */
void InterpNoteArm(bool blended)
{
	if (!gLogEnabled)
		return;

	if (blended)
	{
		if (!g_armBlended)
		{
			g_armBlended = true;
			g_secArm++;
		}
	}
	else if (!g_armRefused)
	{
		g_armRefused = true;
		g_secArmSkip++;
	}
}

/* ARMLOG. See interpolate.h. */
namespace
{
	struct ArmLogState
	{
		long frac;		// what GetFramesNow answered: 0 none, 1 key pair, 2 in time
		float blend;		// the body's factor
		bool still;		// the held first person body
		bool hd;		// HDLaraDrawActive at the time
		int gun;		// -1 until the interpolated build ran for flag 0
		int okR, okL;		// -1 not asked, else InterpArmPrev's answer
		float alphaR, alphaL;
		short prevR, prevL;	// the previous tick's arm frame, -1 when refused
	};

	ArmLogState g_armLog = { 0, 0.0F, false, false, -1, -1, -1, 0.0F, 0.0F, -1, -1 };
	unsigned int g_armLogSerial = 0;
}

void InterpArmLogPath(long frac, float blend, bool still, bool hd)
{
	if (!gLogEnabled)
		return;

	g_armLog.frac = frac;
	g_armLog.blend = blend;
	g_armLog.still = still;
	g_armLog.hd = hd;
	g_armLog.gun = -1;
	g_armLog.okR = -1;
	g_armLog.okL = -1;
	g_armLog.alphaR = 0.0F;
	g_armLog.alphaL = 0.0F;
	g_armLog.prevR = -1;
	g_armLog.prevL = -1;
}

void InterpArmLogNote(int gun, bool okR, bool okL, float alphaR, float alphaL,
	short prevR, short prevL)
{
	if (!gLogEnabled)
		return;

	g_armLog.gun = gun;
	g_armLog.okR = okR ? 1 : 0;
	g_armLog.okL = okL ? 1 : 0;
	g_armLog.alphaR = alphaR;
	g_armLog.alphaL = alphaL;
	g_armLog.prevR = okR ? prevR : -1;
	g_armLog.prevL = okL ? prevL : -1;
}

void InterpArmLogFrame()
{
	if (!gLogEnabled)
		return;

	/* The draw's own test for `gun`: a weapon counts while it is in her
	 * hands or on the way there or back. Empty hands write nothing, so a
	 * run without weapons costs no line. */
	int gun = WEAPON_NONE;

	if (lara.gun_status == LG_READY || lara.gun_status == LG_FLARE ||
		lara.gun_status == LG_DRAW_GUNS || lara.gun_status == LG_UNDRAW_GUNS)
		gun = lara.gun_type;

	if (gun == WEAPON_NONE)
		return;

	const float* m = lara_matrices;
	const int hips = LMX_HIPS * 12;
	const int ur = LMX_UARM_R * 12;
	const int lr = LMX_LARM_R * 12;
	const int hr = LMX_HAND_R * 12;
	const int ul = LMX_UARM_L * 12;
	const int ll = LMX_LARM_L * 12;
	const int hl = LMX_HAND_L * 12;
	int wAnim = -1;
	int wFrame = -1;
	int wState = -1;

	if (lara.weapon_item != NO_ITEM)
	{
		const ITEM_INFO* w = &items[lara.weapon_item];

		wAnim = w->anim_number;
		wFrame = w->frame_number - anims[w->anim_number].frame_base;
		wState = w->current_anim_state;
	}

	Log("ARMLOG f=%u gc=%d tween=%d a=%.3f path=%ld blend=%.3f still=%d hd=%d "
		"lara=%d/%d/%d gun=%d/%d wi=%d/%d/%d "
		"armR=%d/%d/%d armL=%d/%d/%d ok=%d/%d aa=%.3f/%.3f "
		"aimR=%d,%d,%d aimL=%d,%d,%d torso=%d,%d,%d head=%d,%d,%d "
		"hips=(%.0f,%.0f,%.0f) "
		"R=(%.0f,%.0f,%.0f)(%.0f,%.0f,%.0f)(%.0f,%.0f,%.0f) "
		"L=(%.0f,%.0f,%.0f)(%.0f,%.0f,%.0f)(%.0f,%.0f,%.0f) "
		"Rx=(%.3f,%.3f,%.3f)",
		g_armLogSerial++, (int)GlobalCounter, InterpIsTweenFrame() ? 1 : 0,
		InterpFrameAlpha(), g_armLog.frac, g_armLog.blend,
		g_armLog.still ? 1 : 0, g_armLog.hd ? 1 : 0,
		(int)lara_item->anim_number,
		(int)(lara_item->frame_number - anims[lara_item->anim_number].frame_base),
		(int)lara_item->current_anim_state,
		gun, (int)lara.gun_status, wAnim, wFrame, wState,
		(int)lara.right_arm.anim_number, (int)lara.right_arm.frame_number, (int)g_armLog.prevR,
		(int)lara.left_arm.anim_number, (int)lara.left_arm.frame_number, (int)g_armLog.prevL,
		g_armLog.okR, g_armLog.okL, g_armLog.alphaR, g_armLog.alphaL,
		(int)lara.right_arm.y_rot, (int)lara.right_arm.x_rot, (int)lara.right_arm.z_rot,
		(int)lara.left_arm.y_rot, (int)lara.left_arm.x_rot, (int)lara.left_arm.z_rot,
		(int)lara.torso_y_rot, (int)lara.torso_x_rot, (int)lara.torso_z_rot,
		(int)lara.head_y_rot, (int)lara.head_x_rot, (int)lara.head_z_rot,
		m[hips + M03], m[hips + M13], m[hips + M23],
		m[ur + M03], m[ur + M13], m[ur + M23],
		m[lr + M03], m[lr + M13], m[lr + M23],
		m[hr + M03], m[hr + M13], m[hr + M23],
		m[ul + M03], m[ul + M13], m[ul + M23],
		m[ll + M03], m[ll + M13], m[ll + M23],
		m[hl + M03], m[hl + M13], m[hl + M23],
		m[hr + M00], m[hr + M10], m[hr + M20]);
}

void InterpNotePoseHD()
{
	if (gLogEnabled)
		g_secPoseHD++;
}

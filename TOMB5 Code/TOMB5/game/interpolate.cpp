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
#include "lara.h"			// lara.poisoned - the R10 guard
#include "tomb4fx.h"			// Gunshells, Bubbles (#148, order 5)
#include "rat.h"			// Rats
#include "bat.h"			// Bats
#include "spider.h"			// Spiders
#include "rope.h"			// RopeList
#include "../specific/3dmath.h"		// phd_LookAt, CamRoll
#include "../specific/function_stubs.h"	// Log, gLogEnabled
#include "../specific/output.h"		// ActiveFpsMode, GameFrameMinTicks (#148, order 6)
#include "../specific/time.h"		// TIME_SubTick (#148, order 6)
#include "../tomb5/tomb5.h"		// tomb5.fps_mode

namespace
{
	/* HOW MANY EFFECTS THERE ARE. InitialiseFXArray (game\items.cpp) does
	 * `game_malloc(sizeof(FX_INFO) * 24)` and walks 24 slots; the number is
	 * written out there and nowhere else, so it is written out here too
	 * rather than guessed from a pointer that has no length. */
	const int FX_COUNT = 24;

	// hairs[2][7] (game\hair.cpp) - two pigtails of seven points.
	const int HAIR_TAILS = 2;
	const int HAIR_LINKS = 7;

	/* ORDER 5 - THE THINGS THAT MOVE WITHOUT CARRYING A VELOCITY.
	 *
	 * Most particles do not need this file at all: a spark keeps its own
	 * Xvel/Yvel/Zvel and the tick's step is a subtraction away, so it is
	 * interpolated where it is drawn (InterpTrailLong). These six are the
	 * exceptions - what they store is a POSITION and the thing that moved it
	 * is either gone by draw time or was never a velocity in the first place:
	 *
	 *   Gunshells   a shell's fall speed is negated and its position written
	 *               back when it bounces (UpdateGunShells, game\tomb4fx.cpp),
	 *               so the stored speed is NOT the step that was taken on
	 *               the tick a shell hit the floor - which is the one tick
	 *               anybody watches a shell for.
	 *   Rats        their step is `sin(y_rot) * speed`, and both the angle
	 *   Bats        and the speed are rewritten AFTER the position moves.
	 *   Spiders     They are small animals, not particles: pos + speed, the
	 *               same shape as an items[] entry, and the same treatment.
	 *   Bubbles     the drift is a sine of a rotation that has already
	 *               advanced, and the impulse decays after it is spent.
	 *   RopeList    a rope is a constraint solve over 24 links; there is no
	 *               velocity in the drawn segments at all, and Lara HANGS on
	 *               it - if she is interpolated and the rope is not, her
	 *               hands leave it by half a step every second frame. Same
	 *               argument as the braid in order 2, and the same answer.
	 *
	 * Together they are 16.7 KB of statics - 10.1 for the 184 movers with
	 * their pointer table, 1.2 for the bubbles, 5.6 for the ropes - and they
	 * cost their drawing routines not one line: DrawGunshells, DrawRats,
	 * DrawBats, DrawSpiders, DrawBubbles and DrawRope read the same globals
	 * they always read and find the blended values in them, exactly as
	 * items[] does. */
	const int SHELL_COUNT = 24;	// UpdateGunShells / DrawGunshells walk 24
	const int RAT_COUNT = 32;	// UpdateRats walks 32
	const int BAT_COUNT = 64;	// UpdateBats walks 64
	const int SPIDER_COUNT = 64;	// UpdateSpiders walks 64
	const int MOVER_COUNT = SHELL_COUNT + RAT_COUNT + BAT_COUNT + SPIDER_COUNT;

	const int BUBBLE_COUNT = 40;	// UpdateBubbles / DrawBubbles walk 40
	const int ROPE_COUNT = 8;	// RopeList[8] (game\rope.h)
	const int ROPE_LINKS = 24;	// MeshSegment[24], and DrawRope draws 24

	// The thresholds and the three lerps live in interpolate_math.h, which
	// has no engine dependency so that the offline test can include the
	// real thing rather than a copy of it. The reasoning for the numbers is
	// written out there.
	const long ITEM_SNAP = INTERP_ITEM_SNAP;
	const long CAM_SNAP = INTERP_CAM_SNAP;

	/* THE SAME HALF SECTOR, IN A ROPE'S OWN UNITS (#148, order 5). A rope
	 * stores its drawn links as 16.16 fixed point and the draw shifts them
	 * down by sixteen, so the threshold has to be shifted up by sixteen to
	 * mean the same distance. What it is there for is `SetPendulumPoint`
	 * (game\rope.cpp), which moves the whole rope in one tick when Lara
	 * takes hold of it - a jump, not a swing. */
	const long ROPE_SNAP = INTERP_ITEM_SNAP << 16;

	/* WHAT ONE THING LOOKED LIKE BEFORE THE LAST TICK.
	 *
	 * room, object, anim and frame are NOT interpolated. They are here so
	 * that this state can be told from a different thing standing in the
	 * same array slot - an item that was killed and its slot handed to
	 * something else would otherwise be drawn sliding from the dead one's
	 * grave to the new one's spawn - and so that order 4 has the pair of
	 * frames it will need for the pose. */
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

	/* ORDER 5. The same pair for a thing that has a place but no facing - a
	 * bubble and a rope's mesh segment are PHD_VECTORs, and a rope link has
	 * no more of an orientation than a knot in a string does. `valid` carries
	 * the identity test as well as the age: a bubble slot is freed by writing
	 * its size to zero and handed straight to the next bubble, so "was this
	 * slot alive on the last tick" is the whole of what can be known about
	 * whether the thing standing in it now is the same thing. */
	struct InterpVec
	{
		long x, y, z;
		unsigned char valid;
	};

	struct SavedVec
	{
		long x, y, z;
	};

	/* WHAT LARA AIMS WITH, AND WHY IT IS SNAPSHOT AND NOT DRAW CODE (#148,
	 * order 4).
	 *
	 * Her body pose comes out of the animation and is blended by the key
	 * frame pair. Nine angles do NOT come out of the animation - the torso
	 * twist, the head turn and the two arms' aim - and they are the ones the
	 * player is steering: they are written once per logic tick by
	 * LookLeftRight, by the weapon code's aim and by the look camera, and
	 * they are read by both halves of CalcLaraMatrices through phd_RotYXZ.
	 *
	 * So they are exactly order 2's kind of value, not order 4's: the blend
	 * belongs in the snapshot, where it costs eighteen shorts and touches no
	 * draw code at all. The arm's own ANIMATION frame is a different thing
	 * and is not blended - see the note in MODIFICATIONS.md; the arm chain
	 * leaves the second matrix stack behind at the shoulder, so its frames
	 * would have to be blended by the body's factor, which is not theirs.
	 *
	 * The angles wrap and are blended the short way round, like every other
	 * angle in this file. */
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

	/* WHAT THIS COSTS IN MEMORY: 27 KB of statics, all of it below, and not
	 * one byte of it allocated. InterpNode is 28 bytes after alignment, so
	 * the previous state is 15.0 KB; the copy of the live state a frame
	 * overwrote is another 10.7 KB and its index lists 1.1 KB.
	 *
	 * The plan (1.2) estimated 13 KB and had counted only the first of the
	 * three - the copy that has to go back is as large as the snapshot,
	 * because the restore has to be exact rather than approximately right. */
	InterpNode g_prevItem[MAX_ITEMS];
	InterpNode g_prevFx[FX_COUNT];
	InterpNode g_prevHair[HAIR_TAILS * HAIR_LINKS];
	InterpCamera g_prevCam;
	InterpLaraAim g_prevAim;
	InterpLaraAim g_savedAim;
	bool g_aimTouched = false;

	/* ORDER 4. May this slot's POSE be blended in time? Worked out in
	 * InterpBeginFrame's own walk, BEFORE the blended position is written
	 * over the live one - after that the snap test would measure half the
	 * distance it should and a teleport could slip through it. */
	unsigned char g_poseOk[MAX_ITEMS];

	// Lara's pose is built two or three times a frame and counted once.
	bool g_laraPosed = false;

	/* ORDER 4, FIX ROUND U: THE ARM'S OWN ANIMATION FRAME.
	 *
	 * The nine aim angles above come from the player and are blended into the
	 * live structure. These three per arm come from an ANIMATION - the weapon
	 * slot's - and cannot be: the draw needs BOTH the previous tick's frame
	 * and this tick's at the same moment, so what is kept here is read back
	 * through InterpArmPrev instead of being written over anything.
	 *
	 * The pointer is kept as well as the number. lara.?_arm.frame_base is
	 * derived four different ways (larafire.cpp, lara1gun.cpp, lara2gun.cpp,
	 * laraflar.cpp) and comparing it is the cheapest way to be sure the two
	 * frames come out of the same block of animation data. */
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

	/* ORDER 5's SNAPSHOT - the same three arrays as the items above: what
	 * stood there before the tick, a copy of what the frame overwrote, and
	 * the list of what was really touched so that the restore walks only
	 * that. 16.7 KB, and no allocation - which takes this file's whole
	 * footprint to about 44 KB.
	 *
	 * The four simple movers share one index space because they are the same
	 * problem four times over and one list is easier to keep right than four
	 * of them. GatherMovers below is the only place that knows the order. */
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

	// The camera, kept apart because it is one thing and it is restored by
	// rebuilding the view matrix rather than by writing a field back.
	InterpCamera g_savedCam;
	bool g_camTouched = false;

	bool g_applied = false;		// this frame wrote into the live state
	float g_alpha = 1.0F;		// what this frame is being drawn at

	/* ORDER 3. The frame being drawn carries no logic tick. Latched at the
	 * top of the frame and held until the next one - the counters that ask
	 * it sit on both sides of DrawPhaseGame. See the note in the header. */
	bool g_tween = false;

	/* ORDER 6 - HOW THAT LATCH IS SET WHEN THE FRAMES DO NOT COME IN PAIRS.
	 *
	 * At sixty the cadence is two frames per tick and `alpha < 1` tells them
	 * apart by itself. Bound to a 144 Hz panel it is four or five frames per
	 * tick at no fixed phase, and `alpha < 1` would then be true on nearly
	 * every one of them: every counter in the build would stop.
	 *
	 * So the ticks are COUNTED. g_tickSerial steps once per logic tick, in
	 * InterpCapturePrev, which is the first statement of the tick body; a
	 * drawn frame carries a tick when the serial has moved since the last
	 * frame that did, and claims it by writing the serial down. Exactly one
	 * frame per logic tick therefore steps the counters, whatever the refresh
	 * rate is - and a hitch that runs three ticks in one frame steps them
	 * once, which is what thirty hertz does too.
	 *
	 * Only fps_mode 2 uses this. At fps_mode 1 the parity rule stands
	 * untouched, down to WHICH of the two frames carries the step, so that
	 * mode is unchanged to the frame. */
	unsigned long g_tickSerial = 0;
	unsigned long g_steppedSerial = 0;

	// The logic RNG over a tween frame - see the header's note on rand_1.
	long g_seed = 0;
	bool g_seedSaved = false;

	// TICKLOG. What stepped this second, by kind.
	int g_step[INTERP_STEP_COUNT] = { 0 };

	/* THE LOG (INTERPLOG). Counted always - a run at fps_mode 0 is the
	 * comparison the per-frame movement has to halve against, and it cannot
	 * be that if the counters only run at sixty - but written only with
	 * -log, and every counter below is an int being added to. */
	int g_secFrames = 0;
	int g_secTween = 0;		// frames drawn at an alpha other than 1

	/* ORDER 6: THE BLEND FACTOR AS A DISTRIBUTION, not as a formula.
	 *
	 * The line used to work `alpha=` out of the tween count, which was exact
	 * while the only two factors were a half and a one. Bound to the display
	 * every frame has a factor of its own, and what says whether the pacing
	 * is healthy is their SPREAD: at 144 Hz a good second reads roughly
	 * 0.10 / 0.50 / 0.98 - the whole interval walked evenly - while a
	 * maximum pinned at 1.00 with a mean near it is the clamp firing, i.e.
	 * frames arriving faster than the logic has states for. */
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
	int g_snapHalfTurn = 0;	// fix round R: rotations that turned half a turn in one tick and kept the live value
	int g_snapCamCut = 0;
	int g_snapCutscene = 0;

	/* WHY THE CAMERA CUT (#123/#148, fix round R). One counter per reason,
	 * because "camcut=15" says a rule fired fifteen times and not WHICH rule,
	 * and the whole of fix round R is that one of the five was firing for a
	 * reason that was never a cut. A play test that sees a shudder again has
	 * to be able to name the term before anybody touches a threshold. */
	int g_cutType = 0;		// camera.type / old_type / fixed_camera
	int g_cutSpot = 0;		// a flyby beginning or ending
	int g_cutEye = 0;		// the eye jumped further than CAM_SNAP
	int g_cutAim = 0;		// the view DIRECTION jumped (see InterpCamAimCut)
	int g_secPose = 0;		// order 4: items drawn with a blended pose
	int g_secPoseSkip = 0;		// ... and refused one by rule K6
	int g_secPoseFar = 0;		// ... refused because the pair is too far (S2)
	int g_secPoseHD = 0;		// ... of which went through the HD path
	int g_secLaraPose = 0;		// frames on which LARA's own pose was blended
	/* Fix round U (U2): the worst a bone was shortened by the pose blend
	 * this second, in hundredths, with the node, the animation and the frame
	 * it was on. 100 is a rigid pose and is what a second with no tween frame
	 * in it at all reads. */
	int g_secStretch = 100;
	int g_secStretchNode = -1;
	int g_secStretchAnim = -1;
	int g_secStretchFrame = -1;

	int g_secArm = 0;		// fix round U: frames with an arm blended in time
	int g_secArmSkip = 0;		// ... and frames on which the rule refused
	int g_secParticles = 0;		// order 5: particles moved, added over the second
	unsigned long g_secPartKinds = 0;	// ... and which kinds they were, as a mask
	unsigned short g_secCounter = 0;	// GlobalCounter when this second began
	int g_at = 0;			// seconds since the last invalidation
	long g_lastDrawnX = 0, g_lastDrawnY = 0, g_lastDrawnZ = 0;
	bool g_lastDrawnValid = false;

	// Short names for the shared arithmetic, so that the code below reads as
	// what it does rather than as which file it came from.
	inline short LerpAngle(short a, short b, float alpha) { return InterpLerpAngle(a, b, alpha); }
	inline long LerpLong(long a, long b, float alpha) { return InterpLerpLong(a, b, alpha); }
	inline long Manhattan(long dx, long dy, long dz) { return InterpManhattan(dx, dy, dz); }

	/* THE ROLL THE VIEW MUST BE REBUILT WITH.
	 *
	 * CamRoll (specific\3dmath.cpp) is whatever the last phd_LookAt was
	 * handed, and all three cameras that roll go through it. In first person
	 * that value IS fpRoll; it is read through the camera's own getter there
	 * anyway, so that this stays right if the head camera is ever given a
	 * path to the view matrix that does not run through phd_LookAt. */
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

	/* Would blending this node from `n` to `pos` be a lie? Returns 0 to
	 * interpolate, or the reason not to - which is counted, because the
	 * whole point of counting them is that a snap rule firing far more often
	 * than it should is invisible in the picture (it just looks like thirty
	 * hertz) and obvious in the log. */
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

	/* DID IT MOVE AT ALL? Almost nothing does. A level holds up to 512 item
	 * slots and typically a handful of them travel in any one tick - Lara, a
	 * few enemies, a door, a lift; the rest are pickups on the floor,
	 * switches on walls, and the free slots past level_items which hold
	 * nothing and never will. Blending a value into itself is work with no
	 * picture behind it and it would also have to be undone afterwards, so
	 * the whole set is skipped in one comparison.
	 *
	 * It is also what makes the log line's `items=` mean something: not "how
	 * many slots exist" but "how many things really moved this frame". */
	bool NodeMoved(const InterpNode* n, const PHD_3DPOS* pos)
	{
		return n->x != pos->x_pos || n->y != pos->y_pos || n->z != pos->z_pos ||
			n->xr != pos->x_rot || n->yr != pos->y_rot || n->zr != pos->z_rot;
	}

	// One drawn frame, and the factor it was drawn at (order 6). The three
	// ways out of InterpBeginFrame all pass through here so that the
	// distribution counts the live frames too - a second full of snaps reads
	// 1.00/1.00/1.00 and says so.
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

	// Write the halfway state into one live position, remembering what stood
	// there. Never called for a node that snapped as a whole; a rotation that
	// turned half a turn is held here (fix round R).
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

		/* A HALF TURN IS A SNAP (#148, fix round R). turn180_effect turned this
		 * node exactly half a turn between the two ticks and the animation's
		 * own root bone turned the other half; blended apart they do not
		 * cancel, and the body stands sideways for one tween frame - the roll
		 * at 60 fps, seen with HD off as well. The whole rotation keeps the
		 * live value (the effect negates x_rot in the same breath) and only
		 * the position blends. See InterpHalfTurn in interpolate_math.h. */
		if (InterpHalfTurn(n->yr, save->yr))
		{
			g_snapHalfTurn++;
			return;
		}

		pos->x_rot = LerpAngle(n->xr, save->xr, alpha);
		pos->y_rot = LerpAngle(n->yr, save->yr, alpha);
		pos->z_rot = LerpAngle(n->zr, save->zr, alpha);
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

	/* ---------------------------------------------------------- ORDER 5 --
	 *
	 * WHERE THE FOUR SIMPLE MOVERS ARE, in one fixed order. Called from the
	 * capture, from the frame that applies the blend and never again - the
	 * restore uses the pointers this left behind, so that a set which is
	 * there when the frame begins is still the set that gets put back.
	 *
	 * A null pointer is a set the level did not load: Rats, Bats and Spiders
	 * are game_malloc'd by their own initialisers and are simply absent in
	 * most levels. A shell array is static and always there, and a slot with
	 * no shell in it is dealt with by `alive`. */
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

	/* THE SAME THREE STEPS AS A NODE, for a thing that is only a place.
	 *
	 * `snap` is passed in rather than taken from the header because one pair
	 * of functions serves both a bubble, whose coordinates are world units,
	 * and a rope's mesh segment, which is 16.16 fixed point (game\rope.cpp,
	 * ProjectRopePoints shifts it down by 16 to draw it). The threshold is
	 * the only place that difference shows, and one argument is cheaper than
	 * two copies of the same loop. */
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

	/* THE FIVE WAYS THE CAMERA CUTS (#148, order 2; re-measured and moved in
	 * fix round R).
	 *
	 * A cut is a cut on purpose - a fixed camera taking over, a spot cam
	 * stepping to the next node, a flyby beginning or ending, the binoculars
	 * going up - and blending across one would drag the eye through the
	 * level's geometry for half a tick instead of jumping the way the level
	 * designer meant it to.
	 *
	 * camera.type is tested TOGETHER WITH camera.old_type because
	 * CalculateCamera resets type to CHASE_CAMERA at its own end unless a
	 * heavy camera's timer is still running; old_type is what the view was
	 * actually built as, and is the half of the pair that moves when a fixed
	 * camera takes over.
	 *
	 * THE TARGET IS MEASURED AS AN AIM AND NOT AS A PLACE (fix round R). The
	 * whole argument, the numbers and what it is worth in degrees are at
	 * InterpCamAimCut in game\interpolate_math.h; the short of it is that the
	 * first person camera's target lies 20736 units down the line of sight,
	 * where a 2.8 degree turn of her head moved it a whole sector and the old
	 * absolute test called that a cut ten to seventeen times a second while
	 * she stood still.
	 *
	 * It counts its reason on the way past, so that INTERPLOG can say which
	 * of the five fired rather than only how often something did. */
	bool CameraCut()
	{
		bool cut = false;

		if (g_prevCam.type != (short)camera.type ||
			g_prevCam.oldType != (short)camera.old_type ||
			g_prevCam.fixed != (short)camera.fixed_camera)
		{
			cut = true;

			if (gLogEnabled)
				g_cutType++;
		}

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

	/* REBUILD THE VIEW MATRIX FROM WHERE THE CAMERA STANDS NOW.
	 *
	 * The whole world-to-view matrix, CamPos, CamRot and the device's own
	 * view transform are built by phd_LookAt, once per LOGIC tick, at the
	 * end of whichever camera routine ran. Moving camera.pos by hand does
	 * nothing on its own - the picture would not budge. So the interpolator
	 * calls the same function again with the moved eye, and calls it a
	 * second time in InterpEndFrame with the live eye, which puts every one
	 * of those globals back to the bit.
	 *
	 * THE POISON FILTER (risk K7/R10). phd_GenerateW2V, which phd_LookAt
	 * calls, carries a piece of GAME STATE in it:
	 *
	 *     lara.poisoned += (lara.dpoisoned - lara.poisoned) >> 4;
	 *
	 * an exponential ramp that was written when there was exactly one
	 * phd_LookAt per logic tick. Two calls per frame would run it four times
	 * as often and the green would come up four times as fast. It is saved
	 * and put back around both calls, so the ramp still steps exactly once
	 * per tick, in CalculateCamera's own phd_LookAt and nowhere else.
	 *
	 * The engine already knew this hazard: specific\output.cpp does the same
	 * save-and-zero around the load screen's fade loop, for the same reason.
	 *
	 * What the tween frame therefore draws is the shake computed from the
	 * tick's own poison level - one thirtieth of a second stale, which is
	 * what everything else on that frame is too. */
	void RebuildView(long px, long py, long pz, long tx, long ty, long tz, short roll)
	{
		const short poisoned = lara.poisoned;

		phd_LookAt(px, py, pz, tx, ty, tz, roll);

		lara.poisoned = poisoned;
	}

	/* ORDER 4 ADDS FOUR FIELDS TO THIS LINE, and they answer the one question
	 * order 2's fields cannot: is anything's POSE moving on the in-between
	 * frames, or is only the root sliding while every limb still steps thirty
	 * times a second?
	 *
	 *   pose_items   items whose pose was blended, per drawn frame. In a busy
	 *                room at sixty this is most of what `items=` says; at
	 *                fps_mode 0 it is zero, always.
	 *   pose_skips   blends rule K6 refused - the animation changed between
	 *                the two ticks, or the frame did not step by one. A few
	 *                per second is normal and is what an animation change
	 *                looks like; a number near pose_items means the snapshot
	 *                and the animation clock have fallen out of step and
	 *                nothing is really being blended.
	 *   pose_far     blends refused for the other reason (#123, fix round S,
	 *                S2): a RATE 1 pair - two adjacent stored frames, which
	 *                vanilla never blends at all - whose two poses turn more
	 *                than ninety degrees at some node. An element-wise matrix
	 *                blend of a turn that size does not go the long way, it
	 *                SHRINKS, and at half a turn the bone's own frame is
	 *                singular. Measured over ANDREA1 that is one animation of
	 *                319 (number 205, the hips), so this should read a small
	 *                fraction; a big number means an animation nobody has
	 *                looked at yet.
	 *   pose_hd      of those, the ones drawn by the HD object path - the
	 *                frames that exercise HDObjectNode's interpolated branch.
	 *                The plan asks for this number by name.
	 *   lara_pose    frames of this second on which LARA's own pose was
	 *                blended, against the tween frame count beside it. It
	 *                should be nearly all of them while she is moving and it
	 *                drops by one on each frame where she changes animation.
	 *   stretch      fix round U: the worst a BONE was shortened by the pose
	 *                blend this second, over its own length in the skeleton,
	 *                with the node, the animation and the frame it happened
	 *                on. An element-wise matrix blend walks the CHORD of the
	 *                arc, so a bone whose world frame turns D between the two
	 *                key frames comes out cos(D/2) long. 1.00 is a rigid pose
	 *                and is what a second with no tween frame reads. Under
	 *                about 0.85 is a limb one can SEE in the wrong place, and
	 *                the animation and frame beside it are the pair to look
	 *                at.
	 *   arms         fix round U: blended / refused / tween frames. The
	 *                ARM animation is the weapon slot's, and it is the pose
	 *                first person is nearly all made of. With a weapon in her
	 *                hands the first number should be the tween count and the
	 *                second a handful per second - one on each frame the
	 *                weapon animation changes number. With EMPTY HANDS, a
	 *                flare or a torch both stay nought, because the frame is
	 *                not counted at all: the arms then come out of her own key
	 *                frames - and the flare's one driven arm out of a single
	 *                frame - so there is nothing there to blend. The counter
	 *                is asked in the draw where the weapon is known
	 *                (game\delstuff.cpp, after `gun`), and not from the arm
	 *                rule, which answers yes to hands that have simply been
	 *                standing still in a field nobody clears.
	 *
	 * FIX ROUND R ADDS ONE (#148):
	 *
	 *   halfturn     rotations that turned exactly half a turn between the
	 *                two ticks and kept the live value instead of a blend -
	 *                see InterpHalfTurn (game\interpolate_math.h). That is
	 *                turn180_effect firing, once per roll: a run through a
	 *                level reads a handful, a still second reads nought, and
	 *                a large number means something turns half a turn that
	 *                should not.
	 *
	 * AND ORDER 5 ADDS TWO MORE, which are about the small moving things
	 * rather than about the bodies:
	 *
	 *   particles       how many particles were drawn between two ticks, per
	 *                   drawn frame. In rain it is in the hundreds, in a
	 *                   still room it is nought, and at fps_mode 0 it is
	 *                   nought always.
	 *   particle_kinds  which KINDS those were, one bit each, in the order of
	 *                   InterpParticleKind (game\interpolate.h): 0001 sparks,
	 *                   0002 fire, 0004 smoke, 0008 blood, 0010 drips, 0020
	 *                   debris, 0040 rain, 0080 snow, 0100 underwater dust,
	 *                   0200 shockwaves, 0400 shells, 0800 swarms, 1000
	 *                   bubbles, 2000 ropes.
	 *
	 *                   This is the field that finds a draw site whose guard
	 *                   never fires, and there is no other way to see one: a
	 *                   kind that is plainly on screen and missing from this
	 *                   mask simply looks like thirty hertz, which is what it
	 *                   is. Standing in the rain with no 0040 bit is a bug;
	 *                   no 2000 bit in a level with no rope in it is not. */
	void LogSecond()
	{
		if (gLogEnabled && g_secFrames > 0)
			Log("INTERPLOG level=%d at=%ds frames=%d tween=%d "
				"alpha=%.2f/%.2f/%.2f "
				"lara_step=%.1f items=%.1f snaps=%d (teleport=%d roomjump=%d "
				"object=%d invalid=%d) halfturn=%d camcut=%d (type=%d spot=%d eye=%d aim=%d) "
				"cutscene=%d "
				"pose_items=%.1f pose_skips=%.1f pose_far=%.1f pose_hd=%.1f lara_pose=%d/%d "
				"arms=%d/%d/%d stretch=%.2f@node%d(anim%d f%d) "
				"particles=%.1f particle_kinds=%04lx",
				(int)gfLevelId, g_at, g_secFrames, g_secTween,
				(double)(g_secAlphaMin <= g_secAlphaMax ? g_secAlphaMin : 0.0F),
				g_secAlphaSum / g_secFrames,
				(double)g_secAlphaMax,
				g_secLaraFrames ? g_secLaraStep / g_secLaraFrames : 0.0,
				(double)g_secItems / g_secFrames,
				g_snapTeleport + g_snapRoomJump + g_snapObject + g_snapInvalid,
				g_snapTeleport, g_snapRoomJump, g_snapObject, g_snapInvalid,
				g_snapHalfTurn, g_snapCamCut, g_cutType, g_cutSpot, g_cutEye, g_cutAim,
				g_snapCutscene,
				(double)g_secPose / g_secFrames,
				(double)g_secPoseSkip / g_secFrames,
				(double)g_secPoseFar / g_secFrames,
				(double)g_secPoseHD / g_secFrames,
				g_secLaraPose, g_secTween,
				g_secArm, g_secArmSkip, g_secTween,
				(double)g_secStretch / 100.0, g_secStretchNode,
				g_secStretchAnim, g_secStretchFrame,
				(double)g_secParticles / g_secFrames, g_secPartKinds);

		/* TICKLOG - order 3's own proof, and the one the user reads.
		 *
		 * Every counter this order guarded reports how often it stepped in
		 * the second that just ended. The second is a LOGIC second
		 * (GlobalCounter, thirty by construction in both modes), so the
		 * same line has to come out of a thirty hertz run and a sixty hertz
		 * run of the same scene. A number that doubles is a counter that
		 * was missed; a number that halves is a guard that also stopped a
		 * tick frame.
		 *
		 * There is no expected VALUE here to check against - fade steps
		 * only happen while something fades - which is why the line is a
		 * comparison between two runs and not an assertion. Written only
		 * with -log. */
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
		g_cutSpot = 0;
		g_cutEye = 0;
		g_cutAim = 0;
		g_snapCutscene = 0;
		g_secPose = 0;
		g_secPoseSkip = 0;
		g_secPoseFar = 0;
		g_secPoseHD = 0;
		g_secLaraPose = 0;
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

	/* THE MOVEMENT THE PLAYER'S EYE SEES, per drawn frame.
	 *
	 * This is proof 4 of the plan and the one number that says the
	 * interpolation is really happening rather than merely compiling. It is
	 * measured on the position that was DRAWN, not on the one the logic
	 * holds: over the same walk, the same corridor and the same seconds, a
	 * sixty hertz run must read HALF of what a thirty hertz run reads,
	 * because the same distance is being cut into twice as many steps.
	 *
	 * Not a mean of the tick's step: a mean of what changed on screen. That
	 * is the difference between measuring the interpolation and measuring
	 * the game. */
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

			// A teleport would swamp the mean of a whole second, and it is
			// not what this number is asking about.
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

/* THE CAPTURE RUNS AT BOTH FRAME RATES, deliberately, and it is the one part
 * of this file that does anything at fps_mode 0.
 *
 * It writes nothing but its own statics - 15 KB of them, thirty times a
 * second, which is nothing - and guarding it on fps_mode would buy that
 * nothing at the price of the switch. *Frame rate* can be changed from the
 * pause screen in the middle of a level, and the frame right after that
 * change needs a snapshot that is already there. With the capture always
 * running, the switch is instant and clean; without it, the first second at
 * sixty would have no previous state to blend from. */
void InterpCapturePrev()
{
	// The tick counter of order 6. One step per logic tick, at every frame
	// rate, and the only place it is written.
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

	/* THE BRAID IS NOT OPTIONAL (plan 1.2). HairControl derives every link
	 * from the head bone, so the head moving half a step while the braid
	 * stands still is a braid hanging in the air a step behind Lara's neck -
	 * far more visible than the movement the interpolation was for. */
	for (int t = 0; t < HAIR_TAILS; t++)
		for (int l = 0; l < HAIR_LINKS; l++)
			CaptureNode(&g_prevHair[t * HAIR_LINKS + l], &hairs[t][l].pos, 0, 0, 0, 0);

	/* ORDER 5: the shells, the three swarms, the bubbles and the ropes.
	 *
	 * The swarms and the shells are captured whether or not they moved -
	 * they are 184 slots, most of them empty in most levels, and the walk
	 * is cheaper than deciding not to make it. A slot that is not alive gets
	 * `valid = 0` and is therefore drawn live on the next frame, which is
	 * what a shell that has just come to rest and a bat that has just been
	 * spawned both need. */
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

	/* ALL EIGHT ROPES, ALWAYS, and `Active` is the identity test. nRope is
	 * how many the level built and it only ever grows, but a rope that is
	 * not active has stale segments standing in it from the last time it
	 * was - so it is the flag and not the count that says whether the
	 * previous state means anything. */
	for (int r = 0; r < ROPE_COUNT; r++)
	{
		const bool active = RopeList[r].Active != 0;

		for (int l = 0; l < ROPE_LINKS; l++)
			CaptureVec(&g_prevRope[r * ROPE_LINKS + l], &RopeList[r].MeshSegment[l], active);
	}

	/* FIX ROUND U (U3): the two arms' own animation frames. Kept and NOT
	 * written over anything, because the draw needs both this tick's and the
	 * last one's at the same moment - InterpArmPrev hands the older one back.
	 * The pointer is kept beside the number: lara.?_arm.frame_base is derived
	 * four different ways (larafire.cpp, lara1gun.cpp, lara2gun.cpp,
	 * laraflar.cpp) and comparing it is the cheapest way to be sure the two
	 * frames come out of the same block of animation data. */
	g_prevArm.base[INTERP_ARM_RIGHT] = lara.right_arm.frame_base;
	g_prevArm.anim[INTERP_ARM_RIGHT] = lara.right_arm.anim_number;
	g_prevArm.frame[INTERP_ARM_RIGHT] = lara.right_arm.frame_number;
	g_prevArm.base[INTERP_ARM_LEFT] = lara.left_arm.frame_base;
	g_prevArm.anim[INTERP_ARM_LEFT] = lara.left_arm.anim_number;
	g_prevArm.frame[INTERP_ARM_LEFT] = lara.left_arm.frame_number;
	g_prevArm.valid = 1;

	/* ORDER 4: the nine angles Lara aims with, which come from the player
	 * rather than from the animation. See the note over InterpLaraAim. */
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

/* HOW FAR THIS FRAME STANDS PAST THE LAST LOGIC TICK, as a fraction of a
 * logic step (#148, order 6). Zero straight after a tick, one where the next
 * one is due, and clamped at both ends.
 *
 * It is the blend factor of the display-bound mode and it is built out of two
 * numbers that are already there - ControlPhase's accumulator and the
 * remainder in the engine's own clock. The derivation is written out over
 * InterpRefreshAlpha in interpolate_math.h, which is where the arithmetic
 * lives so that InterpTest can check it without a game.
 *
 * Exported because it is the honest answer to "how far between two ticks are
 * we", which is a question the log lines and any later order may ask. At
 * fps_mode 0 and 1 nothing calls it. */
float InterpTickFraction()
{
	return InterpRefreshAlpha(framecount, TIME_SubTick());
}

float InterpAlphaNow()
{
	/* THE DISPLAY-BOUND RATE (order 6), and it is asked first because it is
	 * the only mode that needs a clock.
	 *
	 * ActiveFpsMode is the option AS THE ENGINE CAN HONOUR IT: it is 2 only
	 * with a device whose present really waits for the panel, so this branch
	 * cannot be reached by a build that would then run uncapped.
	 *
	 * GameFrameMinTicks decides per FRAME rather than per mode, and the frame
	 * that is not a display-bound one is drawn live: the title menu and the
	 * inventory keep their two ticks per present at every setting (order 1b),
	 * and a frame drawn at a factor taken from a clock that is pacing
	 * something else would judder the title's own flyby. */
	const long mode = ActiveFpsMode();

	if (mode == 2)
		return GameFrameMinTicks() == 0 ? InterpTickFraction() : 1.0F;

	/* fps_mode 0 IS THE ENGINE AS IT WAS. One test, and every line below
	 * this file's three entry points is dead code for that build.
	 *
	 * It is ActiveFpsMode and not tomb5.fps_mode, so that a "Display" setting
	 * that cannot be honoured falls back to the sixty hertz cadence WITH its
	 * interpolation rather than to a duplicated frame. */
	if (mode != 1)
		return 1.0F;

	/* AND THE FRAME THAT IS NOT A SIXTY HERTZ ONE (#123/#148, fix round R).
	 *
	 * The same test the display-bound branch above has already made, and it
	 * was missing here: the title menu and the inventory keep their TWO
	 * ticks per present at every setting (order 1b, GameFrameMinTicks), so
	 * their frames land on the logic ticks and there is nothing between two
	 * of them to draw.
	 *
	 * WHAT IT COST. framecount is an accumulator that survives a level
	 * change, and the sixty hertz game loop leaves it at 0 or at -1 by turns.
	 * DoLevel has always cleared it (game\gameflow.cpp) and DoTitle never
	 * did, so a title entered on the wrong half of the cadence started with
	 * framecount == -1 - and at two ticks a present the loop then puts it
	 * back to -1 on every single frame. Every frame of the title read 0.5,
	 * every frame was a tween frame, and every counter order 3 guards stood
	 * still for as long as the title was up. One of them is DoScreenFade,
	 * whose nine steps are what sets DoFade to 2: the fade-in from black
	 * never advanced, a full screen black quad was drawn over the menu
	 * forever, and TitleOptions - which zeroes `input` until DoFade reaches 2
	 * - swallowed every key after the first choice. A black screen with no
	 * input, which is what the play test of 2026-09-07 found on the way back
	 * from a level, and only from a level: at program start framecount is 0
	 * and the other half of the coin came up.
	 *
	 * DoTitle now clears framecount as DoLevel does. This line is the rule
	 * that makes the clearing unnecessary, and both are here on purpose - the
	 * parity of an accumulator is not a thing to leave a picture hanging on.
	 *
	 * At fps_mode 0 the return above has already fired and this is dead. */
	if (GameFrameMinTicks() >= 2)
		return 1.0F;

	/* THE CADENCE, WITHOUT A CLOCK OF ITS OWN.
	 *
	 * framecount counts 1/60 s ticks and ControlPhase spends two of them per
	 * logic step (`for (framecount += nframes; framecount > 0; framecount -=
	 * 2)`). At one tick per present the loop therefore leaves it at exactly
	 * one of two values: -1 when this call's tick ran, 0 when it did not.
	 * The drawn states are then
	 *
	 *   T1, (T1+T2)/2, T2, (T2+T3)/2, T3, ...
	 *
	 * - even half steps, forever, with no drift, no QueryPerformanceCounter
	 * and no accumulator to go wrong. At fps_mode 0 two ticks arrive per
	 * present, the loop always ends on 0, and this reads 1 without the test
	 * above ever being needed.
	 *
	 * The cost is half a tick of display latency on every second frame -
	 * 8.3 ms on average - which is what this technique costs everywhere it
	 * is used.
	 *
	 * AND ONE PROPERTY THAT FALLS OUT OF IT AND IS WORTH WRITING DOWN. Walk
	 * the loop - draw, present, logic - and the frame that comes immediately
	 * before a ControlPhase THAT RUNS A TICK is always a live one:
	 *
	 *   framecount 0, frame at 1.0, +1 -> 1, TICK, -2 -> -1
	 *   framecount -1, frame at 0.5, +1 -> 0, no tick
	 *   framecount 0, frame at 1.0, +1 -> 1, TICK, ...
	 *
	 * So no logic tick ever reads what a TWEEN frame left behind. That
	 * matters because the draw phase does leave things behind that the logic
	 * reads back: lara_matrices and lara_joint_matrices (game\delstuff.cpp),
	 * which the first-person and look cameras take the head bone from. At a
	 * steady sixty they see exactly the matrices they would have seen at
	 * thirty.
	 *
	 * The one exception is a LATE frame - if a frame took two ticks or more,
	 * S_DumpScreen returns that number, framecount jumps by it and a tick
	 * can fall behind a tween frame. Then the head bone the camera reads is
	 * half a step stale for that one frame. It is a camera, not game state:
	 * nothing in the savegame and nothing in the simulation hangs on those
	 * matrices, and HairControl - the one piece of logic that could have -
	 * builds its own stack from lara_item->pos instead (game\hair.cpp),
	 * which InterpEndFrame has already put back. */
	return framecount < 0 ? 0.5F : 1.0F;
}

float InterpFrameAlpha()
{
	return g_alpha;
}

/* THE ROOM THE BLENDED EYE STANDS IN (#123, fix round T2, T2-1). The long
 * note is in interpolate.h; what is left to say is here.
 *
 * `g_applied` AND NOT `g_tween` IS THE GATE, and it is the same one the pose
 * and the particles use. g_tween is the CADENCE - it is true on a cutscene
 * frame and on the first frame after a level start as well, and both of those
 * are drawn LIVE, with camera.pos untouched. Asking GetFloor about a live
 * camera would answer with whatever room really holds it, which need not be
 * camera.pos.room_number - the engine's own camera code walks its way there
 * over several ticks - and the picture would change at thirty frames a
 * second too. g_applied is true only where the camera block above really ran,
 * which is the only place the eye and the room can disagree.
 *
 * GetFloor WRITES NOTHING. It walks floor, ceiling and wall portals from the
 * room it is given until it reaches the one that holds the point, and returns
 * that room through a caller's own short (game\control.cpp:1228). The one it
 * is given here is a copy; camera.pos.room_number never sees it. */
short InterpEyeRoom(short logicRoom)
{
	if (!g_applied || !room || logicRoom < 0 || logicRoom >= number_rooms)
		return logicRoom;

	short at = logicRoom;

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &at);

	// The walk cannot leave the room array, but a room list it has never seen
	// is not a thing to hand to draw_room_list on trust.
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
	/* A present that waited its full two ticks is a thirty hertz one - the
	 * inventory, the load/save screen, the death menu, the loading bar, the
	 * load screen's fade loop, and DrawPhaseGame itself at the title. Their
	 * counters step once per present and always did, so the latch has to be
	 * down while they run. Above all: RenderLoadPic's loop is `do { ...
	 * S_OutputPolyList(); S_DumpScreen(); } while (DoFade != 2)`, and
	 * DoScreenFade is the only thing that ever sets DoFade to 2. A latch
	 * left standing from the last game frame would stop it and the loading
	 * screen would never end. */
	g_tween = false;
}

void InterpNoteStep(int kind)
{
	if (gLogEnabled && kind >= 0 && kind < INTERP_STEP_COUNT)
		g_step[kind]++;
}

/* ORDER 5 (#148). WHAT A PARTICLE IS DRAWN AT, and it is g_applied and not
 * the cadence that decides it.
 *
 * g_applied is true only on a frame that really wrote the blended state into
 * the live structures, which is exactly the set of frames where the bodies,
 * the camera and the poses are all showing (tick - 1) + alpha. Tying the
 * particles to the same flag is what keeps them on the same instant as
 * everything else, in every case the flag rules out as well as in the ones it
 * allows: fps_mode 0, the frame that carries the tick, a cutscene, the first
 * frame of a level and every return from a menu all draw the world live, and
 * on all of them this returns 1 and every draw site keeps vanilla's own
 * arithmetic to the bit. */
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
	/* THE LATCH, AND IT IS SET BEFORE EVERY WAY OUT OF THIS FUNCTION.
	 *
	 * A cutscene frame and a frame with no previous state are drawn LIVE -
	 * the returns below write nothing - but they still carry no logic tick,
	 * and the counters of order 3 must stop on them just the same. Reading
	 * `alpha` rather than what the snapshot did is what makes that true.
	 *
	 * fps_mode 0 hands 1 in and this is false for the whole run.
	 *
	 * AND AT fps_mode 2 IT IS THE TICK COUNTER (order 6). `alpha < 1` is a
	 * statement about the cadence and it only means what it says while the
	 * cadence is two frames to a tick. Bound to the panel there are four or
	 * five, so the frame that carries the tick claims it from the serial
	 * instead - see the note over g_tickSerial. */
	if (ActiveFpsMode() == 2 && GameFrameMinTicks() == 0)
	{
		const bool carries = g_tickSerial != g_steppedSerial;

		g_steppedSerial = g_tickSerial;
		g_tween = !carries;
	}
	else
		g_tween = (alpha < 1.0F);

	/* AND THE LOGIC RNG IS PUT ASIDE (order 3). See the header: several
	 * drawing routines call GetRandomControl, which is the SIMULATION's
	 * generator, so a second drawn frame per tick would move the game.
	 * InterpEndFrame puts this back and the tick then finds the stream
	 * exactly where a thirty hertz run would have left it. */
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

	/* ORDER 4: nothing may be pose-blended until this frame's own walk says
	 * so, slot by slot. Every early return below therefore leaves the whole
	 * array at zero and InterpPoseTime answers 0 for every item in the game.
	 * memset of 512 bytes once per frame, against a stale byte drawing a
	 * teleported enemy with half of the pose it wore at its old place. */
	memset(g_poseOk, 0, sizeof(g_poseOk));
	g_laraPosed = false;
	g_armBlended = false;
	g_armRefused = false;

	/* THE FRAME IS DRAWN LIVE. Four ways to get here, and all four write
	 * nothing at all - not the snapshot, not a phd_LookAt, not one byte:
	 *
	 *   alpha >= 1   fps_mode 0, or the frame that lands on a logic tick.
	 *   !items       before a level is loaded, and the title's own frames.
	 *   !valid       the first frame after a level start, a savegame or a
	 *                menu - there is no previous state to blend from.
	 *   cutscene     the cutscene camera is decoded from a frame index
	 *                (game\deltapak.cpp) and frigup_lara moves Lara from
	 *                inside DrawPhaseGame itself. Blending either would be
	 *                fighting a decoder, and restoring afterwards would undo
	 *                what frigup_lara did. A cutscene runs at thirty, whole,
	 *                and says so in the log. */
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

	/* A CAMERA CUT IS A CUT OF THE WHOLE PICTURE (#123/#148, fix round R).
	 *
	 * The test used to sit at the BOTTOM of this function, after every item,
	 * effect, hair link, particle and aim angle had already been overwritten
	 * with its halfway value. So a cut left the camera live and the world
	 * half a step behind it - and in first person, where the eye hangs on
	 * Lara's own head, the whole level then jumped by half a step against her
	 * on every cut frame. At camcut=10..17 a second that is the shudder the
	 * play test reported while she STOOD STILL, the "four models dragged
	 * behind her", and room seams that walk with her breathing.
	 *
	 * The rule is now the one the cutscene branch above already used: if the
	 * camera is not a neighbour of the last one, nothing on this frame is.
	 * The frame is drawn LIVE, whole - which is one frame of thirty hertz
	 * inside a sixty hertz run, at the one moment the picture jumps anyway.
	 *
	 * `alpha` still decides the tween latch (set at the top), so every
	 * counter of order 3 stops on this frame exactly as it must. */
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

		/* ORDER 4, and it is set HERE - one line above the write that would
		 * make the same test lie. Everything that got this far has a
		 * previous state, still holds the same object and did not jump, so
		 * its pose has a neighbour to be blended with. Whether it really is
		 * blended is then GetFramesAt's decision (rule K6), and an item that
		 * did not MOVE gets the byte too: a machine animating on the spot is
		 * exactly the case where the pose is all there is to interpolate. */
		g_poseOk[i] = 1;

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

	/* ORDER 5 - THE SHELLS, THE SWARMS, THE BUBBLES AND THE ROPES.
	 *
	 * Exactly the items[] walk above, with two differences and no third: the
	 * identity test is "was this slot alive on the last tick" rather than an
	 * object number, and there is no pose behind any of them. Everything else
	 * - the half-sector snap, the "did it move at all" skip, the copy that
	 * has to go back - is the same, because it is the same problem. */
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

	/* LARA'S NINE AIM ANGLES (order 4). The torso twist, the head turn and
	 * the two arms - what the PLAYER steers, as against what the animation
	 * dictates. Blended and put back exactly like a position, because that is
	 * what they are: per-tick values that the draw reads and the logic owns.
	 *
	 * Blended whenever there is a previous state at all. There is no snap
	 * rule on them: an angle blends the short way round by construction, and
	 * the largest jump any of them can make in one tick is a fast turn of the
	 * head, which for one sixtieth of a second passes through its own
	 * halfway point - which is what a turn looks like. */
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

	/* THE CAMERA. Whether it cuts was decided at the TOP of this function
	 * (fix round R) - a frame whose camera cuts never gets this far, because
	 * it is drawn live from end to end. So here there is nothing left to test
	 * and the blend is unconditional.
	 *
	 * camera.pos.room_number IS STILL NOT INTERPOLATED - it is the SIMULATION's
	 * camera room, it steps once per tick, and half a dozen callers outside the
	 * draw read it. What stood here until fix round T2 was the REASON given for
	 * that, and the reason was wrong:
	 *
	 *     "a half step across a portal would flip the room list and the
	 *      picture with it. The live room with the interpolated eye is a
	 *      mismatch of under a thirtieth of a second of camera travel."
	 *
	 * The flip IS the right room list once the eye has crossed the portal, and
	 * the mismatch is not harmless. GetRoomBounds follows a portal only while
	 * the eye is on the near side of its plane and SetRoomBounds drops one
	 * whose four corners all fall behind the eye, so a walk seeded with the
	 * logic room and projected from the blended eye can throw a whole
	 * neighbouring room away for one frame - the sky flash the play test of
	 * 2026-09-08 photographed in ANDREA1's alley. The DRAW's walk is therefore
	 * seeded from InterpEyeRoom (below) and this field is left alone. */
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

		/* ORDER 5. The pointers are the ones GatherMovers left when the
		 * frame began - not re-gathered - so what goes back goes back where
		 * it came from even if a set were freed between the two calls. */
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

			/* AND THE VIEW MATRIX WITH IT, which is the half that is easy
			 * to forget and impossible to miss once it happens.
			 *
			 * At sixty the loop is draw, present, logic - and every SECOND
			 * ControlPhase runs no tick at all, so no camera routine runs
			 * and nothing rebuilds the view. Leaving the tween frame's
			 * matrix standing would hand it straight to the next frame,
			 * which draws the LIVE positions of everything: the world half
			 * a step ahead of the camera looking at it, on every second
			 * frame, for the whole game.
			 *
			 * phd_LookAt with the saved eye rebuilds w2v_matrix, the
			 * matrix stack's base, CamPos, CamRot, D3DMView and the
			 * device's own view transform from the same inputs the logic
			 * tick used, so all of them come back to the bit. */
			RebuildView(g_savedCam.px, g_savedCam.py, g_savedCam.pz,
				g_savedCam.tx, g_savedCam.ty, g_savedCam.tz, g_savedCam.roll);
		}

		g_applied = false;
	}

	/* AND THE LOGIC RNG GOES BACK (order 3), whatever this frame drew and
	 * whether or not the snapshot was applied - a cutscene frame draws live
	 * and frigup_lara still re-rolls Lara's speech head out of
	 * GetRandomControl on it. Nothing between here and the next
	 * ControlPhase touches the stream, and on a tween frame that
	 * ControlPhase runs no tick at all, so what the next tick reads is what
	 * a thirty hertz run would have left it. */
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

	/* ONE INTERPLOG LINE PER SECOND, and the second is a LOGIC second:
	 * GlobalCounter steps exactly thirty times a second by construction, in
	 * both modes, and it stands still while the inventory is up - which is
	 * what a second of gameplay should mean. It needs no clock and it makes
	 * the line comparable between a thirty and a sixty run without any
	 * further thought. */
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

	// Order 5. The shells, the swarms, the bubbles and the ropes have no
	// neighbour any more either - and Rats, Bats and Spiders are freed with
	// the level, so their previous state is not merely stale but points at
	// slots that will belong to something else.
	for (int i = 0; i < MOVER_COUNT; i++)
		g_prevMover[i].valid = 0;

	for (int i = 0; i < BUBBLE_COUNT; i++)
		g_prevBubble[i].valid = 0;

	for (int i = 0; i < ROPE_COUNT * ROPE_LINKS; i++)
		g_prevRope[i].valid = 0;

	g_prevCam.valid = 0;
	g_prevAim.valid = 0;

	/* Fix round U (U3), corrected in U7: the arms' snapshot too. g_prevArm.base
	 * is a short* INTO THE LEVEL'S FRAME DATA, which FreeLevel hands back to
	 * the game_malloc pool and the next level allocates over - the same hazard
	 * the movers' note above names, and the same shape as the stale pool
	 * pointer that crashed joby3. That the first drawn frame after a load
	 * carries a tick is an argument, not a guard. */
	g_prevArm.valid = 0;
	g_lastDrawnValid = false;

	// Order 4: and no pose has a neighbour any more either.
	memset(g_poseOk, 0, sizeof(g_poseOk));

	/* A frame may be standing half-applied only if this is called from
	 * inside DrawPhaseGame, which nothing does - every caller is a logic
	 * tick or a level boundary. The reset is here so that a future one
	 * cannot leave the live state carrying interpolated values. */
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

	/* And the frame is a live one again (order 3). Every caller of this is a
	 * level boundary or the return from a menu, and what follows all of them
	 * is a modal-rate present or a fresh cadence - either way no counter may
	 * be left standing still. */
	g_tween = false;
	g_seedSaved = false;

	/* And order 6's counter starts level with the ticks again. What follows
	 * an invalidation is a level start or the return from a menu, and the
	 * first frame after either is a live one whose counters have just been
	 * stepped by a thirty hertz present; letting the serial stand would
	 * either repeat that step or lose one, and lost is the harmless half of
	 * a choice that is worth at most one sixtieth of a second. */
	g_steppedSerial = g_tickSerial;

	if (gLogEnabled)
	{
		LogSecond();
		Log("INTERPLOG level=%d invalidated: %s", (int)gfLevelId, why ? why : "?");
		g_at = 0;
	}
}

/* ---------------------------------------------------------------- ORDER 4 --
 *
 * THE POSE'S ONE QUESTION. Everything else about the pose - which key frames,
 * what factor, when to refuse - is arithmetic and lives in
 * interpolate_math.h and in game\draw.cpp, where the animation table is. What
 * only this file knows is WHETHER this drawn frame is a tween frame at all
 * and what the item wore before the last tick, and that is the whole of this
 * function.
 *
 * g_applied is the gate and it is the right one: it is true only on a frame
 * that really blended the snapshot into the live state, which rules out
 * fps_mode 0, the frame that carries the tick, a cutscene, a level start and
 * every return from a menu, in one test and with no list to keep in step. */
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

	/* LARA IS BUILT MORE THAN ONCE PER FRAME and would otherwise be counted
	 * two or three times in every number on this line: CalcLaraMatrices runs
	 * for the world matrices and again for the joint set (game\draw.cpp), a
	 * mirror room runs it a third time, and the first-person camera runs it
	 * once more on the tick. Her pose is one pose, so it is counted once, and
	 * the frame is what she is counted per. */
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

/* THE BONES OF ONE DRAWN POSE, AGAINST THEIR OWN LENGTHS (#123, fix round
 * U, U2). The bone table is walked exactly as Rich_CalcLaraMatrices walks it -
 * flag 1 pops, flag 2 pushes, and the node whose matrix is current when the
 * offset is applied is the parent - so the parent here is the parent the pose
 * was built from. The length that comes out of the DRAWN matrices is compared
 * against the length written in the table, which no animation changes.
 *
 * A bone under a hundredth of a unit is skipped: a zero length offset has no
 * ratio. Lara's tree has none of those; an imported skeleton might. */
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

/* THE ARM'S OWN PREVIOUS FRAME (#123, fix round U, U3).
 *
 * `g_applied` is the same gate the body's pose uses and it is the right one:
 * it is true only on a frame that really blended the snapshot into the live
 * state, which rules out fps_mode 0, the frame that carries the tick, a
 * cutscene, a level start and every return from a menu in one test.
 *
 * The rest is rule K6 in the arm's own terms. A pistol arm counts DOWN as
 * well as up (larafire.cpp draws and holsters through the same table), so a
 * step of -1, 0 and +1 are all neighbours and anything else is a jump with no
 * pose to blend towards. */
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

void InterpNotePoseHD()
{
	if (gLogEnabled)
		g_secPoseHD++;
}

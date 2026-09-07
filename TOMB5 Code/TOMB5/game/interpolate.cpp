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
#include "../specific/3dmath.h"		// phd_LookAt, CamRoll
#include "../specific/function_stubs.h"	// Log, gLogEnabled
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

	// The thresholds and the three lerps live in interpolate_math.h, which
	// has no engine dependency so that the offline test can include the
	// real thing rather than a copy of it. The reasoning for the numbers is
	// written out there.
	const long ITEM_SNAP = INTERP_ITEM_SNAP;
	const long CAM_SNAP = INTERP_CAM_SNAP;

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

	SavedNode g_savedItem[MAX_ITEMS];
	SavedNode g_savedFx[FX_COUNT];
	SavedNode g_savedHair[HAIR_TAILS * HAIR_LINKS];
	short g_touchedItem[MAX_ITEMS];
	short g_touchedFx[FX_COUNT];
	short g_touchedHair[HAIR_TAILS * HAIR_LINKS];
	int g_touchedItems = 0;
	int g_touchedFxs = 0;
	int g_touchedHairs = 0;

	// The camera, kept apart because it is one thing and it is restored by
	// rebuilding the view matrix rather than by writing a field back.
	InterpCamera g_savedCam;
	bool g_camTouched = false;

	bool g_applied = false;		// this frame wrote into the live state
	float g_alpha = 1.0F;		// what this frame is being drawn at

	/* THE LOG (INTERPLOG). Counted always - a run at fps_mode 0 is the
	 * comparison the per-frame movement has to halve against, and it cannot
	 * be that if the counters only run at sixty - but written only with
	 * -log, and every counter below is an int being added to. */
	int g_secFrames = 0;
	int g_secTween = 0;		// frames drawn at an alpha other than 1
	int g_secItems = 0;		// interpolated items, added over the second
	double g_secLaraStep = 0;	// |dpos| of lara_item per DRAWN frame
	int g_secLaraFrames = 0;
	int g_snapTeleport = 0;
	int g_snapRoomJump = 0;
	int g_snapObject = 0;
	int g_snapInvalid = 0;
	int g_snapCamCut = 0;
	int g_snapCutscene = 0;
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
	// there. Never called for a node that snapped.
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

	void LogSecond()
	{
		if (gLogEnabled && g_secFrames > 0)
			Log("INTERPLOG level=%d at=%ds frames=%d tween=%d alpha=%.2f "
				"lara_step=%.1f items=%.1f snaps=%d (teleport=%d roomjump=%d "
				"object=%d invalid=%d) camcut=%d cutscene=%d",
				(int)gfLevelId, g_at, g_secFrames, g_secTween,
				(double)(g_secFrames - g_secTween * 0.5) / g_secFrames,
				g_secLaraFrames ? g_secLaraStep / g_secLaraFrames : 0.0,
				(double)g_secItems / g_secFrames,
				g_snapTeleport + g_snapRoomJump + g_snapObject + g_snapInvalid,
				g_snapTeleport, g_snapRoomJump, g_snapObject, g_snapInvalid,
				g_snapCamCut, g_snapCutscene);

		g_at++;
		g_secFrames = 0;
		g_secTween = 0;
		g_secItems = 0;
		g_secLaraStep = 0;
		g_secLaraFrames = 0;
		g_snapTeleport = 0;
		g_snapRoomJump = 0;
		g_snapObject = 0;
		g_snapInvalid = 0;
		g_snapCamCut = 0;
		g_snapCutscene = 0;
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

float InterpAlphaNow()
{
	/* fps_mode 0 IS THE ENGINE AS IT WAS. One test, and every line below
	 * this file's three entry points is dead code for that build. */
	if (tomb5.fps_mode != 1)
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

void InterpBeginFrame(float alpha)
{
	g_applied = false;
	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_camTouched = false;

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
			g_secFrames++;
			NoteDrawnLara();
		}

		return;
	}

	if (GLOBAL_playing_cutseq)
	{
		if (gLogEnabled)
		{
			g_secFrames++;
			g_snapCutscene++;
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

	/* THE CAMERA, AND THE FIVE WAYS IT CUTS.
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
	 * camera.pos.room_number IS NOT INTERPOLATED, ever. DrawRooms is handed
	 * it (game\draw.cpp) and it drives the whole portal walk and every clip
	 * window; a half step across a portal would flip the room list and the
	 * picture with it. The live room with the interpolated eye is a
	 * mismatch of under a thirtieth of a second of camera travel, and it is
	 * the same compromise TR1X makes. */
	{
		const short roll = CameraRollNow();
		const bool cut =
			g_prevCam.type != (short)camera.type ||
			g_prevCam.oldType != (short)camera.old_type ||
			g_prevCam.fixed != (short)camera.fixed_camera ||
			g_prevCam.spot != (short)bUseSpotCam ||
			Manhattan(camera.pos.x - g_prevCam.px, camera.pos.y - g_prevCam.py,
				camera.pos.z - g_prevCam.pz) > CAM_SNAP ||
			Manhattan(camera.target.x - g_prevCam.tx, camera.target.y - g_prevCam.ty,
				camera.target.z - g_prevCam.tz) > CAM_SNAP;

		if (cut)
		{
			if (gLogEnabled)
				g_snapCamCut++;
		}
		else
		{
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
	}

	if (gLogEnabled)
	{
		g_secFrames++;
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

	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_camTouched = false;

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

	g_prevCam.valid = 0;
	g_lastDrawnValid = false;

	/* A frame may be standing half-applied only if this is called from
	 * inside DrawPhaseGame, which nothing does - every caller is a logic
	 * tick or a level boundary. The reset is here so that a future one
	 * cannot leave the live state carrying interpolated values. */
	g_applied = false;
	g_alpha = 1.0F;
	g_touchedItems = 0;
	g_touchedFxs = 0;
	g_touchedHairs = 0;
	g_camTouched = false;

	if (gLogEnabled)
	{
		LogSecond();
		Log("INTERPLOG level=%d invalidated: %s", (int)gfLevelId, why ? why : "?");
		g_at = 0;
	}
}

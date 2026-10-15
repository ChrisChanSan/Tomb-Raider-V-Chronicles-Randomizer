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
#include "draw.h"
#include "../specific/3dmath.h"
#include "../specific/output.h"
#include "delstuff.h"
#include "deltapak.h"
#include "../specific/drawlara.h"
#include "health.h"
#include "objects.h"
#include "gameflow.h"
#include "sound.h"
#include "../specific/specificfx.h"
#include "../specific/drawroom.h"
#include "../specific/polyinsert.h"
#include "../specific/function_stubs.h"
#include "../specific/profile.h"		// PROFLOG's frame bracket
#include "interpolate.h"		// the frame between two logic ticks
#include "interpolate_math.h"		// the pose midpoint and the one blend factor
#include "../specific/hd/hd_level.h"
#include "../specific/hd/hd_vis.h"
#include "../specific/hd/hd_object.h"
#include "../specific/lighting.h"
#include "items.h"
#include "../specific/file.h"
#include "../specific/polyinsert.h"
#include "door.h"
#include "tomb4fx.h"
#include "../specific/d3dmatrix.h"
#include "rope.h"
#include "rat.h"
#include "bat.h"
#include "spider.h"
#include "twogun.h"
#include "mirror.h"
#include "../specific/alexstuff.h"
#include "lara_states.h"
#include "control.h"
#include "camera.h"
#include "spotcam.h"
#include "effect2.h"
#include "lara.h"
#include "effects.h"
#include "footprnt.h"
#include "../tomb5/tomb5.h"

STATIC_INFO static_objects[70];

float* aIMXPtr;
float aIFMStack[768];
long* IMptr;
long IMstack[768];
long IM_rate;
long IM_frac;

/* The blend factor between the two matrix stacks.
 *
 * The pair (IM_frac, IM_rate) selects one of three fixed blends; IM_alpha is
 * that choice as a number. InitInterpolate computes it from the pair exactly
 * as the original three-way branches did, so every key frame blend is
 * unchanged, and InitInterpolateAlpha lets the time interpolation ask for a
 * factor no pair of integers can name - the midpoint of two poses (see
 * interpolate_math.h).
 *
 * IM_alpha8 is the same factor in eighths, for the integer stack, which
 * rounds by shifting and has to keep doing so to the bit. */
float IM_alpha = 0.5F;
long IM_alpha8 = 4;

long CurrentRoom;
long outside;
short SkyPos;
short SkyPos2;
ushort LightningRGB[3];
ushort LightningRGBs[3];
static short LightningCount;
static short LightningRand;
static short dLightningRand;
static short LightningSFXDelay = 0;
short no_rotation[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Which dead bodies the draw saw this frame.
 *
 * PrintObjects sets a byte per item; StepAfterDeathFade, called once per
 * logic tick from ControlPhase, counts them up and kills the ones that
 * reached 128. One byte per item slot and no allocation; the notes at
 * PrintObjects and StepAfterDeathFade say why the counting is not in the draw. */
static unsigned char g_afterDeathSeen[MAX_ITEMS];

static long outside_top;
static long outside_left;
static long outside_right;
static long outside_bottom;

static short* skelly_rhandbak;
static short* skelly_lhandbak;
static short skelly_backgunbak;

/* The two arrays of the portal walk, sized to the engine's own maximum.
 *
 * The original short[100] and long[128] were too small: the classic list
 * alone reaches 106 rooms in one JOBY4 frame, and with the reach on JOBY2's
 * stairs and ANDREA2's courtyard sit at the limit for seconds at a time. Both
 * arrays keep a counted guard against overflow.
 *
 * kDrawRoomsMax is the engine's room maximum: BuildOutsideTable
 * (game\setup.cpp) refuses a level with more than 256 rooms, and a room
 * enters draw_rooms at most once per frame - the `bound_active & 1` test in
 * GetRoomBounds guarantees that. No loadable level can fill this array.
 *
 * kRoomListRing is the queue. A room is queued only while `bound_active & 2`
 * is clear, queueing sets that bit and the dequeue in GetRoomBounds clears it,
 * so at most one pending entry per room exists and the depth cannot pass 256.
 * A room may be queued again after its dequeue, so a frame can take more
 * dequeues than it has rooms (worst measured: 319, JOBY4), but the depth stays
 * bounded. 512 is that bound with a factor of two, and a power of two so the
 * wrap is a mask. Deepest queue measured over the fifteen levels: 70
 * (RoomProbe -reachsurvey).
 *
 * The wrap is `& (kRoomListRing - 1)`: room_list_start and room_list_end are
 * set to 0 at the top of every walk and only count up, so neither is ever
 * negative and the mask equals the modulo. The walk itself is the original;
 * bigger arrays only refuse fewer rooms. kReachBudget lives in ReachTrimRooms,
 * which only runs while g_reach_on. */
static const int kDrawRoomsMax = 256;
static const long kRoomListRing = 512;

static long number_draw_rooms;
static short draw_rooms[kDrawRoomsMax];
static long room_list_start = 0;
static long room_list_end = 0;
static long draw_room_list[kRoomListRing];
static long camera_underwater;
static short ClipRoomNum;

/* The reach: a second answer per room in the HD path.
 *
 * The remaster's art opens walls the classic level keeps shut - JOBY2's torn
 * wall beside the stairs, where room 124 is seen far wider than the classic
 * doorway of 123 allows - and behind such a hole the player would see the
 * clear colour, because SetRoomBounds clips the child portal against the
 * parent's window and drops what falls outside it as `empty`. The same
 * arithmetic drops ANDREA2's tower (room 222) from the list at some view
 * directions, which makes it pop in and out.
 *
 * In the HD path every room therefore also records whether the walk reaches
 * it at all. A portal reaches the room behind it when it faces the eye and
 * lands on the screen - the parent's window does not enter that question.
 * The classic window is computed beside it as before and stays the scissor
 * for the intruding triangles, and it may now be empty: a room the reach
 * alone brought in draws all of its art over the whole screen and none of its
 * intruding art, so the bleed guard for intruding triangles still holds.
 *
 * A seen-mark rather than a second rectangle to widen: the reach rectangle of
 * a portal does not depend on how the walk got to it, so one visit per room
 * suffices. A second widening union makes ANDREA3 and ANDY2 walk rings of
 * rooms over and over - over 20,000 dequeues in a frame against the classic
 * walk's 33 - because nothing shrinks along the chain. With the mark the
 * worst frame of the fifteen levels takes 319 dequeues (RoomProbe
 * -reachsurvey).
 *
 * With hd_enable off, or in a level the .TRG has nothing for, HDLevelActive()
 * is false, g_reach_on stays false and the walk is the original one: the
 * classic SetRoomBounds, the classic accumulation, and no room trimmed. The
 * setting hd_reach, on by default, switches it off the same way in an HD
 * level: the walk is then the classic one, and the art of a room the reach
 * alone would list - through an opened wall - is not drawn. */
static bool g_reach_on = false;
// Per room, and only while g_reach_on: the pending queue entry's classic
// rectangle exists (bit 0) and the reach has visited this room (bit 1).
static unsigned char g_reach_state[256];
static const unsigned char REACH_CLASSIC = 1;
static const unsigned char REACH_SEEN = 2;
// The rectangle the reach gave a room, for the ROOMLIST seam alone.
static short g_reach_box[256][4];
// Per frame, for the ROOMWALK line: rooms the reach alone put in the list,
// rooms refused because draw_rooms or the ring was full, rooms the budget
// dropped again.
static int g_reach_added = 0;
static int g_reach_capped = 0;
static int g_reach_trimmed = 0;
// And rooms the reach found that the .TRG has no art for - see ReachTrimRooms.
static int g_reach_noart = 0;
// And rooms taken in for the art they stand under a listed sky - CarryArt.
static int g_reach_carried = 0;

/* The visibility path (specific\hd\hd_vis.h, tomb5.hd_vis 1): the walk is
 * the classic one - SetRoomBounds, the classic windows, no reach - and every
 * room it dequeues lists three more kinds of room, each with the part of
 * that dequeue's window it is seen in: the rooms whose art stands in its
 * inside (its guests, by the box of that art), the rooms behind the classic
 * wall faces its art leaves bare (by the face, as a portal), and through a
 * portal whose opening in the art is wider than the portal, the room behind
 * it by that opening. A room listed so is queued as a portal queues it, so
 * it walks on through its own portals - and asks its own three tables -
 * with the window it got, and that window grows by union as the classic
 * one does. `g_vis_why` is per room the source that queued it first, for
 * VISLOG: 0 the portals or the eye's room, 1 a guest, 2 an opening, 3 a
 * wide portal, 4 a portal at the window's edge (VisFollowPortal); 0xff not
 * queued. `g_vis_edge` is that edge's width in pixels for this frame. */
static bool g_vis_walk = false;
static unsigned char g_vis_why[256];
static int g_vis_count[5];
static long g_vis_dequeues = 0;
static float g_vis_edge = 0.0f;

short* GetBoundsAccurate(ITEM_INFO* item)
{
	short* frmptr[2];
	long rate, frac;
	static short interpolated_bounds[6];

	frac = GetFrames(item, frmptr, &rate);

	if (!frac)
		return frmptr[0];

	for (int i = 0; i < 6; i++, frmptr[0]++, frmptr[1]++)
		interpolated_bounds[i] = short(*frmptr[0] + (*frmptr[1] - *frmptr[0]) * frac / rate);

	return interpolated_bounds;
}

short* GetBestFrame(ITEM_INFO* item)
{
	short* frm[2];
	long frac, rate;
	
	frac = GetFrames(item, frm, &rate);

	if (frac > (rate >> 1))
		return frm[1];
	else
		return frm[0];
}

void InitInterpolate(long frac, long rate)
{
	/* The pair is kept because InitInterpolate2 (game\sphere.cpp) and every
	 * reader of IM_frac / IM_rate outside this file expect it, and because a
	 * pair of integers says where in an animation the blend is, which the
	 * factor alone does not once the time interpolation hands in midpoints.
	 * The factor is derived from the pair by the same three-way test the four
	 * blend functions originally made inline. */
	IM_rate = rate;
	IM_frac = frac;
	IM_alpha = InterpKeyAlpha(frac, rate);
	IM_alpha8 = InterpEighths(IM_alpha);
	IMptr = IMstack;
	aIMXPtr = aIFMStack;
	memcpy(IMstack, phd_mxptr, 48);
	memcpy(aIFMStack, aMXPtr, 48);
}

/* The same set-up with a factor that is not a key frame fraction. The time
 * interpolation asks for the midpoint of the two poses the two neighbouring
 * logic ticks draw, which is an eighth of a key frame interval as often as
 * not and has no (frac, rate) that names it.
 *
 * IM_frac and IM_rate are set to what the factor is worth as a fraction of
 * eight, so that anything still reading the pair - InitInterpolate2's
 * neighbours, a log line, a debugger - sees a pair that means the same thing
 * as the factor rather than the last animation's leftovers. */
void InitInterpolateAlpha(float alpha)
{
	IM_alpha = alpha;
	IM_alpha8 = InterpEighths(alpha);
	IM_rate = 8;
	IM_frac = IM_alpha8;
	IMptr = IMstack;
	aIMXPtr = aIFMStack;
	memcpy(IMstack, phd_mxptr, 48);
	memcpy(aIFMStack, aMXPtr, 48);
}

void phd_PopMatrix_I()
{
	phd_PopMatrix();
	IMptr -= 12;
	aIMXPtr -= 12;
}

void phd_PushMatrix_I()
{
	phd_PushMatrix();
	memcpy(IMptr + 12, IMptr, 48);
	memcpy(aIMXPtr + 12, aIMXPtr, 48);
	IMptr += 12;
	aIMXPtr += 12;
}

void phd_RotY_I(short ang)
{
	float* amxptr;
	long* mxptr;

	phd_RotY(ang);
	mxptr = phd_mxptr;
	amxptr = aMXPtr;
	phd_mxptr = IMptr;
	aMXPtr = aIMXPtr;
	phd_RotY(ang);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void phd_RotX_I(short ang)
{
	float* amxptr;
	long* mxptr;

	phd_RotX(ang);
	mxptr = phd_mxptr;
	amxptr = aMXPtr;
	phd_mxptr = IMptr;
	aMXPtr = aIMXPtr;
	phd_RotX(ang);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void phd_RotZ_I(short ang)
{
	float* amxptr;
	long* mxptr;

	phd_RotZ(ang);
	mxptr = phd_mxptr;
	amxptr = aMXPtr;
	phd_mxptr = IMptr;
	aMXPtr = aIMXPtr;
	phd_RotZ(ang);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void phd_TranslateRel_I(long x, long y, long z)
{
	float* amxptr;
	long* mxptr;

	phd_TranslateRel(x, y, z);
	mxptr = phd_mxptr;
	phd_mxptr = IMptr;
	amxptr = aMXPtr;
	aMXPtr = aIMXPtr;
	phd_TranslateRel(x, y, z);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void phd_TranslateRel_ID(long x, long y, long z, long x2, long y2, long z2)
{
	float* amxptr;
	long* mxptr;

	phd_TranslateRel(x, y, z);
	mxptr = phd_mxptr;
	amxptr = aMXPtr;
	phd_mxptr = IMptr;
	aMXPtr = aIMXPtr;
	phd_TranslateRel(x2, y2, z2);
	aMXPtr = amxptr;
	phd_mxptr = mxptr;
}

void phd_RotYXZ_I(short y, short x, short z)
{
	float* amxptr;
	long* mxptr;

	phd_RotYXZ(y, x, z);
	mxptr = phd_mxptr;
	phd_mxptr = IMptr;
	amxptr = aMXPtr;
	aMXPtr = aIMXPtr;
	phd_RotYXZ(y, x, z);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void gar_RotYXZsuperpack_I(short** pprot1, short** pprot2, long skip)
{
	float* amxptr;
	long* mxptr;

	gar_RotYXZsuperpack(pprot1, skip);
	mxptr = phd_mxptr;
	amxptr = aMXPtr;
	phd_mxptr = IMptr;
	aMXPtr = aIMXPtr;
	gar_RotYXZsuperpack(pprot2, skip);
	phd_mxptr = mxptr;
	aMXPtr = amxptr;
}

void gar_RotYXZsuperpack(short** pprot, long skip)
{
	ushort* prot;
	long packed;

	while (skip)
	{
		prot = (ushort*)*pprot;

		if (*prot & 0xC000)
			(*pprot)++;
		else
			*pprot += 2;

		skip--;
	}

	prot = (ushort*)*pprot;

	switch (*prot >> 14)
	{
	case 0:
		packed = (*prot << 16) + *(prot + 1);
		phd_RotYXZpack(packed);
		(*pprot)++;
		break;

	case 1:
		phd_RotX(short((*prot & 0xFFF) << 4));
		break;

	case 2:
		phd_RotY(short((*prot & 0xFFF) << 4));
		break;

	default:
		phd_RotZ(short((*prot & 0xFFF) << 4));
		break;
	}

	(*pprot)++;
}

void phd_PutPolygons_I(short* ptr, long clip)
{
	phd_PushMatrix();
	InterpolateMatrix();
	phd_PutPolygons(ptr, clip);
	phd_PopMatrix();
}

void aInterpolateMatrix()
{
	float* matrixp = aMXPtr;
	float* iMatrixp = aIMXPtr;

	/* One factor for all twelve elements. InterpBlendFloat reproduces the
	 * original three branches on (IM_frac, IM_rate) expression for
	 * expression, so this loop produces the same floats to the last bit
	 * (interpolate_math.h says why the expressions and not an algebraic
	 * rearrangement of them). */
	for (int i = 0; i < 12; i++)
		matrixp[i] = InterpBlendFloat(matrixp[i], iMatrixp[i], IM_alpha);
}

long DrawPhaseGame()
{
	ProfileFrameBegin();

	/* The in-between frame. At sixty presents and thirty logic ticks every
	 * second frame falls between two ticks; this writes the halfway point
	 * between the last two ticks into items[], effects[], the braid and the
	 * camera, and everything below draws from those globals unchanged.
	 * InterpEndFrame, at the bottom of this function, puts the original bytes
	 * back before the next tick reads them.
	 *
	 * It stands above CalcLaraMatrices because that is the first reader of
	 * lara_item->pos in the frame, and the HD seam takes Lara's matrices from
	 * what it produces - so specific\hd\ needs no change of its own.
	 *
	 * At fps_mode 0, InterpAlphaNow returns 1 and this call writes nothing.
	 * The alpha is read once, so POISONLOG reports the value the frame was
	 * drawn at. */
	const float alpha = InterpAlphaNow();

	InterpBeginFrame(alpha);

	if (gLogEnabled)
		PoisonLogFrame(alpha);

	ProfBegin(PROF_GATHER);
	CalcLaraMatrices(0);
	InterpArmLogFrame();	// ARMLOG: the view set, once per drawn frame
	phd_PushUnitMatrix();
	CalcLaraMatrices(1);
	phd_PopMatrix();

	if (GLOBAL_playing_cutseq)
		frigup_lara();

	SetLaraUnderwaterNodes();
	ProfBegin(PROF_ROOMS);
	DrawRooms(camera.pos.room_number);
	ProfEnd(PROF_ROOMS);
	DrawGameInfo(1);
	ProfEnd(PROF_GATHER);

	ProfBegin(PROF_OUTPUT);
	S_OutputPolyList();
	ProfEnd(PROF_OUTPUT);

	/* The clock split. This present, and no other, may run at sixty; every
	 * modal loop in the game keeps two ticks and therefore keeps its
	 * per-present animation counters at the speed they were written for (see
	 * the note over gPresentMinTicks in specific\output.h).
	 *
	 * The game logic stays at thirty without further change. S_DumpScreen
	 * returns how many 1/60 s ticks the frame really took, DoLevel hands that
	 * to ControlPhase as nframes, and ControlPhase's accumulator
	 * - `for (framecount += nframes; framecount > 0; framecount -= 2)`
	 * (game\control.cpp) - already counts in 1/60 s units and spends two of
	 * them on a logic step. Fed one per frame, it runs a tick on every second
	 * call: sixty presents, thirty ticks, the simulation untouched.
	 *
	 * Not every frame through here is a played one: the title menu draws
	 * through this function too, and its menu code reads input once per
	 * present while input is written once per tick - at sixty it would read
	 * every keypress twice and move two rows at a time. GameFrameMinTicks()
	 * holds that rule; the measurement is written over it in
	 * specific\output.cpp. */
	gPresentMinTicks = GameFrameMinTicks();
	camera.number_frames = S_DumpScreen();

	S_AnimateTextures(camera.number_frames);

	/* The interpolation is undone inside the profile bracket, so its cost is
	 * counted in `work` like everything else the frame did. Every value
	 * InterpBeginFrame overwrote goes back to the byte, and the view matrix
	 * is rebuilt from the live camera - the next ControlPhase may run no tick
	 * at all, and then nothing else would rebuild it. */
	InterpEndFrame();

	/* ProfileFrameEnd comes before the restore: it judges this frame
	 * against the slot this frame actually asked for, and gPresentMinTicks is
	 * where that number lives. Nothing between the present and here presents
	 * anything, so the window costs nothing. */
	ProfileFrameEnd();
	gPresentMinTicks = 2;
	return camera.number_frames;
}

void SkyDrawPhase()
{
	// A profile phase of its own and not part of `output`: this is the one
	// place in the frame that fills and draws the classic buckets and the
	// sort list outside S_OutputPolyList, so without it that time would be
	// counted as output. It also runs the sky lightning state, which must
	// advance on logic ticks only (see below).
	ProfScope prof(PROF_SKY);

	if (outside)
	{
		if (!objects[HORIZON].loaded)
		{
			outside = -1;
			return;
		}

		/* A blended frame whose eye stands where no room holds it and no
		 * point between the two tick eyes does either (InterpSkyWithheld,
		 * game\interpolate.cpp) gets no sky: what its walk leaves undrawn is
		 * the clear colour, as indoors, and not a sky seen through the
		 * level. The storm below still steps, because it is a clock and not a
		 * picture; the moon, the HD dome, the flat layers, the horizon and
		 * the stars are left out. Never true at fps_mode 0. */
		const bool withheld = InterpSkyWithheld();

		if (!withheld && (gfLevelId == LVL5_GALLOWS_TREE || gfLevelId == LVL5_LABYRINTH || gfLevelId == LVL5_OLD_MILL))
			DrawMoon();

		if (BinocularRange)
			AlterFOV(short(GAME_FOV - BinocularRange));

		phd_PushMatrix();
		phd_TranslateAbs(camera.pos.x, camera.pos.y, camera.pos.z);

		/* The storm is a clock, so it runs on logic ticks and not on drawn
		 * frames.
		 *
		 * Everything in this block is state: how many flashes are left in
		 * the strike (LightningCount), how bright it is right now
		 * (LightningRand, which UpdateSkyLightning ramps), and how long
		 * until the thunder (LightningSFXDelay). At sixty presents a
		 * second, unguarded, the flicker would run at double speed and the
		 * thunder would follow the flash half a second too early - and the
		 * chance of a strike starting, `!(GetRandomDraw() & 127)`, would be
		 * rolled twice as often, so storms would begin twice as often too.
		 *
		 * The colour it produces, LightningRGB, is a static the sky reads;
		 * an in-between frame simply draws the last one, which is what
		 * every other thing on that frame does.
		 *
		 * DoTitle calls this before its DrawPhaseGame, after the tick that
		 * lowered the live latch, so it reads the latch of the frame drawn
		 * last; inside a frame the two are the same. */
		if ((gfLevelFlags & GF_LIGHTNING) && !InterpDrawnFrameWasTween())
		{
			if (!LightningCount && !LightningRand)
			{
				if (!(GetRandomDraw() & 127))
				{
					LightningCount = (GetRandomDraw() & 0x1F) + 16;
					dLightningRand = (GetRandomDraw() & 0xFF) + 256;
					LightningSFXDelay = (GetRandomDraw() & 3) + 12;
				}
			}
			else
			{
				UpdateSkyLightning();

				if (LightningSFXDelay > -1)
					LightningSFXDelay--;

				if (!LightningSFXDelay)
					SoundEffect(SFX_THUNDER_RUMBLE, 0, SFX_DEFAULT);
			}
		}

		// The tail of this block, without anything drawn. The moon above is
		// what this block queues for DrawBuckets and DrawSortList, and it was
		// left out; whatever else stands in them is drawn with the frame's own
		// output (specific\output.cpp).
		if (withheld)
		{
			phd_PopMatrix();

			if (BinocularRange)
				AlterFOV(short(GAME_FOV -  (7 * BinocularRange)));

			return;
		}

		nPolyType = 6;
		DrawBuckets();
		DrawSortList();

		/* The HD sky dome, in the classic horizon's place - after the moon
		 * and before the stars, because none of the three writes depth and
		 * the order in this function is the order on the screen.
		 *
		 * The .TRG carries a sky group of its own past the last room: a
		 * panorama band on a sphere plus its own scrolling cloud discs. Where
		 * it exists it replaces both things the classic path draws here - the
		 * two flat colour layers and the HORIZON mesh - because it already
		 * contains them. What it does not contain, and what therefore still
		 * runs below, is the weather: the moon, the stars and the lightning
		 * flash, which the game paints through layer 1 in the lightning's own
		 * colour and which would otherwise be lost with the layer.
		 *
		 * With hd_enable off, before a level is built, or in a level whose
		 * .TRG has no sky group, HDFrameSky returns false on its first test
		 * and the classic sky is drawn unchanged. */
		const bool hdSky = HDFrameSky(aMXPtr);
		const bool lightning = (gfLevelFlags & GF_LIGHTNING) != 0;
		const bool layer1 = (gfLevelFlags & GF_LAYER1) && (!hdSky || lightning);
		const bool layer2 = (gfLevelFlags & GF_LAYER2) && !hdSky;

		phd_PushMatrix();

		if (layer1)
		{
			phd_RotY(32760);

			if (lightning)
				DrawFlatSky(RGBA(LightningRGB[0], LightningRGB[1], LightningRGB[2], 44), SkyPos, -1536, 4);
			else
				DrawFlatSky(*(ulong*)&gfLayer1Col, SkyPos, -1536, 4);
		}

		if (layer2)
			DrawFlatSky(0xFF000000 | *(ulong*)&gfLayer2Col, SkyPos2, -1536, 2);

		if (layer1 || layer2)
			OutputSky();

		phd_PopMatrix();

		if ((gfLevelFlags & GF_HORIZON) && !hdSky)
		{
			if (gfLevelId == LVL5_TITLE && jobyfrigger)
				phd_PutPolygonsSkyMesh(meshes[objects[CHEF_MIP].mesh_index], -1);
			else
				phd_PutPolygonsSkyMesh(meshes[objects[HORIZON].mesh_index], -1);

			OutputSky();
		}

		if (gfLevelId == LVL5_GALLOWS_TREE || gfLevelId == LVL5_LABYRINTH || gfLevelId == LVL5_OLD_MILL)
		{
			DrawStars();
			OutputSky();
		}

		phd_PopMatrix();

		if (BinocularRange)
			AlterFOV(short(GAME_FOV -  (7 * BinocularRange)));
	}
}

void UpdateSkyLightning()
{
	if (LightningCount <= 0)
	{
		if (LightningRand < 4)
			LightningRand = 0;
		else
			LightningRand -= LightningRand >> 2;
	}
	else
	{
		LightningCount--;

		if (LightningCount)
		{
			dLightningRand = GetRandomDraw() & 0x1FF;
			LightningRand += (dLightningRand - LightningRand) >> 1;
		}
		else
		{
			dLightningRand = 0;
			LightningRand = (GetRandomDraw() & 0x7F) + 400;
		}
	}

	for (int i = 0; i < 3; i++)
	{
		LightningRGB[i] = LightningRGBs[i] + ((LightningRGBs[i] * LightningRand) >> 8);

		if (LightningRGB[i] > 255)
			LightningRGB[i] = 255;
	}
}

void CalculateObjectLighting(ITEM_INFO* item, short* frame)
{
	long x, y, z;

	if (item->shade >= 0)
		S_CalculateStaticMeshLight(item->pos.x_pos,item->pos.y_pos,item->pos.z_pos, item->shade & 0x7FFF, &room[item->room_number]);
	else
	{
		phd_PushUnitMatrix();
		phd_SetTrans(0, 0, 0);
		phd_RotYXZ(item->pos.y_rot, item->pos.x_rot, item->pos.z_rot);
		phd_TranslateRel((frame[0] + frame[1]) >> 1, (frame[2] + frame[3]) >> 1, (frame[4] + frame[5]) >> 1);
		x = item->pos.x_pos + (phd_mxptr[3] >> W2V_SHIFT);
		y = item->pos.y_pos + (phd_mxptr[7] >> W2V_SHIFT);
		z = item->pos.z_pos + (phd_mxptr[11] >> W2V_SHIFT);
		phd_PopMatrix();
		current_item = item;
		item->il.item_pos.x = x;
		item->il.item_pos.y = y;
		item->il.item_pos.z = z;
		CalcAmbientLight(item);
		CreateLightList(item);
	}
}

void DrawAnimatingItem(ITEM_INFO* item)
{
	OBJECT_INFO* obj;
	short** meshpp;
	long* bone;
	short* frm[2];
	short* data;
	short* rot;
	short* rot2;
	long frac, clip, bit;
	float blend = 0.0F;

	/* At thirty hertz and on every frame that carries a logic tick this is
	 * GetFrames - the same pair, the same factor. On the frame between two
	 * ticks it is the midpoint of the two poses those ticks draw, and `frac`
	 * is then nonzero even for animations that never interpolate in vanilla
	 * (rate 1 - 3619 of them across the sixteen level files), so a rate-1
	 * walk cycle moves on every drawn frame instead of every second one. */
	frac = GetFramesNow(item, frm, &blend);
	obj = &objects[item->object_number];

	/* The blob, unless the item takes the projected shadow's second map
	 * instead (specific\hd\hd_object.h, HDObjectBlobGivesWay): only an HD
	 * item in a frame whose projected shadow her own draw has just stepped,
	 * and false everywhere else - the classic path, the stored modes 1 to 4,
	 * a classic item, a frame without her step - so the blob is drawn there
	 * as it always is. */
	if (obj->shadow_size && !HDObjectBlobGivesWay(item, obj))
		S_PrintShadow(obj->shadow_size, frm[0], item);

	phd_PushMatrix();
	phd_TranslateAbs(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
	phd_RotYXZ(item->pos.y_rot, item->pos.x_rot, item->pos.z_rot);

	/* Is this item drawn by the HD path?
	 *
	 * The answer costs one array lookup and is false for the whole game
	 * unless hd_enable and hd_objects are both on and the level built its
	 * models. When it is true, no classic polygon of this item goes into the
	 * buckets: the walk below runs unchanged - the same animation, frames,
	 * interpolation and mesh tree - but instead of handing each node's mesh
	 * to phd_PutPolygons it hands the node's matrix to hd_object, which is
	 * what a skinned model needs.
	 *
	 * Decided here because the distance model right below depends on it: an
	 * HD item never switches to its MIP slot. The remaster ships no reduced
	 * model, the MIP slot's own name would bind a different .TRM or none at
	 * all, and a door that turned from HD to classic at five sectors would
	 * pop every time the player walked away from it. Enemies skip the MIP
	 * for their own reason (below); everything else keeps it. */
	const bool hd = HDObjectBegin(item, obj);

	/* An HD item with a pose blended in time goes through HDObjectNode's
	 * `interpolated` branch (specific\hd\hd_object.cpp), which records the
	 * matrix phd_PutPolygons_I would have drawn the classic mesh with. The
	 * counter sits here, where both facts are known, and reaches INTERPLOG
	 * as pose_hd. */
	if (hd && frac == 2)
		InterpNotePoseHD();

	// Distance models (MIP/LOD) are switched off for enemies - they are
	// always drawn at full resolution.
	//
	// The reason is safety, not looks: the slot after the object's own is
	// taken blindly as soon as anything is loaded in it. In vanilla that
	// works because level and enemies were built together; the randomizer
	// puts enemies into levels whose MIP slot carries unrelated material.
	// From 5120 units on, the enemy would be drawn as that material, with
	// its bones and its frame, i.e. as a torn-up lump. Measured across all
	// 13 levels (SlotAnims): 51 such combinations are possible.
	//
	// Almost nothing is lost: TR5 has exactly seven real MIP cases - SWAT,
	// SWAT+, guard and Two Gun in the three rich levels - all with the same
	// mesh count as the original (15/15), i.e. practically the same
	// silhouette at five sectors' distance.
	//
	// "intelligent" separates cleanly: all enemies with object_mip carry
	// it, doors and bridges (mip 2048-4096) do not and keep their distance
	// model.
	if (!hd && obj->object_mip && !obj->intelligent && (obj + 1)->loaded &&
		phd_mxptr[M23] >> 16 > obj->object_mip)
		obj++;

	calc_animating_item_clip_window(item, frm[0]);
	clip = S_GetObjectBounds(frm[0]);

	// CLIPLOG: one line for every new (slot, anim) pair of every thinking
	// enemy, with clip, room and status. It answers whether this function
	// runs at all for an enemy that is invisible on screen: if an anim never
	// shows up in the log, the enemy is not drawn in it, and the fault lies
	// before this function (room list, status), not inside it.
	if (gLogEnabled && obj->intelligent)
	{
		static long seen[128];
		static long nSeen;
		const long id = ((long)item->object_number << 16) | (item->anim_number & 0xFFFF);
		long fresh = 1;

		for (long k = 0; k < nSeen; k++)
			if (seen[k] == id) { fresh = 0; break; }

		if (fresh && nSeen < 128)
		{
			seen[nSeen++] = id;
			Log("CLIPLOG obj=%d item=%d anim=%d frame=%d clip=%d room=%d status=%d state=%d goal=%d meshbits=%X swap=%X",
				item->object_number, (int)(item - items), item->anim_number,
				item->frame_number, clip, item->room_number, item->status,
				item->current_anim_state, item->goal_anim_state,
				(unsigned)item->mesh_bits, (unsigned)item->meshswap_meshbits);
		}
	}

	// MESHLOG: aCheckMeshClip discards meshes whose box lies entirely
	// outside the current window f_left..f_bottom. The counter says, after
	// drawing, whether the body's meshes were discarded there - and the
	// window says which cutout they were tested against.
	const long rejectsBefore = gLogEnabled ? aMeshClipRejects : 0;

	// Arm CLIPDETAIL for this thinker only - otherwise the global throttle
	// also catches legitimate clip-aways (doors behind the camera) and makes
	// the lines ambiguous.
	if (gLogEnabled)
		aClipReportObj = obj->intelligent ? item->object_number : -1;

	/* An HD item clipped away is walked for the projected shadow's second map
	 * alone where its blob gave way to that map and its place lies inside the
	 * second box (specific\hd\hd_object.h, HDObjectCastUnseen): the same walk
	 * without the light and without the root fix with its counter, which
	 * belong to a drawn item. Every other clipped item walks nothing. */
	const bool unseen = hd && !clip && HDObjectCastUnseen();

	if (clip || unseen)
	{
		if (clip)
			CalculateObjectLighting(item, frm[0]);

		if (!item->data)
			data = no_rotation;
		else
			data = (short*)item->data;

		bit = 1;
		meshpp = &meshes[obj->mesh_index];
		bone = &bones[obj->bone_index];

		/* The root offset on a frame whose pose blend was refused (the
		 * animation changed or the frame did not step by exactly one - see
		 * GetFramesAt). False on every other frame - a frame that carries the
		 * tick, an accepted blend, the whole of fps_mode 0 - and then `root` is
		 * not written and the two branches below take the frame's own root.
		 * Asked only for a drawn item (`clip`) because an item that is not
		 * drawn has no root to fix and must not be counted as one: an item
		 * walked for the second map alone takes the frame's own root. */
		long root[3];
		const bool rootFix = clip && InterpRootNow(item, frm, frac, blend, root);

		if (frac)
		{
			InitInterpolateAlpha(blend);

			if (rootFix)
				phd_TranslateRel_ID(root[0], root[1], root[2], root[0], root[1], root[2]);
			else
				phd_TranslateRel_ID(frm[0][6], frm[0][7], frm[0][8], frm[1][6], frm[1][7], frm[1][8]);

			rot = frm[0] + 9;
			rot2 = frm[1] + 9;
			gar_RotYXZsuperpack_I(&rot, &rot2, 0);

			/* The HD seam. Either the node's matrix goes to the HD path or
			 * its mesh goes into the buckets - never both. A node the HD
			 * model does not cover is not drawn at all: TROne tried a classic
			 * fallback there and removed it again because it left classic
			 * patches on an HD body (TROne-main, copy of 2026-10-06, commit
			 * 75c4c843, build_skinned_model).
			 *
			 * The matrix is recorded whether or not mesh_bits shows the node,
			 * because a hidden node still carries the ones below it - the
			 * mask reaches the model as a mask and hides its triangles
			 * there. */
			if (hd)
				HDObjectNode(0, true);
			else if (item->mesh_bits & 1)
			{
				if (item->meshswap_meshbits & 1)
					phd_PutPolygons_I(meshpp[1], clip);
				else
					phd_PutPolygons_I(meshpp[0], clip);
			}

			meshpp += 2;

			for (int i = 0; i < obj->nmeshes - 1; i++, bone += 4, meshpp += 2)
			{
				if (bone[0] & 1)
					phd_PopMatrix_I();

				if (bone[0] & 2)
					phd_PushMatrix_I();

				phd_TranslateRel_I(bone[1], bone[2], bone[3]);
				gar_RotYXZsuperpack_I(&rot, &rot2, 0);

				if (bone[0] & 0x1C)
				{
					if (bone[0] & 8)
						phd_RotY_I(*data++);

					if (bone[0] & 4)
						phd_RotX_I(*data++);

					if (bone[0] & 0x10)
						phd_RotZ_I(*data++);
				}

				bit <<= 1;

				if (hd)
					HDObjectNode(i + 1, true);
				else if (bit & item->mesh_bits)
				{
					if (bit & item->meshswap_meshbits)
						phd_PutPolygons_I(meshpp[1], clip);
					else
						phd_PutPolygons_I(meshpp[0], clip);
				}
			}
		}
		else
		{
			if (rootFix)
				phd_TranslateRel(root[0], root[1], root[2]);
			else
				phd_TranslateRel(frm[0][6], frm[0][7], frm[0][8]);

			rot = frm[0] + 9;
			gar_RotYXZsuperpack(&rot, 0);

			if (hd)
				HDObjectNode(0, false);
			else if (item->mesh_bits & 1)
			{
				if (item->meshswap_meshbits & 1)
					phd_PutPolygons(meshpp[1], clip);
				else
					phd_PutPolygons(meshpp[0], clip);
			}

			meshpp += 2;

			for (int i = 0; i < obj->nmeshes - 1; i++, bone += 4, meshpp += 2)
			{
				if (bone[0] & 1)
					phd_PopMatrix();

				if (bone[0] & 2)
					phd_PushMatrix();

				phd_TranslateRel(bone[1], bone[2], bone[3]);
				gar_RotYXZsuperpack(&rot, 0);

				if (bone[0] & 0x1C)
				{
					if (bone[0] & 8)
						phd_RotY(*data++);

					if (bone[0] & 4)
						phd_RotX(*data++);

					if (bone[0] & 0x10)
						phd_RotZ(*data++);
				}

				bit <<= 1;

				if (hd)
					HDObjectNode(i + 1, false);
				else if (bit & item->mesh_bits)
				{
					if (bit & item->meshswap_meshbits)
						phd_PutPolygons(meshpp[1], clip);
					else
						phd_PutPolygons(meshpp[0], clip);
				}
			}
		}
	}

	/* The item's node walk is over. HDObjectEnd works out the
	 * light out of the engine's own light lists and puts the item on the
	 * frame's list - an item walked for the second map alone without a
	 * light; with no walk above, no node was recorded and it drops the item
	 * again. Outside the walk's condition so that the bracket HDObjectBegin
	 * opened is closed on every road out of this function. */
	if (hd)
		HDObjectEnd();

	if (gLogEnabled)
	{
		aClipReportObj = -1;

		if (obj->intelligent && aMeshClipRejects > rejectsBefore)
		{
			extern float f_left, f_right, f_top, f_bottom;
			static long lastMesh[64];
			const long slotM = (long)(item - items) & 63;

			if (GlobalCounter - lastMesh[slotM] > 30 || GlobalCounter < lastMesh[slotM])
			{
				lastMesh[slotM] = GlobalCounter;
				Log("MESHLOG obj=%d item=%d anim=%d frame=%d clip=%d discarded=%d window=%d..%d/%d..%d",
					item->object_number, (int)(item - items), item->anim_number,
					item->frame_number, clip, (int)(aMeshClipRejects - rejectsBefore),
					(int)f_left, (int)f_right, (int)f_top, (int)f_bottom);
			}
		}
	}

	phd_right = phd_winwidth;
	phd_left = 0;
	phd_top = 0;
	phd_bottom = phd_winheight;
	phd_PopMatrix();
}

void PrintObjects(short room_number)
{
	ITEM_INFO* item;
	ROOM_INFO* r;
	OBJECT_INFO* obj;

	CurrentRoom = room_number;
	nPolyType = 1;
	r = &room[room_number];
	r->bound_active = 0;
	DrawStaticObjects(room_number);
	phd_PushMatrix();
	phd_TranslateAbs(r->x, r->y, r->z);
	phd_left = 0;
	phd_top = 0;
	phd_right = phd_winxmax + 1;
	phd_bottom = phd_winymax + 1;
	nPolyType = 2;

	for (int i = r->item_number; i != NO_ITEM; i = items[i].next_item)
	{
		ClipRoomNum = room_number;
		item = &items[i];
		obj = &objects[item->object_number];

		// SKIPLOG: a thinking enemy skipped here as ITEM_INVISIBLE never
		// reaches DrawAnimatingItem, so CLIPLOG cannot see it; this is the
		// only branch before it. One line per item at most every 30 frames,
		// thinkers only, with the anim its control code is playing. `data`
		// says whether the AI slot survives the invisibility - if it does, the
		// status change does not come from slot allocation.
		if (gLogEnabled && obj->intelligent && item->status == ITEM_INVISIBLE)
		{
			static long lastSkip[64];
			const long slot = i & 63;

			if (GlobalCounter - lastSkip[slot] > 30 || GlobalCounter < lastSkip[slot])
			{
				lastSkip[slot] = GlobalCounter;
				Log("SKIPLOG obj=%d item=%d anim=%d frame=%d state=%d goal=%d flags=%X data=%d tick=%d",
					item->object_number, i, item->anim_number, item->frame_number,
					item->current_anim_state, item->goal_anim_state,
					(unsigned)item->flags, item->data ? 1 : 0, GlobalCounter);
			}
		}

		if (item->status != ITEM_INVISIBLE)
		{
			if (item->after_death)
				GlobalAlpha = 0xFE000000 * item->after_death;

		//	if (gfLevelId == LVL5_BASE && item->object_number == BRIDGE_FLAT)
		//		don't draw

			if (gfLevelId != LVL5_BASE || item->object_number != BRIDGE_FLAT)
			{
				if (obj->draw_routine)
					obj->draw_routine(item);

				if (obj->draw_routine_extra)
					obj->draw_routine_extra(item);
			}

			GlobalAlpha = 0xFF000000;
		}

		/* The corpse fade is game state, so the draw only records that it
		 * saw the body and the logic tick does the counting
		 * (StepAfterDeathFade, below). The original code here was
		 *
		 *     if (item->after_death < 128 && item->after_death > 0 &&
		 *         !(wibble & 3))
		 *             item->after_death++;
		 *
		 *     if (item->after_death == 128)
		 *             KillItem(i);
		 *
		 * It counted per drawn frame, so at sixty every body would fade out
		 * in half the time. And it called KillItem inside the walk of the
		 * room's item list: for a spawned item (item_num >= level_items)
		 * KillItem ends with `item->next_item = next_item_free;
		 * next_item_free = item_num;` (game\items.cpp), rewriting the very
		 * link the loop follows next, so the walk would wander into the free
		 * list, drawing and ageing whatever it finds there. On the tick,
		 * after the walk, the kill cannot do that.
		 *
		 * `!(wibble & 3)` is always true: ControlPhase steps `wibble =
		 * (wibble + 4) & 0xFC` (game\control.cpp), so the low two bits are
		 * always zero. It was evidently meant to make the fade a quarter as
		 * fast (seventeen seconds instead of 4.3); it is dropped rather than
		 * repaired, because repairing it would change how long every body
		 * lies there.
		 *
		 * Only bodies in a room that is actually drawn age, as in the
		 * original; that is why the draw marks and the tick does not sweep
		 * all slots - a corpse in a room the camera never sees stays until
		 * the player comes back.
		 *
		 * The mark is `after_death > 0`, wider than the increment, because
		 * the original kill test also fired on a body already at 128 without
		 * being counted up to it: the skeleton writes the field directly
		 * (`item->after_death = (short)(phd_sqrt(info.distance) >> 4)`,
		 * game\skeleton.cpp). StepAfterDeathFade carries both tests over
		 * unchanged. */
		if (item->after_death > 0)
			g_afterDeathSeen[i] = 1;
	}

	nPolyType = 3;

	for (int i = r->fx_number; i != NO_ITEM; i = effects[i].next_fx)
		DrawEffect(i);

	phd_PopMatrix();
	r->left = phd_winxmax;
	r->top = phd_winymax;
	r->right = 0;
	r->bottom = 0;
}

/* The item loop's body above for one item, drawn as a mirror's reflection.
 *
 * The same skips as the loop - an invisible item, the BRIDGE_FLAT of the
 * base, a slot with no draw routine (which is Lara's) - and the same draw,
 * less draw_routine_extra (a gun flash, a lens flare): effects are not
 * reflected. Only the clip room differs: the reflection is seen through the
 * glass of the mirror's room and not through the item's own room, so
 * ClipRoomNum is that room, and calc_animating_item_clip_window gives the
 * doors and the ANIMATING slots its window. The call comes from DrawRooms
 * before the rooms' objects are printed, while that window is still the one
 * GetRoomBounds gave it this frame - PrintObjects empties it at its end. The
 * view is reflected by the caller, and the caller sets the classic clipper's
 * rectangle f_left .. f_bottom to the copy's rectangle on the screen: that
 * rectangle stands in for PrintObjects' whole screen, and it is what
 * calc_animating_item_clip_window keeps when a door of the room leads to the
 * camera's room. DrawAnimatingItem puts the whole screen back at its end, so
 * it is set per item. */
void PrintObjectReflection(ITEM_INFO* item, short mirror_room)
{
	OBJECT_INFO* obj;
	ROOM_INFO* r;

	obj = &objects[item->object_number];

	if (item->status == ITEM_INVISIBLE || !obj->draw_routine ||
		(gfLevelId == LVL5_BASE && item->object_number == BRIDGE_FLAT))
		return;

	r = &room[mirror_room];
	CurrentRoom = mirror_room;
	nPolyType = 2;
	phd_PushMatrix();
	phd_TranslateAbs(r->x, r->y, r->z);
	phd_left = (long)f_left;
	phd_top = (long)f_top;
	phd_right = (long)f_right;
	phd_bottom = (long)f_bottom;
	ClipRoomNum = mirror_room;

	if (item->after_death)
		GlobalAlpha = 0xFE000000 * item->after_death;

	obj->draw_routine(item);
	GlobalAlpha = 0xFF000000;
	phd_PopMatrix();
}

bool RoomOnDrawList(short room_number)
{
	for (int i = 0; i < number_draw_rooms; i++)
		if (draw_rooms[i] == room_number)
			return true;

	return false;
}

/* The corpse fade, on the logic tick.
 *
 * The other half of the mark PrintObjects leaves. Called once per tick from
 * ControlPhase, so a body takes the same 4.3 seconds to vanish at sixty
 * frames a second as at thirty; and after the room walk rather than inside
 * it, which makes KillItem safe (the note in PrintObjects gives the reasons).
 *
 * The two tests are the original ones word for word, including the `< 128`
 * that stops the count and the `== 128` that kills; only when they run
 * differs. The always-true `!(wibble & 3)` is dropped, which changes nothing.
 *
 * The marks are cleared as they are read, so a body whose room is not drawn
 * for a while does not age, as in the original. */
void StepAfterDeathFade()
{
	for (int i = 0; i < MAX_ITEMS; i++)
	{
		if (!g_afterDeathSeen[i])
			continue;

		g_afterDeathSeen[i] = 0;

		ITEM_INFO* item = &items[i];

		if (item->after_death < 128 && item->after_death > 0)
		{
			item->after_death++;
			InterpNoteStep(INTERP_STEP_AFTERDEATH);
		}

		if (item->after_death == 128)
			KillItem((short)i);
	}
}

/* ROOMLIST's window events: bit 1 a WM_INACTIVE, bit 2 a WM_ACTIVE. Set by
 * the window procedure (specific\winmain.cpp) with -log only, read and
 * cleared by the whole-list dump in NoteRoomWalk, which writes the list on
 * the first frame drawn after each of them. The window procedure runs on
 * another thread than the game, hence the interlocked access. */
volatile long RoomListWindowEvents = 0;

/* ROOMFLASH - a room that is in the draw list, gone for one frame, and back.
 * Behind -log.
 *
 * Such a hole lasts one frame out of sixty, and the per-frame HDLOG line is
 * throttled to one a second, so it would almost never be caught there. A
 * missing room has a shape no throttle can miss, though: it is in the list on
 * the frame before and on the frame after and not on the one in between.
 *
 * So three frames of the room list are kept - three notes of about 4.7 KiB
 * each at draw_rooms' full capacity - and the middle one is judged as soon as
 * the third arrives. Rooms in both neighbours and not in the middle are
 * named. The test needs no knowledge of portals, cameras or geometry: a room
 * the walk sees on either side of a frame and not on it is a hole, whatever
 * made it.
 *
 * The ROOMWALK line beside it says how often the walk started from the
 * blended eye's room instead of the logic camera's (`corrections=N of M
 * tween frames`, see the note in DrawRooms), so a flash with corrections=0
 * points elsewhere. The shape is a hole, not a cause: a portal seen exactly
 * edge-on for one frame, or a door closing, leaves the same three-frame trace
 * at thirty as well as at sixty, so a ROOMFLASH line is judged with its own
 * `tween` and `corrected` columns. */
namespace
{
	struct RoomWalkNote
	{
		unsigned int serial;
		int level;
		int count;
		/* As wide as draw_rooms itself: NoteRoomWalk clamps `count` to it,
		 * and a narrower note would truncate the list silently and make
		 * ROOMFLASH report the rooms past its end as missing on every frame.
		 * The three notes are static and copied only while -log is on. */
		short rooms[kDrawRoomsMax];

		/* Each of those rooms' own window, as the walk left it.
		 * PrintObjects wipes all four at the end of the frame, so a frame
		 * that is judged one frame later has to keep them. This is
		 * `left..bottom`, the union of every window a room was dequeued
		 * with, which is the widest of them - so the reason worked out from
		 * it below is the conservative one: it can say "reached" where the
		 * walk said "empty", never the other way round. */
		short win[kDrawRoomsMax][4];

		/* The rectangle the reach gave each of them, so that a ROOMLIST
		 * line can say which rooms are in the picture on the strength of
		 * the reach alone - those whose window is empty and whose reach is
		 * not. */
		short reach[kDrawRoomsMax][4];
		int reach_only;			// how many of them there are
		int trimmed;			// and how many the budget dropped

		short logic;
		short eye;
		short prev;
		long x, y, z;			// the logic camera

		/* The eye the walk really used. On a tween frame of the sixty
		 * hertz cadence this is the blended eye, and camera.pos is half a
		 * step behind it. */
		long wx, wy, wz;
		float mx[indices_count];	// aW2VMx, for the reason below

		long tx, ty, tz;		// camera.target
		short troom;
		int camtype, camold, camfixed, camnumber;

		float alpha;
		int tween;
		int corrected;
	};

	// [0] is the oldest of the three, [2] the one just walked.
	RoomWalkNote g_walkNote[3];
	int g_walkHeld = 0;
	unsigned int g_walkSerial = 0;

	/* Rooms per frame, classic and reach, over the second the ROOMWALK
	 * line covers. `classic` here is the count of listed rooms that have a
	 * window of their own - the list the walk would have had without the
	 * reach - and `reach` the ones that have none. */
	long g_walkRoomsSum = 0;
	long g_walkReachSum = 0;
	long g_walkCarriedSum = 0;	// of the reach's, those CarryArt took in
	int g_walkRoomsFrames = 0;
	int g_walkRoomsMax = 0;
	int g_walkClassicMax = 0;
	int g_walkReachMax = 0;
	int g_reach_trimmed_second = 0;
	int g_reach_noart_second = 0;

	// Per second, for the throttled line.
	int g_walkTweens = 0;
	int g_walkCorrections = 0;
	int g_walkFlashes = 0;
	int g_walkPops = 0;
	unsigned short g_walkLineAt = 0;
	bool g_walkLineEver = false;

	/* ROOMLIST: the membership of draw_rooms every time it changes, and
	 * the whole of it with each room's window and distance every five
	 * seconds.
	 *
	 * Volume, measured with RoomProbe -walk over a full turn on the spot
	 * at one degree a frame (sixty degrees a second at sixty frames) from
	 * JOBY2 room 130 and ANDREA2 room 210: 16 % of frames see a change,
	 * 0.24 and 0.33 rooms added-or-removed per frame. That is about ten
	 * lines a second while the camera swings hard and none while it is
	 * still - at 110 bytes a line, about 640 KiB for ten minutes of hard
	 * panning. The cap below keeps even a change on every frame under
	 * 2 MiB in ten minutes: 20 lines a second is 1.3 MiB, and the five
	 * second dump of a forty room list another 340 KiB. */
	const int kListLines = 20;
	const int kListDumpSeconds = 5;
	int g_listLines = 0;
	unsigned short g_listDumpAt = 0;
	bool g_listEver = false;

	/* ROOMPOP: a room that joins the draw list while it is a long way
	 * off. See the note over the report below. Four
	 * lines a second at most - a walk through Rome joins a room every few
	 * frames and the ones that matter are the far ones. */
	const long kPopFar = 8192;		// eight sectors
	const int kPopLines = 4;

	// How far the camera is from a room's box, 0 inside it.
	long RoomDistance(const ROOM_INFO* r, long x, long y, long z)
	{
		const long x0 = r->x;
		const long x1 = r->x + (r->y_size << 10);
		const long z0 = r->z;
		const long z1 = r->z + (r->x_size << 10);
		const long dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0);
		const long dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0);
		const long dy = y < r->maxceiling ? r->maxceiling - y
			: (y > r->minfloor ? y - r->minfloor : 0);
		const double d = (double)dx * dx + (double)dy * dy + (double)dz * dz;

		return (long)sqrt(d);
	}

	bool WalkHolds(const RoomWalkNote& note, short which)
	{
		for (int i = 0; i < note.count; i++)
			if (note.rooms[i] == which)
				return true;

		return false;
	}

	/* What refused a room.
	 *
	 * The walk is arithmetic on the level's own portal records, and a
	 * line that says a room is missing is only useful if it says which
	 * piece of that arithmetic dropped it. So the walk's tests are worked
	 * out again here, from the note - the eye, the view matrix and every
	 * listed room's own window - and the walk itself is not touched, read
	 * or slowed: this runs only on the frame a ROOMFLASH is being
	 * written, and only under -log.
	 *
	 * The four answers are the four places a portal to a room that is
	 * not in the list can have been dropped:
	 *
	 *   side     the eye is on the far side of the portal's own plane -
	 *            `if (x && (x ^ p->normal[0]) < 0)` in GetRoomBounds
	 *   toonear  all four corners fell behind the eye, `tooNear == 4`
	 *   empty    the rectangle came out with no width or no height
	 *            after the clamp against the room it was seen through
	 *   noportal no portal to it from any listed room. This is what the
	 *            remaster's art looks like from the inside when it opens
	 *            a wall the classic level keeps shut.
	 *
	 * SetRoomBounds' fifth exit, the early out at its top where the room's
	 * window already covers what this portal would give it, is not tested
	 * for: it only fires for a room that is already in the list, and this
	 * runs on missing rooms alone. RoomProbe -walk, which judges every
	 * portal, reports it as `covered`.
	 *
	 * It is conservative by construction (see the note over `win`): the
	 * window it clamps against is the union of every window the room was
	 * dequeued with, so it can answer "reached" where the walk answered
	 * "empty" and never the other way round. */
	void WhyRoomMissing(const RoomWalkNote& note, short which,
		char* out, int room_for)
	{
		int at = 0;
		int found = 0;

		out[0] = 0;

		for (int i = 0; i < note.count && at < room_for - 48; i++)
		{
			const short from = note.rooms[i];
			const ROOM_INFO* r;
			short* door;
			float mx[indices_count];
			long drn;

			if (from < 0 || from >= number_rooms)
				continue;

			r = &room[from];
			door = r->door;

			if (!door)
				continue;

			// aTranslateAbs (specific\3dmath.cpp) for this room.
			{
				const float fx = (float)r->x - note.mx[M03];
				const float fy = (float)r->y - note.mx[M13];
				const float fz = (float)r->z - note.mx[M23];

				for (int k = 0; k < indices_count; k++)
					mx[k] = note.mx[k];

				mx[M03] = fx * mx[M00] + fy * mx[M01] + fz * mx[M02];
				mx[M13] = fx * mx[M10] + fy * mx[M11] + fz * mx[M12];
				mx[M23] = fx * mx[M20] + fy * mx[M21] + fz * mx[M22];
			}

			/* Sixteen, not the fifteen GetRoomBounds steps: a ROOM_PORTAL
			 * is sixteen shorts, and the walk's own loop steps past `rn`
			 * inside its body while this one reads the portal record where it
			 * stands. calc_animating_item_clip_window counts the same way,
			 * corners at door[4] and all. */
			for (drn = *door++; drn > 0; drn--, door += 16)
			{
				ROOM_PORTAL* p = (ROOM_PORTAL*)door;
				const short* corner = door + 4;
				const char* why = 0;
				double a = 0, b = 0, c = 0, d = 0;
				float vx[4], vy[4], vz[4];
				float tooNear = 0, tL, tR, tT, tB;
				// The portal's own projection, the reach's rectangle.
				float rawL = 1e30f, rawR = -1e30f, rawT = 1e30f, rawB = -1e30f;

				if (p->rn != which)
					continue;

				found++;

				// The side test, all three axes, exactly as the walk asks it.
				if (p->normal[0])
				{
					const long x = r->x + p->v1[0] - note.wx;

					if (!(x && (x ^ p->normal[0]) < 0))
					{
						why = "side";
						a = (double)x;
						b = p->normal[0];
					}
				}

				if (!why && p->normal[1])
				{
					const long y = r->y + p->v1[1] - note.wy;

					if (!(y && (y ^ p->normal[1]) < 0))
					{
						why = "side";
						a = (double)y;
						b = p->normal[1];
					}
				}

				if (!why && p->normal[2])
				{
					const long z = r->z + p->v1[2] - note.wz;

					if (!(z && (z ^ p->normal[2]) < 0))
					{
						why = "side";
						a = (double)z;
						b = p->normal[2];
					}
				}

				// SetRoomBounds on the note's own numbers.
				if (!why)
				{
					tL = (float)note.win[i][2];
					tR = (float)note.win[i][0];
					tB = (float)note.win[i][1];
					tT = (float)note.win[i][3];

					for (int k = 0; k < 4; k++, corner += 3)
					{
						float x, y, z;

						vx[k] = mx[M00] * corner[0] + mx[M01] * corner[1] +
							mx[M02] * corner[2] + mx[M03];
						vy[k] = mx[M10] * corner[0] + mx[M11] * corner[1] +
							mx[M12] * corner[2] + mx[M13];
						vz[k] = mx[M20] * corner[0] + mx[M21] * corner[1] +
							mx[M22] * corner[2] + mx[M23];
						x = vx[k];
						y = vy[k];
						z = vz[k];

						if (z <= 0)
							tooNear++;
						else
						{
							z /= f_mpersp;

							if (z)
							{
								x = x / z + f_centerx;
								y = y / z + f_centery;
							}
							else
							{
								// SetRoomBounds' own degenerate branch. phd_left and
								// phd_right are still what DrawRooms set them to when
								// this note is read - 0 and phd_winxmax - and nothing
								// between the walk and here touches them.
								x = x < 0 ? 0.0f : (float)phd_winxmax;
								y = y < 0 ? 0.0f : (float)phd_winymax;
							}

							if (x - 1 < tL) tL = x - 1;
							if (x + 1 > tR) tR = x + 1;
							if (y - 1 < tT) tT = y - 1;
							if (y + 1 > tB) tB = y + 1;

							// The same four without the parent's window.
							if (x - 1 < rawL) rawL = x - 1;
							if (x + 1 > rawR) rawR = x + 1;
							if (y - 1 < rawT) rawT = y - 1;
							if (y + 1 > rawB) rawB = y + 1;
						}
					}

					if (tooNear == 4)
					{
						why = "toonear";
					}
					else
					{
						if (tooNear > 0)
							for (int k = 0, last = 3; k < 4; last = k, k++)
							{
								if ((vz[last] <= 0) == (vz[k] <= 0))
									continue;

								if (vx[k] < 0 && vx[last] < 0)
									tL = 0;
								else if (vx[k] > 0 && vx[last] > 0)
									tR = (float)phd_winxmax;
								else
								{
									tL = 0;
									tR = (float)phd_winxmax;
								}

								if (vy[k] < 0 && vy[last] < 0)
									tT = 0;
								else if (vy[k] > 0 && vy[last] > 0)
									tB = (float)phd_winymax;
								else
								{
									tT = 0;
									tB = (float)phd_winymax;
								}
							}

						if (tL < note.win[i][0]) tL = note.win[i][0];
						if (tR > note.win[i][2]) tR = note.win[i][2];
						if (tT < note.win[i][1]) tT = note.win[i][1];
						if (tB > note.win[i][3]) tB = note.win[i][3];

						why = (tL >= tR || tT >= tB) ? "empty" : "reached";
						a = tL;
						b = tT;
						c = tR;
						d = tB;

						/* What the reach makes of the same portal - its own
						 * projection against the screen, with the parent's
						 * window left out:
						 *
						 *   offscreen   the portal is not on the screen at
						 *               all; the reach refuses it too, and
						 *               the room really is not visible.
						 *   reach       the classic window came out empty and
						 *               the reach carries the room - so with
						 *               the reach on, a room that is missing
						 *               for this reason was dropped by the
						 *               budget or the cap, not by the walk.
						 *   reached     both rules have it. */
						if (g_reach_on)
						{
							float aL = rawL, aR = rawR, aT = rawT, aB = rawB;

							if (tooNear > 0)
							{
								// The fixup already widened tL..tB to the
								// screen edges; the reach takes the same.
								if (tL <= 0) aL = 0;
								if (tR >= (float)phd_winxmax) aR = (float)phd_winxmax;
								if (tT <= 0) aT = 0;
								if (tB >= (float)phd_winymax) aB = (float)phd_winymax;
							}

							if (aL < 0) aL = 0;
							if (aR > (float)phd_winxmax) aR = (float)phd_winxmax;
							if (aT < 0) aT = 0;
							if (aB > (float)phd_winymax) aB = (float)phd_winymax;

							if (aL >= aR || aT >= aB)
								why = "offscreen";
							else if (tL >= tR || tT >= tB)
							{
								why = "reach";
								a = aL;
								b = aT;
								c = aR;
								d = aB;
							}
						}
					}
				}

				at += sprintf(&out[at], "%sfrom=%d %s[%.0f %.0f %.0f %.0f]",
					at ? " " : "", (int)from, why, a, b, c, d);
				break;
			}
		}

		if (!found)
			sprintf(out, "noportal");
	}
}

/* The walk's reach state, wiped and seeded.
 *
 * Called from DrawRooms and RenderIt - the two places that start a walk -
 * and only while g_reach_on. The root gets both answers: the whole screen as
 * its window, which the caller has just set, and its one reach visit, so
 * that no portal can come back and hand it a second. */
static void ReachBeginWalk(short seed)
{
	memset(g_reach_state, 0, sizeof(g_reach_state));
	g_reach_added = 0;
	g_reach_trimmed = 0;
	g_reach_noart = 0;
	g_reach_carried = 0;

	if (seed >= 0 && seed < 256)
	{
		g_reach_state[seed] = REACH_CLASSIC | REACH_SEEN;
		g_reach_box[seed][0] = 0;
		g_reach_box[seed][1] = 0;
		g_reach_box[seed][2] = (short)phd_winxmax;
		g_reach_box[seed][3] = (short)phd_winymax;
	}
}

/* The safety valve on the reach, with a measured budget.
 *
 * A frame the reach fills with a hundred rooms is a frame of a hundred HD
 * draw calls, and it happens: with the eye at the middle of every room of a
 * level in turn and a fan of forty directions from each (RoomProbe
 * -reachsurvey, 2560x1440, yaw every 45 degrees and pitch -30 to +30 every
 * 15 - 280 to 10,200 frames a level), the reach passes 100 rooms in 0.6 % of
 * ANDREA1's frames, 0.4 % of JOBY2's and 2.0 % of JOBY4's. The share of
 * frames over a budget of 32 / 40 / 48 / 56 / 64 / 80 / 96 / 112 / 128 rooms:
 *
 *   ANDREA1  13.7  9.6  7.0  5.3  4.0  1.8  0.7  0.4  0.1 %
 *   ANDREA2  13.3  9.7  7.0  5.2  3.5  1.2  0.3    0    0 %
 *   JOBY2    24.9 18.9 13.5 10.1  7.8  3.1  0.8    0    0 %
 *   JOBY4    25.9 18.7 13.9 10.7  8.2  4.7  2.4  0.6    0 %
 *   ANDY1    14.2  9.4  5.7  3.5  1.9  0.4    0    0    0 %
 *   ANDY3     7.7  3.8  1.5  0.3    0    0    0    0    0 %
 *   TITLE     3.9  1.5    0    0    0    0    0    0    0 %
 *   JOBY3     3.3  1.4  1.0  0.7  0.4  0.1    0    0    0 %
 *   RICH3     2.7  1.0  0.5  0.3  0.1    0    0    0    0 %
 *   JOBY5     2.5  0.9  0.6  0.6  0.3    0    0    0    0 %
 *   ANDREA3   1.8  0.3    0    0    0    0    0    0    0 %
 *   RICH1     1.8  0.7  0.3  0.1    0    0    0    0    0 %
 *   ANDY2     1.3  0.5  0.1    0    0    0    0    0    0 %
 *   RICH2 and RICHCUT2 are 0 in every column.
 *
 * Cost at a budget of 64: with up to 754 HD draws, FPSLOG on a 165 Hz panel
 * reads real_fps 165.0, avg_ms 6.1, late=0 - display-bound, the frame's work
 * about 6.0 ms of a 6.1 ms budget of which 5.35 is the flip. But 64 is too
 * tight: in JOBY2 and in the courtyard of Trajan's Markets the list sits at
 * 64 for seconds at a time with up to 30 reach-only rooms dropped per frame,
 * and distant art pops (ANDREA2 rooms 230, 47 and 229).
 *
 * At 128 only ANDREA1's 0.1 % of frames still hit the valve. JOBY2's eye at
 * 54410/-5104/43819 looking 216.4/8.0 walks 66 rooms (37 classic, 29 the
 * reach's) and ANDREA2's at 86922/-1231/43587 looking 271.3/15.9 walks 75
 * (39 and 36): at 64 the valve drops 2 and 11 rooms there, at 128 none. The
 * cost of a 128-room frame is not measured (the 6.1 ms above is a frame of
 * 64); if such frames prove too expensive, this constant is the one place to
 * lower it.
 *
 * The valve only drops rooms the reach brought in, farthest first; a room
 * with a classic window of its own is one the classic walk draws and is
 * never trimmed, which is why JOBY4's classic frames of 72 and 106 rooms are
 * untouched by this.
 *
 * The room's bound_active goes back to zero with it, because PrintObjects -
 * which is what clears that byte at the end of a drawn room - will not run
 * for a room that is no longer in the list. */
static const long kReachBudget = 128;

/* The rooms whose art stands under the sky of a room the walk listed with a
 * classic window (hd_level.h, HDRoomCarries; hd_render.h, HDCarryBoxHolds
 * says which art), taken in as rooms with no window of their own, with that
 * room's reach. The walk lists a room by its own portals, and the art of a
 * room can stand far from them: ANDREA2's brick tower of room 222 stands
 * over room 232, beside the alley at the church gate, while 222's one way
 * in is a portal of room 224 at the screen's edge - two pixels of it on
 * the screen, none a few degrees further round, so that without this the
 * tower comes and goes with that portal as the camera swings. Run
 * after the walk and before the trim, which may take them out again like
 * any room the reach brought; their portals are not followed. */
static void CarryArt()
{
	const long listed = number_draw_rooms;

	for (long i = 0; i < listed; i++)
	{
		const short which = draw_rooms[i];
		const ROOM_INFO* a = &room[which];
		const short* carried = 0;

		if (a->left >= a->right || a->top >= a->bottom)
			continue;

		const int count = HDRoomCarries(which, &carried);

		for (int k = 0; k < count; k++)
		{
			// The room drawing that art in this flip state, if any.
			const int b = HDCarriedRoom(carried[k]);

			if (b < 0 || b >= number_rooms || (room[b].bound_active & 1))
				continue;

			// The draw_rooms guard of GetRoomBounds.
			if (number_draw_rooms >= (long)(sizeof(draw_rooms) /
				sizeof(draw_rooms[0])))
			{
				g_reach_capped++;
				return;
			}

			draw_rooms[number_draw_rooms] = (short)b;
			number_draw_rooms++;
			room[b].bound_active |= 1;

			if (b < 256 && which < 256)
			{
				g_reach_state[b] |= REACH_SEEN;
				g_reach_box[b][0] = g_reach_box[which][0];
				g_reach_box[b][1] = g_reach_box[which][1];
				g_reach_box[b][2] = g_reach_box[which][2];
				g_reach_box[b][3] = g_reach_box[which][3];
			}

			g_reach_carried++;
		}
	}
}

/* Takes off the list every reach-only room whose place the art does not draw
 * (the paragraph in ReachTrimRooms says why) and returns how many it took. */
static int ReachDropArtless()
{
	int dropped = 0;

	for (int i = 0; i < number_draw_rooms; )
	{
		const short which = draw_rooms[i];
		const ROOM_INFO* rr = &room[which];

		if ((rr->left >= rr->right || rr->top >= rr->bottom) &&
			!HDRoomArtListed(which, draw_rooms, (int)number_draw_rooms))
		{
			room[which].bound_active = 0;

			for (int k = i; k + 1 < number_draw_rooms; k++)
				draw_rooms[k] = draw_rooms[k + 1];

			number_draw_rooms--;
			g_reach_noart++;
			dropped++;
			continue;
		}

		i++;
	}

	return dropped;
}

static void ReachTrimRooms(long ex, long ey, long ez)
{
	int trimmed = 0;

	/* First, the rooms the reach cannot help. What a reach-only room shows
	 * is its HD art: its classic geometry is clipped away by its own empty
	 * window - `phd_left = r->left` in DrawStaticObjects and `clip_left =
	 * r->left` in InsertRoom (specific\drawroom.cpp) both reject everything
	 * when left > right - so a room the .TRG has nothing for would be in the
	 * list with nothing of itself in the picture, and only its items would
	 * be drawn (PrintObjects hands every item the whole screen), leaving
	 * enemies standing in a room that is not there. Such a room comes
	 * straight back out of the list. A room drawn wholly as another group's
	 * art stays while that group is listed, because then its place is in the
	 * picture and its items belong there (hd_level.h, HDRoomArtListed).
	 *
	 * This also keeps the reach from touching the classic picture at all:
	 * everything it adds is .TRG art. */
	ReachDropArtless();

	g_reach_added = 0;

	for (int i = 0; i < number_draw_rooms; i++)
	{
		const ROOM_INFO* rr = &room[draw_rooms[i]];

		if (rr->left >= rr->right || rr->top >= rr->bottom)
			g_reach_added++;
	}

	while (number_draw_rooms > kReachBudget)
	{
		int worst = -1;
		long worstDist = -1;

		for (int i = 0; i < number_draw_rooms; i++)
		{
			const ROOM_INFO* rr = &room[draw_rooms[i]];
			long d;

			// A classic window means the classic walk draws it too.
			if (rr->left < rr->right && rr->top < rr->bottom)
				continue;

			d = RoomDistance(rr, ex, ey, ez);

			if (d > worstDist)
			{
				worstDist = d;
				worst = i;
			}
		}

		if (worst < 0)
			break;

		room[draw_rooms[worst]].bound_active = 0;

		for (int i = worst; i + 1 < number_draw_rooms; i++)
			draw_rooms[i] = draw_rooms[i + 1];

		number_draw_rooms--;
		g_reach_trimmed++;
		g_reach_added--;
		trimmed++;
	}

	/* The budget can trim the carrier of a room the first pass kept for
	 * being drawn wholly as that carrier's art; the room's place would then
	 * be drawn by nothing while its items still are. Asked again after a
	 * trim, such a room comes off too. A room taken here is never a carrier
	 * itself - a carrier is an HD room, which the art always draws - so one
	 * more pass leaves no such room behind. Without a trim the list is the
	 * one the first pass settled and nothing is asked. */
	if (trimmed > 0)
		g_reach_added -= ReachDropArtless();
}

/* The visibility path's start of a walk, called beside the reach's: on with
 * a level built with it and HD drawn (HDVisWalkOn), and then the reach is
 * off - the three tables take its place. */
static void VisBeginWalk()
{
	g_vis_walk = HDVisWalkOn();

	if (!g_vis_walk)
		return;

	g_reach_on = false;
	memset(g_vis_why, 0xff, sizeof(g_vis_why));
	memset(g_vis_count, 0, sizeof(g_vis_count));
	g_vis_dequeues = 0;
	g_vis_edge = (float)(HDVisDefaults().edge * (double)(phd_winxmax + 1));
}

/* VISLOG, with -log, once a second and whenever the eye's room changes,
 * on the visibility path:
 *
 *   VISLOG frame=<walk serial> eye_room=<n> rooms=<listed> walk=<n>
 *          guest=<n> opening=<n> wide=<n> dequeues=<n> list=<room><why>,...
 *
 * walk the rooms the portals queued first (and the eye's room), guest,
 * opening and wide those the three tables queued first, dequeues the walk's
 * whole work; the list every listed room with the letter of its source:
 * w portals, g guest, o opening, p wide portal, e a portal at the window's
 * edge, counted again in edge= at the line's end. A table that only widens
 * the window of a room already queued is credited nothing, and the rooms
 * the wider window lets the portals reach read w. */
static void VisEndWalk(short walkRoom)
{
	static DWORD last = 0;
	static short lastRoom = -1;

	if (!g_vis_walk || !gLogEnabled)
		return;

	const DWORD now = GetTickCount();

	if (now - last < 1000 && walkRoom == lastRoom)
		return;

	last = now;
	lastRoom = walkRoom;

	char list[1024];
	int at = 0;
	static const char kWhy[5] = { 'w', 'g', 'o', 'p', 'e' };

	list[0] = 0;

	for (int i = 0; i < number_draw_rooms && at < (int)sizeof(list) - 16; i++)
	{
		const int r = draw_rooms[i];
		const int why = r >= 0 && r < 256 && g_vis_why[r] < 5 ?
			g_vis_why[r] : 0;

		at += sprintf(&list[at], "%s%d%c", i ? "," : "", r, kWhy[why]);
	}

	Log("VISLOG frame=%u eye_room=%d rooms=%ld walk=%ld guest=%d opening=%d "
		"wide=%d dequeues=%ld list=%s edge=%d", g_walkSerial, (int)walkRoom,
		number_draw_rooms, number_draw_rooms - g_vis_count[1] - g_vis_count[2] -
		g_vis_count[3] - g_vis_count[4], g_vis_count[1], g_vis_count[2],
		g_vis_count[3], g_vis_dequeues, list, g_vis_count[4]);
}

static void NoteRoomWalk(short logicRoom, short walkRoom)
{
	if (!gLogEnabled)
		return;

	if (InterpIsTweenFrame())
		g_walkTweens++;

	if (walkRoom != logicRoom)
		g_walkCorrections++;

	RoomWalkNote& note = g_walkNote[2];

	g_walkNote[0] = g_walkNote[1];
	g_walkNote[1] = g_walkNote[2];

	note.serial = g_walkSerial++;
	note.level = (int)gfLevelId;
	note.count = (int)number_draw_rooms;

	if (note.count > (int)(sizeof(note.rooms) / sizeof(note.rooms[0])))
		note.count = (int)(sizeof(note.rooms) / sizeof(note.rooms[0]));

	for (int i = 0; i < note.count; i++)
	{
		note.rooms[i] = draw_rooms[i];

		/* The window the walk left that room with, because PrintObjects
		 * wipes all four before this note is read. */
		{
			const ROOM_INFO* rr = &room[draw_rooms[i]];

			note.win[i][0] = rr->left;
			note.win[i][1] = rr->top;
			note.win[i][2] = rr->right;
			note.win[i][3] = rr->bottom;

			if (draw_rooms[i] >= 0 && draw_rooms[i] < 256)
				for (int k = 0; k < 4; k++)
					note.reach[i][k] = g_reach_box[draw_rooms[i]][k];
		}
	}

	/* The reach rectangles, for the HD path's reachart line: HDFrameRoom is
	 * handed each room's classic window alone. */
	if (gLogEnabled)
		for (int i = 0; i < note.count; i++)
			HDFrameNoteReach(note.rooms[i], note.reach[i]);

	note.logic = logicRoom;
	note.eye = walkRoom;
	note.prev = InterpPrevCamRoom();
	note.x = camera.pos.x;
	note.y = camera.pos.y;
	note.z = camera.pos.z;

	/* The eye the walk really used, and the matrix it used it with.
	 * w2v_matrix M03/M13/M23 are the camera position and not a
	 * translation term (specific\\3dmath.cpp), which is why the
	 * side test in GetRoomBounds can subtract them straight off a
	 * portal corner. */
	note.wx = w2v_matrix[M03];
	note.wy = w2v_matrix[M13];
	note.wz = w2v_matrix[M23];

	for (int i = 0; i < indices_count; i++)
		note.mx[i] = aW2VMx[i];

	note.tx = camera.target.x;
	note.ty = camera.target.y;
	note.tz = camera.target.z;
	note.troom = camera.target.room_number;
	note.camtype = (int)camera.type;
	note.camold = (int)camera.old_type;
	note.camfixed = (int)camera.fixed_camera;
	note.camnumber = (int)camera.number;
	note.alpha = InterpFrameAlpha();
	note.tween = InterpIsTweenFrame() ? 1 : 0;
	note.corrected = walkRoom != logicRoom ? 1 : 0;
	note.reach_only = g_reach_added;
	note.trimmed = g_reach_trimmed;

	/* The frame's own cost, for the once-a-second line. The rooms of the
	 * list are the rooms the HD path draws - it has no visibility test of
	 * its own. */
	g_walkRoomsSum += note.count;
	g_walkRoomsFrames++;
	g_walkReachSum += g_reach_added;
	g_walkCarriedSum += g_reach_carried;
	g_reach_trimmed_second += g_reach_trimmed;
	g_reach_noart_second += g_reach_noart;

	if (note.count > g_walkRoomsMax)
		g_walkRoomsMax = note.count;

	if (note.count - g_reach_added > g_walkClassicMax)
		g_walkClassicMax = note.count - g_reach_added;

	if (g_reach_added > g_walkReachMax)
		g_walkReachMax = g_reach_added;

	if (g_walkHeld < 3)
		g_walkHeld++;

	/* The middle frame is the one judged, and only while the three really
	 * are three consecutive frames of one level - a level change, a savegame
	 * or a menu leaves a note behind that is no neighbour of anything. */
	if (g_walkHeld >= 3 &&
		g_walkNote[0].level == g_walkNote[2].level &&
		g_walkNote[1].level == g_walkNote[2].level &&
		g_walkNote[1].serial == g_walkNote[0].serial + 1 &&
		g_walkNote[2].serial == g_walkNote[1].serial + 1)
	{
		char missing[128];
		int at = 0;
		int found = 0;

		missing[0] = 0;

		for (int i = 0; i < g_walkNote[0].count; i++)
		{
			const short which = g_walkNote[0].rooms[i];

			if (WalkHolds(g_walkNote[1], which) || !WalkHolds(g_walkNote[2], which))
				continue;

			found++;

			if (at < (int)sizeof(missing) - 8)
				at += sprintf(&missing[at], at ? ",%d" : "%d", (int)which);
		}

		if (found)
		{
			const RoomWalkNote& bad = g_walkNote[1];

			g_walkFlashes++;

			/* The walk eye beside the logic one (on a tween frame
			 * camera.pos is half a step behind the eye the walk used), the
			 * camera state that chose it and what it was aimed at, so a
			 * flash under a fixed camera or on the frame of a cut can be
			 * told apart. */
			Log("ROOMFLASH frame=%u tween=%d alpha=%.2f logic_room=%d "
				"eye_room=%d prev_cam_room=%d eye=(%d,%d,%d) "
				"walk_eye=(%d,%d,%d) target=(%d,%d,%d) target_room=%d "
				"cam=%d/%d fixed=%d number=%d rooms=%d missing=%s "
				"corrected=%d",
				bad.serial, bad.tween, bad.alpha, (int)bad.logic,
				(int)bad.eye, (int)bad.prev, (int)bad.x, (int)bad.y,
				(int)bad.z, (int)bad.wx, (int)bad.wy, (int)bad.wz,
				(int)bad.tx, (int)bad.ty, (int)bad.tz, (int)bad.troom,
				bad.camtype, bad.camold, bad.camfixed, bad.camnumber,
				bad.count, missing, bad.corrected);

			/* And one line per missing room saying what dropped it, worked
			 * out from the note the walk left behind. */
			for (int i = 0; i < g_walkNote[0].count; i++)
			{
				const short which = g_walkNote[0].rooms[i];
				char why[192];

				if (WalkHolds(bad, which) || !WalkHolds(g_walkNote[2], which))
					continue;

				WhyRoomMissing(bad, which, why, (int)sizeof(why));
				Log("ROOMFLASH frame=%u room=%d why %s", bad.serial,
					(int)which, why);
			}
		}
	}

	/* ROOMPOP - a room that turns up from a long way off. Behind -log.
	 *
	 * Remaster art that is missing from a distance and pops into the
	 * picture as Lara walks up to it (such as the brick tower with a barred
	 * window at the corner of ANDREA1's Piazza dell'Opera, which exists only
	 * in the remaster art) has two possible causes, and this line tells
	 * them apart:
	 *
	 *   the room was not in the draw list. The HD path has no visibility
	 *      test of its own - there is no view frustum anywhere in
	 *      specific\hd (the only culling in it is back face culling on
	 *      Lara's model), and the `dropped=` of its frame line counts only
	 *      the fixed array's overflow - so the rooms it draws are exactly
	 *      the rooms this walk hands it. A room the portal chain does not
	 *      reach is not drawn in the classic path either; that is vanilla
	 *      behaviour the remaster's taller art makes visible.
	 *   the far plane. InitWindow sets it at 20480 units, twenty sectors
	 *      (specific\gamemain.cpp), and both paths project with the same
	 *      constants, so art past it is clipped in both.
	 *
	 * If the art appears on a frame that also joins its room to the list,
	 * it is the walk; if the room was there all along, it is the far plane.
	 * Measured with TrgProbe -near over that corner: ANDREA1 has no remaster
	 * art more than 936 units outside its own room's box anywhere within
	 * 8,000 units of it, so the tower is ordinary art of some room and not
	 * something hung far outside one; this line names the room. */
	if (gLogEnabled && g_walkHeld >= 2 &&
		g_walkNote[1].level == g_walkNote[2].level &&
		g_walkNote[2].serial == g_walkNote[1].serial + 1)
	{
		const RoomWalkNote& now = g_walkNote[2];

		for (int i = 0; i < now.count && g_walkPops < kPopLines; i++)
		{
			const short which = now.rooms[i];

			if (WalkHolds(g_walkNote[1], which))
				continue;

			const long dist = RoomDistance(&room[which], now.x, now.y, now.z);

			if (dist < kPopFar)
				continue;

			g_walkPops++;
			Log("ROOMPOP frame=%u room=%d dist=%d eye=(%d,%d,%d) rooms=%d "
				"tween=%d far_plane=%d", now.serial, (int)which, (int)dist,
				(int)now.x, (int)now.y, (int)now.z, now.count, now.tween,
				(int)phd_zfar);
		}
	}

	/* ROOMLIST - what the walk hands the picture, and what it does not.
	 * Behind -log.
	 *
	 * The HD path has no visibility test of its own, so the rooms it draws
	 * are draw_rooms, and anything seen as black through a hole is a room
	 * this list does not hold. ROOMFLASH names a room that comes and goes
	 * between two frames; this names the list itself.
	 *
	 * The five second dump lists, beside each drawn room, that room's
	 * portal neighbours that are not drawn (`shadow=`). A player standing
	 * at a hole in the remaster's opened-up wall and seeing black is
	 * standing in a room whose shadow list holds the room that cannot be
	 * seen. */
	if (gLogEnabled && g_walkHeld >= 2 &&
		g_walkNote[1].level == g_walkNote[2].level &&
		g_walkNote[2].serial == g_walkNote[1].serial + 1)
	{
		const RoomWalkNote& now = g_walkNote[2];
		const RoomWalkNote& was = g_walkNote[1];
		char added[160], removed[160];
		int a = 0, r = 0;

		added[0] = 0;
		removed[0] = 0;

		for (int i = 0; i < now.count; i++)
			if (!WalkHolds(was, now.rooms[i]) && a < (int)sizeof(added) - 8)
				a += sprintf(&added[a], a ? ",%d" : "%d", (int)now.rooms[i]);

		for (int i = 0; i < was.count; i++)
			if (!WalkHolds(now, was.rooms[i]) && r < (int)sizeof(removed) - 8)
				r += sprintf(&removed[r], r ? ",%d" : "%d", (int)was.rooms[i]);

		if ((a || r) && g_listLines < kListLines)
		{
			g_listLines++;
			Log("ROOMLIST frame=%u tween=%d cam_room=%d walk_room=%d "
				"eye=(%d,%d,%d) rooms=%d reach_only=%d trimmed=%d added=%s "
				"removed=%s", now.serial, now.tween, (int)now.logic,
				(int)now.eye, (int)now.wx, (int)now.wy, (int)now.wz,
				now.count, now.reach_only, now.trimmed, a ? added : "-",
				r ? removed : "-");
		}

		/* Every five seconds, and on the first frame after a window event
		 * (RoomListWindowEvents) whatever the timer says: a tool that
		 * captures the screen takes the focus away and gives it back, and
		 * the list of the captured view is then in the log beside it. A
		 * WM_INACTIVE is written first when both are waiting, the WM_ACTIVE
		 * on the frame after. */
		const long events = RoomListWindowEvents;
		const long event = (events & 1) ? 1 : (events & 2);
		const bool due = (unsigned short)(GlobalCounter - g_listDumpAt) >=
			30 * kListDumpSeconds || !g_listEver;

		if (due || event)
		{
			if (due)
			{
				g_listDumpAt = GlobalCounter;
				g_listEver = true;
			}

			// And the HD path writes its reachart lines on this frame.
			if (event)
			{
				InterlockedAnd(&RoomListWindowEvents, ~event);
				HDFrameNoteDump();
			}

			/* The target beside the eye: a room list without the
			 * direction it was walked in cannot be replayed with
			 * RoomProbe -walk. `why` is what wrote it: the five second
			 * timer, or the window event. `reach` is whether the walk
			 * carried its second rectangle - an HD level with the setting
			 * hd_reach on - so that a replay walks the same way. */
			Log("ROOMLIST frame=%u the whole list, %d room(s), "
				"eye=(%d,%d,%d) target=(%d,%d,%d) target_room=%d cam=%d/%d "
				"fixed=%d number=%d reach_only=%d trimmed=%d budget=%d why=%s"
				" reach=%d",
				now.serial, now.count, (int)now.wx, (int)now.wy, (int)now.wz,
				(int)now.tx, (int)now.ty, (int)now.tz, (int)now.troom,
				now.camtype, now.camold, now.camfixed, now.camnumber,
				now.reach_only, now.trimmed, (int)kReachBudget,
				event == 1 ? "inactive" : event == 2 ? "active" : "timer",
				g_reach_on ? 1 : 0);

			for (int i = 0; i < now.count; i++)
			{
				const short which = now.rooms[i];
				const ROOM_INFO* rr;
				char shadow[192];
				short* door;
				long drn;
				int at = 0;

				if (which < 0 || which >= number_rooms)
					continue;

				rr = &room[which];
				shadow[0] = 0;
				door = rr->door;

				if (door)
					// Sixteen: see the note at the same loop above.
					for (drn = *door++; drn > 0; drn--, door += 16)
					{
						ROOM_PORTAL* p = (ROOM_PORTAL*)door;

						if (WalkHolds(now, p->rn) ||
							at >= (int)sizeof(shadow) - 8)
							continue;

						at += sprintf(&shadow[at], at ? ",%d" : "%d",
							(int)p->rn);
					}

				/* window= is the classic rectangle and the scissor of the
				 * intruding art; reach= is what the walk carried it in on.
				 * A room of the reach alone (window empty, reach not) draws
				 * none of its intruding art where the holder is listed, and
				 * the part outside the smaller box whole where it is not. */
				Log("ROOMLIST   room=%d window=%d,%d..%d,%d reach=%d,%d..%d,%d"
					" %s dist=%d outside=%d shadow=%s", (int)which,
					(int)now.win[i][0], (int)now.win[i][1],
					(int)now.win[i][2], (int)now.win[i][3],
					(int)now.reach[i][0], (int)now.reach[i][1],
					(int)now.reach[i][2], (int)now.reach[i][3],
					(now.win[i][0] >= now.win[i][2] ||
					now.win[i][1] >= now.win[i][3]) ? "REACH" : "window",
					(int)RoomDistance(rr, now.wx, now.wy, now.wz),
					(rr->flags & ROOM_OUTSIDE) ? 1 : 0, at ? shadow : "-");
			}
		}
	}

	// One line a second, and only while there is something to say. The
	// counter ticks thirty times a second at every fps_mode.
	if ((unsigned short)(GlobalCounter - g_walkLineAt) >= 30 || !g_walkLineEver)
	{
		if (g_walkLineEver && (g_walkTweens || g_walkFlashes || g_walkPops))
			Log("ROOMWALK tween corrections=%d of %d tween frames, "
				"flashes=%d, far rooms joined=%d", g_walkCorrections,
				g_walkTweens, g_walkFlashes, g_walkPops);

		/* What the reach costs, once a second. rooms= is the whole list,
		 * classic= the part of it that has a window of its own - the list
		 * the walk would have without the reach - and reach= the rooms the
		 * reach alone brought in. cap hit= counts the rooms refused because
		 * draw_rooms (kDrawRoomsMax) or the queue ring (kRoomListRing) was
		 * full - neither can happen in a loadable level, so this column must
		 * stay zero; trimmed= the ones the budget dropped again, farthest
		 * first. What those rooms cost is on the FPSLOG line of the same
		 * second, which carries hd_rooms / hd_items / hd_draws as the maxima
		 * over it (specific\hd\hd_level.cpp). */
		if (g_walkLineEver && g_reach_on && g_walkRoomsFrames)
			Log("ROOMWALK reach rooms added=%d of %d (mean %.1f of %.1f, max "
				"%d classic %d reach %d), budget=%d trimmed=%d no_art=%d "
				"cap hit=%d carried=%d",
				(int)g_walkReachSum, (int)g_walkRoomsSum,
				(double)g_walkReachSum / g_walkRoomsFrames,
				(double)g_walkRoomsSum / g_walkRoomsFrames, g_walkRoomsMax,
				g_walkClassicMax, g_walkReachMax, (int)kReachBudget,
				g_reach_trimmed_second, g_reach_noart_second, g_reach_capped,
				(int)g_walkCarriedSum);

		g_walkLineAt = GlobalCounter;
		g_walkLineEver = true;
		g_walkTweens = 0;
		g_walkCorrections = 0;
		g_walkFlashes = 0;
		g_walkPops = 0;
		g_listLines = 0;
		g_walkRoomsSum = 0;
		g_walkReachSum = 0;
		g_walkCarriedSum = 0;
		g_walkRoomsFrames = 0;
		g_walkRoomsMax = 0;
		g_walkClassicMax = 0;
		g_walkReachMax = 0;
		g_reach_capped = 0;
		g_reach_trimmed_second = 0;
		g_reach_noart_second = 0;
	}
}

/* Distance between two points in doubles: the eye can be further from her
 * than a long's square reaches. */
static long RopeDrawDist(long x, long y, long z)
{
	return (long)sqrt((double)x * x + (double)y * y + (double)z * z);
}

/* ROPELOG draw, behind -log: one line per drawn frame while she hangs on a
 * rope (lara.RopePtr != -1) and one on the frame after she lets go, the draw's
 * side of ROPELOG's tick line (game\rope.cpp):
 *   f       GlobalCounter; the frames of one tick share it
 *   drawn   1 when the Lara block below draws her this frame; the terms it is
 *           made of follow: mb her mesh_bits, scno SCNoDrawLara, mhide
 *           ModernLaraHidden, inv the IFL_INVISIBLE flag
 *   ct      camera.type
 *   eye, tgt   camera.pos and camera.target, with their rooms
 *   eyed    units from the eye to the look-at point
 *   larad   units from the eye to her position
 *   cand, clip   ChaseCameraLogPick: the candidate ChaseCamera took (0 the
 *           dialled angle, 1..4 a world cardinal) and whether mgLOS cut the
 *           dialled one short; both -1 when the pass ran another camera */
static void RopeDrawLog()
{
	static long tail;
	long cand, clip, mhide, drawn;

	if (!objects[LARA].loaded || !lara_item)
		return;

	if (lara.RopePtr != -1)
		tail = 1;
	else if (tail)
		tail = 0;
	else
		return;

	ChaseCameraLogPick(&cand, &clip);
	mhide = ModernLaraHidden() ? 1 : 0;
	drawn = objects[LARA].loaded && !(lara_item->flags & IFL_INVISIBLE) && lara_item->mesh_bits &&
		!SCNoDrawLara && !mhide;
	Log("ROPELOG draw f=%d drawn=%ld mb=0x%lX scno=%d mhide=%ld inv=%d ct=%d eye=%ld,%ld,%ld/%d "
		"tgt=%ld,%ld,%ld/%d eyed=%ld larad=%ld cand=%ld clip=%ld",
		(int)GlobalCounter, drawn, (ulong)lara_item->mesh_bits, (int)SCNoDrawLara, mhide,
		(lara_item->flags & IFL_INVISIBLE) ? 1 : 0, (int)camera.type,
		camera.pos.x, camera.pos.y, camera.pos.z, (int)camera.pos.room_number,
		camera.target.x, camera.target.y, camera.target.z, (int)camera.target.room_number,
		RopeDrawDist(camera.pos.x - camera.target.x, camera.pos.y - camera.target.y,
			camera.pos.z - camera.target.z),
		RopeDrawDist(camera.pos.x - lara_item->pos.x_pos, camera.pos.y - lara_item->pos.y_pos,
			camera.pos.z - lara_item->pos.z_pos),
		cand, clip);
}

void DrawRooms(short current_room)
{
	ROOM_INFO* r;
	long lx, ly, lz;
	short lr;

	DoMonitorScreen();

	/* The walk starts where the eye is.
	 *
	 * On an in-between frame of the sixty hertz cadence the eye has been
	 * blended and the view matrix rebuilt from it, while `current_room` is
	 * still the logic camera's room. Where the two disagree - the chase
	 * camera swinging around a corner crosses a portal plane between two
	 * ticks - GetRoomBounds would refuse to follow the portal the eye has
	 * just stepped through and the whole room behind it would be missing for
	 * one frame. See the note over InterpEyeRoom (game\interpolate.h) for how
	 * the eye's room is found and for the player's two controls.
	 *
	 * InterpEyeRoom returns `current_room` unchanged on every frame that was
	 * not blended - fps_mode 0, the frame that carries the tick, a cutscene,
	 * a camera cut, a level start - so at thirty hertz this is the identity.
	 *
	 * It seeds the portal walk and nothing else. `outside` and
	 * `camera_underwater` are read from the logic room: they are the frame's
	 * own flags rather than a property of the room list, the live frames on
	 * either side of a tween one read them from the logic room, and a tween
	 * frame that took them from a room half a step away could put one frame
	 * of underwater tint or weather into a run that has neither. `outside` is
	 * widened by the walk itself in any case - GetRoomBounds sets it from
	 * every ROOM_OUTSIDE room it reaches. */
	const short walk_room = InterpEyeRoom(current_room);
	const ROOM_INFO* logic_room = &room[current_room];

	CurrentRoom = walk_room;
	r = &room[walk_room];
	r->test_left = 0;
	r->test_top = 0;
	phd_left = 0;
	phd_top = 0;
	phd_right = phd_winxmax;
	phd_bottom = phd_winymax;
	r->test_right = phd_winxmax;
	r->test_bottom = phd_winymax;
	outside = logic_room->flags & ROOM_OUTSIDE;
	snow_outside = 0;
	camera_underwater = logic_room->flags & ROOM_UNDERWATER;
	r->bound_active = 2;
	draw_room_list[0] = walk_room;
	room_list_start = 0;
	room_list_end = 1;
	number_draw_rooms = 0;

	if (outside)
	{
		outside_top = 0;
		outside_left = 0;
		outside_right = phd_winxmax;
		outside_bottom = phd_winymax;
	}
	else
	{
		outside_top = phd_winymax;
		outside_left = phd_winxmax;
		outside_right = 0;
		outside_bottom = 0;
	}

	/* The reach, and only where there is HD art to reach and the setting
	 * hd_reach is on. HDLevelActive() is false with hd_enable off and in a
	 * level the .TRG had nothing for, and then the walk below is the
	 * original one. */
	g_reach_on = HDLevelActive() && tomb5.hd_reach;
	VisBeginWalk();

	if (g_reach_on)
		ReachBeginWalk(walk_room);

	GetRoomBounds();

	if (g_reach_on)
		CarryArt();

	/* And the budget, over the eye the walk really used - w2v_matrix M03/M13/
	 * M23 is the camera position (specific\3dmath.cpp), the same three
	 * numbers the side test subtracts from a portal corner. */
	if (g_reach_on)
		ReachTrimRooms((long)w2v_matrix[M03], (long)w2v_matrix[M13],
			(long)w2v_matrix[M23]);

	VisEndWalk(walk_room);
	NoteRoomWalk(current_room, walk_room);
	ProcessClosedDoors();

	if (gfLevelId != LVL5_TITLE)
		SkyDrawPhase();

	// CAMLOG's draw line, for a frame the drawn-eye rule acted on. NoteRoomWalk
	// has just stepped the serial, so this is the frame= ROOMLIST prints.
	if (gLogEnabled)
		InterpLogEyeFix(g_walkSerial - 1, walk_room, number_draw_rooms,
			(logic_room->flags & ROOM_OUTSIDE) ? 1 : 0,
			(gfLevelId != LVL5_TITLE && outside > 0 && !InterpSkyWithheld()) ? 1 : 0);

	if (gLogEnabled)
		RopeDrawLog();

	if (objects[LARA].loaded && !(lara_item->flags & IFL_INVISIBLE))
	{
		nPolyType = 4;

		/* Lara is hidden completely when the camera is too close to her.
		 *
		 * This one term hides her entirely, because this block is every path
		 * that draws her: the classic composite with its braid and shadow
		 * (specific\drawlara.cpp - S_PrintShadow and DrawHair are inside the
		 * four variants), the remaster's wardrobe (HDLaraDrawFrame, called
		 * from those same variants) and the muzzle flash's matrices below.
		 * objects[LARA].draw_routine is 0 (game\setup.cpp), so the room's
		 * item loop never draws her either; the binoculars rely on the same
		 * arrangement - they set mesh_bits = 0 and she disappears
		 * completely, shadow and all, through this same condition.
		 *
		 * Her mirror image stays, which is why the term is here and not
		 * inside DrawLara: Draw_Mirror_Lara is below this block, and the
		 * reflection of a camera stuck inside her is not stuck inside
		 * anything - it is a Lara at twice the distance to the glass.
		 *
		 * The predicate itself measures the camera's own two points once per
		 * frame and refuses first person outright (game\camera.cpp). */
		if (lara_item->mesh_bits && !SCNoDrawLara && !ModernLaraHidden())
		{
			if (lara.skelebob)
				SetupSkelebobMeshswaps();

			DrawLara(lara_item, 0);

			if (lara.skelebob)
				RestoreLaraMeshswaps();

			phd_PushMatrix();

			if (lara.right_arm.flash_gun)
			{
				aMXPtr[M00] = lara_matrices[LMX_HAND_R * indices_count + M00];
				aMXPtr[M01] = lara_matrices[LMX_HAND_R * indices_count + M01];
				aMXPtr[M02] = lara_matrices[LMX_HAND_R * indices_count + M02];
				aMXPtr[M03] = lara_matrices[LMX_HAND_R * indices_count + M03];
				aMXPtr[M10] = lara_matrices[LMX_HAND_R * indices_count + M10];
				aMXPtr[M11] = lara_matrices[LMX_HAND_R * indices_count + M11];
				aMXPtr[M12] = lara_matrices[LMX_HAND_R * indices_count + M12];
				aMXPtr[M13] = lara_matrices[LMX_HAND_R * indices_count + M13];
				aMXPtr[M20] = lara_matrices[LMX_HAND_R * indices_count + M20];
				aMXPtr[M21] = lara_matrices[LMX_HAND_R * indices_count + M21];
				aMXPtr[M22] = lara_matrices[LMX_HAND_R * indices_count + M22];
				aMXPtr[M23] = lara_matrices[LMX_HAND_R * indices_count + M23];
				SetGunFlash(lara.gun_type);
			}

			if (lara.left_arm.flash_gun)
			{
				aMXPtr[M00] = lara_matrices[LMX_HAND_L * indices_count + M00];
				aMXPtr[M01] = lara_matrices[LMX_HAND_L * indices_count + M01];
				aMXPtr[M02] = lara_matrices[LMX_HAND_L * indices_count + M02];
				aMXPtr[M03] = lara_matrices[LMX_HAND_L * indices_count + M03];
				aMXPtr[M10] = lara_matrices[LMX_HAND_L * indices_count + M10];
				aMXPtr[M11] = lara_matrices[LMX_HAND_L * indices_count + M11];
				aMXPtr[M12] = lara_matrices[LMX_HAND_L * indices_count + M12];
				aMXPtr[M13] = lara_matrices[LMX_HAND_L * indices_count + M13];
				aMXPtr[M20] = lara_matrices[LMX_HAND_L * indices_count + M20];
				aMXPtr[M21] = lara_matrices[LMX_HAND_L * indices_count + M21];
				aMXPtr[M22] = lara_matrices[LMX_HAND_L * indices_count + M22];
				aMXPtr[M23] = lara_matrices[LMX_HAND_L * indices_count + M23];
				SetGunFlash(lara.gun_type);
			}

			phd_PopMatrix();
		}

		if (gfLevelFlags & GF_MIRROR && lara_item->room_number == gfMirrorRoom)
			Draw_Mirror_Lara();
	}

	/* The mirrors the HD level found in its rooms' walls: Lara and the
	 * items in front of the glass, drawn once more across its plane. No TR5
	 * level has a script mirror, so the block above never runs; this one
	 * runs only with an HD level built, and a classic frame is the frame it
	 * always was. Here, before the rooms' objects are printed, because the
	 * mirror room's portal window is still this frame's - see
	 * PrintObjectReflection. */
	if (HDLevelActive())
		Draw_Mirror_Items();

	nPolyType = 4;
	DrawGunflashes();
	nPolyType = 0;

	for (int i = 0; i < 32; i++)
	{
		if (dynamics[i].on)
		{
			if (dynamics[i].z < 0)
				dynamics[i].z = 0;

			if (dynamics[i].x < 0)
				dynamics[i].x = 0;
		}
	}

	phd_PushMatrix();
	phd_TranslateAbs(0, 0, 0);
	SaveD3DCameraMatrix();
	phd_PopMatrix();

	aResetFogBulbList();

	if (tomb5.fog)
	{
		aBuildFogBulbList();
		aBuildFXFogBulbList();
	}

	for (int i = 0; i < number_draw_rooms; i++)
	{
		r = &room[draw_rooms[i]];
		phd_PushMatrix();
		phd_TranslateAbs(r->x, r->y, r->z);
		CurrentRoom = draw_rooms[i];
		phd_left = r->left;
		phd_right = r->right;
		phd_top = r->top;
		phd_bottom = r->bottom;
		aSetViewMatrix();
		InsertRoom(r);
		phd_PopMatrix();
	}

	DrawGunshells();
	nPolyType = 3;

	if (GLOBAL_playing_cutseq)
		DrawCutSeqActors();

	DrawRopeList();
	S_DrawSparks();
	DrawRats();
	DrawBats();
	DrawSpiders();
	lx = lara_item->pos.x_pos;
	ly = lara_item->pos.y_pos;
	lz = lara_item->pos.z_pos;
	lr = lara_item->room_number;
	lara_item->pos.x_pos = camera.pos.x;
	lara_item->pos.y_pos = camera.pos.y;
	lara_item->pos.z_pos = camera.pos.z;
	lara_item->room_number = camera.pos.room_number;
	DoWeather();
	DoUwEffect();
	S_DrawFires();
	S_DrawSmokeSparks();
	S_DrawSplashes();
	DrawBubbles();
	DrawDebris();
	DrawBlood();
	DrawDrips();
	DrawShockwaves();
	DrawLightning();
	DrawTwogunLasers();
	S_DrawFootPrints();
	lara_item->pos.x_pos = lx;
	lara_item->pos.y_pos = ly;
	lara_item->pos.z_pos = lz;
	lara_item->room_number = lr;

	if (LaserSightActive)
		DrawLaserSightSprite();

	for (int i = 0; i < number_draw_rooms; i++)
		PrintObjects(draw_rooms[i]);

	/* aUpdate is `aWibble++` and nothing else (specific\alexstuff.cpp).
	 * aWibble is a UV scroll read by the water surfaces and the flat sky
	 * (specific\specificfx.cpp), so at sixty it would scroll at double
	 * speed; it advances on logic ticks only. The guard is at the call so
	 * that it stands beside the rest of the frame's tick guards. */
	if (!InterpIsTweenFrame())
		aUpdate();
}

void CalculateObjectLightingLara()
{
	PHD_VECTOR pos;

	if (GLOBAL_playing_cutseq)
		CalculateObjectLightingLaraCutSeq();
	else
	{
		pos.x = 0;
		pos.y = 0;
		pos.z = 0;

		if (lara_item->anim_number == ANIM_DUCKBREATHE || lara_item->anim_number == ANIM_ALL4S || lara_item->anim_number == ANIM_BREATH)
		{
			pos.x = lara_item->pos.x_pos;

			if (lara_item->anim_number == ANIM_BREATH)
				pos.y = lara_item->pos.y_pos - 512;
			else
				pos.y = lara_item->pos.y_pos - 192;

			pos.z = lara_item->pos.z_pos;
		}
		else
			GetLaraJointPos(&pos, LMX_TORSO);

		current_item = lara_item;
		lara_item->il.item_pos.x = pos.x;
		lara_item->il.item_pos.y = pos.y;
		lara_item->il.item_pos.z = pos.z;
		CalcAmbientLight(lara_item);
		CreateLightList(lara_item);
	}
}

long GetFrames(ITEM_INFO* item, short* frm[], long* rate)
{
	ANIM_STRUCT* anim;
	long frame, size, frac, num;

	anim = &anims[item->anim_number];
	frm[0] = anim->frame_ptr;
	frm[1] = anim->frame_ptr;
	*rate = anim->interpolation & 0xFF;
	frame = item->frame_number - anim->frame_base;
	size = anim->interpolation >> 8;
	frm[0] += size * (frame / *rate);
	frm[1] = frm[0] + size;
	frac = (frame % *rate);

	if (!frac)
		return 0;

	num = *rate * (frame / *rate + 1);

	if (num > anim->frame_end)
		*rate = *rate + anim->frame_end - num;

	return frac;
}

/* True when Lara's key pair `pair` (key `pair` to key `pair` + 1) of
 * animation `animNumber` is far: the drawn rotation of some node turns more
 * than 90 degrees between the two keys, and the pair is no seam in the data
 * (InterpKeySeamAt), which vanilla's blend draws (InterpKeyFarOf, a table
 * lookup). */
static bool LaraKeyFar(short animNumber, long pair)
{
	const OBJECT_INFO* obj = &objects[lara_item->object_number];

	return InterpKeyFarOf(animNumber, pair, obj->nmeshes, &bones[obj->bone_index]);
}

/* The key pair of animation `anim` that `frm` holds - frm[0] one of its key
 * frames and frm[1] the next - or -1 where the two are anything else, such as
 * the bridge's frame in both slots (LaraBridgeFrames). */
static long LaraKeyOf(const ANIM_STRUCT* anim, short* const frm[])
{
	const long size = anim->interpolation >> 8;

	if (size < 9 || !anim->frame_ptr || !frm[0] || frm[1] != frm[0] + size ||
		frm[0] < anim->frame_ptr)
		return -1;

	const long at = (long)(frm[0] - anim->frame_ptr);

	return at % size ? -1 : at / size;
}

/* True when Lara's far key pair `pair` of animation `animNumber` is held on
 * its first key frame (InterpKeyHold): turn180_effect fires on its second.
 * The caller has found the pair far (LaraKeyFar). */
static bool LaraKeyHold(short animNumber, long pair)
{
	long flip, turnPair;
	const int kind = InterpTurnOf(animNumber, &flip, &turnPair);

	if (kind != INTERP_TURN_INSIDE)
		return false;

	const ANIM_STRUCT* anim = &anims[animNumber];

	return InterpKeyHold(kind, turnPair, flip, anim->frame_base, anim->interpolation & 0xFF, pair);
}

/* True when frame `frameNumber` of Lara's animation `animNumber` is the frame
 * an early turn180_effect fires on (INTERP_TURN_EARLY): the frame belongs to
 * the yaw before the effect and is drawn turned back (InterpKeyFrameTurn). */
static bool LaraTurnFrame(short animNumber, short frameNumber)
{
	long flip, pair;

	return InterpTurnOf(animNumber, &flip, &pair) == INTERP_TURN_EARLY && frameNumber == flip;
}

/* The pose this drawn frame wears when it falls between two logic ticks.
 *
 * `prevAnim` and `prevFrame` are what the item wore before the last tick -
 * the snapshot game\interpolate.cpp keeps - and `t` is how far this frame
 * stands between then and now, a half at sixty hertz. What comes back is the
 * key frame pair and the factor whose blend is the midpoint of the two poses
 * the game draws at thirty; interpolate_math.h derives it and says why it is
 * a midpoint and not a freshly computed "correct" fraction.
 *
 * Returns 0 when there is nothing to blend, and then nothing has been
 * written: the caller falls back to GetFrames and the pose is the one thirty
 * hertz draws. The pose rule refuses the blend when the animation number
 * changed (tested here, because only this side knows it) and in every
 * frame-number case InterpPoseAt names (the frame did not step by exactly
 * one) - except the ledge hang's lowering, whose backwards steps
 * InterpPoseBack blends. Lara's frames across the animation change after an
 * early half turn are refused here too; the caller draws them from a bridge
 * between the two poses instead (LaraBridgeFrames).
 *
 * This refuses the pose, not the place: the position under the pose is
 * already blended, so on a refused frame of an animation that carries its
 * own travel - a ladder climb - the root offset has to be blended in time as
 * well, which InterpRootNow does. */
long GetFramesAt(ITEM_INFO* item, short prevAnim, short prevFrame, float t,
	short* frm[], float* blend)
{
	if (prevAnim != item->anim_number)
		return 0;

	ANIM_STRUCT* anim = &anims[item->anim_number];
	const long rate = anim->interpolation & 0xFF;
	const long size = anim->interpolation >> 8;
	long key;
	float b;
	bool back = false;

	if (!InterpPoseAt(prevFrame, item->frame_number, anim->frame_base, rate,
		anim->frame_end, t, &key, &b))
	{
		/* The ledge hang's lowering steps the pull-up down three frames a
		 * tick, which the neighbour rule refuses on every tick; InterpPoseBack
		 * walks the animation's own path between the two tick poses instead,
		 * so the lowering is drawn at the full frame rate. Asked for Lara
		 * only, and only while LaraLedgeReversing (state AS_LEDGEHANG in its
		 * lowering phase), so no other frame reaches it. The tween after the
		 * hand-over into the pull-up and the one after the hand-over out of it
		 * are refused by the animation number test above, as every animation
		 * change is. */
		if (item != lara_item || !LaraLedgeReversing(item) ||
			!InterpPoseBack(prevFrame, item->frame_number, anim->frame_base, rate,
			anim->frame_end, t, &key, &b))
			return 0;

		back = true;
	}

	frm[0] = anim->frame_ptr + size * key;
	frm[1] = frm[0] + size;

	/* And the pair a matrix blend cannot survive.
	 *
	 * An element-wise lerp of two rotations shrinks - to cos(D/2) of the
	 * bone's own frame at the midpoint - and at half a turn the frame is
	 * singular and whatever hangs off it lands nowhere real (a leg pointing
	 * the wrong way in the dive roll).
	 *
	 * At rate 1 the test covers every node: a rate 1 pair is two adjacent
	 * stored frames a whole game frame apart that vanilla never blends at
	 * all. Measured with ModelProbe -poserisk: over ANDREA1's 319 Lara
	 * animations this refuses animation 205 - whose hips turn 179.9 degrees
	 * between key 0 and key 1 - and nothing else.
	 *
	 * At every other rate the tween frame lies inside the segment vanilla
	 * blends the two tick frames on, so it is no worse than its own
	 * neighbours - except on a pair of Lara's where some node turns more than
	 * 90 degrees in the drawn frame (LaraKeyFar). LaraPoseFrames draws such a
	 * pair from a key frame built at the weight in hand, and a tween frame
	 * asks it for the weight between the two ticks like any other. Only the
	 * pair held on its first key (LaraKeyHold, the tightrope turn) refuses the
	 * tween, so that it falls back to the pose of the current tick - the
	 * first key inside the pair, the second key on the tick that reaches it.
	 * It is not counted as pose_far, which stays the rate 1 count.
	 * interpolate_math.h carries the arithmetic. */
	if (rate == 1 && InterpPoseFar(frm[0], frm[1],
		objects[item->object_number].nmeshes, INTERP_POSE_TURN_COS))
	{
		InterpNotePoseFar();

		return 0;
	}

	if (rate > 1 && item == lara_item && LaraKeyFar(item->anim_number, key) &&
		LaraKeyHold(item->anim_number, key))
		return 0;

	if (back)
		InterpNotePoseBack();

	*blend = b;

	return 1;
}

/* The key frame pair a tick on frame `frameNumber` of animation `animNumber`
 * draws from, with the remainder `frac` and the factor `alpha` over the pair.
 *
 * The same lines GetFrames spends on the key pair, run on an animation and a
 * frame that need not be the item's - a function of its own and not a
 * second path through GetFrames, because nothing here may write into frm[]
 * or into anything the caller is about to draw from.
 *
 * It is exactly the arithmetic the tick uses: the key index and the
 * remainder from the animation's own rate, the last interval shortened by
 * InterpKeyRate the way GetFrames shortens it, and InterpKeyAlpha's factor
 * over the pair. `lara` is set for Lara, whose far pair held on its first key
 * (LaraKeyHold) is drawn from that key frame alone, which comes back as a
 * remainder of zero. pair[1] is the next key frame, or null where pair[0] is
 * the animation's last and the next would lie past its block of frames; a
 * remainder other than zero always has one.
 *
 * Returns 0 for an animation with no frames behind it, a rate or a frame
 * size that cannot index one, and a frame outside the animation it is
 * claimed to be in. A level's transition table can name a frame that belongs
 * to another animation's numbering, and the key index off such a frame would
 * walk out of the animation's own block of frames.
 *
 * The animation number is not bounded above: the level's animation count is
 * a local of the loader (specific\file.cpp) and no global keeps it, so there
 * is nothing to compare against. The number comes from a snapshot of this
 * item, is the same short item->anim_number carries, and every GetFrames in
 * the engine dereferences it unguarded. A slot whose object changed under it
 * is stopped by SNAP_OBJECT before a tween frame of it is drawn. */
static long GetKeyAt(short animNumber, short frameNumber, bool lara, const short* pair[2],
	long* frac, float* alpha)
{
	if (!anims || animNumber < 0)
		return 0;

	const ANIM_STRUCT* anim = &anims[animNumber];
	const long rate = anim->interpolation & 0xFF;
	const long size = anim->interpolation >> 8;

	if (rate < 1 || size < 9 || !anim->frame_ptr)
		return 0;

	const long frame = (long)frameNumber - (long)anim->frame_base;

	if (frame < 0 || frameNumber > anim->frame_end)
		return 0;

	const long key = frame / rate;
	long rest = frame % rate;
	const short* frm0 = anim->frame_ptr + size * key;

	/* A far key pair of Lara's held on its first key frame (LaraPoseFrames,
	 * LaraKeyHold) is drawn from that key frame alone. */
	if (rest && lara && LaraKeyFar(animNumber, key) && LaraKeyHold(animNumber, key))
		rest = 0;

	pair[0] = frm0;
	pair[1] = key * rate < (long)anim->frame_end - (long)anim->frame_base ? frm0 + size : 0;
	*frac = rest;
	*alpha = InterpKeyAlpha(rest, InterpKeyRate(rate, key, anim->frame_end));

	return 1;
}

/* The root offset the previous tick drew: InterpRootKey over the pair and
 * the factor GetKeyAt finds, so what comes back is bit for bit the offset
 * drawn one thirtieth of a second ago. `lara` is set for Lara, whose pose
 * LaraPoseFrames may have drawn from other frames than the pair: a far pair
 * held on its first key (LaraKeyHold) drew that key frame's offset, which is
 * what comes back; a far pair built at the tick's weight
 * (InterpKeyFrameFar) drew the pair's own blend, as vanilla; and on the
 * frame an early turn180_effect fires on (LaraTurnFrame) the frame was drawn
 * turned by half a turn, which `turned` reports so that the offset is turned
 * with it (InterpRootRefused).
 *
 * Returns 0 where GetKeyAt does - and then the caller blends no root and the
 * frame is drawn with its own offset. */
static long GetRootAt(short animNumber, short frameNumber, bool lara, long out[3], bool* turned)
{
	*turned = false;

	const short* pair[2];
	long frac;
	float alpha;

	if (!GetKeyAt(animNumber, frameNumber, lara, pair, &frac, &alpha))
		return 0;

	InterpRootKey(pair[0], pair[1], frac, alpha, out);

	*turned = lara && LaraTurnFrame(animNumber, frameNumber);

	return 1;
}

/* The frame drawn between two ticks across the animation change after an
 * early half turn (InterpBridgeFires, interpolate_math.h, says what and why):
 * a key frame built at `t` from the pose the flip tick drew to the pose of
 * the current tick (InterpKeyFrameBridge), put into both slots of `frm`, so
 * that the draw takes it as it is, a pose blended with itself being that
 * pose.
 *
 * The first pose is the pair GetKeyAt finds for the previous tick, both key
 * frames turned by half a turn as LaraPoseFrames drew them and built at the
 * tick's factor where the frame lies between them; the second is the current
 * tick's pair, built at its factor where the frame lies between two keys. The
 * hips take the longer way round where the shorter one turns against her own
 * turn on both sides (InterpBridgeLong): over the turned key pair the flip
 * frame lies on, and over the pair the current tick stands on.
 *
 * Returns false, with nothing written, where it does not fire, where a pose
 * cannot be found (GetKeyAt), and where the current tick fires a
 * turn180_effect itself: the item has then turned again between the two
 * ticks and the pose the flip tick drew belongs to the yaw before. The frame
 * is built into a static buffer that lives until the next call; the caller
 * draws from it at once. */
static bool LaraBridgeFrames(ITEM_INFO* item, short prevAnim, short prevFrame, float t,
	short* frm[], float* blend)
{
	static short bridge[INTERP_KEYFRAME_MAX];
	long flip, pair, nowFlip, nowPair;
	const int kind = InterpTurnOf(prevAnim, &flip, &pair);

	if (!InterpBridgeFires(kind, flip, prevAnim, prevFrame, item->anim_number, t))
		return false;

	InterpTurnOf(item->anim_number, &nowFlip, &nowPair);

	if (nowFlip == item->frame_number)
		return false;

	const int nodes = objects[item->object_number].nmeshes;

	if (nodes < 1 || nodes > 32 || pair < 0)
		return false;

	const short* was[2];
	const short* now[2];
	long wasFrac, nowFrac;
	float wasAlpha, nowAlpha;

	if (!GetKeyAt(prevAnim, prevFrame, true, was, &wasFrac, &wasAlpha) ||
		!GetKeyAt(item->anim_number, item->frame_number, true, now, &nowFrac, &nowAlpha))
		return false;

	short turned[2][INTERP_KEYFRAME_MAX];
	short from[INTERP_KEYFRAME_MAX];
	short to[INTERP_KEYFRAME_MAX];
	const short* fromFrame = turned[0];
	const short* toFrame = now[0];

	InterpKeyFrameTurn(was[0], nodes, turned[0]);

	if (wasFrac)
	{
		InterpKeyFrameTurn(was[1], nodes, turned[1]);
		InterpKeyFrameLerp(turned[0], turned[1], nodes, wasAlpha, from);
		fromFrame = from;
	}

	if (nowFrac)
	{
		InterpKeyFrameLerp(now[0], now[1], nodes, nowAlpha, to);
		toFrame = to;
	}

	/* The key pair the flip frame lies on (InterpTurnOf's `pair`), turned as
	 * it was drawn; InterpHipsTurn reads node 0 alone, so only node 0 is
	 * turned. */
	const ANIM_STRUCT* anim = &anims[prevAnim];
	const long size = anim->interpolation >> 8;
	short before[2][9 + 2];

	InterpKeyFrameTurn(anim->frame_ptr + size * pair, 1, before[0]);
	InterpKeyFrameTurn(anim->frame_ptr + size * (pair + 1), 1, before[1]);

	const bool longHips = InterpBridgeLong(fromFrame, toFrame, before[0], before[1],
		now[0], now[1]);

	InterpKeyFrameBridge(fromFrame, toFrame, nodes, t, longHips, bridge);
	InterpNoteBridge(item, prevAnim, prevFrame, fromFrame, toFrame, longHips);

	frm[0] = bridge;
	frm[1] = bridge;
	*blend = t;

	return true;
}

/* What the three draw sites ask for: the pose, whatever frame rate the game
 * is running at.
 *
 * On a frame that carries a logic tick, and at fps_mode 0 always, this is
 * GetFrames and nothing else - the same pair, the same factor, the same
 * picture; InterpPoseTime returns zero before any time blending is reached.
 * On the frame between two ticks it is the midpoint of the poses those two
 * ticks draw, and for Lara across the animation change after an early half
 * turn a bridge between them (LaraBridgeFrames).
 *
 * Returns 0 for "no blend, the pose is frm[0]", 1 for vanilla's own key frame
 * blend and 2 for a blend in time. That is the same shape GetFrames has, so
 * every call site keeps its own `if (frac)`, and the two kinds of blend stay
 * apart for the log line and for the HD counter. */
long GetFramesNow(ITEM_INFO* item, short* frm[], float* blend)
{
	short prevAnim = 0;
	short prevFrame = 0;
	const float t = InterpPoseTime(item, &prevAnim, &prevFrame);

	if (t > 0.0F)
	{
		/* The root latch is cleared first and raised only by the refusal
		 * below, so it lives only from here to the draw. An item whose pose
		 * is blended, or whose pose was refused for a reason that leaves no
		 * previous offset, is drawn with its own frame's offset. */
		InterpRootRefused(item, 0);

		if (GetFramesAt(item, prevAnim, prevFrame, t, frm, blend))
		{
			InterpNotePose(item);

			return 2;
		}

		/* Pose refused: the animation changed, or the frame did not step by
		 * one. The pose of the current tick is drawn, which is one frame of
		 * thirty hertz inside a sixty hertz run and is invisible - but it is
		 * counted, because a skip count that is not tiny means the snapshot
		 * and the animation clock have fallen out of step.
		 *
		 * It is invisible only if the root agrees (a ladder climb is where it
		 * does not): the refusal covers the pose and never the root, so the
		 * offset the previous tick drew is handed to the draw and blended in
		 * time with this tick's. Where the two are equal - nearly every
		 * refusal, because an item that did not step its frame has not changed
		 * its offset either - InterpRootNow hands the draw nothing and counts
		 * nothing. See the note over InterpRootRefused in game\interpolate.h.
		 *
		 * One refusal is drawn from a pose of its own: Lara's frames across
		 * the animation change after an early half turn, bridged from the
		 * pose the flip tick drew to this tick's (LaraBridgeFrames). Such a
		 * frame is counted as a blended pose and not as a skip. Its root is a
		 * refused frame's all the same: the latch below is raised as on every
		 * refusal, and the bridge carries the current tick's own offset, which
		 * InterpRootNow reads from the drawn pair where it would have read
		 * that tick's frame, so the root comes out as it does without the
		 * bridge. */
		const bool bridged = item == lara_item &&
			LaraBridgeFrames(item, prevAnim, prevFrame, t, frm, blend);

		if (!bridged)
			InterpNotePoseSkip();

		long prevRoot[3];
		bool turned = false;

		if (GetRootAt(prevAnim, prevFrame, item == lara_item, prevRoot, &turned))
			InterpRootRefused(item, prevRoot, turned);

		if (bridged)
		{
			InterpNotePose(item);

			return 2;
		}
	}

	long rate;
	const long frac = GetFrames(item, frm, &rate);

	if (!frac)
		return 0;

	*blend = InterpKeyAlpha(frac, rate);

	return 1;
}

/* Lara's pose frames as her draw and her braid take them: the pair
 * GetFramesNow or GetFrames left in `frm` (with `frac` and the factor
 * `blend` they left), replaced by other frames in the two places where the
 * base engine's own draw goes wrong, at every fps_mode. Every other pair is
 * left as it is.
 *
 *   The frame an early turn180_effect fires on (LaraTurnFrame). The effect
 *   turns her by half a turn on that tick, and the frame, the last of the
 *   roll's end 48 and of the twists 207, 210 and 212, was made for the yaw
 *   before it: drawn as it is, her body faces the other way for one tick
 *   (171.9 degrees at the hips in 212, 176.8 in 207) and back again on the
 *   next. Both frames of the pair are replaced by copies turned by half a
 *   turn (InterpKeyFrameTurn), which land exactly where the frames land
 *   under the yaw before; the tween frames between the tick before and this
 *   one, whose item keeps the live yaw (InterpHalfTurn), take the same
 *   pair. Where that pair is far as well (below; rich3's 210 key 1 to 2,
 *   the flip on its second key) the tween frames draw a key frame built from
 *   the two turned copies.
 *
 *   A far key pair (LaraKeyFar: some node's drawn rotation turns more than
 *   90 degrees between the keys, and the pair is no seam in the data,
 *   InterpKeySeamAt: a seam, the start of the handstand from a ledge, 159
 *   key 1 to 2, is left to vanilla's blend, which keeps her hands on the
 *   ledge). Vanilla's blend averages the two
 *   multiplied-out matrix stacks element by element, so across such a pair
 *   that node collapses, and with it everything hanging off it: at the
 *   standing jump's take-off (76 key 0 to 1) the forearm is 0.09 of itself
 *   across on the in-between frame, and in the tightrope turn the hips'
 *   columns are 0.087, 0.995 and 0.059 long. Where turn180_effect fires on
 *   the pair's second key (LaraKeyHold, the tightrope turn) every frame
 *   between the two keys belongs to the yaw of the first, and the first key
 *   is drawn in both slots, a pose blended with itself being that pose.
 *   Every other far pair is drawn from a key frame built at `blend`
 *   (InterpKeyFrameFar), in both slots: each node on the shortest turn
 *   between its keys (InterpKeyFrameLerp), a node whose own turn lies within
 *   a degree of half a turn on the way the drawn frame asks for, a rotation
 *   at every weight, so the take-offs of the jumps, the twists 209 and 213
 *   and rich3's crouch roll 219 turn through their in-between frames with her
 *   body whole.
 *
 * Both keep the interpolated branch and with it the arms' own time blend,
 * and the root offset follows (InterpRootNow reads the same pair; GetRootAt
 * turns and holds the previous tick's offset alike; a built frame carries
 * the offset vanilla's blend gives). The frames are built into static
 * buffers that live until the next call; each caller draws from them at
 * once. `flag` 0 is the view set, the only one that writes the -log lines.
 * What it costs: a table lookup on every call (InterpTurnOf) and another on
 * every blended one (InterpKeyFarOf, whose fill runs once per animation),
 * and a node blend for each of her fifteen nodes that differ between the
 * keys, only where a frame is built, and where a node turns within a degree
 * of half a turn on its own the drawn rotations of both keys besides; on a
 * blended frame of any other pair a call that returns at once without -log
 * (InterpNoteKeySeam). */
void LaraPoseFrames(short* frm[], long frac, float blend, long flag)
{
	static short turned[2][INTERP_KEYFRAME_MAX];
	static short built[INTERP_KEYFRAME_MAX];

	if (!lara_item || !anims || !frm || !frm[0])
		return;

	const short animNumber = lara_item->anim_number;
	const int nodes = objects[lara_item->object_number].nmeshes;

	if (nodes < 1 || nodes > 32)
		return;

	const ANIM_STRUCT* anim = &anims[animNumber];
	const long* bone = &bones[objects[lara_item->object_number].bone_index];
	const long key = frac && frm[1] ? LaraKeyOf(anim, frm) : -1;
	const bool farPair = key >= 0 && LaraKeyFar(animNumber, key);

	if (LaraTurnFrame(animNumber, lara_item->frame_number))
	{
		long flip, pair;

		InterpTurnOf(animNumber, &flip, &pair);
		InterpKeyFrameTurn(frm[0], nodes, turned[0]);

		/* frm[1] is not read at frac zero: it may lie past the level's
		 * frame block. */
		if (frac)
			InterpKeyFrameTurn(frm[1], nodes, turned[1]);

		if (flag == 0 && farPair)
			InterpNoteKeyFar(lara_item, frm[0], frm[1], false, nodes, bone);

		frm[0] = turned[0];
		frm[1] = frac ? turned[1] : turned[0];

		/* A far pair turned is built from the turned copies. The same half
		 * turn stands in front of both keys, so the drawn turn between the
		 * copies is the pair's own; an early turn is never held
		 * (InterpKeyHold). */
		if (farPair)
		{
			InterpKeyFrameFar(turned[0], turned[1], nodes, bone, blend, built);
			frm[0] = built;
			frm[1] = built;
		}

		if (flag == 0)
			InterpNoteTurn(lara_item, flip, pair);

		return;
	}

	if (!farPair)
	{
		/* A seam is left to vanilla's blend; -log names it. */
		if (flag == 0 && key >= 0)
			InterpNoteKeySeam(lara_item, key, frm[0], frm[1], nodes, bone);

		return;
	}

	const bool hold = LaraKeyHold(animNumber, key);

	if (flag == 0)
		InterpNoteKeyFar(lara_item, frm[0], frm[1], hold, nodes, bone);

	if (hold)
	{
		frm[1] = frm[0];
		return;
	}

	InterpKeyFrameFar(frm[0], frm[1], nodes, bone, blend, built);
	frm[0] = built;
	frm[1] = built;
}

void SetupSkelebobMeshswaps()
{
	lara.mesh_ptrs[LM_HEAD] = meshes[objects[LARA_EXTRA_MESH2].mesh_index + 2 * LM_HEAD];
	lara.mesh_ptrs[LM_TORSO] = meshes[objects[LARA_EXTRA_MESH2].mesh_index + 2 * LM_TORSO];
	skelly_backgunbak = lara.back_gun;
	skelly_rhandbak = lara.mesh_ptrs[LM_RHAND];
	skelly_lhandbak = lara.mesh_ptrs[LM_LHAND];

	if (lara.back_gun)
	{
		if (lara.back_gun == CROSSBOW_ANIM)
			lara.back_gun = UZI_ANIM;
		else if (lara.back_gun == HK_ANIM)
			lara.back_gun = PISTOLS_ANIM;
	}

	if (lara.gun_type == WEAPON_CROSSBOW)
	{
		if (lara.mesh_ptrs[LM_RHAND] == meshes[objects[CROSSBOW_ANIM].mesh_index + 2 * LM_RHAND])
			lara.mesh_ptrs[LM_RHAND] = meshes[objects[UZI_ANIM].mesh_index + 2 * LM_RHAND];
		else
			lara.mesh_ptrs[LM_RHAND] = meshes[objects[LARA_EXTRA_MESH2].mesh_index + 2 * LM_RHAND];
	}
	else if (lara.gun_type == WEAPON_HK)
	{
		if (lara.mesh_ptrs[LM_RHAND] == meshes[objects[HK_ANIM].mesh_index + 20])
			lara.mesh_ptrs[LM_RHAND] = meshes[objects[PISTOLS_ANIM].mesh_index + 2 * LM_RHAND];
		else
			lara.mesh_ptrs[LM_RHAND] = meshes[objects[LARA_EXTRA_MESH2].mesh_index + 2 * LM_RHAND];
	}
	
	lara.mesh_ptrs[LM_LHAND] = meshes[objects[LARA_EXTRA_MESH2].mesh_index + 2 * LM_LHAND];
}

void RestoreLaraMeshswaps()
{
	lara.mesh_ptrs[LM_HEAD] = meshes[objects[LARA].mesh_index + 2 * LM_HEAD];
	lara.mesh_ptrs[LM_TORSO] = meshes[objects[LARA].mesh_index + 2 * LM_TORSO];
	lara.back_gun = skelly_backgunbak;
	lara.mesh_ptrs[LM_RHAND] = skelly_rhandbak;
	lara.mesh_ptrs[LM_LHAND] = skelly_lhandbak;
}

void RenderIt(short current_room)
{
	ROOM_INFO* r;

	CurrentRoom = current_room;
	r = &room[current_room];
	r->test_left = 0;
	r->test_top = 0;
	phd_left = 0;
	phd_top = 0;
	phd_right = phd_winxmax;
	phd_bottom = phd_winymax;
	r->test_right = phd_winxmax;
	r->test_bottom = phd_winymax;
	outside = r->flags & ROOM_OUTSIDE;
	camera_underwater = r->flags & ROOM_UNDERWATER;
	r->bound_active = 2;
	draw_room_list[0] = current_room;
	room_list_start = 0;
	room_list_end = 1;
	number_draw_rooms = 0;

	if (outside)
	{
		outside_top = 0;
		outside_left = 0;
		outside_right = phd_winxmax;
		outside_bottom = phd_winymax;
	}
	else
	{
		outside_left = phd_winxmax;
		outside_top = phd_winymax;
		outside_bottom = 0;
		outside_right = 0;
	}

	// The cutscene walk starts the same way the game's does.
	g_reach_on = HDLevelActive() && tomb5.hd_reach;
	VisBeginWalk();

	if (g_reach_on)
		ReachBeginWalk(current_room);

	GetRoomBounds();

	if (g_reach_on)
		CarryArt();

	if (g_reach_on)
		ReachTrimRooms((long)w2v_matrix[M03], (long)w2v_matrix[M13],
			(long)w2v_matrix[M23]);

	ProcessClosedDoors();
	SkyDrawPhase();
	nPolyType = 0;

	phd_PushMatrix();
	phd_TranslateAbs(0, 0, 0);
	SaveD3DCameraMatrix();
	phd_PopMatrix();

	aResetFogBulbList();

	for (int i = 0; i < number_draw_rooms; i++)
	{
		r = &room[draw_rooms[i]];
		phd_PushMatrix();
		phd_TranslateAbs(r->x, r->y, r->z);
		CurrentRoom = draw_rooms[i];
		phd_left = r->left;
		phd_right = r->right;
		phd_top = r->top;
		phd_bottom = r->bottom;
		aSetViewMatrix();
		InsertRoom(r);
		phd_PopMatrix();
	}

	DoWeather();
	S_DrawFires();
	DrawLightning();

	for (int i = 0; i < number_draw_rooms; i++)
		PrintObjects(draw_rooms[i]);
}

void mRotBoundingBoxNoPersp(short* bounds, short* rotatedBounds)
{
	PHD_VECTOR pos[8];
	long x, y, z;
	short xMin, xMax, yMin, yMax, zMin, zMax;

	xMin = bounds[0];
	xMax = bounds[1];
	yMin = bounds[2];
	yMax = bounds[3];
	zMin = bounds[4];
	zMax = bounds[5];

	pos[0].x = xMin;
	pos[0].y = yMin;
	pos[0].z = zMin;

	pos[1].x = xMax;
	pos[1].y = yMin;
	pos[1].z = zMin;

	pos[2].x = xMin;
	pos[2].y = yMax;
	pos[2].z = zMin;

	pos[3].x = xMax;
	pos[3].y = yMax;
	pos[3].z = zMin;

	pos[4].x = xMin;
	pos[4].y = yMin;
	pos[4].z = zMax;

	pos[5].x = xMax;
	pos[5].y = yMin;
	pos[5].z = zMax;

	pos[6].x = xMin;
	pos[6].y = yMax;
	pos[6].z = zMax;

	pos[7].x = xMax;
	pos[7].y = yMax;
	pos[7].z = zMax;

	xMin = 0x7FFF;
	yMin = 0x7FFF;
	zMin = 0x7FFF;
	xMax = -0x7FFF;
	yMax = -0x7FFF;
	zMax = -0x7FFF;

	for (int i = 0; i < 8; i++)
	{
		x = (pos[i].x * phd_mxptr[M00] + pos[i].y * phd_mxptr[M01] + pos[i].z * phd_mxptr[M02]) >> W2V_SHIFT;
		y = (pos[i].x * phd_mxptr[M10] + pos[i].y * phd_mxptr[M11] + pos[i].z * phd_mxptr[M12]) >> W2V_SHIFT;
		z = (pos[i].x * phd_mxptr[M20] + pos[i].y * phd_mxptr[M21] + pos[i].z * phd_mxptr[M22]) >> W2V_SHIFT;

		if (x < xMin)
			xMin = (short)x;

		if (x > xMax)
			xMax = (short)x;

		if (y < yMin)
			yMin = (short)y;

		if (y > yMax)
			yMax = (short)y;

		if (z < zMin)
			zMin = (short)z;

		if (z > zMax)
			zMax = (short)z;
	}

	rotatedBounds[0] = xMin;
	rotatedBounds[1] = xMax;
	rotatedBounds[2] = yMin;
	rotatedBounds[3] = yMax;
	rotatedBounds[4] = zMin;
	rotatedBounds[5] = zMax;
}

void PrintRooms(short room_number)
{
	ROOM_INFO* r;

	CurrentRoom = room_number;
	r = &room[room_number];
	phd_left = r->left;
	phd_right = r->right;
	phd_top = r->top;
	phd_bottom = r->bottom;
	aSetViewMatrix();
	S_InsertRoom(r, 1);
}

void DrawStaticObjects(short room_number)
{
	ROOM_INFO* r;
	MESH_INFO* mesh;
	STATIC_INFO* sinfo;
	long clip, mip;
	short n;

	CurrentRoom = room_number;
	nPolyType = 1;
	r = &room[room_number];

	/* The static meshes of an HD room are already in its art - the Remaster
	 * baked them into the .TRG - so drawing them again would put a second
	 * lantern inside the first one; TROne avoids the same doubling with its
	 * baked-mesh rule (TROne-main, copy of 2026-10-06, commit 75c4c843,
	 * draw_mesh).
	 *
	 * TROne suppresses by mesh pointer and needs an exception for the case
	 * where a moveable claims that same pointer. This seam is narrower: it
	 * skips entries of a room's own MESH_INFO list and nothing else, so a
	 * moveable sharing the mesh still draws (it comes through
	 * DrawAnimatingItem, not through here). The exception cannot arise.
	 *
	 * Which entries: those whose place the art draws in this frame. hd_level
	 * asks the renderer once per level whether the art draws anything where
	 * each static stands - the room's own group, the carrier's art of a room
	 * drawn as another group's art, or another group's art - and
	 * HDStaticDrawnClassic answers with the frame's room list. Another
	 * group's art draws the object in a frame that lists that group, and its
	 * piece - that group's triangles of the object alone - in a frame that
	 * lists the static's room and none of its holders, so the object is the
	 * art at every angle; such a static with no piece is drawn here while no
	 * holder is listed. A static the .TRG has nothing for
	 * is still drawn here (103 of the game's 2,845), in any room. The answer
	 * follows the level as it was built and, on every frame, the HD textures
	 * row and the HD/SD switch (HDLevelActive): while they have the world
	 * drawn classic the art draws no room, and every static is drawn here.
	 *
	 * A static the game destroys at run time is handled on the other side:
	 * the .TRG's own destructible pieces are switched off with their
	 * MESH_INFO's Flags bit, so nothing has to be drawn here for it. */
	phd_PushMatrix();
	phd_TranslateAbs(r->x, r->y, r->z);
	phd_left = r->left;
	phd_right = r->right;
	phd_top = r->top;
	phd_bottom = r->bottom;

	mesh = r->mesh;

	for (int i = 0; i < r->num_meshes; i++, mesh++)
	{
		if (!(mesh->Flags & 1))
			continue;

		if (!HDStaticDrawnClassic(room_number, i, draw_rooms,
			(int)number_draw_rooms))
			continue;

		phd_PushMatrix();
		phd_TranslateAbs(mesh->x, mesh->y, mesh->z);
		phd_RotY(mesh->y_rot);
		n = mesh->static_number;
		sinfo = &static_objects[n];
		clip = S_GetObjectBounds(&sinfo->x_minp);

		if (clip)
		{
			mip = sinfo->flags & 0x3C;
			S_CalculateStaticMeshLight(mesh->x, mesh->y, mesh->z, mesh->shade, r);

			if (mip)
			{
				if (phd_mxptr[M23] >> 15 > (mip & 0xFFFC) << 8)
					n++;
			}

			sinfo = &static_objects[n];
			phd_PutPolygons(meshes[sinfo->mesh_number], clip);
		}

		phd_PopMatrix();
	}

	phd_PopMatrix();
}

void InterpolateMatrix()
{
	aInterpolateMatrix();

	/* The integer stack rounds by shifting - which is a floor and not a
	 * truncation. InterpBlendLong keeps that exactly, in eighths, with no
	 * float and no rounding call in it: the three original shifts are the
	 * same expression at alpha8 2, 4 and 6. */
	for (int i = 0; i < 12; i++)
		phd_mxptr[i] = InterpBlendLong(phd_mxptr[i], IMptr[i], IM_alpha8);
}

/* The arm chain's own blend starts here.
 *
 * From the shoulder on, an arm holding a weapon is driven not by Lara's
 * animation but by the weapon slot's, read through lara.right_arm /
 * lara.left_arm - and with the HK in state 0 to 4 the torso and the head are
 * read from it as well. Blending only the body's key frame pair in time would
 * leave those frames raw, so at sixty the world glides while the weapon in
 * her hands, most of the picture in first person, jumps once per logic tick.
 *
 * The two matrix stacks below this point carry the body's two key frames,
 * and their blend at the body's own factor is the joint the arm hangs from.
 * This collapses both stacks, integer and float, to exactly that and then
 * sets the arm's factor, the time fraction of the drawn frame. From here the
 * ordinary _I functions do the rest: stack one is walked with the arm's
 * previous frame, stack two with this tick's, and every InterpolateMatrix
 * below is the midpoint of the two poses the ticks either side of this frame
 * draw.
 *
 * With the HK in state 0 to 4 it is called twice. The torso collapses first,
 * at bone[25..27], and the two stacks are then walked apart again - head and
 * torso turned by the arm's previous frame on one stack and this tick's on
 * the other. By the time case WEAPON_HK reaches its own call the stacks hold
 * two different poses, so the second collapse takes the shoulder to its
 * midpoint in time and hangs both arm chains off that one joint. The arm is
 * then built once, from a shoulder already where the drawn body has it, so
 * the hand cannot drift off a forearm blended elsewhere. The factor is the
 * arm's in both calls. For the shotgun and the crossbow, which do not
 * collapse at the torso, the arm branch's call is the only one.
 *
 * That second call sits on the torso level, before either arm is pushed: on
 * a pushed copy inside the right arm, the pop after that arm would hand the
 * left arm the uncollapsed level, blended at the arm's factor from there on,
 * and the left hand would sawtooth (ARMLOG, game\interpolate.cpp, shows it).
 *
 * The caller reaches it only when InterpArmPrev said yes, which is never at
 * fps_mode 0 and never on a frame that carries a tick. */
void InterpArmStack(float alpha)
{
	InterpolateMatrix();

	memcpy(aIMXPtr, aMXPtr, 48);
	memcpy(IMptr, phd_mxptr, 48);

	IM_alpha = alpha;
	IM_alpha8 = InterpEighths(alpha);
	IM_rate = 8;
	IM_frac = IM_alpha8;
}

/* The arm matrices take the rotation whole from the matrix they are handed -
 * the arm chain leaves the second stack behind at the shoulder and is drawn
 * from one stack from there on - and blend only the three translation
 * elements, which is what keeps the hand on the end of a blended forearm. */
void InterpolateArmMatrix(long* mx)
{
	phd_mxptr[M00] = mx[M00];
	phd_mxptr[M01] = mx[M01];
	phd_mxptr[M02] = mx[M02];
	phd_mxptr[M10] = mx[M10];
	phd_mxptr[M11] = mx[M11];
	phd_mxptr[M12] = mx[M12];
	phd_mxptr[M20] = mx[M20];
	phd_mxptr[M21] = mx[M21];
	phd_mxptr[M22] = mx[M22];

	phd_mxptr[M03] = InterpBlendLong(phd_mxptr[M03], IMptr[M03], IM_alpha8);
	phd_mxptr[M13] = InterpBlendLong(phd_mxptr[M13], IMptr[M13], IM_alpha8);
	phd_mxptr[M23] = InterpBlendLong(phd_mxptr[M23], IMptr[M23], IM_alpha8);
}

void aInterpolateArmMatrix(float* mx)
{
	aMXPtr[M00] = mx[M00];
	aMXPtr[M01] = mx[M01];
	aMXPtr[M02] = mx[M02];
	aMXPtr[M10] = mx[M10];
	aMXPtr[M11] = mx[M11];
	aMXPtr[M12] = mx[M12];
	aMXPtr[M20] = mx[M20];
	aMXPtr[M21] = mx[M21];
	aMXPtr[M22] = mx[M22];

	aMXPtr[M03] = InterpBlendFloat(aMXPtr[M03], aIMXPtr[M03], IM_alpha);
	aMXPtr[M13] = InterpBlendFloat(aMXPtr[M13], aIMXPtr[M13], IM_alpha);
	aMXPtr[M23] = InterpBlendFloat(aMXPtr[M23], aIMXPtr[M23], IM_alpha);
}

void DrawEffect(short fx_num)
{
	FX_INFO* fx;
	OBJECT_INFO* obj;
	short* meshp;

	fx = &effects[fx_num];
	obj = &objects[fx->object_number];

	if (obj->draw_routine && obj->loaded)
	{
		phd_PushMatrix();
		phd_TranslateAbs(fx->pos.x_pos, fx->pos.y_pos, fx->pos.z_pos);

		if (phd_mxptr[M23] > phd_znear && phd_mxptr[M23] < phd_zfar)
		{
			phd_RotYXZ(fx->pos.y_rot, fx->pos.x_rot, fx->pos.z_rot);

			if (obj->nmeshes)
				meshp = meshes[obj->mesh_index];
			else
				meshp = meshes[fx->frame_number];

			phd_PutPolygons(meshp, -1);
		}

		phd_PopMatrix();
	}
}

void calc_animating_item_clip_window(ITEM_INFO* item, short* bounds)
{
	ROOM_INFO* r;
	short* door;
	long xMin, xMax, yMin, yMax, zMin, zMax;		//object bounds
	long xMinR, xMaxR, yMinR, yMaxR, zMinR, zMaxR;	//room bounds
	long xMinD, xMaxD, yMinD, yMaxD, zMinD, zMaxD;	//door bounds
	short rotatedBounds[6];
	short nDoors;

	r = &room[ClipRoomNum];

	if (item->object_number >= ANIMATING1 && item->object_number <= ANIMATING16 ||
		item->object_number >= DOOR_TYPE1 && item->object_number <= LIFT_DOORS2_MIP)
	{
		phd_left = r->left;
		phd_right = r->right;
		phd_top = r->top;
		phd_bottom = r->bottom;
		return;
	}

	phd_PushUnitMatrix();
	phd_RotYXZ(item->pos.y_rot, item->pos.x_rot, item->pos.z_rot);
	phd_mxptr[M03] = 0;
	phd_mxptr[M13] = 0;
	phd_mxptr[M23] = 0;
	mRotBoundingBoxNoPersp(bounds, rotatedBounds);
	phd_PopMatrix();

	xMin = item->pos.x_pos + rotatedBounds[0];
	xMax = item->pos.x_pos + rotatedBounds[1];
	yMin = item->pos.y_pos + rotatedBounds[2];
	yMax = item->pos.y_pos + rotatedBounds[3];
	zMin = item->pos.z_pos + rotatedBounds[4];
	zMax = item->pos.z_pos + rotatedBounds[5];

	xMinR = r->x + 1024;
	xMaxR = xMinR + ((r->y_size - 2) << 10);
	yMinR = r->maxceiling;
	yMaxR = r->minfloor;
	zMinR = r->z + 1024;
	zMaxR = zMinR + ((r->x_size - 2) << 10);

	if (xMin >= xMinR && xMax <= xMaxR && yMin >= yMinR && yMax <= yMaxR && zMin >= zMinR && zMax <= zMaxR)
	{
		phd_left = r->left;
		phd_right = r->right;
		phd_top = r->top;
		phd_bottom = r->bottom;
		return;
	}

	if (camera.pos.room_number != item->room_number && camera.pos.room_number != ClipRoomNum)
	{
		door = r->door;
		nDoors = *door++;

		for (; nDoors > 0; nDoors--, door += 16)
		{
			if (door[0] != camera.pos.room_number)
				continue;

			xMinD = door[4];	//skip door normal
			xMaxD = door[4];
			yMinD = door[5];
			yMaxD = door[5];
			zMinD = door[6];
			zMaxD = door[6];

			if (door[7] < xMinD)
				xMinD = door[7];
			else if (door[7] > xMaxD)
				xMaxD = door[7];

			if (door[8] < yMinD)
				yMinD = door[8];
			else if (door[8] > yMaxD)
				yMaxD = door[8];

			if (door[9] < zMinD)
				zMinD = door[9];
			else if (door[9] > zMaxD)
				zMaxD = door[9];

			if (door[10] < xMinD)
				xMinD = door[10];
			else if (door[10] > xMaxD)
				xMaxD = door[10];

			if (door[11] < yMinD)
				yMinD = door[11];
			else if (door[11] > yMaxD)
				yMaxD = door[11];

			if (door[12] < zMinD)
				zMinD = door[12];
			else if (door[12] > zMaxD)
				zMaxD = door[12];

			if (door[13] < xMinD)
				xMinD = door[13];
			else if (door[13] > xMaxD)
				xMaxD = door[13];

			if (door[14] < yMinD)
				yMinD = door[14];
			else if (door[14] > yMaxD)
				yMaxD = door[14];

			if (door[15] < zMinD)
				zMinD = door[15];
			else if (door[15] > zMaxD)
				zMaxD = door[15];

			xMinD += xMinR - 1024;
			xMaxD += xMinR - 1024;
			yMinD += yMinR;
			yMaxD += yMinR;
			zMinD += zMinR - 1024;
			zMaxD += zMinR - 1024;

			if (xMinD <= xMax && xMaxD >= xMin && yMinD <= yMax && yMaxD >= yMin && zMinD <= zMax && zMaxD >= zMin)
				break;
		}

		if (!nDoors)
		{
			phd_left = r->left;
			phd_right = r->right;
			phd_top = r->top;
			phd_bottom = r->bottom;
		}
	}
}

void SetRoomBounds(short* door, long rn, ROOM_INFO* actualRoom)
{
	ROOM_INFO* r;
	FVECTOR* v;
	FVECTOR* lastV;
	static FVECTOR vbuf[4];
	float x, y, z, tooNear, tL, tR, tT, tB;

	r = &room[rn];

	if (r->left <= actualRoom->test_left && r->right >= actualRoom->test_right && r->top <= actualRoom->test_top && r->bottom >= actualRoom->test_bottom)
		return;

	tL = (float)actualRoom->test_right;
	tR = (float)actualRoom->test_left;
	tB = (float)actualRoom->test_top;
	tT = (float)actualRoom->test_bottom;
	door += 3;
	v = vbuf;
	tooNear = 0;

	for (int i = 0; i < 4; i++, v++, door += 3)
	{
		v->x = aMXPtr[M00] * door[0] + aMXPtr[M01] * door[1] + aMXPtr[M02] * door[2] + aMXPtr[M03];
		v->y = aMXPtr[M10] * door[0] + aMXPtr[M11] * door[1] + aMXPtr[M12] * door[2] + aMXPtr[M13];
		v->z = aMXPtr[M20] * door[0] + aMXPtr[M21] * door[1] + aMXPtr[M22] * door[2] + aMXPtr[M23];
		x = v->x;
		y = v->y;
		z = v->z;

		if (z <= 0)
			tooNear++;
		else
		{
			z /= f_mpersp;

			if (z)
			{
				x = x / z + f_centerx;
				y = y / z + f_centery;
			}
			else
			{
				if (x < 0)
					x = (float)phd_left;
				else
					x = (float)phd_right;

				if (y < 0)
					y = (float)phd_top;
				else
					y = (float)phd_bottom;
			}

			if (x - 1 < tL)
				tL = x - 1;

			if (x + 1 > tR)
				tR = x + 1;

			if (y - 1 < tT)
				tT = y - 1;

			if (y + 1 > tB)
				tB = y + 1;
		}
	}

	if (tooNear == 4)
		return;

	if (tooNear > 0)
	{
		v = vbuf;
		lastV = &vbuf[3];

		for (int i = 0; i < 4; i++, lastV = v, v++)
		{
			if (lastV->z <= 0 == v->z <= 0)
				continue;

			if (v->x < 0 && lastV->x < 0)
				tL = 0;
			else if (v->x > 0 && lastV->x > 0)
				tR = phd_winxmax;
			else
			{
				tL = 0;
				tR = phd_winxmax;
			}

			if (v->y < 0 && lastV->y < 0)
				tT = 0;
			else if (v->y > 0 && lastV->y > 0)
				tB = phd_winymax;
			else
			{
				tT = 0;
				tB = phd_winymax;
			}
		}
	}

	if (tL < actualRoom->test_left)
		tL = actualRoom->test_left;

	if (tR > actualRoom->test_right)
		tR = actualRoom->test_right;

	if (tT < actualRoom->test_top)
		tT = actualRoom->test_top;

	if (tB > actualRoom->test_bottom)
		tB = actualRoom->test_bottom;

	if (tL >= tR || tT >= tB)
		return;

	if (r->bound_active & 2)
	{
		if (tL < r->test_left)
			r->test_left = (short)tL;

		if (tT < r->test_top)
			r->test_top = (short)tT;

		if (tR > r->test_right)
			r->test_right = (short)tR;

		if (tB > r->test_bottom)
			r->test_bottom = (short)tB;
	}
	else
	{
		/* The ring guard. Without it, a walk with more entries queued at once
		 * than the ring holds would overwrite an entry not yet dequeued and
		 * walk one room twice while losing another. It is unconditional, so
		 * the classic path is guarded as well as the HD path.
		 *
		 * It is a backstop rather than a limit: with kRoomListRing at 512
		 * against a queue that cannot be deeper than the 256 rooms a level may
		 * hold - see the note over the two arrays - no loadable level reaches
		 * this line. It is still counted (cap hit= on the ROOMWALK line). */
		if (room_list_end - room_list_start >= kRoomListRing)
		{
			g_reach_capped++;
			return;
		}

		// A power of two, so the wrap is a mask - see kRoomListRing.
		draw_room_list[room_list_end & (kRoomListRing - 1)] = rn;
		room_list_end++;
		r->bound_active |= 2;
		r->test_left = (short)tL;
		r->test_right = (short)tR;
		r->test_top = (short)tT;
		r->test_bottom = (short)tB;
	}
}

/* SetRoomBounds with the reach.
 *
 * The same arithmetic on the same four corners of the same portal record,
 * carried into two answers instead of one - see the note over
 * g_reach_on at the top of this file for what they are and why.
 *
 * The corner projection is done once and the classic rectangle is formed out
 * of its minima and maxima, which is what the seeded loop of the function
 * above computes: tL is the smallest of the parent's right edge and every
 * (x - 1), and a minimum of the same floats is the same float whichever
 * order it is taken in. The reach is that projection clamped against the
 * screen and against nothing else.
 *
 * Everything the classic function refuses for a reason other than the
 * parent's window is refused here too: the side test in the caller, the four
 * corners behind the eye, and the fixup that widens a portal straddling the
 * eye plane to the screen edge. */
static void SetRoomBoundsReach(short* door, long rn, ROOM_INFO* actualRoom)
{
	ROOM_INFO* r;
	FVECTOR* v;
	FVECTOR* lastV;
	static FVECTOR vbuf[4];
	float x, y, z, tooNear;
	float rawL, rawR, rawT, rawB;
	float cL, cR, cT, cB;		// the classic window
	float aL, aR, aT, aB;		// the reach

	const long parent = (long)(actualRoom - room);

	/* Out of the reach's fixed arrays. A level of more than 256 rooms does
	 * not load at all (game\setup.cpp) and a portal names a room of the
	 * level it is in, so this is a guard and not a case - and the fallback
	 * is the classic walk. */
	if (rn < 0 || rn >= 256 || parent < 0 || parent >= 256)
	{
		SetRoomBounds(door, rn, actualRoom);
		return;
	}

	r = &room[rn];

	const bool parentClassic = (g_reach_state[parent] & REACH_CLASSIC) != 0;
	const bool first = !(g_reach_state[rn] & REACH_SEEN);

	/* The classic early out, and it may only be taken when the reach has
	 * nothing to add either - that is, when this room has had its one visit
	 * already. */
	if (!first && r->left <= actualRoom->test_left &&
		r->right >= actualRoom->test_right &&
		r->top <= actualRoom->test_top &&
		r->bottom >= actualRoom->test_bottom)
		return;

	door += 3;
	v = vbuf;
	tooNear = 0;
	rawL = 1e30f;
	rawR = -1e30f;
	rawT = 1e30f;
	rawB = -1e30f;

	for (int i = 0; i < 4; i++, v++, door += 3)
	{
		v->x = aMXPtr[M00] * door[0] + aMXPtr[M01] * door[1] + aMXPtr[M02] * door[2] + aMXPtr[M03];
		v->y = aMXPtr[M10] * door[0] + aMXPtr[M11] * door[1] + aMXPtr[M12] * door[2] + aMXPtr[M13];
		v->z = aMXPtr[M20] * door[0] + aMXPtr[M21] * door[1] + aMXPtr[M22] * door[2] + aMXPtr[M23];
		x = v->x;
		y = v->y;
		z = v->z;

		if (z <= 0)
			tooNear++;
		else
		{
			z /= f_mpersp;

			if (z)
			{
				x = x / z + f_centerx;
				y = y / z + f_centery;
			}
			else
			{
				if (x < 0)
					x = (float)phd_left;
				else
					x = (float)phd_right;

				if (y < 0)
					y = (float)phd_top;
				else
					y = (float)phd_bottom;
			}

			if (x - 1 < rawL)
				rawL = x - 1;

			if (x + 1 > rawR)
				rawR = x + 1;

			if (y - 1 < rawT)
				rawT = y - 1;

			if (y + 1 > rawB)
				rawB = y + 1;
		}
	}

	if (tooNear == 4)
		return;

	cL = rawL < (float)actualRoom->test_right ? rawL : (float)actualRoom->test_right;
	cR = rawR > (float)actualRoom->test_left ? rawR : (float)actualRoom->test_left;
	cT = rawT < (float)actualRoom->test_bottom ? rawT : (float)actualRoom->test_bottom;
	cB = rawB > (float)actualRoom->test_top ? rawB : (float)actualRoom->test_top;
	aL = rawL;
	aR = rawR;
	aT = rawT;
	aB = rawB;

	if (tooNear > 0)
	{
		v = vbuf;
		lastV = &vbuf[3];

		for (int i = 0; i < 4; i++, lastV = v, v++)
		{
			if (lastV->z <= 0 == v->z <= 0)
				continue;

			if (v->x < 0 && lastV->x < 0)
				cL = aL = 0;
			else if (v->x > 0 && lastV->x > 0)
				cR = aR = phd_winxmax;
			else
			{
				cL = aL = 0;
				cR = aR = phd_winxmax;
			}

			if (v->y < 0 && lastV->y < 0)
				cT = aT = 0;
			else if (v->y > 0 && lastV->y > 0)
				cB = aB = phd_winymax;
			else
			{
				cT = aT = 0;
				cB = aB = phd_winymax;
			}
		}
	}

	if (cL < actualRoom->test_left)
		cL = actualRoom->test_left;

	if (cR > actualRoom->test_right)
		cR = actualRoom->test_right;

	if (cT < actualRoom->test_top)
		cT = actualRoom->test_top;

	if (cB > actualRoom->test_bottom)
		cB = actualRoom->test_bottom;

	// The reach against the screen, and against nothing else.
	if (aL < 0)
		aL = 0;

	if (aR > phd_winxmax)
		aR = phd_winxmax;

	if (aT < 0)
		aT = 0;

	if (aB > phd_winymax)
		aB = phd_winymax;

	/* Off the screen altogether, and the classic rectangle cannot survive
	 * that either: it is clamped against the parent's window, which is
	 * inside the screen. */
	if (aL >= aR || aT >= aB)
		return;

	// The classic window only exists under a parent that has one.
	const bool classicOk = parentClassic && cL < cR && cT < cB;

	// Nothing new: no classic window, and the reach has been here.
	if (!classicOk && !first)
		return;

	// The rectangle for the seam, and the mark that spends the one visit.
	if (first)
	{
		g_reach_box[rn][0] = (short)aL;
		g_reach_box[rn][1] = (short)aT;
		g_reach_box[rn][2] = (short)aR;
		g_reach_box[rn][3] = (short)aB;
	}
	else
	{
		if ((short)aL < g_reach_box[rn][0]) g_reach_box[rn][0] = (short)aL;
		if ((short)aT < g_reach_box[rn][1]) g_reach_box[rn][1] = (short)aT;
		if ((short)aR > g_reach_box[rn][2]) g_reach_box[rn][2] = (short)aR;
		if ((short)aB > g_reach_box[rn][3]) g_reach_box[rn][3] = (short)aB;
	}

	if (r->bound_active & 2)
	{
		// It was queued by an earlier portal, so its one reach visit is
		// spent already; all this one can still add is a classic window.
		if (classicOk)
		{
			if (g_reach_state[rn] & REACH_CLASSIC)
			{
				if (cL < r->test_left)
					r->test_left = (short)cL;

				if (cT < r->test_top)
					r->test_top = (short)cT;

				if (cR > r->test_right)
					r->test_right = (short)cR;

				if (cB > r->test_bottom)
					r->test_bottom = (short)cB;
			}
			else
			{
				r->test_left = (short)cL;
				r->test_right = (short)cR;
				r->test_top = (short)cT;
				r->test_bottom = (short)cB;
				g_reach_state[rn] |= REACH_CLASSIC;
			}
		}

		return;
	}

	// The ring guard, exactly as in the classic function above.
	if (room_list_end - room_list_start >= kRoomListRing)
	{
		g_reach_capped++;
		return;
	}

	draw_room_list[room_list_end & (kRoomListRing - 1)] = rn;
	room_list_end++;
	r->bound_active |= 2;

	/* Here the one reach visit is spent - after the ring guard,
	 * so that a room refused for want of a queue place can still be reached
	 * by a later portal of the same frame. */
	g_reach_state[rn] |= REACH_SEEN;

	if (classicOk)
	{
		g_reach_state[rn] |= REACH_CLASSIC;
		r->test_left = (short)cL;
		r->test_right = (short)cR;
		r->test_top = (short)cT;
		r->test_bottom = (short)cB;
	}
	else
		g_reach_state[rn] &= ~REACH_CLASSIC;
}

/* The visibility path's source of a queued room: `before` is the room's
 * bound_active ahead of the call that may have queued it. A room queued for
 * the first time this walk keeps the source that queued it. */
static void VisNote(long rn, int why, char before)
{
	if (rn < 0 || rn >= 256 || (before & 3) || !(room[rn].bound_active & 2) ||
		g_vis_why[rn] != 0xff)
		return;

	g_vis_why[rn] = (unsigned char)why;
	g_vis_count[why]++;
}

/* SetRoomBounds' tail for a rectangle the visibility path worked out itself:
 * clamped to the window `actualRoom` is seen through in this dequeue (or,
 * with `toScreen`, to the screen alone), then left alone when the room's
 * own window already holds it, and otherwise queued or widened exactly as a
 * portal's rectangle is. */
static void VisQueue(long rn, ROOM_INFO* actualRoom, float tL, float tT,
	float tR, float tB, bool toScreen = false)
{
	ROOM_INFO* r = &room[rn];
	const float l = toScreen ? 0.0f : (float)actualRoom->test_left;
	const float t = toScreen ? 0.0f : (float)actualRoom->test_top;
	const float rr = toScreen ? (float)phd_winxmax :
		(float)actualRoom->test_right;
	const float b = toScreen ? (float)phd_winymax :
		(float)actualRoom->test_bottom;

	if (tL < l)
		tL = l;

	if (tR > rr)
		tR = rr;

	if (tT < t)
		tT = t;

	if (tB > b)
		tB = b;

	if (tL >= tR || tT >= tB)
		return;

	if (r->left <= tL && r->right >= tR && r->top <= tT && r->bottom >= tB)
		return;

	if (r->bound_active & 2)
	{
		if (tL < r->test_left)
			r->test_left = (short)tL;

		if (tT < r->test_top)
			r->test_top = (short)tT;

		if (tR > r->test_right)
			r->test_right = (short)tR;

		if (tB > r->test_bottom)
			r->test_bottom = (short)tB;

		return;
	}

	// The ring guard of SetRoomBounds.
	if (room_list_end - room_list_start >= kRoomListRing)
	{
		g_reach_capped++;
		return;
	}

	draw_room_list[room_list_end & (kRoomListRing - 1)] = rn;
	room_list_end++;
	r->bound_active |= 2;
	r->test_left = (short)tL;
	r->test_right = (short)tR;
	r->test_top = (short)tT;
	r->test_bottom = (short)tB;
}

/* A guest of the room being walked (`r`, whose matrix is on the stack): the
 * box of its art in the room's inside, projected as SetRoomBounds projects
 * a portal's corners, one pixel out on every side. A box that reaches past
 * the eye's plane takes the whole window, as a portal straddling it takes
 * the screen's edges; one wholly behind it lists nothing. */
static void VisGuestBounds(const HDVisGuest& guest, long rn, ROOM_INFO* r)
{
	float tL = 1e30f, tR = -1e30f, tT = 1e30f, tB = -1e30f;
	int behind = 0;

	for (int c = 0; c < 8; c++)
	{
		const float lx = ((c & 1) ? guest.hi[0] : guest.lo[0]) - (float)r->x;
		const float ly = ((c & 2) ? guest.hi[1] : guest.lo[1]) - (float)r->y;
		const float lz = ((c & 4) ? guest.hi[2] : guest.lo[2]) - (float)r->z;
		float x = aMXPtr[M00] * lx + aMXPtr[M01] * ly + aMXPtr[M02] * lz +
			aMXPtr[M03];
		float y = aMXPtr[M10] * lx + aMXPtr[M11] * ly + aMXPtr[M12] * lz +
			aMXPtr[M13];
		float z = aMXPtr[M20] * lx + aMXPtr[M21] * ly + aMXPtr[M22] * lz +
			aMXPtr[M23];

		if (z <= 0)
		{
			behind++;
			continue;
		}

		z /= f_mpersp;

		if (!z)
		{
			behind++;
			continue;
		}

		x = x / z + f_centerx;
		y = y / z + f_centery;

		if (x - 1 < tL) tL = x - 1;
		if (x + 1 > tR) tR = x + 1;
		if (y - 1 < tT) tT = y - 1;
		if (y + 1 > tB) tB = y + 1;
	}

	if (behind == 8)
		return;

	if (behind)
	{
		tL = 0;
		tT = 0;
		tR = (float)phd_winxmax;
		tB = (float)phd_winymax;
	}

	VisQueue(rn, r, tL, tT, tR, tB);
}

/* The four corners of an opening or a wide portal as a portal record's
 * words: the normal, then the corners, which is all SetRoomBounds reads. */
static void VisDoor(const short n[3], const short v[4][3], short* door)
{
	for (int k = 0; k < 3; k++)
		door[k] = n[k];

	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
			door[3 + c * 3 + k] = v[c][k];
}

/* The three tables of the room being walked, after its portals: its guests,
 * and the rooms behind its bare wall faces that face the eye - the face's
 * normal pointing to the eye's side, the test GetRoomBounds makes of a
 * portal's - each by the face's own corners. */
static void VisExtend(ROOM_INFO* r)
{
	const long here = (long)(r - room);
	const HDVisGuest* guests = 0;
	const HDVisOpening* openings = 0;
	const int ng = HDVisRoomGuests((int)here, &guests);
	const int no = HDVisRoomOpenings((int)here, &openings);

	g_vis_dequeues++;

	for (int k = 0; k < ng; k++)
	{
		const int b = HDVisRoomOfGroup(guests[k].group);

		if (b < 0 || b >= number_rooms || b == here)
			continue;

		const char before = room[b].bound_active;

		VisGuestBounds(guests[k], b, r);
		VisNote(b, 1, before);
	}

	for (int k = 0; k < no; k++)
	{
		const HDVisOpening& o = openings[k];
		const float ex = (float)(w2v_matrix[M03] - r->x);
		const float ey = (float)(w2v_matrix[M13] - r->y);
		const float ez = (float)(w2v_matrix[M23] - r->z);
		const float side = o.n[0] * ((float)o.v[0][0] - ex) +
			o.n[1] * ((float)o.v[0][1] - ey) + o.n[2] * ((float)o.v[0][2] - ez);
		short door[15];

		if (side >= 0 || o.behind < 0 || o.behind >= number_rooms ||
			o.behind == here)
			continue;

		const char before = room[o.behind].bound_active;

		VisDoor(o.n, o.v, door);
		SetRoomBounds(door, o.behind, r);
		VisNote(o.behind, 2, before);
	}
}

/* A portal's corners (the words after its normal) projected as
 * SetRoomBounds projects them, one pixel out, the behind-the-eye fixup
 * taking the screen's edges; false when all four lie behind the eye. */
static bool VisPortalRect(const short* door, float* rect)
{
	float vx[4], vz[4], vy[4];
	int tooNear = 0;

	rect[0] = rect[1] = 1e30f;
	rect[2] = rect[3] = -1e30f;

	for (int i = 0; i < 4; i++)
	{
		const short* c = &door[3 + i * 3];

		vx[i] = aMXPtr[M00] * c[0] + aMXPtr[M01] * c[1] + aMXPtr[M02] * c[2] +
			aMXPtr[M03];
		vy[i] = aMXPtr[M10] * c[0] + aMXPtr[M11] * c[1] + aMXPtr[M12] * c[2] +
			aMXPtr[M13];
		vz[i] = aMXPtr[M20] * c[0] + aMXPtr[M21] * c[1] + aMXPtr[M22] * c[2] +
			aMXPtr[M23];

		if (vz[i] <= 0)
		{
			tooNear++;
			continue;
		}

		const float z = vz[i] / f_mpersp;
		float x = vx[i] < 0 ? 0.0f : (float)phd_winxmax;
		float y = vy[i] < 0 ? 0.0f : (float)phd_winymax;

		if (z)
		{
			x = vx[i] / z + f_centerx;
			y = vy[i] / z + f_centery;
		}

		if (x - 1 < rect[0]) rect[0] = x - 1;
		if (y - 1 < rect[1]) rect[1] = y - 1;
		if (x + 1 > rect[2]) rect[2] = x + 1;
		if (y + 1 > rect[3]) rect[3] = y + 1;
	}

	if (tooNear == 4)
		return false;

	for (int i = 0, last = 3; tooNear && i < 4; last = i, i++)
	{
		if ((vz[last] <= 0) == (vz[i] <= 0))
			continue;

		if (!(vx[i] > 0 && vx[last] > 0))
			rect[0] = 0;

		if (!(vx[i] < 0 && vx[last] < 0))
			rect[2] = (float)phd_winxmax;

		if (!(vy[i] > 0 && vy[last] > 0))
			rect[1] = 0;

		if (!(vy[i] < 0 && vy[last] < 0))
			rect[3] = (float)phd_winymax;
	}

	return true;
}

/* A portal on the visibility path: the classic rectangle, and where the art
 * opens wider round the portal (HDVisRoomWide) the rectangle of that
 * opening too, so that the room behind gets the window the art shows it
 * in. And where that wider opening has no part inside the window the room
 * is seen through, but lands on the screen, the room behind is listed with
 * the opening's rectangle clamped to the screen alone: the window is the
 * classic geometry's, and the art opens beside it. A portal the art opens
 * no wider than the classic level keeps the classic answer: from JOBY2's
 * court, room 30's portal lands on the screen beside the window, and
 * listing room 30 there draws the stairwell's pipes into the court's wall
 * (24,640 pixels at the court's camera). */
static void VisFollowPortal(short* door, long rn, ROOM_INFO* r)
{
	const int portal = (int)((door - 1 - (r->door + 1)) / 16);
	const HDVisWide* wide = HDVisRoomWide((int)(r - room), portal);
	char before = room[rn].bound_active;
	short corners[15];
	const short* open = door;
	float rect[4];

	SetRoomBounds(door, rn, r);
	VisNote(rn, 0, before);

	if (wide)
	{
		VisDoor(&door[0], wide->v, corners);
		before = room[rn].bound_active;
		SetRoomBounds(corners, rn, r);
		VisNote(rn, 3, before);
		open = corners;
	}

	if (!VisPortalRect(open, rect))
		return;

	// Some of it inside the window: the classic rectangle has it.
	if (rect[0] < r->test_right && rect[2] > r->test_left &&
		rect[1] < r->test_bottom && rect[3] > r->test_top)
		return;

	if (wide)
	{
		before = room[rn].bound_active;
		VisQueue(rn, r, rect[0], rect[1], rect[2], rect[3], true);
		VisNote(rn, 3, before);
		return;
	}

	/* A portal whose projection stops just short of the window, or meets it
	 * on an edge, lists the room behind with the strip it takes of the
	 * window grown by g_vis_edge. The classic walk refuses it, and a room
	 * whose art is drawn whole then drops out of the list for a frame as
	 * the eye sways over that edge - the sky shows where its art stood -
	 * while the frames either side list it. Walked again at the owner's
	 * 181 dropped rooms, 155 were refused that way, 71 of the 102 that
	 * could be measured standing 2 pixels or less off the window at 2,560
	 * (HDVisDefaults' edge). */
	const float e = g_vis_edge;
	const float l = (float)r->test_left - e;
	const float t = (float)r->test_top - e;
	const float rr = (float)r->test_right + e;
	const float b = (float)r->test_bottom + e;

	if (e <= 0.0f || rect[0] >= rr || rect[2] <= l || rect[1] >= b ||
		rect[3] <= t)
		return;

	before = room[rn].bound_active;
	VisQueue(rn, r, rect[0] > l ? rect[0] : l, rect[1] > t ? rect[1] : t,
		rect[2] < rr ? rect[2] : rr, rect[3] < b ? rect[3] : b, true);
	VisNote(rn, 4, before);
}

/* Which of the two SetRoomBounds this walk is using. Off the HD path
 * g_reach_on is false and this calls the classic function. */
static void FollowPortal(short* door, long rn, ROOM_INFO* r)
{
	if (g_vis_walk)
	{
		VisFollowPortal(door, rn, r);
		return;
	}

	if (g_reach_on)
		SetRoomBoundsReach(door, rn, r);
	else
		SetRoomBounds(door, rn, r);
}

void GetRoomBounds()
{
	ROOM_INFO* r;
	ROOM_PORTAL* p;
	short* door;
	long rn, drn, x, y, z;

	while (room_list_start != room_list_end)
	{
		rn = draw_room_list[room_list_start & (kRoomListRing - 1)];
		room_list_start++;
		r = &room[rn];
		r->bound_active -= 2;

		/* The classic half of this dequeue, and everything that hangs off
		 * it. `classicHere` is true for every room the classic walk would
		 * list - and with g_reach_on false it is always true, so the walk is
		 * the classic one.
		 *
		 * What hangs off it is not only the window. `outside`, the outside
		 * rectangle and `snow_outside` decide whether the sky and the
		 * weather are drawn at all, and a room the reach alone brought in
		 * must not turn them on: that would flash sky into a room the classic
		 * walk calls indoors. So the reach adds .TRG art to the picture and
		 * nothing else. */
		const bool classicHere = !g_reach_on || rn < 0 || rn >= 256 ||
			(g_reach_state[rn] & REACH_CLASSIC) != 0;

		if (classicHere)
		{
			if (r->test_left < r->left)
				r->left = r->test_left;

			if (r->test_top < r->top)
				r->top = r->test_top;

			if (r->test_right > r->right)
				r->right = r->test_right;

			if (r->test_bottom > r->bottom)
				r->bottom = r->test_bottom;
		}

		if (!(r->bound_active & 1))
		{
			/* The draw_rooms guard: a room refused here is not drawn; the
			 * alternative is writing past the array. The classic walk alone
			 * reaches 106 rooms in one JOBY4 frame (RoomProbe -reachsurvey,
			 * the eye at a room's own centre).
			 *
			 * With the array at kDrawRoomsMax = 256, the most rooms a level
			 * may have, and every room entering it once (the `bound_active &
			 * 1` test above), no loadable level reaches this line; it stays
			 * as a counted backstop. The array size matters because this
			 * guard fires during the walk while ReachTrimRooms runs only after
			 * it, so at a full array a classic room could be refused while
			 * reach-only rooms held places. */
			if (number_draw_rooms >= (long)(sizeof(draw_rooms) /
				sizeof(draw_rooms[0])))
			{
				g_reach_capped++;
				continue;
			}

			draw_rooms[number_draw_rooms] = (short)rn;
			number_draw_rooms++;
			r->bound_active |= 1;

			if (classicHere && r->flags & ROOM_OUTSIDE)
				outside = ROOM_OUTSIDE;
		}

		if (classicHere && r->flags & ROOM_OUTSIDE)
		{
			if (r->left < outside_left)
				outside_left = r->left;

			if (r->right > outside_right)
				outside_right = r->right;

			if (r->top < outside_top)
				outside_top = r->top;

			if (r->bottom > outside_bottom)
				outside_bottom = r->bottom;
		}

		phd_PushMatrix();
		phd_TranslateAbs(r->x, r->y, r->z);
		door = r->door;

		if (door)
		{
			for (drn = *door++; drn > 0; drn--, door += 15)
			{
				p = (ROOM_PORTAL*)door;
				door++;

				rn = p->rn;

				if (classicHere && room[rn].flags & ROOM_OUTSIDE)
					snow_outside = 1;

				if (p->normal[0])
				{
					x = r->x + p->v1[0] - w2v_matrix[M03];

					if (x && (x ^ p->normal[0]) < 0)
						FollowPortal(door, rn, r);
					else
						continue;
				}

				if (p->normal[1])
				{
					y = r->y + p->v1[1] - w2v_matrix[M13];

					if (y && (y ^ p->normal[1]) < 0)
						FollowPortal(door, rn, r);
					else
						continue;
				}

				if (p->normal[2])
				{
					z = r->z + p->v1[2] - w2v_matrix[M23];

					if (z && (z ^ p->normal[2]) < 0)
						FollowPortal(door, rn, r);
					else
						continue;
				}
			}
		}

		if (g_vis_walk)
			VisExtend(r);

		phd_PopMatrix();
	}
}

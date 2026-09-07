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
#include "../specific/profile.h"		// PROFLOG's frame bracket (#148)
#include "interpolate.h"		// the frame between two logic ticks (#148, order 2)
#include "interpolate_math.h"		// the pose midpoint and the one blend factor (#148, order 4)
#include "../specific/hd/hd_level.h"
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

/* THE ONE FACTOR (#148, order 4).
 *
 * Until this order the blend between the two matrix stacks was three
 * hard-wired branches, written out twelve times each in four functions, and
 * chosen by the pair (IM_frac, IM_rate). IM_alpha is that choice as a number:
 * InitInterpolate computes it from the pair exactly as the branches did, so
 * every animation in the game blends the way it always has, and
 * InitInterpolateAlpha lets the TIME interpolation ask for a factor no pair
 * of integers can name - the midpoint of two poses (see interpolate_math.h).
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

/* R6 (#148, order 3): WHICH BODIES THE DRAW SAW THIS FRAME.
 *
 * PrintObjects sets a byte per item; StepAfterDeathFade, called once per
 * logic tick from ControlPhase, counts them up and kills the ones that
 * reached 128. One byte per item slot and no allocation, and the two long
 * notes at the two ends say why the counting had to leave the draw at all. */
static unsigned char g_afterDeathSeen[MAX_ITEMS];

static long outside_top;
static long outside_left;
static long outside_right;
static long outside_bottom;

static short* skelly_rhandbak;
static short* skelly_lhandbak;
static short skelly_backgunbak;

static long number_draw_rooms;
static short draw_rooms[100];
static long room_list_start = 0;
static long room_list_end = 0;
static long draw_room_list[128];
static long camera_underwater;
static short ClipRoomNum;

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
	/* The pair still stands, because InitInterpolate2 (game\sphere.cpp) and
	 * every reader of IM_frac / IM_rate outside this file expect it, and
	 * because a pair of integers says WHERE IN AN ANIMATION the blend is,
	 * which the factor alone no longer does once order 4 hands in midpoints.
	 * The factor is derived from the pair by the same three-way test the four
	 * blend functions used to make twelve times each. */
	IM_rate = rate;
	IM_frac = frac;
	IM_alpha = InterpKeyAlpha(frac, rate);
	IM_alpha8 = InterpEighths(IM_alpha);
	IMptr = IMstack;
	aIMXPtr = aIFMStack;
	memcpy(IMstack, phd_mxptr, 48);
	memcpy(aIFMStack, aMXPtr, 48);
}

/* THE SAME SET-UP WITH A FACTOR THAT IS NOT A KEY FRAME FRACTION (#148, order
 * 4). The time interpolation asks for the MIDPOINT of the two poses the two
 * neighbouring logic ticks draw, which is an eighth of a key frame interval
 * as often as not and has no (frac, rate) that names it.
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

	/* ONE FACTOR, TWELVE ELEMENTS (#148, order 4). What stood here was three
	 * branches on (IM_frac, IM_rate), each of them writing out all twelve
	 * matrix elements by hand - a hundred and eight lines for three numbers.
	 * InterpBlendFloat is those three numbers' own arithmetic, expression for
	 * expression, so this loop produces the same floats to the last bit as
	 * the branches did (interpolate_math.h says why the expressions and not
	 * an algebraic rearrangement of them). */
	for (int i = 0; i < 12; i++)
		matrixp[i] = InterpBlendFloat(matrixp[i], iMatrixp[i], IM_alpha);
}

long DrawPhaseGame()
{
	ProfileFrameBegin();

	/* THE IN-BETWEEN FRAME (#148, order 2).
	 *
	 * Order 1 left every second frame a duplicate of the one before it -
	 * sixty presents of thirty distinct pictures. This is the line that
	 * fills them in: it writes the halfway point between the last two logic
	 * ticks into items[], effects[], the braid and the camera, and
	 * everything below draws from those globals without knowing anything
	 * has happened. InterpEndFrame, at the bottom of this function, puts the
	 * original bytes back before the next tick reads them.
	 *
	 * It stands ABOVE CalcLaraMatrices on purpose - that is the first thing
	 * in the frame that reads lara_item->pos, and the HD seam takes Lara's
	 * matrices from what it produces. The whole of specific\hd\ therefore
	 * follows this order with not one line changed in it.
	 *
	 * At fps_mode 0, InterpAlphaNow returns 1 and this call writes nothing
	 * at all. */
	InterpBeginFrame(InterpAlphaNow());

	ProfBegin(PROF_GATHER);
	CalcLaraMatrices(0);
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

	/* THE CLOCK SPLIT (#148, order 1). THIS present, and no other, may run at
	 * sixty; every modal loop in the game keeps two ticks and therefore keeps
	 * its per-present animation counters at the speed they were written for
	 * (see the note over gPresentMinTicks in specific\output.h).
	 *
	 * Nothing else has to change for the game to stay at thirty. S_DumpScreen
	 * returns how many 1/60 s ticks the frame really took, DoLevel hands that
	 * straight to ControlPhase as nframes, and ControlPhase's own accumulator
	 * - `for (framecount += nframes; framecount > 0; framecount -= 2)`
	 * (game\control.cpp) - was already counting in 1/60 s units and already
	 * spending two of them on a logic step. Feed it one per frame and it runs
	 * a tick on every second call: sixty presents, thirty ticks, and not one
	 * line of the simulation touched.
	 *
	 * Until order 2 there is no interpolation, so every second frame is a
	 * duplicate of the one before it. That is not a shortcoming of this
	 * order, it IS this order's proof: the picture stands still for two
	 * frames while the clock runs at sixty.
	 *
	 * NOT EVERY FRAME THAT COMES THROUGH HERE IS A PLAYED ONE (#148, order
	 * 1b). The title menu draws through this function too, and its menu code
	 * reads input once per present while the input itself is written once per
	 * tick - at sixty it therefore read every keypress twice and walked two
	 * rows at a time. GameFrameMinTicks() is that rule; the measurement is
	 * written out over it in specific\output.cpp. */
	gPresentMinTicks = GameFrameMinTicks();
	camera.number_frames = S_DumpScreen();

	S_AnimateTextures(camera.number_frames);

	/* AND THE INTERPOLATION IS PUT BACK (#148, order 2), inside the profile
	 * bracket so that what it costs is in `work` like everything else the
	 * frame did. Every value InterpBeginFrame overwrote goes back to the
	 * byte, and the view matrix is rebuilt from the live camera - the next
	 * ControlPhase may run no tick at all, and then nothing else would. */
	InterpEndFrame();

	/* ProfileFrameEnd BEFORE the restore, on purpose: it judges this frame
	 * against the slot this frame actually asked for, and gPresentMinTicks is
	 * where that number lives. Nothing between the present and here presents
	 * anything, so the window costs nothing. */
	ProfileFrameEnd();
	gPresentMinTicks = 2;
	return camera.number_frames;
}

void SkyDrawPhase()
{
	// A phase of its own (#148) and not part of `output`: this is the one
	// place in the frame that fills and draws the classic buckets and the
	// sort list OUTSIDE S_OutputPolyList, so without it the two would look
	// like output that output never spent. It also carries the sky
	// lightning machine, which is one of the counters order 3 has to guard.
	ProfScope prof(PROF_SKY);

	if (outside)
	{
		if (!objects[HORIZON].loaded)
		{
			outside = -1;
			return;
		}

		if (gfLevelId == LVL5_GALLOWS_TREE || gfLevelId == LVL5_LABYRINTH || gfLevelId == LVL5_OLD_MILL)
			DrawMoon();

		if (BinocularRange)
			AlterFOV(short(GAME_FOV - BinocularRange));

		phd_PushMatrix();
		phd_TranslateAbs(camera.pos.x, camera.pos.y, camera.pos.z);

		/* R7 (#148, order 3): THE STORM IS A CLOCK, so it runs on logic
		 * ticks and not on drawn frames.
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
		 * every other thing on that frame does. */
		if ((gfLevelFlags & GF_LIGHTNING) && !InterpIsTweenFrame())
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

		nPolyType = 6;
		DrawBuckets();
		DrawSortList();

		/* #123 (fix round R3a): THE HD SKY DOME, in the classic horizon's own
		 * place in this phase - after the moon that was drawn a moment ago
		 * and before the stars below, because none of the three writes depth
		 * and the order in this function is the order on the screen.
		 *
		 * The .TRG carries a sky group of its own past the last room: a
		 * panorama band on a sphere plus its own scrolling cloud discs. Where
		 * it exists it replaces BOTH things the classic path draws here - the
		 * two flat colour layers and the HORIZON mesh - because it already
		 * contains them. What it does NOT contain, and what therefore still
		 * runs below, is the weather: the moon, the stars and the lightning
		 * flash, which the game paints THROUGH layer 1 in the lightning's own
		 * colour and which would otherwise be lost with the layer.
		 *
		 * With hd_enable off, before a level is built, or in a level whose
		 * .TRG has no sky group, HDFrameSky returns false on its first test
		 * and every line below runs exactly as it did. */
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

	/* #148, order 4: the same call as before at thirty hertz and on every
	 * frame that carries a logic tick - GetFrames, the same pair, the same
	 * factor. On the frame between two ticks it is the midpoint of the two
	 * poses those ticks draw, and `frac` is then 1 for animations that never
	 * interpolated at all in vanilla (rate 1 - 3619 of them across the
	 * sixteen level files), which is what makes a rate-1 walk cycle move on
	 * every drawn frame instead of every second one. */
	frac = GetFramesNow(item, frm, &blend);
	obj = &objects[item->object_number];

	if (obj->shadow_size)
		S_PrintShadow(obj->shadow_size, frm[0], item);

	phd_PushMatrix();
	phd_TranslateAbs(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
	phd_RotYXZ(item->pos.y_rot, item->pos.x_rot, item->pos.z_rot);

	/* #123 (S3-2): IS THIS ITEM DRAWN BY THE HD PATH?
	 *
	 * The answer costs one array lookup and is false for the whole game
	 * unless hd_enable and hd_objects are both on and the level built its
	 * models. When it is true, not one classic polygon of this item goes into
	 * the buckets: the walk below runs exactly as it always did - the same
	 * animation, the same frames, the same interpolation, the same mesh tree
	 * - and instead of handing each node's mesh to phd_PutPolygons it hands
	 * the node's MATRIX to hd_object, which is what a skinned model wants.
	 *
	 * Taken here and not further down because the distance model right below
	 * has to know: an HD item never switches to its MIP slot. The remaster
	 * ships no reduced model, the MIP slot's own name would bind a different
	 * .TRM or none at all, and a door that turned from HD to classic at five
	 * sectors would pop in front of the player every time he walked away from
	 * it. Enemies already skip the MIP for their own reason; HD items now
	 * join them, and everything else keeps it. */
	const bool hd = HDObjectBegin(item, obj);

	/* #148, order 4, and it is the plan's own proof for this order's HD half:
	 * an item drawn by the HD path with a pose blended in TIME goes through
	 * HDObjectNode's `interpolated` branch (specific\hd\hd_object.cpp), which
	 * records the matrix phd_PutPolygons_I would have drawn the classic mesh
	 * with. Not one line of specific\hd\ changed for this order; the counter
	 * is here, where both facts are known, and reaches INTERPLOG as pose_hd. */
	if (hd && frac == 2)
		InterpNotePoseHD();

	// Distance models (MIP/LOD) are switched off for ENEMIES - they are
	// always drawn at full resolution.
	//
	// The reason is not looks but safety: the slot behind it is taken
	// BLINDLY here as soon as anything is loaded at all. In vanilla that
	// works out because level and enemies were built together; for the
	// randomizer it does not - it puts enemies into levels whose MIP slot
	// carries entirely foreign material. From 5120 units on, the enemy
	// would be drawn as that foreign material, with ITS bones and ITS
	// frame, i.e. as a torn-up lump. Measured across all 13 levels
	// (SlotAnims): 51 such combinations are possible.
	//
	// What is lost is almost nothing: MIPs are barely used in TR5. There
	// are exactly SEVEN real cases - SWAT, SWAT+, guard and Two Gun in the
	// three rich levels -, all with the same mesh count as the original
	// (15/15), i.e. practically the same silhouette at five sectors'
	// distance. Nobody needs the render savings of 2000 anymore.
	//
	// "intelligent" separates cleanly: all enemies with object_mip carry
	// it (verified), doors and bridges (mip 2048-4096) do not and keep
	// their distance model.
	if (!hd && obj->object_mip && !obj->intelligent && (obj + 1)->loaded &&
		phd_mxptr[M23] >> 16 > obj->object_mip)
		obj++;

	calc_animating_item_clip_window(item, frm[0]);
	clip = S_GetObjectBounds(frm[0]);

	// CLIPLOG (#121), second version. The first wrote only at clip == 0 and
	// stayed EMPTY for the blinking soldier. That ruled out the bounds check
	// as the culprit and moved the question: does this function RUN at all
	// during the invisible frames, and if so with which anim and which clip?
	// Hence every NEW (slot, anim) pair of every thinking enemy, with clip,
	// room and status. If an anim never shows up in the log, the enemy is
	// not drawn in it at all - then the fault lies before this function
	// (room list, status), not inside it.
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

	// MESHLOG (#121, sixth measurement): aCheckMeshClip discards meshes
	// whose box lies entirely outside the current window f_left..f_bottom.
	// The counter says, after drawing, whether the body died THERE - and the
	// window says which cutout it was tested against.
	const long rejectsBefore = gLogEnabled ? aMeshClipRejects : 0;

	// Arm CLIPDETAIL for THIS thinker only - the global throttle otherwise
	// also caught legitimate clip-aways (doors behind the camera) and made
	// the lines ambiguous.
	if (gLogEnabled)
		aClipReportObj = obj->intelligent ? item->object_number : -1;

	if (clip)
	{
		CalculateObjectLighting(item, frm[0]);

		if (!item->data)
			data = no_rotation;
		else
			data = (short*)item->data;

		bit = 1;
		meshpp = &meshes[obj->mesh_index];
		bone = &bones[obj->bone_index];

		if (frac)
		{
			InitInterpolateAlpha(blend);
			phd_TranslateRel_ID(frm[0][6], frm[0][7], frm[0][8], frm[1][6], frm[1][7], frm[1][8]);
			rot = frm[0] + 9;
			rot2 = frm[1] + 9;
			gar_RotYXZsuperpack_I(&rot, &rot2, 0);

			/* #123 (S3-2): THE SEAM. Either the node's matrix goes to the
			 * HD path or its mesh goes into the buckets - never both. A node
			 * the HD model does not cover is drawn by NOBODY: TROne tried
			 * the classic fallback there and took it out again after user
			 * reports of classic patches sitting on an HD body
			 * (game_renderer.cpp:1825-1830).
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

	/* #123 (S3-2): the item's node walk is over. HDObjectEnd works out the
	 * light out of the engine's own light lists and puts the item on the
	 * frame's list; with clip == 0 above, no node was recorded and it drops
	 * the item again. Outside `if (clip)` so that the bracket HDObjectBegin
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

		// SKIPLOG (#121, third measurement): the blinking soldier (Mafia2,
		// slot 51) NEVER turned up in the CLIPLOG - so he does not even
		// reach DrawAnimatingItem. The only branch before it is this one.
		// One line per (item, status, anim), thinkers only: then it is in
		// black and white which status sorts him out and what his control
		// code is playing at that moment.
		// Second version: no longer ONCE per (item, anim) but per item
		// again every 30 frames - the first version saw only the very first
		// entry of the blink cycle and was blind afterwards. data is logged
		// along: if the AI slot survives the invisibility, the status change
		// does NOT come from slot allocation.
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

		/* R6 (#148, order 3): THE CORPSE FADE IS GAME STATE, so the DRAW
		 * only writes down that it saw the body and the LOGIC TICK does the
		 * counting. StepAfterDeathFade(), below, is the other half.
		 *
		 * What stood here was
		 *
		 *     if (item->after_death < 128 && item->after_death > 0 &&
		 *         !(wibble & 3))
		 *             item->after_death++;
		 *
		 *     if (item->after_death == 128)
		 *             KillItem(i);
		 *
		 * and it has three separate problems, only one of which is about
		 * the frame rate:
		 *
		 * ONE. It counts per drawn frame, so at sixty every body would fade
		 * out and be deleted in half the time.
		 *
		 * TWO. `!(wibble & 3)` is DEAD and has been for as long as this
		 * source has existed. ControlPhase steps `wibble = (wibble + 4) &
		 * 0xFC` (game\control.cpp), so the low two bits are always zero and
		 * the test is always true. It was plainly meant to make the fade a
		 * quarter as fast - 128 steps at 30 Hz would be seventeen seconds
		 * instead of the 4.3 it actually takes. It is left dead on purpose:
		 * repairing it would change how long every body in the game lies
		 * there, which is a game change and not a frame-rate change.
		 *
		 * THREE, and this is a real bug that the move happens to remove.
		 * KillItem was being called from inside the walk of the room's own
		 * item list, `for (i = r->item_number; i != NO_ITEM; i =
		 * items[i].next_item)`. For a SPAWNED item (item_num >=
		 * level_items) KillItem ends with `item->next_item = next_item_free;
		 * next_item_free = item_num;` (game\items.cpp) - it hands the slot
		 * to the free list and REWRITES the very link the loop is about to
		 * follow. The walk then leaves the room and wanders into the free
		 * list, drawing and ageing whatever it finds there. Doing the kill
		 * on the tick, after the walk is over, cannot do that.
		 *
		 * WHAT DOES NOT CHANGE: only bodies in a room that was actually
		 * DRAWN age, exactly as before. That is why the draw marks and the
		 * tick does not simply sweep all 512 slots - a corpse in a room the
		 * camera never sees must still be lying there when the player comes
		 * back, which is what happens at thirty today.
		 *
		 * THE MARK IS WIDER THAN THE INCREMENT on purpose: `after_death > 0`
		 * and not `> 0 && < 128`, because the second `if` above also fired
		 * on a body that was ALREADY at 128 without being counted up to it.
		 * The skeleton writes the field directly (`item->after_death =
		 * (short)(phd_sqrt(info.distance) >> 4)`, game\skeleton.cpp), so
		 * that case is reachable. StepAfterDeathFade carries both tests
		 * over unchanged. */
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

/* R6 (#148, order 3): THE CORPSE FADE, ON THE LOGIC TICK.
 *
 * The other half of the mark PrintObjects leaves. Called once per tick from
 * ControlPhase, which is what makes a body take the same 4.3 seconds to
 * vanish at sixty frames a second as it does at thirty; and after the room
 * walk rather than inside it, which is what makes KillItem safe (the long
 * note in PrintObjects has the three reasons).
 *
 * The two tests are carried over from that code word for word, including the
 * `< 128` that stops the count and the `== 128` that kills, so that the only
 * thing that changed is WHEN they run. The dead `!(wibble & 3)` is gone with
 * its own note there; it was always true and dropping it changes nothing.
 *
 * The marks are cleared as they are read, so a body whose room is not drawn
 * for a while simply does not age - which is exactly what happens today. */
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

void DrawRooms(short current_room)
{
	ROOM_INFO* r;
	long lx, ly, lz;
	short lr;

	DoMonitorScreen();
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
	snow_outside = 0;
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
		outside_top = phd_winymax;
		outside_left = phd_winxmax;
		outside_right = 0;
		outside_bottom = 0;
	}

	GetRoomBounds();
	ProcessClosedDoors();

	if (gfLevelId != LVL5_TITLE)
		SkyDrawPhase();

	if (objects[LARA].loaded && !(lara_item->flags & IFL_INVISIBLE))
	{
		nPolyType = 4;

		if (lara_item->mesh_bits && !SCNoDrawLara)
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

	/* R9 (#148, order 3): aUpdate is `aWibble++` and nothing else
	 * (specific\alexstuff.cpp). aWibble is a UV scroll read by the water
	 * surfaces and the flat sky (specific\specificfx.cpp), so at sixty it
	 * would scroll at double speed. The guard is at the call and not inside
	 * aUpdate so that the counter and its rule stand on the same page as
	 * the rest of the frame. */
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

/* THE POSE THIS DRAWN FRAME WEARS, when the frame falls between two logic
 * ticks (#148, order 4).
 *
 * `prevAnim` and `prevFrame` are what the item wore before the last tick -
 * the snapshot game\interpolate.cpp keeps - and `t` is how far this frame
 * stands between then and now, a half at sixty hertz. What comes back is the
 * key frame PAIR and the factor whose blend is the MIDPOINT OF THE TWO POSES
 * THE GAME DRAWS AT THIRTY; interpolate_math.h derives it and says why it is
 * a midpoint and not a freshly computed "correct" fraction.
 *
 * Returns 0 when there is nothing to blend, and then nothing has been
 * written: the caller falls back to GetFrames and the frame is drawn exactly
 * as thirty hertz draws it. That is rule K6 of the plan and it covers the
 * animation NUMBER changing, which is tested here because only the caller's
 * side knows it, as well as every frame-number case InterpPoseAt names. */
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

	if (!InterpPoseAt(prevFrame, item->frame_number, anim->frame_base, rate,
		anim->frame_end, t, &key, &b))
		return 0;

	frm[0] = anim->frame_ptr + size * key;
	frm[1] = frm[0] + size;

	/* AND THE PAIR A MATRIX BLEND CANNOT SURVIVE (#123, fix round S, S2).
	 *
	 * An element-wise lerp of two rotations is a SHRINK - cos(D/2) of the
	 * bone's own frame at the midpoint - and at half a turn the frame is
	 * singular and whatever hangs off it lands nowhere real. That is the leg
	 * pointing the wrong way in the play test's dive roll.
	 *
	 * ONLY FOR RATE 1, and the reason is the whole rule: every other rate
	 * puts the tween frame INSIDE the very segment vanilla blends the two
	 * tick frames on, so it is no worse than its own neighbours and refusing
	 * it would be a jump. A rate 1 pair is two adjacent stored frames a whole
	 * game frame apart that vanilla NEVER blends at all - which is the one
	 * thing order 4 added, and the one place a pose can be too far.
	 *
	 * Measured, ModelProbe -poserisk: over ANDREA1's 319 Lara animations this
	 * refuses animation 205 - whose HIPS turn 179.9 degrees between key 0 and
	 * key 1 - and nothing else. interpolate_math.h carries the arithmetic. */
	if (rate == 1 && InterpPoseFar(frm[0], frm[1],
		objects[item->object_number].nmeshes, INTERP_POSE_TURN_COS))
	{
		InterpNotePoseFar();

		return 0;
	}

	*blend = b;

	return 1;
}

/* WHAT THE THREE DRAW SITES ASK FOR (#148, order 4): the pose, whatever frame
 * rate the game is running at.
 *
 * On a frame that carries a logic tick, and at fps_mode 0 always, this is
 * GetFrames and nothing else - the same pair, the same factor, the same
 * picture, and InterpPoseTime returns zero before a single line of order 4 is
 * reached. On the frame between two ticks it is the midpoint of the poses
 * those two ticks draw.
 *
 * Returns 0 for "no blend, the pose is frm[0]", 1 for vanilla's own key frame
 * blend and 2 for a blend in TIME. That is the same shape GetFrames has, so
 * every call site keeps its own `if (frac)` exactly as it was written, and
 * the two reasons stay apart for the log line and for the HD counter. */
long GetFramesNow(ITEM_INFO* item, short* frm[], float* blend)
{
	short prevAnim = 0;
	short prevFrame = 0;
	const float t = InterpPoseTime(item, &prevAnim, &prevFrame);

	if (t > 0.0F)
	{
		if (GetFramesAt(item, prevAnim, prevFrame, t, frm, blend))
		{
			InterpNotePose(item);

			return 2;
		}

		/* K6: the animation changed, or the frame did not step by one. The
		 * pose of the CURRENT tick is drawn, which is one frame of thirty
		 * hertz inside a sixty hertz run and is invisible - but it is counted,
		 * because a skip count that is not tiny means the snapshot and the
		 * animation clock have fallen out of step with each other. */
		InterpNotePoseSkip();
	}

	long rate;
	const long frac = GetFrames(item, frm, &rate);

	if (!frac)
		return 0;

	*blend = InterpKeyAlpha(frac, rate);

	return 1;
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

	GetRoomBounds();
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

	/* #123 (S1-2): the static meshes of an HD room are already IN its art -
	 * the Remaster baked them into the .TRG - so drawing them again would put
	 * a second lantern inside the first one, which is the bug report TROne
	 * fixed with its baked-mesh rule (game_renderer.cpp:3722-3736).
	 *
	 * TROne suppresses by mesh POINTER and needs an exception for the case
	 * where a moveable claims that same pointer. Our seam is narrower: this
	 * skips a ROOM's own MESH_INFO list and nothing else, so a moveable
	 * sharing the mesh still draws (it comes through DrawAnimatingItem, not
	 * through here), and a room that stayed classic keeps every static it
	 * ever had. The exception cannot arise.
	 *
	 * TWO STATICS OF AN HD ROOM ARE STILL DRAWN HERE (#123, fix round R3b).
	 *
	 * The first is the one the .TRG has nothing for. Fix round B measured 21
	 * of them in ANDREA1's rooms 121 and 221: they are in no group at all, so
	 * the seam above took them away and put nothing in their place. hd_level
	 * asks the renderer, once per level, whether the room's art draws anything
	 * where the static stands, and the answer comes back through
	 * HDStaticIsClassic.
	 *
	 * The second used to be the hole this comment named: a static the game
	 * destroys at run time was gone from the classic list and went on standing
	 * in the baked art. It is closed on the other side now - the .TRG's own
	 * destructible pieces are switched off with their MESH_INFO's Flags bit -
	 * so nothing has to be drawn here for it. */
	const bool hd = HDRoomIsHD(room_number);

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

		if (hd && !HDStaticIsClassic(room_number, i))
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

	/* The integer stack, and it rounds by SHIFTING - which is a floor and not
	 * a truncation. InterpBlendLong keeps that exactly, in eighths, with no
	 * float and no rounding call in it: the three shifts the branches used
	 * are the same expression at alpha8 2, 4 and 6. */
	for (int i = 0; i < 12; i++)
		phd_mxptr[i] = InterpBlendLong(phd_mxptr[i], IMptr[i], IM_alpha8);
}

/* THE ARM CHAIN'S OWN BLEND STARTS HERE (#123, fix round U, U3).
 *
 * WHAT IT IS FOR. From the shoulder on, an arm holding a weapon is not driven
 * by Lara's animation at all but by the WEAPON slot's, read through
 * lara.right_arm / lara.left_arm - and with the HK in state 0 to 4 the TORSO
 * and the HEAD are read from it as well. Order 4 blended her body's key frame
 * pair in time and wrote those frames RAW, so at sixty the world glides and
 * the weapon in her hands, which in first person is most of the picture,
 * stands still and jumps once per logic tick.
 *
 * WHAT THIS DOES. The two matrix stacks below this point carry the body's two
 * key frames, and their blend at the body's own factor is the joint the arm
 * hangs from. This collapses them to exactly that - both stacks, integer and
 * float - and then sets the ARM's factor, which is the time fraction of the
 * drawn frame and not the body's key frame factor. From here the ordinary _I
 * machinery does the rest: stack one is walked with the arm's PREVIOUS frame,
 * stack two with this tick's, and every InterpolateMatrix below is the
 * midpoint of the two poses the ticks either side of this frame draw.
 *
 * WHAT THE SECOND CALL REALLY DOES, with the HK in state 0 to 4 (this
 * paragraph corrected in U7; the sentence that stood here said the second call
 * blended two stacks that were already equal, and that is only true where
 * nothing walked them apart in between). There the TORSO collapses first, at
 * bone[25..27], and the two stacks are then walked APART again - the head and
 * the torso are turned by the arm's previous frame on one stack and by this
 * tick's on the other, which is the whole point of collapsing there. By the
 * time case WEAPON_HK reaches its own call the two stacks hold two different
 * poses, so the second collapse is not free: it takes the SHOULDER to its
 * midpoint in time and hangs both arm chains off that one joint, instead of
 * letting each chain descend from its own tick's shoulder and blending them
 * at every node below.
 *
 * THAT IS THE ORDER THIS WANTS. Both readings are a midpoint of the same two
 * poses; collapsing first means the arm is built ONCE, from a shoulder that is
 * already where the drawn body has it, so the hand cannot drift off a forearm
 * that was blended somewhere else - and the alternative would have to blend
 * two chains that no longer share a root. The factor is the arm's in both
 * calls, so the second sets nothing the first did not.
 *
 * EVERYWHERE ELSE THE SECOND CALL IS THE ONLY ONE: the shotgun and the
 * crossbow do not collapse at the torso, so their arm branch collapses the
 * body's two key frames itself, which is what it is for.
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

/* THE ARM MATRICES take the ROTATION whole from the matrix they are handed -
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
		draw_room_list[room_list_end % 128] = rn;
		room_list_end++;
		r->bound_active |= 2;
		r->test_left = (short)tL;
		r->test_right = (short)tR;
		r->test_top = (short)tT;
		r->test_bottom = (short)tB;
	}
}

void GetRoomBounds()
{
	ROOM_INFO* r;
	ROOM_PORTAL* p;
	short* door;
	long rn, drn, x, y, z;

	while (room_list_start != room_list_end)
	{
		rn = draw_room_list[room_list_start % 128];
		room_list_start++;
		r = &room[rn];
		r->bound_active -= 2;

		if (r->test_left < r->left)
			r->left = r->test_left;

		if (r->test_top < r->top)
			r->top = r->test_top;

		if (r->test_right > r->right)
			r->right = r->test_right;

		if (r->test_bottom > r->bottom)
			r->bottom = r->test_bottom;

		if (!(r->bound_active & 1))
		{
			draw_rooms[number_draw_rooms] = (short)rn;
			number_draw_rooms++;
			r->bound_active |= 1;

			if (r->flags & ROOM_OUTSIDE)
				outside = ROOM_OUTSIDE;
		}

		if (r->flags & ROOM_OUTSIDE)
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

				if (room[rn].flags & ROOM_OUTSIDE)
					snow_outside = 1;

				if (p->normal[0])
				{
					x = r->x + p->v1[0] - w2v_matrix[M03];

					if (x && (x ^ p->normal[0]) < 0)
						SetRoomBounds(door, rn, r);
					else
						continue;
				}

				if (p->normal[1])
				{
					y = r->y + p->v1[1] - w2v_matrix[M13];

					if (y && (y ^ p->normal[1]) < 0)
						SetRoomBounds(door, rn, r);
					else
						continue;
				}

				if (p->normal[2])
				{
					z = r->z + p->v1[2] - w2v_matrix[M23];

					if (z && (z ^ p->normal[2]) < 0)
						SetRoomBounds(door, rn, r);
					else
						continue;
				}
			}
		}

		phd_PopMatrix();
	}
}

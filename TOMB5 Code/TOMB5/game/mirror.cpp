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
#include "mirror.h"
#include "delstuff.h"
#include "../specific/function_table.h"
#include "../specific/drawlara.h"
#include "tomb4fx.h"
#include "../specific/3dmath.h"
#include "gameflow.h"
#include "hair.h"
#include "lara.h"
#include "control.h"
#include "draw.h"
#include "objects.h"
#include "spotcam.h"
#include "mirror_math.h"
#include "../specific/function_stubs.h"
#include "../specific/hd/hd_level.h"
#include "../specific/hd/hd_object.h"

void Draw_Mirror_Lara()
{
	HAIR_STRUCT* hair;
	GUNSHELL_STRUCT* gunshell;
	long zplane;
	
	zplane = gfMirrorZPlane << 1;
	hair = &hairs[0][0];

	for (int i = 0; i < 6; i++)
	{
		hair->pos.z_pos = zplane - hair->pos.z_pos;
		hair->pos.y_rot = 0x8000 - hair->pos.y_rot;
		hair++;
	}

	for (int i = 0; i < 24; i++)
	{
		gunshell = &Gunshells[i];

		if (gunshell->counter)
			gunshell->pos.z_pos = zplane - gunshell->pos.z_pos;
	}

	lara_item->pos.z_pos = zplane - lara_item->pos.z_pos;
	lara_item->pos.x_rot = -lara_item->pos.x_rot;
	lara_item->pos.y_rot = -lara_item->pos.y_rot;
	lara_item->pos.z_rot += 0x8000;
	CalcLaraMatrices(2);
	SetCullCW();
	DrawLara(lara_item, 1);
	DrawGunshells();
	SetCullCCW();
	lara_item->pos.z_pos = zplane - lara_item->pos.z_pos;
	lara_item->pos.x_rot = -lara_item->pos.x_rot;
	lara_item->pos.y_rot = -lara_item->pos.y_rot;
	lara_item->pos.z_rot += 0x8000;
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

	for (int i = 0; i < 24; i++)
	{
		gunshell = &Gunshells[i];

		if (gunshell->counter)
			gunshell->pos.z_pos = zplane - gunshell->pos.z_pos;
	}

	hair = &hairs[0][0];

	for (int i = 0; i < 6; i++)
	{
		hair->pos.z_pos = zplane - hair->pos.z_pos;
		hair->pos.y_rot = 0x8000 - hair->pos.y_rot;
		hair++;
	}
}

/* The room a reflected item's origin may stand off the copy's own art: an
 * item's origin is at its feet and on its pivot, and a door's pivot stands
 * at the frame's edge. A quarter of a sector, along the glass's normal and in
 * height only (MirrorLandsInCopy says why not along the glass). */
#define MIRROR_ITEM_SLACK	256

/* One mirror of the HD level, drawn: the view reflected in the glass's plane
 * for the length of the call, every item in front of the glass whose
 * reflection lands in the copy behind it drawn through that view, Lara the
 * same, and the view put back.
 *
 * What is reflected is everything the item draw and Lara's draw take the
 * camera from: w2v_matrix and aW2VMx (the camera's rotation and position,
 * which phd_TranslateAbs builds every model's matrix from), the base of both
 * matrix stacks (which the braid starts each link from, hd_laradraw.cpp,
 * braid_matrices) and the top a push makes. All four are copied and copied
 * back, not reflected twice, so the frame's view comes back to the bit.
 *
 * The item keeps its pose: nothing of it is written, and what the draw puts
 * on the screen is the pose the frame draws - the interpolated one at 60 and
 * at the uncapped rate, the tick's at 30. What a draw changes in the item is
 * put back: the ambient fade CalcAmbientLight steps per call (item->il),
 * which would run twice as fast for an item drawn twice. A reflection casts
 * no shadow of its own - the slot's shadow size is 0 for the call, so it has
 * no blob and is drawn into no shadow map - and its effects are not
 * reflected: they are drawn from their own lists after the rooms. What the
 * slot's shadow size was is handed to the seam beforehand
 * (HDObjectReflectedBlob), and the mirror's index with HDObjectReflection:
 * the projected shadow's models' pass gives the reflection the shadows of
 * what it reflects, through that mirror's reflected view.
 *
 * The classic path culls by the winding on the screen, which the reflection
 * turns round, so SetCullCW for the call as Draw_Mirror_Lara does; the HD
 * models are drawn double sided and need nothing, and HDObjectReflection
 * tells the seam the items are a reflection.
 *
 * What a reflection may put on the screen is the copy behind the glass and
 * nothing past it. MirrorLandsInCopy decides by an origin, and a body about
 * an origin a few units inside the copy's end reaches past that end into the
 * real room beside it (RICH2: Lara 20 units inside glass 135's copy reaches
 * about 130 units past its end at z 74764, into corridor room 56's open
 * floor), and a body close to the glass reaches through the plane into the
 * room in front. So an HD draw is clipped per pixel against three planes
 * (MirrorClipPlanes, handed to the seam with HDObjectReflection), and a
 * classic draw is cut by the classic clipper to the copy's rectangle on the
 * screen (MirrorBoxWindow): f_left .. f_bottom, which every phd_PutPolygons
 * variant copies into clip_left .. clip_bottom and XYUVGClipper cuts to
 * (specific\clipping.cpp), and the mirror room's portal window narrowed to
 * it, which calc_animating_item_clip_window hands the doors and the
 * ANIMATING slots. `win` receives the rectangle; `index` is the mirror's
 * among the level's (HDLevelMirrorAt). */
static int DrawOneMirror(const HDLevelMirror& m, int index, bool* laraDrawn, long* win)
{
	const int a = m.axis;
	long frontMin[3], frontMax[3];
	int drawn = 0;

	// The volume in front of the glass the copy reflects.
	for (int k = 0; k < 3; k++)
	{
		frontMin[k] = m.mins[k] - MIRROR_ITEM_SLACK;
		frontMax[k] = m.maxs[k] + MIRROR_ITEM_SLACK;
	}

	frontMin[a] = 2 * m.plane - m.maxs[a] - MIRROR_ITEM_SLACK;
	frontMax[a] = 2 * m.plane - m.mins[a] + MIRROR_ITEM_SLACK;

	long w2v[indices_count], base[indices_count];
	float aw2v[indices_count], abase[indices_count];
	float q[9], t[3];

	memcpy(w2v, w2v_matrix, sizeof(w2v));
	memcpy(aw2v, aW2VMx, sizeof(aw2v));
	memcpy(base, matrix_stack, sizeof(base));
	memcpy(abase, aFMatrixStack, sizeof(abase));
	MirrorViewSpaceSetup(aw2v, abase, a, (float)m.plane, q, t);

	const long left = phd_left, right = phd_right, top = phd_top, bottom = phd_bottom;
	const long room_now = CurrentRoom;
	const float fLeft = f_left, fTop = f_top, fRight = f_right, fBottom = f_bottom;
	ROOM_INFO* mr = &room[m.room];
	const short roomWin[4] = { mr->left, mr->top, mr->right, mr->bottom };

	/* The copy's rectangle, through the real view and inside the mirror
	 * room's portal window. That window's right and bottom are inclusive -
	 * the camera room's are phd_winxmax and phd_winymax (DrawRooms) - and
	 * the rectangle's are exclusive, as f_right and f_bottom are, so the
	 * window goes in one past its edge and comes back one short of the
	 * rectangle's. A room the walk left without a window - an empty one,
	 * right before left - bounds nothing, and the whole screen stands in
	 * for it. */
	long portal[4] = { mr->left, mr->top, mr->right + 1, mr->bottom + 1 };

	if (portal[0] >= portal[2] || portal[1] >= portal[3])
	{
		portal[0] = 0;
		portal[1] = 0;
		portal[2] = phd_winxmax + 1;
		portal[3] = phd_winymax + 1;
	}

	MirrorBoxWindow(aW2VMx, m.mins, m.maxs, f_mpersp, f_centerx, f_centery,
		(long)f_mznear, portal, win);

	f_left = (float)win[0];
	f_top = (float)win[1];
	f_right = (float)win[2];
	f_bottom = (float)win[3];
	phd_left = win[0];
	phd_top = win[1];
	phd_right = win[2];
	phd_bottom = win[3];

	// The portal window narrowed to the rectangle, for the doors' window.
	mr->left = (short)(mr->left > win[0] ? mr->left : win[0]);
	mr->top = (short)(mr->top > win[1] ? mr->top : win[1]);
	mr->right = (short)(mr->right < win[2] - 1 ? mr->right : win[2] - 1);
	mr->bottom = (short)(mr->bottom < win[3] - 1 ? mr->bottom : win[3] - 1);

	phd_PushMatrix();
	MirrorView(w2v_matrix, a, m.plane, true);
	MirrorView(aW2VMx, a, (float)m.plane, true);
	MirrorView(matrix_stack, a, 0L, false);
	MirrorView(aFMatrixStack, a, 0.0f, false);
	MirrorView(phd_mxptr, a, 0L, false);
	MirrorView(aMXPtr, a, 0.0f, false);

	// The three planes, in the reflected view every reflected node lands in.
	float planes[12];

	MirrorClipPlanes(aW2VMx, a, m.plane, m.side, m.mins, m.maxs, planes);
	HDObjectReflection(true, planes, index);
	SetCullCW();

	/* Lara: the view set the frame worked out for her, moved into the
	 * reflected view (MirrorViewSpaceApply), and her draw with the mirror
	 * flag - her lighting as the frame has it, and in first person the other
	 * side culled (hd_laradraw.cpp). The same visibility as her own draw in
	 * DrawRooms, less the camera-too-close test: her reflection is a Lara at
	 * twice the distance to the glass. Drawn before the items: with the
	 * mirror flag DrawLara keeps the light arrays as they stand, and an
	 * item's CalculateObjectLighting in between would leave the classic
	 * body its light instead of hers. */
	const long lp[3] = { lara_item->pos.x_pos, lara_item->pos.y_pos,
		lara_item->pos.z_pos };

	*laraDrawn = false;

	if (objects[LARA].loaded && !(lara_item->flags & IFL_INVISIBLE) &&
		lara_item->mesh_bits && !SCNoDrawLara &&
		MirrorLandsInCopy(lp, a, m.plane, m.side, m.mins, m.maxs,
			MIRROR_ITEM_SLACK))
	{
		float saved[sizeof(lara_matrices) / sizeof(lara_matrices[0])];
		const short shadow = objects[LARA].shadow_size;
		const long polyType = nPolyType;

		memcpy(saved, lara_matrices, sizeof(saved));

		for (int n = 0; n + indices_count <= (int)(sizeof(saved) / sizeof(saved[0]));
			n += indices_count)
			MirrorViewSpaceApply(q, t, &lara_matrices[n]);

		objects[LARA].shadow_size = 0;
		nPolyType = 4;

		if (lara.skelebob)
			SetupSkelebobMeshswaps();

		DrawLara(lara_item, 1);

		if (lara.skelebob)
			RestoreLaraMeshswaps();

		nPolyType = polyType;
		objects[LARA].shadow_size = shadow;
		memcpy(lara_matrices, saved, sizeof(saved));
		*laraDrawn = true;
	}


	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* rm = &room[r];

		if (rm->x > frontMax[0] || rm->x + (rm->y_size << 10) < frontMin[0] ||
			rm->z > frontMax[2] || rm->z + (rm->x_size << 10) < frontMin[2] ||
			rm->maxceiling > frontMax[1] || rm->minfloor < frontMin[1])
			continue;

		for (short i = rm->item_number; i != NO_ITEM; i = items[i].next_item)
		{
			ITEM_INFO* item = &items[i];
			const long p[3] = { item->pos.x_pos, item->pos.y_pos, item->pos.z_pos };

			if (!MirrorLandsInCopy(p, a, m.plane, m.side, m.mins, m.maxs,
				MIRROR_ITEM_SLACK))
				continue;

			OBJECT_INFO* obj = &objects[item->object_number];

			if (item->status == ITEM_INVISIBLE || !obj->draw_routine)
				continue;

			const ITEM_LIGHT il = item->il;
			const short shadow = obj->shadow_size;

			HDObjectReflectedBlob(shadow != 0);
			obj->shadow_size = 0;
			PrintObjectReflection(item, (short)m.room);
			obj->shadow_size = shadow;
			item->il = il;
			drawn++;
		}
	}

	SetCullCCW();
	HDObjectReflection(false, 0);
	phd_PopMatrix();
	memcpy(w2v_matrix, w2v, sizeof(w2v));
	memcpy(aW2VMx, aw2v, sizeof(aw2v));
	memcpy(matrix_stack, base, sizeof(base));
	memcpy(aFMatrixStack, abase, sizeof(abase));
	phd_left = left;
	phd_right = right;
	phd_top = top;
	phd_bottom = bottom;
	f_left = fLeft;
	f_top = fTop;
	f_right = fRight;
	f_bottom = fBottom;
	mr->left = roomWin[0];
	mr->top = roomWin[1];
	mr->right = roomWin[2];
	mr->bottom = roomWin[3];
	CurrentRoom = room_now;
	return drawn;
}

void Draw_Mirror_Items()
{
	HDLevelMirror m;
	const int count = HDLevelMirrorCount();

	for (int i = 0; i < count; i++)
	{
		if (!HDLevelMirrorAt(i, &m) || !RoomOnDrawList((short)m.room))
			continue;

		// A mirror seen from behind shows nothing.
		const long camera = m.axis == 0 ? w2v_matrix[M03] : w2v_matrix[M23];

		if ((camera - m.plane) * m.side >= 0)
			continue;

		bool lara_drawn = false;
		long win[4];

		HDObjectReflectedTake();

		const int drawn = DrawOneMirror(m, i, &lara_drawn, win);
		const int hd = HDObjectReflectedTake();

		/* The mirror's log line, once a second while a mirror draws
		 * anything: how many items it reflected (hd of them through the HD
		 * models, the rest classic), the plane and its room, whether Lara
		 * was in it, and the copy's rectangle on the screen the classic draws
		 * were cut to (left, top, right, bottom; right and bottom exclusive). */
		static unsigned short last = 0;
		static bool said = false;

		if (gLogEnabled && (drawn || lara_drawn) &&
			(!said || (unsigned short)(GlobalCounter - last) >= 30))
		{
			said = true;
			last = GlobalCounter;
			Log("HDLOG mirror items=%d plane=%ld room=%d axis=%c lara=%d hd=%d "
				"win=%ld,%ld,%ld,%ld",
				drawn, m.plane, m.room, m.axis == 0 ? 'x' : 'z',
				lara_drawn ? 1 : 0, hd, win[0], win[1], win[2], win[3]);
		}
	}
}

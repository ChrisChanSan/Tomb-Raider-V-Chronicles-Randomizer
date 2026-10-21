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
#pragma once

#include <math.h>

/* Three rules of the HD object path, as pure functions, so that the engine,
 * ModelProbe and ModernTest judge by the same lines.
 *
 * The skeleton fit. The bind drift (hd_model.h, HDModelBindDrift) compares
 * the distances between the centres of a .TRM's nodes with the distances
 * between the slot's bind joints. A node whose mesh hangs far off its own
 * joint - a cabinet's body standing 1700 units above its origin, a door's
 * leaf 5000 units off its hinge - reads as a skeleton that does not fit
 * even when the .TRM sits exactly where the classic meshes sit. So a model
 * the drift refuses gets a second question, asked of the place itself:
 * per node, how far the centre of the .TRM's vertices that ride on that
 * node alone stands from the centre of the classic mesh of the same node,
 * both in the bind pose - the pose the bind fold draws both in. A model
 * made for this tree answers with a few dozen units; a foreign one does
 * not. The comparison of places is TROne's (Copyright 2026 Todesengel,
 * GPLv3), which picks a slot's tree for a stub that way
 * (host/render/game_renderer.cpp, the node_offset lambda: box centres by
 * the leading bone, within 256 units); here the drift's own centres.
 *
 * The pocket. A door that slides out of its doorway into the wall beside
 * it folds, at its open frame, its front and its back panel into one
 * plane - the frame's box is flat there - so that the leaf stands in the
 * pocket as a single sheet with a face to each side. The classic engine
 * draws every object one-sided (it culls the back of each polygon in
 * software), so each side shows its own panel; the HD path draws its
 * models two-sided, and there the two panels lie at one depth and the
 * picture alternates between them pixel by pixel. A slot slides into a
 * pocket when no keyframe of an animation turns a node against that
 * animation's first, some keyframe's own box (the frame's first six
 * words) lies wholly outside the box of its rest - it has left its own
 * place - and some keyframe's box is flat on an axis on which the rest's
 * is not - it has folded into one plane. Its items are drawn one-sided,
 * as the classic draws them (HDPocketSide). The fold is what limits the
 * rule to the case that fights: a slot whose geometry never folds keeps
 * the HD path's two-sided draw, which an open model needs. The walk keeps
 * to the slot's own animations, the run from its first to the next
 * object's first (HDPocketAnimEnd): a slot's state change may name an
 * animation of another object - JOBY4's ATTACK_SUB, two animations of
 * one keyframe each, links to four of its neighbours' - and their
 * keyframes say nothing about this slot's place.
 *
 * The glass. The classic draws a face see-through when its effects word
 * has bit 0 set or its texture's draw type is 2 (specific/output.cpp,
 * the item path). A .TRM sorts the see-through part of a model into its
 * alpha run with material 14, the material the room art's glass carries;
 * the HD path drew that run with the alpha test and the depth write, as
 * a solid sheet. A triangle of a material 14 alpha run on a node whose
 * classic mesh has a see-through face is drawn with the classic's blend
 * for that face, added to what lies behind it (ONE / ONE), weighed by
 * the page's own alpha as the HD additive pass weighs its texels, with
 * no depth write and after everything solid. A node the classic draws
 * solid keeps its .TRM part as it was, whatever the material: the
 * classic is the measure of what is meant to be seen through. It is the
 * measure of how much as well: the classic adds one face at a pixel, so
 * glass with a layer facing against it is drawn one-sided and a copy of
 * the glass in the additive run is not drawn beside it; and the part's
 * own opacity weighs what the one-sided glass adds (HDGlassWeight). */

/* The fewest single-weight vertices a node needs to be measured: the bind
 * drift's own working set (hd_model.cpp) uses the same count. */
#define HD_FIT_MIN_VERTICES		16

/* The largest distance, in classic units, of any node's .TRM centre from
 * the centre of its classic mesh. Over the fifteen shipped levels the
 * models the drift refuses that do belong read 21 to 153, every model put
 * on a foreign tree of the same node count reads 383 or more; 256 stands
 * in that gap and is the drift's own limit (HD_SKELETON_DRIFT_LIMIT). */
#define HD_FIT_LIMIT			256.0f

/* The fit's input on the model's side, defined in hd_model.cpp beside the
 * drift it shares its working set with: per node the centre of the
 * vertices that ride on that node alone (weight 255), in the model's bind
 * pose, and as the return value how many there are - 0 for a node with
 * none, or for a slot with no model. Declared here and not in hd_model.h
 * because only the fit's callers want it - hd_object.cpp and ModelProbe,
 * both of which link hd_model.cpp; ModernTest includes this header and
 * never calls it. */
int HDModelNodeCentre(int slot, int node, float* centre /* 3 */);

/* The worst node of the fit, or -1 when no node can be measured (no node
 * with HD_FIT_MIN_VERTICES single-weight vertices and a classic mesh with
 * any). `trm` and `mesh` are nodes * 3 centres, `trmCount` and `meshCount`
 * the vertices behind each. */
inline float HDSkeletonFitWorst(const float* trm, const int* trmCount,
	const float* mesh, const int* meshCount, int nodes)
{
	float worst = -1.0f;

	for (int node = 0; node < nodes; node++)
	{
		if (trmCount[node] < HD_FIT_MIN_VERTICES || meshCount[node] < 1)
			continue;

		const float dx = trm[node * 3 + 0] - mesh[node * 3 + 0];
		const float dy = trm[node * 3 + 1] - mesh[node * 3 + 1];
		const float dz = trm[node * 3 + 2] - mesh[node * 3 + 2];
		const float d = sqrtf(dx * dx + dy * dy + dz * dz);

		if (d > worst)
			worst = d;
	}

	return worst;
}

enum HDSkeletonVerdict
{
	HD_SKELETON_FITS = 0,		// the drift is inside the limit
	HD_SKELETON_PLACED,		// the drift is not, the places are
	HD_SKELETON_DRIFTS		// neither: a foreign tree
};

/* The drift first, as it always was; the fit only for a model the drift
 * refuses, and a fit that could not be measured (-1) vouches for
 * nothing. */
inline HDSkeletonVerdict HDSkeletonJudge(float drift, float fitWorst,
	float limit)
{
	if (drift <= limit)
		return HD_SKELETON_FITS;

	if (fitWorst >= 0.0f && fitWorst <= HD_FIT_LIMIT)
		return HD_SKELETON_PLACED;

	return HD_SKELETON_DRIFTS;
}

/* Whether a keyframe's box lies wholly outside the rest box on one axis
 * at least. Both are a frame's first six words: min x, max x, min y,
 * max y, min z, max z. Two boxes that only touch share no inside. */
inline bool HDSlidBoxLeaves(const short* rest, const short* key)
{
	for (int axis = 0; axis < 3; axis++)
		if (key[axis * 2] >= rest[axis * 2 + 1] ||
			key[axis * 2 + 1] <= rest[axis * 2])
			return true;

	return false;
}

/* Whether a keyframe's box is flat on an axis on which the rest box is
 * not: the object's geometry has folded into one plane. */
inline bool HDSlidBoxFolds(const short* rest, const short* key)
{
	for (int axis = 0; axis < 3; axis++)
		if (key[axis * 2] == key[axis * 2 + 1] &&
			rest[axis * 2] < rest[axis * 2 + 1])
			return true;

	return false;
}

/* The pocket rule over a slot's animations: some keyframe left the rest
 * box, some keyframe folded flat, and no keyframe turned a node against
 * its own animation's first. */
inline bool HDSlidesIntoPocket(bool leaves, bool folds, bool turns)
{
	return leaves && folds && !turns;
}

/* The most animations one slot's graph is followed through. A door has
 * five; a slot past this many is no door. */
#define HD_POCKET_ANIMS			64

/* Where a slot's own animations end: the smallest first animation of any
 * other object that lies past this slot's `first`, out of `starts`, the
 * first animation of every loaded object; -1 when none lies past it (the
 * level's last object, whose run the file's count ends). */
inline int HDPocketAnimEnd(int first, const int* starts, int count)
{
	int end = -1;

	for (int i = 0; i < count; i++)
		if (starts[i] > first && (end < 0 || starts[i] < end))
			end = starts[i];

	return end;
}

/* Whether animation `anim` is one of the slot's own: from its `first` up
 * to `end`, or without an upper bound when `end` is -1. */
inline bool HDPocketAnimOwn(int anim, int first, int end)
{
	return anim >= first && (end < 0 || anim < end);
}

/* The walk over the animations a slot can play: its first, and every one
 * of its own (HDPocketAnimOwn) a jump or a state change links to. The rest
 * is the first animation's first keyframe. A keyframe is the frame's
 * words: six of box, three of offset, then the nodes' rotations; a turn
 * is a rotation word that
 * differs from the same word of its own animation's first keyframe - the
 * rotations exactly as the level holds them, no angle measured.
 *
 * `frames(anim, &keyframes, &keys, &size)` hands one animation's
 * keyframes, `size` words apart, and is false for an animation that has
 * none; `links(anim, out, most)` writes the animations it links to and
 * returns how many. The engine answers both out of anims[], changes[] and
 * ranges[]; ModelProbe out of the .TRC - the same walk over the same
 * words. */
template <typename Frames, typename Links>
inline bool HDSlotSlidesIntoPocket(int first, int end, Frames frames,
	Links links)
{
	int queue[HD_POCKET_ANIMS];
	int queued = 0;
	const short* rest = 0;
	int keys = 0, size = 0;
	bool leaves = false, folds = false, turns = false;

	if (first < 0 || !frames(first, &rest, &keys, &size) || keys < 1 ||
		size < 9)
		return false;

	queue[queued++] = first;

	for (int at = 0; at < queued; at++)
	{
		const short* key0 = 0;

		if (!frames(queue[at], &key0, &keys, &size) || size < 9)
			continue;

		for (int key = 0; key < keys; key++)
		{
			const short* frame = key0 + key * size;

			if (HDSlidBoxLeaves(rest, frame))
				leaves = true;

			if (HDSlidBoxFolds(rest, frame))
				folds = true;

			for (int word = 9; word < size; word++)
				if (frame[word] != key0[word])
					turns = true;
		}

		int linked[HD_POCKET_ANIMS];
		const int count = links(queue[at], linked, HD_POCKET_ANIMS);

		for (int l = 0; l < count; l++)
		{
			bool known = !HDPocketAnimOwn(linked[l], first, end);

			for (int q = 0; q < queued && !known; q++)
				known = queue[q] == linked[l];

			if (!known && queued < HD_POCKET_ANIMS)
				queue[queued++] = linked[l];
		}
	}

	return HDSlidesIntoPocket(leaves, folds, turns);
}

/* The walk with no end known: every link the slot names is followed. */
template <typename Frames, typename Links>
inline bool HDSlotSlidesIntoPocket(int first, Frames frames, Links links)
{
	return HDSlotSlidesIntoPocket(first, -1, frames, links);
}

/* Which side of a pocket slot's triangles is drawn: HD_POCKET_BOTH for a
 * slot that is no pocket (the HD path's two-sided rule), the outside -
 * HD_POCKET_OUTSIDE, the cull mode that keeps a .TRM's outside (hd_model.h,
 * HD_MODEL_CULL_BACK) - for a pocket, and HD_POCKET_INSIDE under a mirrored
 * matrix (a reflection), whose turned winding makes the other mode keep
 * the same outside. */
enum HDPocketSideRule
{
	HD_POCKET_BOTH = 0,
	HD_POCKET_OUTSIDE,
	HD_POCKET_INSIDE
};

inline HDPocketSideRule HDPocketSide(bool pocket, bool mirrored)
{
	if (!pocket)
		return HD_POCKET_BOTH;

	return mirrored ? HD_POCKET_INSIDE : HD_POCKET_OUTSIDE;
}

/* The material a .TRM part's glass carries: the room art's glass material
 * (hd_render.cpp, HD_TRG_MATERIAL_DECAL), the same number in a .TRM. */
#define HD_TRM_MATERIAL_GLASS		14

/* The runs of a .TRM part, in their order: the opaque, the alpha and the
 * additive run (hd_trm.h). */
#define HD_TRM_RUN_OPAQUE		0
#define HD_TRM_RUN_ALPHA		1
#define HD_TRM_RUN_ADDITIVE		2

/* Whether the classic draws a face see-through: its effects word (the word
 * behind its texture word) has bit 0 set, or its texture's draw type is 2.
 * Either sends the face down the item path's additive branch
 * (specific/output.cpp, phd_PutPolygons; polyinsert.cpp, case 2). */
inline bool HDClassicFaceSeeThrough(int effects, int drawtype)
{
	return (effects & 1) != 0 || drawtype == 2;
}

/* A node mask with `node` set when the face is see-through; nodes past 31
 * are not counted, the joint palette's own limit. */
inline unsigned int HDSeeThroughNodes(unsigned int mask, int node,
	bool seeThrough)
{
	if (!seeThrough || node < 0 || node > 31)
		return mask;

	return mask | (1u << node);
}

/* Whether a .TRM triangle is drawn as glass: it lies in its part's alpha
 * run, the part's material is the glass material, and the classic mesh of
 * its node has a see-through face (`nodes`, HDSeeThroughNodes). */
inline bool HDTrmTriangleIsGlass(int run, unsigned int material, int node,
	unsigned int nodes)
{
	if (run != HD_TRM_RUN_ALPHA || material != HD_TRM_MATERIAL_GLASS)
		return false;

	if (node < 0 || node > 31)
		return false;

	return (nodes & (1u << node)) != 0;
}

/* The glass's two sides. The classic draws a see-through face one-sided -
 * TR5 culls back faces in software (specific/output.cpp) - and a pane seen
 * from both sides carries a face for each side. A .TRM glass built the
 * same way, a triangle with a glass triangle of its node facing against
 * it, has a layer for each side as well; drawn two-sided, the layer turned
 * away from the eye adds its texels on top of the one facing it. A range
 * of glass every triangle of which has such a layer is drawn one-sided,
 * as the classic draws its face. A glass triangle that nothing faces
 * against is a single pane seen from both sides, and its range keeps the
 * two-sided draw. Two triangles face against each other when their
 * normals stand more than 135 degrees apart: halfway between the two sides
 * of one pane (180) and two faces meeting at a box's corner (90), which
 * are no layers of one another. `a` and `b` are the two triangles'
 * normals, of any length; a zero normal faces against nothing. */
inline bool HDTrmTrianglesFaceAgainst(const float* a, const float* b)
{
	const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	const float aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
	const float bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];

	return dot < 0.0f && dot * dot > 0.5f * aa * bb;
}

/* A copy of a glass triangle. A .TRM's additive run can carry the glass's
 * own pane again - the same page, the same texture corners, the same place
 * to within a unit - and the additive pass adds it on top of the glass,
 * where the classic adds the face once. Triangle `b` copies triangle `a`
 * when every corner of `b` has a corner of `a` with the same texture
 * coordinate (u and v, a byte each, as the file stores them) lying within
 * HD_TRM_COPY_SHARE of `a`'s longest edge: far below the size of any pane
 * and far above the rounding of a file's float coordinates. Three floats a
 * corner for the places, two bytes a corner for the texture coordinates;
 * the page is the caller's to compare. */
#define HD_TRM_COPY_SHARE		(1.0f / 32.0f)

inline bool HDTrmTriangleCopies(const float* a, const unsigned char* uvA,
	const float* b, const unsigned char* uvB)
{
	float longest = 0.0f;

	for (int k = 0; k < 3; k++)
	{
		const float* p = a + k * 3;
		const float* q = a + ((k + 1) % 3) * 3;
		const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
		const float edge = dx * dx + dy * dy + dz * dz;

		if (edge > longest)
			longest = edge;
	}

	if (!(longest > 0.0f))
		return false;

	const float reach = HD_TRM_COPY_SHARE * HD_TRM_COPY_SHARE * longest;

	for (int k = 0; k < 3; k++)
	{
		bool found = false;

		for (int j = 0; j < 3 && !found; j++)
		{
			if (uvB[k * 2] != uvA[j * 2] || uvB[k * 2 + 1] != uvA[j * 2 + 1])
				continue;

			const float dx = b[k * 3] - a[j * 3];
			const float dy = b[k * 3 + 1] - a[j * 3 + 1];
			const float dz = b[k * 3 + 2] - a[j * 3 + 2];

			found = dx * dx + dy * dy + dz * dz <= reach;
		}

		if (!found)
			return false;
	}

	return true;
}

/* The glass's opacity: the part's material carries it in param[1], 0.1 to
 * 1 over the glass parts the HD objects bind, and the room art's additive
 * pass already weighs its material by it (hd_render.cpp,
 * MaterialAddWeight). The glass is added weighed by it. A value outside
 * (0, 1) - 0, missing, not a number, or 1 itself - weighs 1: the glass as
 * the classic adds it. */
#define HD_TRM_PARAM_OPACITY		1

inline float HDGlassOpacity(float param)
{
	if (!(param > 0.0f) || !(param < 1.0f))
		return 1.0f;

	return param;
}

/* The opacity as the blend factor's byte (D3DRS_BLENDFACTOR), rounded to
 * the nearest of its 255 steps, 0 to 255. */
inline unsigned int HDGlassOpacityByte(float opacity)
{
	if (!(opacity > 0.0f))
		return 0;

	if (!(opacity < 1.0f))
		return 255;

	return (unsigned int)(opacity * 255.0f + 0.5f);
}

/* What a glass range is added times: its opacity where the range is drawn
 * one-sided (`sided`, HDTrmTrianglesFaceAgainst over every triangle of it),
 * 1 where it is not. A one-sided range is a pane modelled with a layer for
 * each side, the glass whose layers stacked where the classic adds one
 * face; its opacity weighs the one layer left. A two-sided range is a
 * single sheet with nothing stacked on it, and the page's own alpha stays
 * its only weight: over the glass the shipped levels bind, every such
 * sheet weighed by the opacity would add less than the tenth of the
 * classic's band at its node (ModelProbe -alpha), and it adds no more than
 * the band unweighed. */
inline float HDGlassWeight(float opacity, bool sided)
{
	return sided ? HDGlassOpacity(opacity) : 1.0f;
}

/* The classic's band as the ceiling of the glass: the factor that holds
 * what the glass adds - the top of its band, the texels' luma times their
 * alpha times the opacity - under the top of the band the classic's
 * see-through faces of the same node add, their texels' luma. Both are
 * times the same light (the additive pass clamps the light as the classic's
 * colour byte does), so the factor holds at any light. 1 when the glass
 * adds no more, or when either band is empty. ModelProbe (-alpha) measures
 * it: over the glass the shipped levels bind it is 1 once the sides, the
 * copies and the opacity are applied, so the object path, which holds no
 * page's texels, applies none. */
inline float HDGlassBandFactor(float classicTop, float glassTop)
{
	if (!(classicTop > 0.0f) || !(glassTop > classicTop))
		return 1.0f;

	return classicTop / glassTop;
}

/* Whether a node matrix mirrors - a reflection: the determinant of its
 * turn is negative, and the winding that faces the eye is the other one.
 * Sixteen floats, the turn in the first three of the first three rows. */
inline bool HDMatrixMirrors(const float* m)
{
	const float det =
		m[0] * (m[5] * m[10] - m[6] * m[9]) -
		m[1] * (m[4] * m[10] - m[6] * m[8]) +
		m[2] * (m[4] * m[9] - m[5] * m[8]);

	return det < 0.0f;
}

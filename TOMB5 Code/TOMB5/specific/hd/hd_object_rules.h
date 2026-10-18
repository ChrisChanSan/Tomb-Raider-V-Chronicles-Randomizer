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

/* Two rules of the HD object path, as pure functions, so that the engine,
 * ModelProbe and ModernTest judge by the same lines.
 *
 * THE SKELETON FIT. The bind drift (hd_model.h, HDModelBindDrift) compares
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
 * not. The same comparison of places is TROne's for a slot whose tree is a
 * stub (host/render/game_renderer.cpp, the node_offset lambda: box centres
 * by the leading bone, within 256 units).
 *
 * THE POCKET. A door that slides out of its doorway into the wall beside
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
 * the HD path's two-sided draw, which an open model needs. */

/* The fewest single-weight vertices a node needs to be measured: the bind
 * drift's own working set (hd_model.cpp) uses the same count. */
#define HD_FIT_MIN_VERTICES		16

/* The largest distance, in classic units, of any node's .TRM centre from
 * the centre of its classic mesh. Over the fifteen shipped levels the
 * models the drift refuses that do belong read 21 to 153, every model put
 * on a foreign tree of the same node count reads 383 or more; 256 stands
 * in that gap and is the drift's own limit (HD_SKELETON_DRIFT_LIMIT). */
#define HD_FIT_LIMIT			256.0f

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

/* The walk over the animations a slot can play: its first, and every one
 * a jump or a state change links to. The rest is the first animation's
 * first keyframe. A keyframe is the frame's words: six of box, three of
 * offset, then the nodes' rotations; a turn is a rotation word that
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
inline bool HDSlotSlidesIntoPocket(int first, Frames frames, Links links)
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
			bool known = linked[l] < 0;

			for (int q = 0; q < queued && !known; q++)
				known = queue[q] == linked[l];

			if (!known && queued < HD_POCKET_ANIMS)
				queue[queued++] = linked[l];
		}
	}

	return HDSlidesIntoPocket(leaves, folds, turns);
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

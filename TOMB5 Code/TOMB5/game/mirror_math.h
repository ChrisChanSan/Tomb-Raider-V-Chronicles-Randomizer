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

/* The arithmetic of a mirror's reflection, and nothing else.
 *
 * This header includes no engine header on purpose:
 * `Preparation\Tools\Diagnose\ModernTest.cpp` includes this file itself and
 * asserts on the functions below without a game, a level or a device.
 * game\mirror.cpp holds the draw that uses them.
 *
 * A matrix here is the engine's 3x4 in its own layout - M00 M01 M02 M03,
 * M10 .. M13, M20 .. M23, twelve numbers, rows first (global\types.h,
 * matrix_indices) - as a long (w2v_matrix, matrix_stack) or a float
 * (aW2VMx, aFMatrixStack, lara_matrices).
 *
 * The reflection is put into the view and not into the item. A mirror in the
 * plane x = P shows every point p where the point S p + 2P e_x stands, S the
 * diagonal (-1, 1, 1); drawing the item where it is through a view that
 * reflects the world does exactly that for every item alike, with its own
 * pose, its own animation and its own interpolation left as the frame has
 * them. Writing a reflected pose into the item instead - what
 * Draw_Mirror_Lara does for the plane z - needs a scale of -1 behind the
 * rotation (CalcLaraMatrices(2)), which Lara's matrices have and
 * DrawAnimatingItem's do not. */

/* The view reflected in the plane of constant `axis` (0 x, 1 y, 2 z) at
 * `plane`: the view's column `axis` negated, and - for a camera matrix, whose
 * M03, M13 and M23 hold the camera's world position (aW2VMx, w2v_matrix; see
 * specific\3dmath.cpp, phd_TranslateAbs) - the camera reflected with it.
 * The stack's own matrices hold a translation phd_TranslateAbs overwrites
 * and are reflected with `camera` false. Applied twice it is the identity,
 * to the bit for integral positions. */
template <typename T>
inline void MirrorView(T* m, int axis, T plane, bool camera)
{
	m[axis] = -m[axis];
	m[4 + axis] = -m[4 + axis];
	m[8 + axis] = -m[8 + axis];

	if (camera)
	{
		T& c = m[4 * axis + 3];

		c = plane + plane - c;
	}
}

/* A matrix already built through the real view, moved into the reflected
 * one: v' = Q v + t for its translation and Q v for its three axes, with
 *
 *     Q = I - 2 u u^T,  u = column `axis` of the camera's rotation `w2v`
 *     t = column `axis` of the stack's base `base` * 2 (plane - camera)
 *
 * which is what building the same matrix through MirrorView's views gives.
 * `base` is the matrix the engine builds a model's matrices from, the
 * camera's rotation with the poison shake scaled into its columns
 * (phd_GenerateW2V, ScaleCurrentMatrix); `w2v` is the same rotation
 * without it. For Lara, whose fifteen matrices the frame has already worked
 * out once and whose second set must not run CalcLaraMatrices a second time
 * (its log counts only the real view's set). */
inline void MirrorViewSpaceSetup(const float* w2v, const float* base, int axis,
	float plane, float* q /* 9 */, float* t /* 3 */)
{
	const float u[3] = { w2v[axis], w2v[4 + axis], w2v[8 + axis] };
	const float along = 2.0f * (plane - w2v[4 * axis + 3]);

	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			q[r * 3 + c] = (r == c ? 1.0f : 0.0f) - 2.0f * u[r] * u[c];

		t[r] = base[4 * r + axis] * along;
	}
}

inline void MirrorViewSpaceApply(const float* q, const float* t, float* m)
{
	for (int c = 0; c < 4; c++)
	{
		const float v[3] = { m[c], m[4 + c], m[8 + c] };

		for (int r = 0; r < 3; r++)
			m[4 * r + c] = q[r * 3 + 0] * v[0] + q[r * 3 + 1] * v[1] +
				q[r * 3 + 2] * v[2] + (c == 3 ? t[r] : 0.0f);
	}
}

/* Whether a point on the room's side of the plane lands, reflected, inside
 * the copy behind the glass: `side` is where the copy lies (-1 at smaller
 * coordinates than the plane, +1 at larger), the box the copy's art, `slack`
 * the room a point has around it - an item's origin is at its feet and on
 * its pivot, not inside its mesh.
 *
 * The slack holds along the plane's normal and in height, and not along the
 * glass: past the copy's ends along the glass lie real, drawn rooms (RICH2:
 * corridor room 56 at z 73728..74752 straddles mirror 135's plane x 30720,
 * whose copy starts at z 74764; doorway room 130 stands the same way beside
 * mirror 134), and a reflection there would stand in the open room, outside
 * any glass. */
inline bool MirrorLandsInCopy(const long* p, int axis, long plane, int side,
	const long* mins, const long* maxs, long slack)
{
	if ((p[axis] - plane) * side >= 0)
		return false;	// on the glass or behind it

	for (int k = 0; k < 3; k++)
	{
		const long v = k == axis ? plane + plane - p[k] : p[k];
		const long s = k == axis || k == 1 ? slack : 0;

		if (v < mins[k] - s || v > maxs[k] + s)
			return false;
	}

	return true;
}

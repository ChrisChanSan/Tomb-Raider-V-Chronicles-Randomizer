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

/* The visibility path's level tables (tomb5.hd_vis 1).
 *
 * With the path on, the rooms a frame draws are the engine's own portal walk
 * from the eye's room (game\draw.cpp, GetRoomBounds), every listed room's art
 * is drawn whole but for its art standing inside another room, which is cut
 * to its room's window as on the other path (hd_level.cpp, DecidePairs), and
 * the depth buffer decides the rest. The walk is extended where the art
 * opens more than the classic level, by three tables made here once per
 * level:
 *
 *   guests    per room, the groups whose art stands in its inside
 *   openings  per room, the classic wall faces its own art leaves bare,
 *             each with a room behind it
 *   wides     per room, the portals that bare faces round them widen
 *
 * A room any of them lists walks on through its own portals with the window
 * it was listed with, as a room the portals list does.
 *
 * The approach - the walk as the list, every listed room's art drawn whole,
 * a group listed by its art in a listed room's box, eight triangles making
 * it a guest - is re-derived from the TR2 visibility prototype of TROne
 * (GPLv3) by Todesengel1931 - https://github.com/todesengel1931. No TROne
 * source is in this file or in hd_vis.cpp; the tables and their rules are
 * this engine's own.
 *
 * No engine header, like hd_cover.h: the engine, RoomProbe and TrgProbe make
 * the same tables out of the same .TRG and the same classic faces. World
 * units with TR5's y down, unless a field says room-local. */

#include <vector>
#include "hd_cover.h"

namespace trf
{
	namespace remaster
	{
		struct Trg;
	}
}

/* One room of the classic level, as the walk sees it: its origin, its
 * sectors, its box in y, its flags, its flipmap twin, and where its classic
 * faces, its portals and its floor links stand in the three lists handed in
 * beside it. A floor link is a room a floor square names above or below it
 * (FLOOR_INFO sky_room and pit_room), a stacked pair having no portal
 * record of its own for it. */
struct HDVisRoom
{
	long x, y, z;
	int xs, zs;			// sectors along x and along z
	long top, bottom;	// the box in y: the highest ceiling, the lowest floor
	int flags;			// ROOM_INFO flags: 8 is ROOM_OUTSIDE, under the sky
	int twin;			// the flipmap twin, -1 for none
	int firstFace, faceCount;
	int firstPortal, portalCount;	// in the room's own door order
	int firstLink, linkCount;
};

// One portal record: the room behind it, its normal and its four corners,
// room-local, as GetRoomBounds reads them.
struct HDVisPortal
{
	int to;
	short n[3];
	short v[4][3];
};

// Art of `group` standing in a holder's inside: how much, and its box.
struct HDVisGuest
{
	int group;
	int tris;
	float lo[3], hi[3];
};

/* A classic wall face of a room its own art leaves bare, and the room behind
 * it. `n` points into the holder, the side the eye has to be on; `v` holds
 * the face's corners room-local (a triangle repeats its first). `bare` is
 * the share of the face's samples no art covers. */
struct HDVisOpening
{
	int behind;
	int face;
	float bare;
	short n[3];
	short v[4][3];
};

// A portal and the opening round it, room-local, in the portal's plane.
struct HDVisWide
{
	int portal;
	short v[4][3];
};

/* A static mesh of the classic level, for the art that lies on it
 * (HDVisStaticCovers): the room whose list holds it at the build, its index
 * in that list, its box local to that room as the statics' rule takes it
 * (hd_level.cpp, StaticBox), and where its triangles stand in the face list
 * handed in beside it - world units, as DrawStaticObjects places the mesh;
 * none for a static the engine does not draw. */
struct HDVisStatic
{
	int room;
	int mesh;
	float mins[3], maxs[3];
	int firstFace, faceCount;
};

/* The numbers of the tables, measured on the fifteen levels (TrgProbe
 * -vistable, RoomProbe -vis; hd_vis.cpp, HDVisDefaults, says why each):
 *
 *   reach      how far off a classic face art may stand and still cover it
 *   step       the spacing of a face's samples (HD_COVER_STEP)
 *   bare       the share of bare samples that makes a face an opening
 *   guestTris  the triangles a group needs in a room's inside to be its
 *              guest
 *   behind     how far behind a bare face the room behind is looked for
 *   artShare   the share of a room's wall samples its own art must cover
 *              before its bare faces count: below it the room's walls are
 *              not its own art's at all (no group, an empty or a hollow
 *              one)
 *   guestRule  0: every triangle in the holder's inside counts and a
 *              guest must be linked with the holder; 1: only the art
 *              reaching out of its own room counts, linked or not; 2,
 *              the default: that art, and linked with a holder not under
 *              the sky
 *   overhang   how far outside its own room's whole box art must stand
 *              to reach out of the room (rules 1 and 2)
 *   wideBare   the share of bare samples that lets a face beside a portal
 *              widen it: a broken-out edge of a wall, less than an
 *              opening, still shows the room behind the portal
 *   edge       how far a portal's projection may stand off the window it
 *              is seen through and still list the room behind, as a share
 *              of the screen's width (the walk, game\draw.cpp)
 *   staticReach  how far art may stand off a static mesh's face and still
 *              lie on it
 *   staticStep   the spacing of a static's samples
 *   staticShare  the share of a static's samples one group's art must lie
 *              on before the static's place is that art's
 *              (HDVisStaticDrawnByArt) */
struct HDVisParams
{
	double reach;
	double step;
	double bare;
	int guestTris;
	double behind;
	double artShare;
	int guestRule;
	double overhang;
	double wideBare;
	double edge;
	double staticReach;
	double staticStep;
	double staticShare;
};

HDVisParams HDVisDefaults();

struct HDVisTables
{
	int rooms = 0;
	std::vector<int> guestFirst, guestCount;
	std::vector<HDVisGuest> guests;
	std::vector<int> openFirst, openCount;
	std::vector<HDVisOpening> openings;
	std::vector<int> wideFirst, wideCount;
	std::vector<HDVisWide> wides;
	/* Per face of the list handed in: the bare share of a wall face of a
	 * room whose walls are asked, -1 for every other face. */
	std::vector<float> faceBare;
	/* Per room: the group of a room linked with it whose box holds the
	 * room's inside wholly and whose art stands in that inside, guestTris
	 * triangles or more - the most of them -, -1 for none; and those
	 * triangles. Noted for the HDLOG vis line and RoomProbe's -vis lines;
	 * nothing is drawn or left out by it: a classic room standing so keeps
	 * its faces, its own group being hollow (never drawn) and the enclosing
	 * art covering none of its faces - JOBY2's sniper rooms 19 and 21,
	 * whose faces are the black behind the broken facade windows. */
	std::vector<int> enclosedBy;
	std::vector<int> enclosedTris;
	int roomsEnclosed = 0;
	/* Per room: the rooms its floor links name (HDVisRoom's links: a floor
	 * square's room above or below), each once, in floorRooms from
	 * floorFirst on (HDVisClassicRoomHidden's floorOnArt). */
	std::vector<int> floorFirst, floorCount;
	std::vector<int> floorRooms;
	/* Per static handed in (HDVisStaticCovers): the group whose art lies on
	 * the most of its samples, -1 for none, and that share; and how many
	 * reach staticShare. */
	std::vector<int> staticGroup;
	std::vector<float> staticShare;
	int staticsHeld = 0;
	double staticMs = 0.0;		// what asking them cost
	// The counts of the level line.
	int artRooms = 0;		// rooms whose walls are asked
	int wallFaces = 0;		// their wall faces
	int bareFaces = 0;		// of those, the openings
	int voidFaces = 0;		// of those, the ones with no room behind
	int guestPairs = 0;
	long long guestTris = 0;
	/* What the link rule took out: guests of a room not under the sky
	 * with no link to it, and openings onto a room with none. */
	int guestsUnlinked = 0;
	int openingsUnlinked = 0;
	/* Openings that named the other room of a flipmap pair, named by the
	 * pair's main room instead. */
	int openingsTwin = 0;
	int widePortals = 0;
	/* Of the wide portals, those wide only by the record leading back: the
	 * art's hole round the portal bare on the other room's faces alone. */
	int wideMirrored = 0;
	long long samples = 0, samplesCovered = 0;
	double ms = 0.0;
};

/* The three tables of one level out of its .TRG (group i is room i) and its
 * classic side. `faces` are the rooms' classic faces in world units, as
 * InsertRoomlet draws them; `portals` the portal records, room-local;
 * `links` the rooms' floor links. */
bool HDVisBuild(const trf::remaster::Trg& trg, const HDVisRoom* rooms,
	int roomCount, const HDCoverFace* faces, int faceCount,
	const HDVisPortal* portals, int portalCount, const int* links,
	int linkCount, const HDVisParams& params, HDVisTables* out);

/* The engine's copy. hd_level.cpp hands the classic side in before the
 * level's build (HDVisLevelInput), the renderer's build hands in the .TRG it
 * has open (HDVisLevelBuild, which does nothing unless an input waits), and
 * the walk asks the tables (HDVisLevelTables, null when the level was not
 * built with the path). HDVisLevelClear drops both. */
void HDVisLevelInput(const HDVisRoom* rooms, int roomCount,
	const HDCoverFace* faces, int faceCount, const HDVisPortal* portals,
	int portalCount, const int* links, int linkCount);
/* The static meshes, handed in after HDVisLevelInput (which drops them) and
 * asked by HDVisLevelBuild after the three tables (HDVisStaticCovers). */
void HDVisLevelStatics(const HDVisStatic* statics, int staticCount,
	const HDCoverFace* faces, int faceCount);
void HDVisLevelBuild(const trf::remaster::Trg& trg);
void HDVisLevelClear();
const HDVisTables* HDVisLevelTables();

/* The art that lies on each static mesh: per static, of the groups whose
 * room's box holds the centre of the static's faces - the room it stands
 * in, its own or another that shares the place -, the one whose art (its
 * opaque and alpha passes) covers the most of the static's samples
 * (HDCoverSamples at staticStep; a sample is covered by a triangle facing
 * either way within staticReach of it), into out->staticGroup and
 * out->staticShare. Group i is room i of `rooms`. Not asked for a static
 * whose own room's art holds a twentieth of its box's cells
 * (HDStaticCellsHold over the cells of the group's vertices, as the
 * renderer's build notes them): that art draws it already. The tables'
 * other fields are left as they are. */
void HDVisStaticCovers(const trf::remaster::Trg& trg, const HDVisRoom* rooms,
	int roomCount, const HDVisStatic* statics, int staticCount,
	const HDCoverFace* faces, int faceCount, const HDVisParams& params,
	HDVisTables* out);

/* Whether a static mesh the frame would draw classic is left to the art on
 * the visibility path: the share of its samples the art of one group lies
 * on (HDVisStaticCovers) reaches `needed`, and that group is drawn this
 * frame.
 *
 * What for: a static whose place another room's art models. ANDY3's mill
 * house is room 39, standing in room 35's box; the dormer of its roof is
 * six classic statics of room 39 - no art of room 39 is there, and room
 * 35's art in its own box is not asked by the statics' rule (hd_cover.h,
 * HDStaticAnswer) - and room 35's art models the dormer at the statics'
 * own faces. Drawn with that art, the classic faces fight with it where
 * the two lie together and stand in front of it as old texture where they
 * do not. */
inline bool HDVisStaticDrawnByArt(float share, double needed, bool groupDrawn)
{
	return groupDrawn && share > 0.0f && (double)share >= needed;
}

/* Whether a triangle whose corners span the heights y0 to y1 (y down, y0
 * the higher) reaches into an intrusion box's height, top to bottom, by
 * more than `margin` - the art of an unlinked room hung into the holder's
 * space where its centroid stands over the holder's ceiling or under its
 * floor (hd_render.cpp, the index fill's height_at).
 *
 * What for: JOBY2's room 30 has its floor at y -3,072, 256 units below
 * the ceiling of room 60 (-3,328), and two of its triangles stand on room
 * 60's east wall plane from -3,413 to -3,242: their centroid 28 units over
 * the box, they reach 86 units into it, and the air keeps them out by its
 * margin, which a point on the wall's plane cannot pass. Drawn whole they
 * are a sliver of room 30 in room 60's ceiling corner. */
inline bool HDVisHeightReaches(float y0, float y1, float top, float bottom,
	float margin)
{
	return y1 > top + margin && y0 < bottom - margin;
}

/* Whether a triangle's three corners (x and z, in the box's frame) lie in
 * one side plane of an intrusion box, within a unit: on the plane of the
 * holder's wall, where the air's margin keeps a point out
 * (HDVisHeightReaches says what for). Art that only stands near a side -
 * a beam's end, a corner's trim - is not on it. */
inline bool HDVisOnBoxSide(const float x[3], const float z[3],
	const float* mins, const float* maxs)
{
	const float side[4] = { mins[0], maxs[0], mins[2], maxs[2] };

	for (int s = 0; s < 4; s++)
	{
		const float* v = s < 2 ? x : z;
		bool on = true;

		for (int c = 0; c < 3 && on; c++)
			on = v[c] - side[s] <= 1.0f && side[s] - v[c] <= 1.0f;

		if (on)
			return true;
	}

	return false;
}

/* Whether classic face b stands in the plane of classic face a: every corner
 * of b within `tolerance` units of the plane through a's first three corners
 * (its first, third and fourth when those three lie on a line).
 *
 * What for: the faces of one wall of a room (HDVisFacesOneWall,
 * HDVisWallLabels). The classic faces lie on TR5's grid, so a wall's faces
 * share their plane to the unit. */
inline bool HDVisFacesShareAPlane(const float a[4][3], int aCorners,
	const float b[4][3], int bCorners, double tolerance)
{
	if (aCorners < 3 || bCorners < 1)
		return false;

	for (int pick = 0; pick < 2; pick++)
	{
		const int i0 = 0;
		const int i1 = pick == 0 ? 1 : 2;
		const int i2 = pick == 0 ? 2 : (aCorners > 3 ? 3 : 2);
		double u[3], v[3], n[3];

		for (int k = 0; k < 3; k++)
		{
			u[k] = (double)a[i1][k] - a[i0][k];
			v[k] = (double)a[i2][k] - a[i0][k];
		}

		n[0] = u[1] * v[2] - u[2] * v[1];
		n[1] = u[2] * v[0] - u[0] * v[2];
		n[2] = u[0] * v[1] - u[1] * v[0];

		const double len = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];

		if (len <= 1e-6)
			continue;

		const double inv = 1.0 / sqrt(len);

		for (int c = 0; c < bCorners && c < 4; c++)
		{
			double d = 0.0;

			for (int k = 0; k < 3; k++)
				d += ((double)b[c][k] - a[i0][k]) * n[k] * inv;

			if (d > tolerance || d < -tolerance)
				return false;
		}

		return true;
	}

	return false;
}

/* The tolerance of that test, in world units. */
#define HD_VIS_PLANE_TOLERANCE	1.0

/* Whether classic face a stands upright, a wall: the normal of its plane,
 * taken as HDVisFacesShareAPlane takes it, leans less than 1 in 100 off the
 * horizontal. A floor, a ceiling or a slope does not. */
inline bool HDVisFaceUpright(const float a[4][3], int corners)
{
	if (corners < 3)
		return false;

	for (int pick = 0; pick < 2; pick++)
	{
		const int i1 = pick == 0 ? 1 : 2;
		const int i2 = pick == 0 ? 2 : (corners > 3 ? 3 : 2);
		double u[3], v[3];

		for (int k = 0; k < 3; k++)
		{
			u[k] = (double)a[i1][k] - a[0][k];
			v[k] = (double)a[i2][k] - a[0][k];
		}

		const double nx = u[1] * v[2] - u[2] * v[1];
		const double ny = u[2] * v[0] - u[0] * v[2];
		const double nz = u[0] * v[1] - u[1] * v[0];
		const double len = nx * nx + ny * ny + nz * nz;

		if (len <= 1e-6)
			continue;

		return ny * ny <= 1e-4 * len;
	}

	return false;
}

/* Whether classic faces a and b meet along an edge: two points `tolerance`
 * or more apart, each a corner of one face within `tolerance` of an edge of
 * the other. Faces that touch at one corner only do not. */
inline bool HDVisFacesShareAnEdge(const float a[4][3], int aCorners,
	const float b[4][3], int bCorners, double tolerance)
{
	double found[8][3];
	int count = 0;

	if (aCorners < 2 || bCorners < 2)
		return false;

	// Faces whose boxes stand apart by more than the tolerance never meet.
	for (int k = 0; k < 3; k++)
	{
		double aLo = a[0][k], aHi = a[0][k], bLo = b[0][k], bHi = b[0][k];

		for (int c = 1; c < aCorners && c < 4; c++)
		{
			aLo = a[c][k] < aLo ? a[c][k] : aLo;
			aHi = a[c][k] > aHi ? a[c][k] : aHi;
		}

		for (int c = 1; c < bCorners && c < 4; c++)
		{
			bLo = b[c][k] < bLo ? b[c][k] : bLo;
			bHi = b[c][k] > bHi ? b[c][k] : bHi;
		}

		if (aLo > bHi + tolerance || bLo > aHi + tolerance)
			return false;
	}

	for (int side = 0; side < 2; side++)
	{
		const float (*p)[3] = side == 0 ? a : b;
		const float (*q)[3] = side == 0 ? b : a;
		const int pn = (side == 0 ? aCorners : bCorners) < 4
			? (side == 0 ? aCorners : bCorners) : 4;
		const int qn = (side == 0 ? bCorners : aCorners) < 4
			? (side == 0 ? bCorners : aCorners) : 4;

		for (int c = 0; c < pn && qn >= 2; c++)
		{
			bool on = false;

			for (int e = 0; e < qn && !on; e++)
			{
				const float* s = q[e];
				const float* t = q[(e + 1) % qn];
				double d[3], w[3], dd = 0.0, dw = 0.0, gap = 0.0;

				for (int k = 0; k < 3; k++)
				{
					d[k] = (double)t[k] - s[k];
					w[k] = (double)p[c][k] - s[k];
					dd += d[k] * d[k];
					dw += d[k] * w[k];
				}

				double f = dd > 0.0 ? dw / dd : 0.0;

				f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;

				for (int k = 0; k < 3; k++)
					gap += (w[k] - f * d[k]) * (w[k] - f * d[k]);

				on = gap <= tolerance * tolerance;
			}

			bool known = !on;

			for (int i = 0; i < count && !known; i++)
			{
				double gap = 0.0;

				for (int k = 0; k < 3; k++)
					gap += (found[i][k] - p[c][k]) * (found[i][k] - p[c][k]);

				known = gap < tolerance * tolerance;
			}

			if (!known && count < 8)
			{
				for (int k = 0; k < 3; k++)
					found[count][k] = p[c][k];

				count++;
			}
		}
	}

	return count >= 2;
}

/* Whether classic faces a and b are one wall: b stands in a's plane
 * (HDVisFacesShareAPlane), or both stand upright and meet along an edge -
 * the two sides of a corner the wall turns. */
inline bool HDVisFacesOneWall(const float a[4][3], int aCorners,
	const float b[4][3], int bCorners, double tolerance)
{
	if (HDVisFacesShareAPlane(a, aCorners, b, bCorners, tolerance))
		return true;

	return HDVisFaceUpright(a, aCorners) && HDVisFaceUpright(b, bCorners) &&
		HDVisFacesShareAnEdge(a, aCorners, b, bCorners, tolerance);
}

/* A claimed classic face of one pair as HDVisWallLabels and HDVisWallDrawn
 * ask it: its corners and whether the classic path draws it this frame. */
struct HDVisWallFace
{
	const float (*v)[3];
	int corners;
	bool drawn;
};

/* The most faces of one pair HDVisWallLabels asks; the largest pair of the
 * fifteen levels claims 173 (ANDY2, room 31's art in room 44). Faces past
 * it are left out. */
#define HD_VIS_WALL_FACES	256

/* The walls of a pair's claimed faces: label[i] is the lowest index among
 * the faces that are one wall with face i, face to face through
 * HDVisFacesOneWall asked either way round. Built once per level; a frame
 * asks only whether a drawn face carries a piece's label (HDVisWallDrawn).
 *
 * What for: on the visibility path a piece of art standing in a room its own
 * room has no link with is drawn whole only on a claimed face the classic
 * path draws that is one wall with the piece's own face (hd_level.cpp,
 * DecidePairs), and inside its room's window otherwise. The art of one wall
 * reaches over the seams of the faces the wall is made of, in its plane and
 * round the corners it turns, so a drawn face of the wall shows all of it.
 * A face of another surface does not: from ANDREA1's balcony the floor of
 * room 93's brick block is drawn through the loft's opening, while the
 * block's walls and roof stand in room 101 beside the facade's corner, and
 * their art is seen where the classic path shows them, in room 93's window.
 * Round a corner: ANDREA3's wooden wall of room 8 in room 115's air is faces
 * 76 and 77 on one side of the corner and the drawn face 78 on the other;
 * held to the plane alone it is cut at the corner, 59,516 pixels at
 * 2560x1440 from room 16, the gallery behind it showing with black seams. */
inline void HDVisWallLabels(const HDVisWallFace* faces, int count, int* label,
	double tolerance)
{
	if (count > HD_VIS_WALL_FACES)
		count = HD_VIS_WALL_FACES;

	for (int i = 0; i < count; i++)
		label[i] = i;

	for (int i = 1; i < count; i++)
		for (int j = 0; j < i; j++)
		{
			int ri = i, rj = j;

			while (label[ri] != ri)
				ri = label[ri];

			while (label[rj] != rj)
				rj = label[rj];

			if (ri == rj)
				continue;

			if (HDVisFacesOneWall(faces[i].v, faces[i].corners, faces[j].v,
					faces[j].corners, tolerance) ||
				HDVisFacesOneWall(faces[j].v, faces[j].corners, faces[i].v,
					faces[i].corners, tolerance))
			{
				if (ri < rj)
					label[rj] = ri;
				else
					label[ri] = rj;
			}
		}

	for (int i = 0; i < count; i++)
	{
		int r = i;

		while (label[r] != r)
			r = label[r];

		label[i] = r;
	}
}

/* Whether face `target` of a pair's claimed faces is one wall with a face
 * the classic path draws this frame (HDVisWallLabels says what for). */
inline bool HDVisWallDrawn(const HDVisWallFace* faces, int count, int target,
	double tolerance)
{
	if (count > HD_VIS_WALL_FACES)
		count = HD_VIS_WALL_FACES;

	if (!faces || target < 0 || target >= count)
		return false;

	int label[HD_VIS_WALL_FACES];

	HDVisWallLabels(faces, count, label, tolerance);

	for (int i = 0; i < count; i++)
		if (faces[i].drawn && label[i] == label[target])
			return true;

	return false;
}

/* Whether the visibility path draws nothing of a classic room: a room drawn
 * classic (GroupOfRoom has no group for it) whose own group holds no art at
 * all, none of whose classic faces any group's art covers (neither covered
 * nor covered in part by the stand rule), and whose inside holds a guest -
 * the art of a group linked with it, as many triangles as make a guest
 * (HDVisTables::guests) - or whose floor links all name rooms drawn HD
 * (`floorOnArt`, HDVisFloorOnArt: the level's verdict for each room, not
 * the frame's list).
 *
 * What for: such a room is classic scenery standing in for what the art
 * now models in its place - ANDY3's room 88 is a band of tree trunks and
 * leaves painted on flat faces round the trees of room 35's art that stand
 * in its inside (10,064 triangles). Drawn classic it hangs a flat slab of
 * old texture across the new trees; left out, the trees stand against the
 * sky. ANDY3's rooms 26, 28 and 210 are the same lids of painted trunks
 * and leaves over the hollows of rooms 24, 27 and 209, every floor square
 * of theirs over the room below, with no guest: the trees round the hollows
 * are rooms 86's and 87's art standing in their own boxes. The floor links
 * tell such a room from ANDY3's room 103, which has none. */
inline bool HDVisClassicRoomHidden(bool groupEmpty, bool facesCovered,
	int classicFaces, int guests, bool floorOnArt)
{
	return groupEmpty && !facesCovered && classicFaces > 0 &&
		(guests > 0 || floorOnArt);
}

/* Whether a room's floor links all name rooms drawn HD: `count` rooms from
 * `floorRooms` (HDVisTables::floorRooms, each room once), one at least, and
 * `drawnHD` true for every one of them. Every link and not one, so a room
 * that lies partly over classic ground keeps its faces. `drawnHD` is asked
 * of the level's verdicts (a group with art in its own box, through the
 * flip table), the same in every frame. */
template <class DrawnHD>
inline bool HDVisFloorOnArt(const int* floorRooms, int count,
	DrawnHD drawnHD)
{
	if (!floorRooms || count <= 0)
		return false;

	for (int i = 0; i < count; i++)
		if (!drawnHD(floorRooms[i]))
			return false;

	return true;
}

/* HDVisClassicRoomHidden over the tables, as the visibility path asks it of
 * room `room` (hd_level.cpp, VisHidesClassicRoom): its guests and its floor
 * links read from `t`, `drawnHD` as HDVisFloorOnArt asks it. False for a
 * room the tables do not hold.
 *
 * The enclosure table (HDVisTables::enclosedBy) is not asked: a room that
 * stands inside another keeps its faces, its own group hollow and never
 * drawn and the enclosing art covering none of them, so nothing else
 * closes its inside - JOBY2's sniper rooms 19 and 21, whose faces are the
 * black behind the facade's broken glass block windows. */
template <class DrawnHD>
inline bool HDVisClassicRoomVerdict(const HDVisTables& t, int room,
	bool groupEmpty, bool facesCovered, int classicFaces, DrawnHD drawnHD)
{
	if (room < 0 || room >= t.rooms || (size_t)room >= t.guestCount.size())
		return false;

	bool onArt = false;

	if ((size_t)room < t.floorCount.size() &&
		(size_t)room < t.floorFirst.size())
	{
		const int first = t.floorFirst[(size_t)room];
		const int count = t.floorCount[(size_t)room];

		onArt = count > 0 && first >= 0 &&
			first + count <= (int)t.floorRooms.size() &&
			HDVisFloorOnArt(&t.floorRooms[(size_t)first], count, drawnHD);
	}

	return HDVisClassicRoomHidden(groupEmpty, facesCovered, classicFaces,
		t.guestCount[(size_t)room], onArt);
}

/* Whether a room the walk lists turns the sky on: a classic room the
 * visibility path leaves out (HDVisClassicRoomHidden) that is not under the
 * sky itself (ROOM_OUTSIDE). The walk (game\draw.cpp, GetRoomBounds) then
 * sets `outside` as it does for a listed room under the sky.
 *
 * What for: such a room's own faces closed its window, and no art covers
 * them - ANDY3's lids 26, 28 and 210, whose leaf ceilings close the hollows
 * of rooms 24, 27 and 209. Left out, the window shows what stands behind
 * it: the trees of rooms 86 and 87 and, between them, the sky. The walk
 * from inside a hollow lists no room under the sky in many directions
 * (from room 24 looking west), and without the sky that window is the
 * clear colour, black, turning to sky and back as the eye turns. A room
 * left out that is under the sky (ANDY3's 88) turns it on already. */
inline bool HDVisClassicRoomOpensSky(bool hidden, bool underSky)
{
	return hidden && !underSky;
}

/* The walk's question for room `roomIndex` in the flipmap state of this
 * moment (defined in hd_level.cpp beside the rule it asks): false off the
 * visibility path and for a room a group draws. */
bool HDVisRoomOpensSky(int roomIndex);

/* The settings revision of hd_vis an installation's registry holds (the
 * value hd_vis_rev beside hd_vis, tomb5\tomb5.cpp). 0 - the value absent -
 * is every registry an earlier build wrote. Most of those builds wrote
 * hd_vis 0 on their first run, or the 0 an absent hd_vis read as; the last
 * few before the revision wrote and read 1. The option has no row, so a 0
 * among them is a default and never a choice. 1 is a registry whose hd_vis
 * this build or a later one has written or kept. */
#define HD_VIS_REV	1

/* The hd_vis a game runs with, from the value read (`saved`) and the
 * revision read (`rev`, 0 when absent).
 *
 * Below HD_VIS_REV the saved value is an earlier build's default and is
 * moved to 1, the visibility path, once: the caller writes the answer and
 * the revision back, so the next start reads both as they stand. From
 * HD_VIS_REV up the saved value is kept as it was set - a 0 written after
 * the move stays 0 - and anything above 1 reads as 0, as before.
 *
 * What for: hd_vis 1 is the default now, and an installation that ran an
 * earlier build may hold the 0 that build wrote on its first run; without
 * the revision a fresh 0 and a kept one look the same. */
inline unsigned long HDVisSetting(unsigned long saved, unsigned long rev)
{
	if (rev < HD_VIS_REV)
		return 1;

	return saved > 1 ? 0 : saved;
}

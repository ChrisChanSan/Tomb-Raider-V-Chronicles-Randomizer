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
 *              of the screen's width (the walk, game\draw.cpp) */
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
void HDVisLevelBuild(const trf::remaster::Trg& trg);
void HDVisLevelClear();
const HDVisTables* HDVisLevelTables();

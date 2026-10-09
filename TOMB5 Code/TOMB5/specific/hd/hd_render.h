/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3, TROne-main
 * (copy of 2026-10-06, commit 75c4c843), host\render\game_renderer.cpp,
 * hd_load and hd_draw_room), TROne's own design. The renderer under it is
 * new: TROne is OpenGL 3.3 with vertex arrays, texture arrays and runtime
 * GLSL, this is Direct3D 9 with a vertex declaration, one texture per page
 * and compiled bytecode, beside a fixed-function neighbour that must find its
 * state exactly as it left it. See THIRD-PARTY.md.
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

#include "hd_pagecache.h"
#include "hd_gloss_math.h"		// HDSectorFloor, HDRoomGrid
#include "hd_cover.h"			// HDCoverTri, HDCoverFace - the stand ranges

#include <math.h>			// sqrt, for HDPairFaceDistance

struct IDirect3DDevice9;
struct _D3DMATRIX;
typedef struct _D3DMATRIX D3DMATRIX;

// The HD room renderer.
//
// A second draw path beside the classic one. It shares the back buffer and
// the depth buffer with it and nothing else: its own shaders, its own vertex
// declaration, its own buffers, its own textures. The engine calls it from
// draw.cpp; RoomProbe drives it without the engine.
//
// Why the depth buffer can be shared: the classic path is fixed function
// with pre-transformed vertices and writes its own depth,
//
//     sz = f_a - f_boo * rhw,  rhw = 2048 / z_view,  f_boo = f_b / 2048
//        = f_a - f_b / z_view
//
// which is exactly what a Direct3D projection matrix produces after the w
// division, with _33 = f_a, _43 = -f_b, _34 = 1, _44 = 0. So an HD room and a
// classic item land on the same depth for the same point in space, and they
// can be drawn in any order, in the same frame, into the same buffer. There
// is no sorting heuristic and no depth bias anywhere in this file;
// HDRenderSelfTest() checks the identity at start-up.

/* ------------------------------------------------------ the parity block --
 *
 * Everything the classic path does to a room vertex that the .TRG's baked
 * light does not already contain. The engine fills these in hd_level.cpp out
 * of its own globals; the diagnostic tool fills them by hand. It is the list
 * of what specific\drawroom.cpp, aRoomletTransformLight, computes per vertex,
 * turned into shader constants.
 *
 * The two maxima are the shader's, not the engine's: ps_3_0 has no relative
 * addressing of constant registers, so the light loop is unrolled against a
 * compile time bound (hd_room.hlsl says why). Both are logged when they clip:
 * once per level with the room that did it, and as a total at the end of the
 * level, never once per frame.
 */
#define HD_MAX_LIGHTS	8
#define HD_MAX_BULBS	16

// One entry of the engine's `dynamics` array, made room local and scaled the
// way aBuildRoomletLights scales it (drawroom.cpp).
struct HDLightSource
{
	float x, y, z;		// room local
	float falloff;		// (dynamic->falloff >> 1) + (dynamic->falloff >> 3)
	float r, g, b;		// 0..1
};

// One entry of ActiveFogBulbs, already in VIEW space - aBuildFogBulbList puts
// it there once per frame (drawroom.cpp) and the shader wants it in
// exactly that space, because the surface point arrives there too.
struct HDFogBulb
{
	float x, y, z;
	float rad;
	float r, g, b;
	float density;		// bulb->d, which is 1 / den
};

// Per frame, set once between HDRenderFrameBegin and the first room.
struct HDFrameLighting
{
	float centerX, centerY;		// f_centerx, f_centery
	float wibble;			// the engine's own counter, as a float
	int cameraUnderwater;		// camera.underwater
	float distanceFogStart;		// tomb5.distance_fog * 1024
	const float* wibbleTable;	// vert_wibble_table, 32 floats
	const HDFogBulb* bulbs;
	int bulbCount;

	/* `clock` is the game clock in seconds - the engine's own GlobalCounter
	 * divided by 30, made monotonic, so it stops with the game and never
	 * wraps. `motion` is tomb5.hd_motion: the UV scroll a .TRG part carries
	 * itself, the wind weight of its vertices and the water surface wobble,
	 * all three at once. Zero on both draws a frame with nothing moving. */
	float clock;
	int motion;

	/* tomb5.hd_reflect: the specular the .TRG materials 3 and 17 carry
	 * themselves - the marble of Rome, the painted steel of the submarine.
	 * Zero draws the same picture with no specular, to the bit. */
	int reflect;

	/* The time of the state this frame draws, in seconds: `clock` less the
	 * part of a logic tick the drawn state stands behind it (hd_book.h,
	 * HDBookClock). The .TRG material 15 books turn their pages on it, so
	 * that at 60 frames a second a page stands for the same number of
	 * presents every time. Equal to `clock` at fps_mode 0. A double, because
	 * the page is a whole number of steps since the level began and a float
	 * of an hour's seconds no longer tells two sixtieths apart cleanly. */
	double bookClock;

	/* The eye under the water (hd_room.hlsl, "under the water"): `under` 1
	 * when the eye is in a water room of a level with sheets
	 * (HDRenderWaterUnderOn), that room, the eye's world y and world up
	 * (-y) in view space. 0 everywhere else, which leaves every shader's
	 * colour as it was. */
	int under;
	int underRoom;
	float underEyeY;
	float underUp[3];
};

// Per room. `waterTable` is 64 records of four bytes in WATERTAB order
// (signed shimmer, signed choppy, unsigned random, unsigned abs) - the
// engine's own WaterTable[room->MeshEffect] rather than a rebuilt one.
struct HDRoomLighting
{
	float originX, originY, originZ;	// r->x, r->y, r->z
	float surfaceY;				// minfloor - r->y, room local
	int underwater;				// r->flags & ROOM_UNDERWATER
	int choppy;				// run the choppy displacement at all
	int surfaceGate;			// shimmer needs the vertex on surfaceY
	int redAlert;				// the Red Alert gas branch
	float sel1[4];				// which baked mark drives flags & 1
	const void* waterTable;
	const HDLightSource* lights;
	int lightCount;
};

/* The water marks. The .TRG marks nothing: no bit in a TrgVertex says "water
 * surface" or "this one shimmers". The classic path knows, because a roomlet
 * keeps its water vertices first (ROOMLET::nWVtx) and because the shimmer
 * predicate walks the neighbouring rooms through the portals
 * (IsMistVert, IsReflectionVert and IsShoreVert in drawroom.cpp). So the
 * answer is computed once, at level build, on the classic vertices, and
 * carried to the HD vertices by position - the same road TROne took for its
 * own water flag in its copy of 2026-10-05 (hd_load and hd_water_mark: a per
 * room hash of packed int16 x/y/z; in 75c4c843 TROne keeps that hash for shore
 * corners and matches water surfaces as horizontal planes). The road holds on
 * our own count: 107,118 of the 126,170 classic room vertices of the fifteen
 * levels have a .TRG vertex of the same room at exactly their position
 * (TrgProbe -shadeorder).
 *
 *   bit 0  the vertex is one of the roomlet's nWVtx water vertices - it gets
 *          the choppy displacement
 *   bit 1  the answer of the "vertex sits on the water surface" branch of the
 *          shimmer, without the height test: mist, reflection or shore
 *   bit 2  the answer of the "whole room reflects" branch: the room carries
 *          ROOM_REFLECT and ROOM_REFLECT_CEILING and this vertex passes
 *
 * Which of the three the shader looks at is a per room decision, taken per
 * frame on the processor and handed over in HDRoomLighting::sel1 - the room
 * flags can change while a level runs, the geometry cannot.
 */
/* A few of a group's own vertices, kept aside while the vertex buffer is
 * filled - the .TRG dies at the end of HDRenderLevelBegin and the buffer is
 * write only, so this is the only chance. The matrix proof reads the three
 * positions; the parity proof (hd_light.h) reads all of it. */
struct HDRenderSample
{
	short x, y, z;			// room local
	/* Biased by 127, as on disk: read so, 53.9 % of the corners of the
	 * fifteen files lie within 10 degrees of their triangle's plane normal,
	 * read as signed bytes 0.5 % (TrgProbe -normals). */
	unsigned char nx, ny, nz;
	unsigned char c1[3], c2[3];	// the two baked light triples, R G B
	unsigned char bits;		// the water marks that were found here
};

/* What moves in one room, for the log and for RoomProbe -animtex. All of it
 * is read out of the .TRG: the scroll is material 1's param.xy, the sway is
 * TrgVertex::sway, the flipbook is material 13, whose param[0..2] name three
 * further texture layers of the file's own list. */
struct HDRenderGroupMotion
{
	int scrollRanges;		// draw calls in this room that scroll
	int scrollTris;
	float scrollU, scrollV;		// the fastest of them, page units per second
	int swayVertices;		// vertices with a wind weight
	int swayMax;			// the largest weight found, 0..32767
	int flipbookParts;		// material 13 parts
};

struct HDWaterMark
{
	short room;
	short x, y, z;		// room local, classic units
	unsigned char bits;
};

/* The room's own box, room local.
 *
 * The box TR5 itself calls the inside of a room - game\draw.cpp,
 * calc_animating_item_clip_window, where an item is asked whether it fits
 * wholly inside the room it is in:
 * `r->x + 1024 .. r->x + (y_size - 1) * 1024` across, the same in z out of
 * `x_size`, and `maxceiling .. minfloor` in height. The border ring of wall
 * sectors is not in it on purpose: the art the remaster puts in that ring - a
 * wall's relief, a corner pier, a doorway's reveal - stands in the
 * neighbouring room's space by construction, and the portal window worked out
 * for the room's inside cannot hold it.
 *
 * Handed in room local (the .TRG's own frame: x and z relative to the room's
 * origin, y in world units, which is what the file stores) and in classic
 * units, one entry per room, in room order. Call it before
 * HDRenderLevelBegin; the pointer is not kept past that call. Without it
 * every group is drawn as one piece.
 *
 * `originX` and `originZ` are the room's origin in world units, r->x and
 * r->z: what puts two rooms' art side by side, so that the pieces of one
 * light shaft standing in several rooms are faded as one asset (hd_render.cpp,
 * BuildGlowParts). */
struct HDRoomBox
{
	float mins[3];
	float maxs[3];
	float originX, originZ;
};

/* A mirror in a room's wall, found in the art when the level is built.
 *
 * TR5 carries no mirror of its own: no level of the game's SCRIPT.DAT has a
 * CMD_MIRROR or the GF_MIRROR flag, so gfMirrorRoom stays unset and
 * Draw_Mirror_Lara (game\mirror.cpp) never runs. The remaster builds its
 * mirrors out of art instead: behind the wall stands a copy of the room,
 * reflected in the wall's plane, and a pane in the wall's opening shows it.
 * RICH2's two washrooms are the case - room 134 in the plane x = 33792 and
 * room 135 in x = 30720, their cabins and basins standing a second time,
 * mirror-inverted, on the far side. What the art cannot hold is what moves:
 * Lara and the items. They are drawn once more across the plane
 * (Draw_Mirror_Items, game\mirror.cpp) out of this record.
 *
 * Everything is in world units. `side` is where the copy lies: -1 at smaller
 * x (or z) than the plane, +1 at larger; the room is on the other side. The
 * box is the copy's own art, the triangles of the group behind the plane.
 * The three counts are what the rule in hd_render.cpp (FindMirrors) decided
 * on, for the log. */
struct HDMirror
{
	int group;
	int axis;		// 0: a plane of constant x, 2: of constant z
	int side;
	float plane;
	float mins[3];
	float maxs[3];
	int pane;		// triangles lying in the plane - the glass
	int behind;		// triangles of the group behind the plane
	int mirrored;		// of those, the ones the reflection lands on art
};

/* One room's inside, asked of another room's art.
 *
 * The portal window is a property of a triangle, not of a room. One of these
 * says: "the art of `group`, wherever it falls inside this box, is standing
 * inside room `holder` and is to be drawn inside `group`'s own portal
 * window." Every other triangle, the holder's included, draws over the whole
 * screen. A window on the holder's whole room would be wrong: in ANDREA2's
 * courtyard rooms 134 and 203 both hold another room's art (143 and 248), and
 * scissoring both to their own portal rectangles cuts a black band across the
 * picture where the two rectangles meet.
 *
 * The box is the holder's inside - the volume TR5 itself calls the inside of
 * a room (game\draw.cpp, calc_animating_item_clip_window): one sector in
 * from the edge of its grid on each side. Handed in the GROUP's own local
 * frame, like the room boxes above, and on the same contract: call
 * HDRenderSetIntrusions before HDRenderLevelBegin, the pointer is not kept.
 * Without it nothing is ever tagged and no room is ever windowed.
 *
 * Tagged is not yet windowed. The triangles of one entry are split by the
 * classic face of `group` they belong to (HDPairFace below), and each piece
 * is drawn whole or inside the window per frame - see HDRenderPairWhole. */
struct HDIntrusion
{
	int group;			// whose art is being asked about
	int holder;			// the room whose inside the box is
	float mins[3];
	float maxs[3];
};

/* One classic face of a room whose art stands inside another room.
 *
 * The classic path draws a room's face whole or not at all: its corner rule
 * drops a face whose corners all lie beyond one edge of the room's portal
 * window and keeps every other one whole (specific\drawroom.cpp, the four
 * clip bits per vertex; specific\polyinsert.cpp, `c[v0] & c[v1] & ...`) -
 * the rule asked here; facing and the water chop are not. The remaster's art
 * of that face stands where the face stands, a few units off it and with its
 * relief in front of it, so the art follows the face: an intruding triangle
 * whose centroid lies within HD_PAIR_FACE_REACH of a classic face's plane and
 * inside its outline belongs to that face, the nearest one if several qualify.
 * The engine draws the art its pair's faces claimed whole on a frame the
 * classic path draws any of those faces (hd_level.cpp, DecidePairs). A
 * triangle no face claims is drawn inside the window when it stands in the
 * box one sector smaller than the inside, in x and z, and open otherwise.
 *
 * `v` are the face's corners in the GROUP's own local frame, like the boxes
 * above; `corners` is 4 for a quad and 3 for a triangle. Same contract again
 * - HDRenderSetPairFaces before HDRenderLevelBegin, the pointer is not kept. */
struct HDPairFace
{
	int group;			// the room the face belongs to
	int corners;
	float v[4][3];
};

/* How far the art of a face stands off it. Measured on ANDREA1's pair of
 * rooms 93 and 101, where room 101's facade stands inside room 93: all 540
 * of its intruding triangles lie within 86 units of one of six classic faces
 * of 101 (the cornices and window frames furthest out), 90 of them within 1
 * unit; room 93's 108 triangles inside room 101 lie on six faces of 93
 * within 1 unit. 128, half a click of TR5's height grid, is the next round
 * number past the furthest of them. */
#define HD_PAIR_FACE_REACH	128.0
// The stand ranges ask the same reach of the faces of a room drawn classic.
static_assert(HD_COVER_REACH == HD_PAIR_FACE_REACH, "one reach for both rules");

/* The distance from `p` to the face's plane when `p` stands within the
 * face's outline, and -1 when it does not. A quad is asked as its two
 * triangles (0,1,2) and (0,2,3), which is how the classic path splits it,
 * and the nearer answer is kept. Inline, so that TrgProbe asks exactly this
 * of the same numbers. */
inline double HDPairFaceDistance(const HDPairFace& face, const float p[3])
{
	double best = -1.0;

	for (int half = 0; half + 2 < face.corners; half++)
	{
		const float* a = face.v[0];
		const float* b = face.v[half + 1];
		const float* c = face.v[half + 2];
		const double e1[3] = { (double)b[0] - a[0], (double)b[1] - a[1], (double)b[2] - a[2] };
		const double e2[3] = { (double)c[0] - a[0], (double)c[1] - a[1], (double)c[2] - a[2] };
		const double n[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};
		const double length = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];

		if (length <= 0.0)
			continue;			// a face of no area

		const float* edge[3][2] = { { a, b }, { b, c }, { c, a } };
		bool inside = true;

		// p lies on the inner side of all three edges, seen along n.
		for (int k = 0; k < 3 && inside; k++)
		{
			const float* u = edge[k][0];
			const float* w = edge[k][1];
			const double e[3] = { (double)w[0] - u[0], (double)w[1] - u[1], (double)w[2] - u[2] };
			const double m[3] =
			{
				n[1] * e[2] - n[2] * e[1],
				n[2] * e[0] - n[0] * e[2],
				n[0] * e[1] - n[1] * e[0]
			};

			if (m[0] * ((double)p[0] - u[0]) + m[1] * ((double)p[1] - u[1]) +
				m[2] * ((double)p[2] - u[2]) < 0.0)
				inside = false;
		}

		if (!inside)
			continue;

		double d = (n[0] * ((double)p[0] - a[0]) + n[1] * ((double)p[1] - a[1]) +
			n[2] * ((double)p[2] - a[2])) / sqrt(length);

		if (d < 0.0)
			d = -d;

		if (best < 0.0 || d < best)
			best = d;
	}

	return best;
}

/* An opening the randomizer made, taken out of the art.
 *
 * The remaster's art still shows a wall the randomizer has cut a doorway
 * through. The .hdm sidecar names the box the opening lives in, and every
 * triangle of every group that intersects that box is left out of the
 * buffers at build time: the room keeps its HD art, the doorway is a hole in
 * it, and nothing is decided per frame. The alternative - drawing the whole
 * room the classic way - leaves seams in the middle of an HD level, with the
 * void or the skybox showing between HD and classic floors and walls.
 *
 * Every group, not just the two rooms the doorway joins: the remaster's rooms
 * reach into each other by construction, and a neighbour's overhang standing
 * in a doorway shuts it as surely as the room's own wall does.
 *
 * Handed in the GROUP's own local frame, like the room boxes and the
 * intrusions above, and on the same contract: call HDRenderSetCuts before
 * HDRenderLevelBegin, the pointer is not kept. Without it nothing is ever
 * cut, which is what the game does for a vanilla level and what the
 * diagnostic tools do unless they are handed a sidecar. */
struct HDCut
{
	int group;			// whose art is being asked about
	int box;			// which sidecar box this is - for the log
	float mins[3];
	float maxs[3];
};

struct HDRenderStats
{
	int trgVersion;
	int groups;
	int rooms;			// as handed in
	int skyGroups;
	int emptyGroups;		// a group with no index at all -> that room classic
	int vertices;
	int indices;
	int ranges;			// draw calls for a whole level, all rooms, all passes
	int splitRanges;		// ranges the device's primitive limit forced apart
	/* Triangles standing outside their own room's box, and the groups that
	 * have nothing inside their room at all - ten in the whole game. Such a
	 * room stays classic; drawn HD it would be a black hole. */
	int overhangTris;
	int hollowGroups;
	/* The triangles that stand inside another room, the
	 * draw calls they cost and the groups that have any at all. These are the
	 * only draw calls in the game the scissor test is ever on for. */
	int intrudingTris;
	int intrudingRanges;
	int intrudingGroups;
	/* The triangles the sidecar's cut boxes took away before
	 * they ever reached a buffer, and the groups that lost any. Zero for
	 * every vanilla level and for every randomized level whose run opened
	 * nothing. */
	int cutTris;
	int cutGroups;
	int index32;			// 1 = 32 bit index buffer
	/* The widest span of vertex numbers one draw call reaches over. It
	 * decides whether a card with no 32 bit index buffer can still draw the
	 * level: with 16 bit indices a draw call addresses 65536 vertices from
	 * its own base, so a level whose every range stays inside that window
	 * needs no 32 bit buffer at all. */
	int indexWindow;
	/* The same for one triangle: how far apart the three corners of the
	 * worst triangle in the level are in the vertex array. It is the floor
	 * under indexWindow - no cutting of ranges can go below it - and says
	 * whether a card with only 16 bit indices could draw this level at all. */
	int triangleWindow;
	int maxTextureSide;		// what the device said, 0 = it did not say
	int waterMarks;			// classic marks handed in
	int markedVertices;		// HD vertices a mark was found for
	int markedChoppy;		// of those, the nWVtx set
	int markedShimmer;		// of those, the surface shimmer set
	int markedRoomShimmer;		// of those, the whole-room set
	/* Of the shimmer set, the vertices no classic corner named and that the
	 * surface plane answered for - see the note in hd_render.cpp beside
	 * plane_map. */
	int markedFlooded;
	/* Of the marked set, the vertices whose own normal is not vertical - a
	 * wall or a pillar standing at exactly the water's height inside the
	 * pool's box. The colour term keeps them, because the classic path gives
	 * them one; the geometry, the UV wobble and the caustic do not, because a
	 * water surface is flat. */
	int markedWall;
	/* Flood planes past the fourth a group could not keep, so the
	 * shore taper has no box for them. Measured 0 in all fifteen files; a
	 * level that ever reports one has a room with more water planes than the
	 * table was built for. */
	int waterPlanesDropped;
	int biggestRangeTris;
	int fallbackPageTris;		// layer 0 - drawn on the level's first page
	int scrollParts;		// .TRG parts carrying a material 1 UV scroll
	// Opaque parts of the cut-out materials 6, 7 and 8 - the ones whose
	// param[0] is an alpha reference and that draw with the alpha test on
	// (hd_render.cpp, MaterialAlphaRef).
	int cutoutParts;
	/* Additive parts whose material handed them a weight below 1, the
	 * smallest weight found, and how many of those also carry a swing time
	 * (hd_render.cpp, MaterialAddWeight). */
	int addWeightParts;
	int addSwingParts;
	float addWeightMin;
	/* Material 2 parts that draw still because their swing is under half a
	 * second (hd_render.cpp, MaterialAddPeriod), and the stacked seas drawn
	 * once: the parts and the copies left out (MarkStackedCopies). */
	int addStillParts;
	int stackedParts;
	int stackedTris;
	/* The material 2 parts the glow shader draws, how many of them fade at
	 * their card sides, how many take |N.V| from the face normal, and the
	 * assets they make up (hd_render.cpp, BuildGlowParts). All zero with the
	 * glow shader off. */
	int glowParts;
	int glowEdgeParts;
	int glowFaceParts;
	int glowAssets;
	/* Parts of a sky group that carry no baked light and are therefore drawn
	 * at light 1 - the band of sky itself. */
	int skyFlatParts;
	int scrollRanges;		// draw calls that scroll, of `ranges`
	int scrollTris;			// triangles in them
	int swayVertices;		// vertices with TrgVertex::sway != 0
	int swayParts;			// parts of the moving materials 1, 7, 8
	int swayDropped;		// vertices whose weight was taken away again
	int flipbookParts;		// material 13 parts - see HDRenderGroupAnim
	/* Of those, the ones whose four pages and frame time all resolved and
	 * that therefore run as a flipbook, the ones that did not and keep their
	 * own page, and the draw calls the running ones cost (hd_render.cpp,
	 * MaterialFlipTime). */
	int flipbookDrawn;
	int flipbookDropped;
	int flipbookRanges;
	/* The material 15 books (hd_render.cpp, MaterialBookTime), counted per
	 * part and pass that holds triangles: the ones whose every page resolved
	 * and that turn, how many of those are in the additive pass, the ones
	 * that keep their own page because a page or a number did not resolve,
	 * the distinct pages the turning ones hold, the draw calls they cost, and
	 * the shortest and longest time a page stands among them (0 and 0 where
	 * none turns). */
	int bookParts;
	int bookAdditive;
	int bookDropped;
	int bookPages;
	int bookRanges;
	float bookStepMin;
	float bookStepMax;
	/* Opaque parts of materials 3 and 17 whose own numbers gave them a
	 * gloss (hd_render.cpp, MaterialGloss). */
	int glossParts;
	/* Distinct horizontal planes the art covers, over every group - the
	 * table HDRenderGroupFlatArt answers out of. */
	int flatPlanes;
	/* The see-through parts of the alpha pass - material 14, the glass -
	 * and the draw calls they cost. */
	int blendParts;
	int blendRanges;
	/* The clear floors of the alpha pass (HD_PASS_CLEAR): the parts with at
	 * least one triangle on such a page, the distinct pages those
	 * triangles are on, and the draw calls they cost. */
	int clearParts;
	int clearPages;
	int clearRanges;
	/* The still water surfaces - material 16. The parts, counted per pass
	 * block, how the file splits them between the passes, their triangles,
	 * how many of them live in a sub-rectangle of their page, the smallest
	 * such span in 255ths, and the largest UV travel the ripple was given, in
	 * page units (hd_render.cpp, MaterialRipple). */
	int waterParts;
	int waterOpaqueParts;
	int waterAdditiveParts;
	int waterTris;
	int waterSubRectParts;
	int waterStillParts;		// of those, the ones with no ripple at all
	int waterSmallestSpan;
	float waterRippleCap;
	double vertexBytes;
	double indexBytes;
	double readMs;			// reading and parsing the .TRG
	double buildMs;			// sorting and filling the buffers
	HDPageCacheStats pages;
};

// One row of the depth proof.
struct HDRenderZSample
{
	double zView;			// distance along the view axis, classic units
	double classic;			// f_a - f_boo * rhw, as polyinsert.cpp writes it
	double matrix;			// (_33 * z + _43) / z, what the device computes
	double diff;
};

// Creates the shaders and the vertex declaration. Nothing level-specific.
bool HDRenderInit(IDirect3DDevice9* device, const char** why);

/* Pretend the card is poorer than it is.
 *
 * The fallbacks for a small card - pages that start below their top level
 * because the device cannot hold 4096 pixels, and 16 bit index buffers with a
 * base vertex per draw call because the device cannot address 1.1 million -
 * are unreachable on a current card. This call bends the caps that
 * HDRenderInit read from the device so that the diagnostic tool can draw the
 * same picture through those fallbacks.
 *
 * maxTextureSide 0 leaves the device's own answer alone; noIndex32 non-zero
 * makes the renderer believe MaxVertexIndex is 65535. Call it before
 * HDRenderInit. The engine never calls it - there is no registry key and no
 * option for it, on purpose. */
void HDRenderCapOverride(int maxTextureSide, int noIndex32);

/* The shore taper off, for the diagnostic tool alone. Non-zero hands the
 * shader a shore box no vertex can be near the edge of, so the swell reaches
 * the water's own edge at full height - the comparison picture out of the
 * same build. The engine never calls it; there is no option and no registry
 * key for it. */
void HDRenderShoreOverride(int off);

/* The glass off, for the diagnostic tool alone. Non-zero leaves material 14
 * in the alpha pass as a cut-out - an alpha test at 0.5, drawn solid - the
 * comparison picture out of the same build. Call it before
 * HDRenderLevelBegin; the engine never calls it and there is no option and
 * no registry key for it. */
void HDRenderBlendOverride(int off);

/* The clear floors off, for the diagnostic tool alone. Non-zero leaves the
 * alpha-pass triangles on a part-way clear page (HD_PASS_CLEAR) in the alpha
 * test, drawn solid - the before picture out of the same build. Call it
 * before HDRenderLevelBegin; the engine never calls it and there is no
 * option and no registry key for it. */
void HDRenderClearOverride(int off);

/* The water's "a surface is flat" test off, for the diagnostic tool alone.
 * Non-zero gives the wall vertices at the waterline the surface's UV wobble
 * and its caustic back - the comparison picture out of the same build. May
 * be called at any time; it is one vertex shader constant per draw call. The
 * engine never calls it and there is no option and no registry key for it. */
void HDRenderUprightOverride(int off);

/* The stacked seas drawn with every copy, for the diagnostic tool alone.
 * Non-zero keeps the higher copies of a material 16 sheet the file stacks
 * three times or more (hd_render.cpp, MarkStackedCopies) - the comparison
 * picture out of the same build. Call it before HDRenderLevelBegin; the
 * engine never calls it and there is no option and no registry key for it. */
void HDRenderStackOverride(int keep);

/* The swing fence off, for the diagnostic tool alone. Non-zero lets a
 * material 2 part swing whatever its period, under half a second as well
 * (hd_render.cpp, MaterialAddPeriod) - the comparison picture out of the same
 * build. Call it before HDRenderLevelBegin; the engine never calls it and
 * there is no option and no registry key for it. */
void HDRenderSwingOverride(int keepFast);

/* One material's additive draw calls alone, for the diagnostic tool alone:
 * the additive pass draws only the ranges of that .TRG material (the low
 * byte, without the destructible bits), and -1 draws all of them, which is
 * what the engine always has. May be called at any time. */
void HDRenderAdditiveOnly(int material);

/* The glow's screen composition, its anisotropic sampling and its own pixel
 * shader off, for the diagnostic tool alone. `sum` non-zero draws material 2
 * ONE / ONE like every other additive material, `noAniso` non-zero samples it
 * with the plain trilinear filter of every other draw call (hd_render.cpp,
 * draw_pass), `noShader` non-zero draws it with the room pixel shader and
 * none of the glow shader's fades (hd_glow.hlsl) - the comparison pictures
 * out of the same build. `noShader` takes effect at the next
 * HDRenderLevelBegin, because the glow's draw calls are split by part at
 * level build; the other two may change at any time. The engine never calls
 * it and there is no option and no registry key for it. */
void HDRenderGlowOverride(int sum, int noAniso, int noShader);

/* What the level build decided for one glow part (hd_render.cpp,
 * BuildGlowParts), for the diagnostic tool alone. */
struct HDRenderGlowPart
{
	int group, part;
	int page;			// the .DDS number of its first triangle
	int u0, u1;			// the u range it uses, 0..255
	int v0, v1;			// and the v range
	int edge;			// 1 = the card sides fade
	int face;			// 1 = |N.V| from the face normal
	int corners, disagree;		// corners seen, and those with |N.f| < 0.9
	int asset;			// which asset it belongs to
	float axis[3];			// the asset's page-v axis
	float lo, hi;			// the asset's ends along it, world units
	float fade;			// the end fade's length, 0 = none
};
int HDRenderGlowPartCount();
bool HDRenderGlowPartInfo(int at, HDRenderGlowPart* out);

/* What the level build decided for one material 15 part in one pass
 * (hd_render.cpp, MaterialBookTime), for the log and the diagnostic tool:
 * the book's first and last page, how many pages it holds, how long one
 * stands, where in the cycle the part starts, the weight it is drawn with
 * (1 outside the additive pass), and 1 in `dropped` for a part that keeps
 * its own page because a page or a number did not resolve. */
struct HDRenderBookPart
{
	int group, part, pass;
	int page0, pageLast;
	int pages;
	float step;
	int phase;
	float weight;
	int dropped;
};
int HDRenderBookPartCount();
bool HDRenderBookPartInfo(int at, HDRenderBookPart* out);

/* The first book the frames of this level drew, and the page it showed in
 * the last frame that drew it - the book the log follows page by page. False
 * until a book has been drawn with hd_motion on. `group` and `part` name the
 * part of the draw call's first triangle; the parts of one draw call share
 * their book, step and phase, so they turn together. */
bool HDRenderBookWatch(int* group, int* part, int* page);

/* The diagnostic tool's -bookpage: every book shows page `page` of its own
 * run, whatever the clock and the phase say, or runs on the clock again with
 * -1. For finding a book's brightest page by drawing each in turn. The engine
 * never calls it and there is no option and no registry key for it. */
void HDRenderBookPageOverride(int page);

void HDRenderShutdown();
bool HDRenderReady();

// Reads <installRoot>\DATA\<stem>.TRG, builds one vertex buffer and one index
// buffer for the whole level, sorts every group's triangles by (pass, page)
// into ranges, and loads the pages through hd_pagecache. numRooms is the
// classic level's room count: groups at or above it are the sky dome and are
// kept out of the room table (they can still be drawn by number, which is
// what the diagnostic tool does).
bool HDRenderLevelBegin(const char* installRoot, const char* stem, int numRooms,
	bool managedPages, int threads, HDRenderStats* stats, const char** why);

// The classic path's water marks for the level that is about to be built.
// Call it before HDRenderLevelBegin; it keeps a copy of nothing, it only
// remembers the pointer for the length of that call. Passing 0 (or never
// calling it) leaves every HD vertex unmarked, which is what the diagnostic
// tool does unless it is asked for water.
void HDRenderSetWaterMarks(const HDWaterMark* marks, int count);

/* The water sheets: this project's own water surface.
 *
 * A water sector is a 1024-unit square of a room that is not under water
 * and whose room below (FLOOR_INFO::pit_room) is an underwater room; its
 * surface is the top of that water room there - the highest corner of its
 * ceiling over the square, where Lara's head breaks the surface - and not
 * the square's own floor, which a split sector raises over the water on
 * one triangle. A square whose dry floor stands more than a click over that
 * top at every corner is no water square (water_square in hd_render.cpp).
 * Built with `sheets` 0, a square's surface is its own floor, none is left
 * out and no body is a river: what moves the remaster's water art then is
 * what moves it without the sheets.
 * HDRenderSetWaterSheets finds them in
 * the classic level's floor data, one HDWaterRoom per room, joins them into
 * bodies - four-connected at one world height, across rooms - classes every
 * body (basin, channel, sea, river: WaterClassOf in hd_render.cpp), applies
 * the
 * override rows (the compiled table, then hd_water.txt) and builds a grid of
 * 256-unit quads over every sector that draws a sheet, for WaterVS and
 * WaterPS in the blend phase of its room. A sector of a room drawn classic,
 * or of a body an override row of class none switches off, stays in its
 * body and in the shore test and draws no sheet. Flat art the remaster lays
 * over a sector does not take its sheet away. Its own water art - material
 * 16 and the flat art its water marks name - in the sheet's plane over a
 * sector whose sheet draws is left out, unless a row says `art keep`; any
 * other art draws in its own pass as it does without the sheet, and where
 * it lies at the surface's own height the depth variant of WaterPS clips
 * the sheet, so the art shows itself.
 *
 * Called after HDRenderLevelBegin (it reads the groups' light samples) and
 * before the first frame; nothing of the arrays is kept. `sheets` 0 builds
 * the bodies and their classes and no sheet, and switches the caustic under
 * them off. No rooms removes it all. The engine and the diagnostic tool both
 * build through this one function. */
enum
{
	HD_WATER_BASIN = 0,
	HD_WATER_CHANNEL = 1,
	HD_WATER_SEA = 2,
	/* A body the classic level's currents pull through: any of its squares
	 * carries a TO_SINK trigger in its water. */
	HD_WATER_RIVER = 3,
	HD_WATER_CLASSES = 4,
	/* An override row's class 'none': no sheet for the bodies the row
	 * reaches, which keep the class they have. */
	HD_WATER_NONE = -2
};

// The palette's four sections of the game.
enum
{
	HD_WATER_ROME = 0,
	HD_WATER_RUSSIA = 1,
	HD_WATER_IRELAND = 2,
	HD_WATER_VCI = 3,
	HD_WATER_SECTIONS = 4
};

// The room flags the sheets read: ROOM_UNDERWATER, and ROOM_OUTSIDE together
// with ROOM_NOT_INSIDE for open air (global\types.h).
#define HD_WATER_ROOM_UNDERWATER	0x0001
#define HD_WATER_ROOM_OPEN		0x0028

// The rooms a body line names before it counts the rest.
#define HD_WATER_BODY_ROOMS	16

/* One room of the classic level. The squares run x-major: square (sx, sz)
 * is entry sx * sizeZ + sz, sx along world x - ROOM_INFO's y_size squares,
 * sz along z - its x_size.
 *
 * `slope` holds four words a square, as the floor data has them: the type
 * word and the data word of the floor's slope entry - the first TILT,
 * SPLIT1/2 or NOCOLF entry of the square's chain, which GetHeight adds -
 * and of the ceiling's - the entry GetMinimumCeiling reads, the first of
 * the chain or the second behind a floor slope, when it is a ROOF, SPLIT3/4
 * or NOCOLC entry; 0 and 0 where there is none. `sink` is the camera
 * entry the first TO_SINK command of the square's chain names, or -1
 * (game\control.cpp, the triggers: lara.current_active). The three may be
 * 0: every square is then flat and pulls nothing. */
struct HDWaterRoom
{
	int originX, originY, originZ;	// r->x, r->y, r->z
	int sizeX, sizeZ;		// squares along x and along z
	int flags;			// the room's flags
	int flip;			// the flipmap partner, or -1
	int drawn;			// 1 when the room draws its own HD group
	const signed char* floor;	// the floor of each square, clicks
	const short* below;		// the room below each square, or -1
	const signed char* ceiling;	// the ceiling of each square, clicks
	const unsigned short* slope;	// four words a square, see above
	const short* sink;		// the sink each square names, or -1
};

/* The sinks of the classic level (the camera table, OBJECT_VECTOR): world x,
 * y and z of each entry and its `data`, which a sink reads as its strength
 * (game\laraswim.cpp, LaraWaterCurrent). Before HDRenderSetWaterSheets;
 * the array is copied. None leaves every body without a current. */
struct HDWaterSink
{
	int x, z;
	int strength;
	int y;
};

void HDRenderWaterSinks(const HDWaterSink* sinks, int count);

void HDRenderSetWaterSheets(const HDWaterRoom* rooms, int count, int section,
	const char* stem, int sheets);

/* The corner heights of one square out of its height byte and its slope
 * entry, as the water squares take them: out[0..3] the world y at the
 * corners (0, 0), (1024, 0), (0, 1024) and (1024, 1024) of the square, x
 * first. `ceiling` 1 reads the words as a ceiling's. RoomProbe holds them
 * against the randomizer's own Tr5Level::cornerFloor and cornerCeiling. */
void HDRenderWaterCorners(int clicks, unsigned short type,
	unsigned short data, int ceiling, int* out);

/* The override file for the next HDRenderSetWaterSheets: hd_water.txt,
 * beside the executable, read at every level load. None or a missing file
 * leaves the compiled rows alone. The format is in hd_render.cpp, over
 * ParseWaterLine. */
void HDRenderWaterFile(const char* path);

// Whether the square under the middle of that room-local box draws a sheet
// of this group - the classic face there is then not restored.
bool HDRenderWaterSheetAt(int group, int x0, int z0, int x1, int z1);

// Whether the water shaders exist on this device - without them no sheet is
// built and the classic faces are restored as they are without the sheets.
bool HDRenderWaterSheetsAvailable();

// One body, for the log and the diagnostic tool.
struct HDRenderWaterBody
{
	int cls;			// HD_WATER_BASIN .. HD_WATER_SEA, as drawn
	int ruleCls;			// what WaterClassOf gives
	int sectors;			// its water sectors, each place once
	int sheets;			// of those places, the ones drawing a sheet
	int twins;			// sectors of a flipmap twin at those places
	int noSheet;			// 1 when a row of class none applied
	int perimeter;			// square edges with no water behind
	int openAir;			// a room of it is open air
	int surfaceY;			// world y of the surface
	int rooms[HD_WATER_BODY_ROOMS];	// its rooms, smallest first
	int roomCount;			// how many rooms in all
	int depthMax;			// the deepest square, units
	float light[3];			// the light its colours are lit by
	float raw[3];			// that light before gain, floor and cap
	int source;			// 0 the rule, 1 the compiled row, 2 the file
	int sourceRoom;			// the room that row names, or -1
	float tint[3];			// the applied row's numbers
	float waves, foam;
	/* 1 when a row says `art keep`: the remaster's own water art over its
	 * sheet is drawn; 0, the default, hides it (HDRenderSetWaterSheets,
	 * "the art over a sheet"). */
	int artKeep;
	// The applied row's refraction scale, `bend` (1 without one).
	float bend;
	/* 1 when a row says `foot off`: no waterfall of this body gets the
	 * foot HDRenderWaterFalls gives it (a row's own `foot` stays). */
	int footOff;
	/* The current: a unit vector in world x and z and units a second, from
	 * the sinks its squares name (the mean way toward them and the speed
	 * LaraWaterCurrent gives their strength) or from a row's `flow`; 0 0 0
	 * without either. `sinks` counts its places whose water names a sink. */
	float flow[2];
	float speed;
	int sinks;
	/* Sinks of the table that lie in its water (HDRenderSetWaterSheets,
	 * "a sink in the water"): `lying` those counted - the body is long -,
	 * `refused` those that lie in a body too short to be a river. */
	int lying;
	int refused;
};

// What the level built, for its log line.
struct HDRenderWaterLevel
{
	int sectors;		// water sectors, each place once
	int sheets;		// of those places, the ones drawing a sheet
	int twins;		// sectors of a flipmap twin at those places
	int bodies;
	int section;		// HD_WATER_ROME ..
	int built;		// 1 when the sheets' buffers exist
	int fileLines;		// lines of the file, -1 without one
	int fileRows;		// rows the file gave
	int fileBad;		// lines that could not be read
	int rowsApplied;	// rows of this level that found a body
	int rowsUnmatched;	// rows of this level whose room has no water
	int overlaps;		// squares another room's sector holds too
	int causticRooms;	// underwater rooms with a surface above
	int vertices, triangles;
	int copy;		// 1 when the sheets read the frame's copy
	int depth;		// 1 when they read the depth texture as well
	int ringSources;	// ripple rows of the file that found a body
	int artHidden;		// the remaster's water art triangles hidden
	int falls;		// waterfall objects whose foot stands over a sheet
	int feet;		// the sources at their feet and the rows' feet
	/* The squares whose surface is the water room's top and not their own
	 * floor byte (lowered), and those left out because their dry floor
	 * stands more than a click over it at every corner (dropped). */
	int lowered;
	int dropped;
	int oldKeys;		// rows of the file that use the key swirl
	/* The underwater rooms an eye looks through a body from: by the sheet
	 * over them or a room above them, and by the level's largest body for
	 * want of one (hd_render.cpp, "under the water"). */
	int underRooms;
	int underFallback;
	// 1 when the level's water rooms take the deep look (the same note).
	int underDeep;
};

int HDRenderWaterBodies();
bool HDRenderWaterBodyInfo(int at, HDRenderWaterBody* out);
void HDRenderWaterLevelInfo(HDRenderWaterLevel* out);
const char* HDRenderWaterClassName(int cls);
const char* HDRenderWaterSectionName(int section);
/* The WATERLOG line of the level (body -1, `option` the tomb5.hd_water the
 * build ran with) or of one body - the engine's -log and RoomProbe -hdwater
 * print the same text. False for a body that does not exist. */
bool HDRenderWaterLogLine(int body, const char* stem, int option, char* out,
	size_t size);

/* The diagnostic tool's GPU timing of the sheet draws (timestamp queries,
 * waited for - it stalls, so the engine never turns it on): on 1 or 0 resets
 * and switches, -1 only reads the milliseconds and draws so far. */
void HDRenderWaterTiming(int on, double* ms, int* draws);
// And of the frame's copy, the same way: milliseconds and copies so far.
void HDRenderWaterCopyTiming(double* ms, int* copies);
// And of the feet's draws: milliseconds, draws and the segments they held.
void HDRenderWaterFootTiming(double* ms, int* draws, int* segs);
// And of the rivers' draws (RiverPS): milliseconds and draws.
void HDRenderWaterRiverTiming(double* ms, int* draws);

/* The frame behind the sheets (hd_room.hlsl, "the frame behind the sheet").
 *
 * HDRenderWaterCopy copies render target 0 into a texture of its size and
 * format (StretchRect, D3DPOOL_DEFAULT, made on first use and released with
 * the level's buffers), at most once a bracket, and only while this level
 * has built sheets: the blend phase calls it before its first draw, when a
 * room of the frame has a sheet (HDRenderGroupHasSheet). A sheet drawn in a
 * bracket without a copy keeps the look it has without one.
 *
 * HDRenderWaterDepthBind makes the device's depth-stencil the surface of an
 * INTZ texture of the same size - a depth buffer the sheet can read - where
 * the adapter offers that format as a depth texture, keeping the device's
 * own surface to put back; HDRenderWaterDepthUnbind puts it back and
 * releases the texture, and release_buffers calls it, so a lost device and
 * a new level never find it bound. Both paths draw into it unchanged. The
 * engine binds it when a level has built sheets (hd_level.cpp,
 * BuildWaterSheets), RoomProbe for its picture. `why` names a refusal.
 *
 * HDRenderWaterStage2 is RoomProbe's: copy 0 (-nocopy) and depth 0
 * (-nodepth) switch the two off, `view` is -waterdepthview, `bend` scales
 * the refraction for a proof picture (-waterbend, 1 as built). */
bool HDRenderGroupHasSheet(int group);
bool HDRenderWaterCopy();
bool HDRenderWaterDepthOffered();
bool HDRenderWaterDepthBind(const char** why);
void HDRenderWaterDepthUnbind();
bool HDRenderWaterDepthBound();
void HDRenderWaterStage2(int copy, int depth, int view, float bend);

/* The rings on the sheets (hd_render.cpp, "the rings"), out of the game's
 * own ripples. HDRenderWaterRipples takes the live slots of ripples[]
 * (game\tomb4fx.cpp) once a frame, after HDRenderFrameBegin, each with its
 * slot and its fields as UpdateSplashes leaves them; slots it is not given
 * count as empty. HDRenderWaterRing is RoomProbe's -ripple: a ring of that
 * age (seconds) and first size at world x, z in every picture until the
 * next level. HDRenderWaterSheetUnder says whether a drawing sheet lies
 * under world x, z within 256 units of height y - the classic ripple
 * sprite is left out there (hd_level.cpp, HDWaterRippleOnSheet). */
#define HD_WATER_RIPPLE_SLOTS	128	// MAX_RIPPLES, global\types.h

struct HDRenderRipple
{
	int slot;			// its index in ripples[]
	int x, y, z;			// world; y the water's height
	int flags, life, size, init;	// RIPPLE_STRUCT's own
};

// Whether this level built sheets: the frame then feeds the ripples.
bool HDRenderWaterSheetsBuilt();
/* Whether an eye in that room is under the water of a level with sheets:
 * the room is an underwater room that looks through a body (hd_render.cpp,
 * "under the water"). The frame then sets HDFrameLighting::under. */
bool HDRenderWaterUnderOn(int room);
/* The dry room over that underwater room's water: the room most of the
 * sheet squares over it lie in, the same for the rooms walked down under
 * it; -1 for a room no sheet lies over and in a level without sheets. Lara's
 * nodes in that water take its ambient (hd_laradraw.cpp). */
int HDRenderWaterAirRoom(int room);
/* The diagnostic tools' side of it. HDRenderWaterUnderProbe hands in the
 * constants of an eye under the water where no level's water stands behind
 * them (ModelProbe -under): sigma r, g, b and sigma_s, the scattered colour
 * r, g, b, the upward gain and the eye's depth under the surface; a frame
 * with `under` set then takes them with its own world up. Null gives the
 * bodies back; the clear zone round the eye is the engine's either way.
 * HDRenderWaterUnderNow reads the twelve the last frame uploaded (c134 -
 * c136), all zero when its eye was not under the water. */
void HDRenderWaterUnderProbe(const float* nine);
void HDRenderWaterUnderNow(float* twelve);
void HDRenderWaterRipples(const HDRenderRipple* live, int count);
void HDRenderWaterRing(float x, float z, float age, float size);
bool HDRenderWaterSheetUnder(int x, int y, int z);
/* Lara's world x and z for the sheets' waterline, which leaves her out
 * (hd_room.hlsl, gWaterLara): `inWater` 0 while she is out of the water or
 * the level has no Lara. The engine sets it once a frame, RoomProbe for its
 * picture (-lara); a new level starts with 0. */
void HDRenderWaterLara(int inWater, float x, float z);

/* The creatures' mark (hd_render.cpp, "the creatures' mark"): what is
 * drawn between HDRenderWaterMarkBegin and HDRenderWaterMarkEnd marks its
 * pixels in a stencil bit of their own, and the sheets draw no waterline
 * on them. The engine brackets the HD objects' solid draw - Lara and every
 * creature with an HD model - inside the frame's first bracket; both do
 * nothing unless this level built sheets that read the depth texture and
 * the depth surface has a stencil. HDRenderWaterProbeCreature is
 * RoomProbe's -creature: a grey box standing in for a swimming creature at
 * world x, z on the surface of the sheet there, drawn in the frame `offX`,
 * `offZ` (the seed room's origin) shifts the world into, inside the mark
 * bracket - or, `mark` 0, outside it, the picture without the mark.
 * HDRenderWaterMarkView is RoomProbe's -markview: the marked pixels in
 * half-transparent red over the finished frame.
 *
 * HDRenderWaterMarkEnd takes the number of objects drawn in the bracket.
 * HDRenderWaterMarkLine writes, once a level and for the last frame that
 * drew a sheet, one WATERLOG line with -log: `mark=` whether that frame's
 * bracket marked (0/1), `why=` why it did not (0 it did, 1 no device, 2 no
 * sheets built, 3 no depth texture, 4 no depth variant, 5 no stencil),
 * `mark_intz=` and `sheet_intz=` whether the depth-stencil surface set at
 * the mark and at the first sheet draw was the sheets' INTZ surface (0/1),
 * `objects=` the objects drawn in the bracket, `sheets=` the sheet draws
 * and `split=` the ranges drawn twice for the mark. Called before
 * HDRenderWaterMarkBegin, which starts the next frame's count. */
void HDRenderWaterMarkBegin();
void HDRenderWaterMarkEnd(int objects = 0);
bool HDRenderWaterMarkLine(const char* stem, char* out, size_t size);
bool HDRenderWaterProbeCreature(float x, float z, float offX, float offZ,
	int mark);
bool HDRenderWaterMarkView();

/* The waterfalls' feet (hd_render.cpp, "the foot of a waterfall").
 *
 * HDRenderWaterFalls takes the level's waterfall objects after
 * HDRenderSetWaterSheets, each with its classic mesh placed as the game
 * places it: the highest and the lowest y and the x, z of the vertices that
 * lie within HD_WATER_FOOT_BAND of the lowest. Where those stand over a
 * square that draws a sheet the objects close together form one curtain,
 * whose footprint gets a zone of foam pushed away from it and sources of
 * rings and drops along it, and the WATERLOG gets a line per object, per
 * source and per segment of a footprint. The engine calls it from
 * hd_level.cpp with items[], RoomProbe with the classic .TRC.
 * HDRenderWaterFootAdd is RoomProbe's -foot and -swirl: a curtain along the
 * line from world x1, z1 to x2, z2 with that reach on the surface of the
 * square under its middle, until the next level. HDRenderWaterFeet and
 * HDRenderWaterFoot hand the sources to the logic tick's drops
 * (hd_level.cpp, HDWaterFallsTick): the place on the surface, the
 * footprint's direction there and half the stretch the drops scatter
 * over. */
#define HD_WATER_FOOT_BAND	64	// units over the lowest vertex
#define HD_WATER_FOOT_POINTS	256	// foot points kept per object

struct HDWaterFall
{
	int item;			// its number in items[]
	int slot;			// WATERFALL1 ..
	int room;
	float top, bottom;		// world y, the highest and the lowest vertex
	int points;			// foot points in `foot`
	float foot[HD_WATER_FOOT_POINTS * 2];	// world x, z
};

void HDRenderWaterFalls(const HDWaterFall* falls, int count, const char* stem);
void HDRenderWaterFootAdd(float x1, float z1, float x2, float z2,
	float reach);
int HDRenderWaterFeet();
bool HDRenderWaterFoot(int at, float* place, float* dir, float* half);
/* The WATERLOG line of one waterfall object HDRenderWaterFalls was given -
 * false past the last. */
bool HDRenderWaterFallLine(int at, const char* stem, char* out, size_t size);
// And of one source, the objects' and the rows' - false past the last.
bool HDRenderWaterFootLine(int at, const char* stem, char* out, size_t size);
// And of one segment of a curtain's footprint - false past the last.
bool HDRenderWaterSegLine(int at, const char* stem, char* out, size_t size);
/* The rooms' own boxes for the level about to be built.
 * Same contract as the water marks: call it before HDRenderLevelBegin, the
 * pointer is only remembered for the length of that call. */
void HDRenderSetRoomBoxes(const HDRoomBox* boxes, int count);
/* The classic floor squares of the level about to be built - one
 * HDSectorFloor per square of every room and one HDRoomGrid per room, in the
 * order hd_gloss_math.h describes. The sheen reads the .TRG's light cube of
 * the squares a draw call stands over, and needs the grid to find them; the
 * cubes are taken only when the count of squares is the count of cubes the
 * .TRG carries (a level whose rooms were resized has no cubes that match).
 * Same contract again - before HDRenderLevelBegin, the pointers are only
 * remembered for the length of that call. Without it no draw call has a
 * cube or a lamp, and the sheen is the eye's alone. */
void HDRenderSetSectors(const HDRoomGrid* rooms, int roomCount,
	const HDSectorFloor* sectors, int sectorCount);
/* The mirrors the build found (HDMirror above). None without the room boxes,
 * which carry the rooms' origins the rule needs; none in any level but
 * RICH2. Kept until the level is released. */
int HDRenderMirrorCount();
bool HDRenderMirrorInfo(int at, HDMirror* out);
/* The rooms that may be holding another room's art.
 * Same contract again - before HDRenderLevelBegin, the pointer is only
 * remembered for the length of that call. After it, HDRenderIntrusionHits
 * says how many triangles each entry caught, which is what the level's log
 * line is made of. */
void HDRenderSetIntrusions(const HDIntrusion* list, int count);
/* The classic faces the intruding triangles are held against - see
 * HDPairFace. Same contract again. Without them every intruding triangle is
 * one piece per entry that no face claims, drawn inside the window. */
void HDRenderSetPairFaces(const HDPairFace* faces, int count);

/* The classic faces another group's art may stand on - see hd_cover.h.
 *
 * `rooms` is every room of the level, in room order: its origin in world units
 * and its flipmap twin (-1 for none). `holders` are the rooms whose faces are
 * handed in, each with its run of `faces` (world units) and its whole box -
 * the grid with its wall ring, maxceiling to minfloor - grown by
 * HD_COVER_REACH, in world units. The caller hands every room with a classic
 * face that the randomizer did not mark; the build keeps those whose group
 * draws nothing inside the room's own box (no group, no triangle, or every
 * centroid outside the box and its HD_OVERHANG_SLACK - the classic rooms, as
 * far as the .TRG alone can say before the build), and tags every triangle
 * of every other room group that stands on one of their faces
 * (HDCoverOverlaps), its flipmap twin's excepted. Those triangles leave the
 * open and the intruding half and become a range per (group, holder, kind),
 * as do those on no face whose centroid stands in a holder's box -
 * HDRenderGroupStandRange below.
 *
 * Same contract as the lists above - before HDRenderLevelBegin, the pointers
 * are only remembered for the length of that call. Without it nothing is
 * tagged and every group draws as it did. */
struct HDStandRoom
{
	float origin[3];
	int twin;
};

struct HDStandHolder
{
	int room;
	int firstFace;
	int faceCount;
	float mins[3];
	float maxs[3];
};

void HDRenderSetStandFaces(const HDStandRoom* rooms, int roomCount,
	const HDStandHolder* holders, int holderCount, const HDCoverFace* faces,
	int faceCount);

/* What the build tagged. A group's stand ranges, per holder its art stands
 * in: the holder's room, the kind - 0 for triangles that reach onto the
 * holder's faces, 1 for those that lie on them whole (HDCoverTriOnFaces), 2
 * for those on no face whose centroid stands in the holder's box all the
 * same (the relief of a room drawn as the art) - the triangles, all three
 * passes together, and the faces they stand on. `faces` is 0 for kind 2 and
 * wherever the group's own tagged art covers none or all of the holder's
 * faces; where it covers some (HDCoverRoomState's HD_STAND_ROOM_PART) a kind
 * 0 or 1 triangle says whether the faces it stands on are covered ones only
 * (HD_STAND_FACES_COVERED), kept ones only (HD_STAND_FACES_KEPT) or both
 * (HD_STAND_FACES_BOTH) - so up to seven ranges per holder, and exactly the
 * three of a room no group covers in part. A triangle an intrusion piece
 * holds is never kind 2.
 * HDRenderStandTriangleAt hands out every tagged triangle in world units
 * (HDCoverTri::group is its group) with its holder; the list is kept until
 * the level is released. */
struct HDStandRange
{
	int holder;
	int kind;
	int triangles;
	int faces;
};

int HDRenderGroupStandRanges(int group);
bool HDRenderGroupStandRange(int group, int index, HDStandRange* out);
int HDRenderStandTriangleCount();
bool HDRenderStandTriangleAt(int at, HDCoverTri* tri, int* holder);
/* What one group's tagged art covers of one holder's faces, worked out at the
 * build before the ranges are split (HDCoverFaces with HD_COVER_REACH,
 * HD_COVER_STEP and the facing rule, on the triangles the tag gives that
 * holder): `tris` the triangles, `faces` the holder's faces of any area and
 * `facesCovered` those the triangles cover, and `covered` one flag per face
 * handed in for the holder (HDStandHolder::faceCount of them, in its order),
 * 1 for a covered one (HDCoverFaceCovered). One record per (group, holder)
 * with tagged triangles, every group of the file - hd_level.cpp picks the
 * carrier among the HD ones (HDCoverCarrierBetter), so the faces it leaves
 * out are the ones the carrier's ranges were split by. Kept until the level
 * is released. */
struct HDStandCover
{
	int group;
	int holder;
	int tris;
	int faces;
	int facesCovered;
	int faceCount;
	const unsigned char* covered;
};

int HDRenderStandCovers();
bool HDRenderStandCoverAt(int at, HDStandCover* out);
/* The holders the build kept out of those handed in, and of the tagged
 * triangles how many the intrusion list had asked about too (a stand range
 * takes them out of their intrusion piece). For the log. */
int HDRenderStandHolders();
int HDRenderStandFromIntrusions();
/* This frame's answer for one stand range: HD_STAND_WITH draws it with its
 * own group (HD_WINDOW_ALL and HD_WINDOW_OPEN), HD_STAND_BEHIND does the same
 * pushed back in depth (HDStandDraw's `behind`), HD_STAND_OUT leaves it out
 * there. HD_STAND_WITH after the build, so a frame that never answers draws
 * every triangle as a group without stand ranges would. Kept until it is set
 * again, so the glow phase draws the answer the solid phase was given. */
#define HD_STAND_OUT		0
#define HD_STAND_WITH		1
#define HD_STAND_BEHIND		2
/* How far HD_STAND_BEHIND pushes the art back: this fraction of its distance
 * from the eye, so 2 units at 1024 and 8 at 4096 - a fraction rather than a
 * fixed depth offset because the depth buffer's step grows with the square
 * of the distance, and a fixed offset would be hundreds of units far off and
 * nothing close by. Measured with RoomProbe -stand -coincide 16 in seven
 * views of RICH2's rooms 16, 18 and 19: the pixels the art wins of those it
 * shares with the classic faces fall from 23,564 / 40,855 / 6,497 / 15 /
 * 12,500 / 5,465 / 7 unpushed to 1,231 / 602 / 526 / 15 / 29 / 53 / 7 at
 * 1/1024, 1/512 and 1/256 alike - a rest that does not move with the push,
 * art the room's grazing ranges do not hold - while 1/2048 leaves 4,355 in
 * one view of room 19. 1/512 keeps a factor of two in hand for nearer
 * views. */
#define HD_STAND_PUSH		(1.0f / 512.0f)
void HDRenderStandWith(int group, int index, int with);
/* One stand range alone - the triangles of `group` that stand on the faces
 * of its holder - with the group's own matrix and lighting, pushed back in
 * depth as HD_STAND_BEHIND does when `behind` is set; the caller sets the
 * holder's portal window. False when there is nothing to draw. */
bool HDRenderRoomStand(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes, int index,
	bool behind = false);

/* The pieces a group's intruding triangles were split into at the build, one
 * per (entry of the intrusion list, classic face, smaller box) and one per
 * entry for the triangles no face claims. `intrusion` is the entry (the first
 * one whose box holds the triangle, when several do), `face` the index into
 * the list handed to HDRenderSetPairFaces or -1, `inner` 1 when the triangles
 * also stand in the entry's box made one sector smaller in x and z (always 1
 * for a piece no face claims), `triangles` all three passes together. */
struct HDPairRange
{
	int intrusion;
	int holder;
	int face;
	int inner;
	int triangles;
};
int HDRenderGroupPairRanges(int group);
bool HDRenderGroupPairRange(int group, int index, HDPairRange* out);
/* This frame's answer for one piece: nonzero draws it whole, with the open
 * half (HD_WINDOW_OPEN), zero inside the window (HD_WINDOW_INTRUDING). Zero
 * after the build, which is the window for every piece. Kept until it is set
 * again, so the glow phase draws the answer the solid phase was given. */
void HDRenderPairWhole(int group, int index, int whole);
/* The openings the randomizer cut into this level. Same
 * contract again - before HDRenderLevelBegin, the pointer is only remembered
 * for the length of that call. After it, HDRenderCutHits says how many
 * triangles each entry took away, which is what the level's log lines are
 * made of. */
void HDRenderSetCuts(const HDCut* list, int count);
int HDRenderCutHits(int at);
// How many triangles the cuts took out of that group, all three passes
// together. Zero for every group of every vanilla level.
int HDRenderGroupCutTriangles(int group);
/* Sort the inside half of every group apart from its overhang while the next
 * level is built (HD_WINDOW_INSIDE / HD_WINDOW_OVERHANG). 0 by default and in
 * the engine always: it costs draw calls and buys the engine nothing, since
 * the window is a property of the triangle. RoomProbe turns it on for a
 * comparison picture. Call it before HDRenderLevelBegin. */
void HDRenderSplitOverhang(int on);
/* Back-face culling for the opaque room ranges - pass 0 with no alpha test,
 * no water and no sky (hd_render.cpp, RangeCulls). 1 by default and in the
 * engine always; RoomProbe's -cull none sets 0 for the two-sided picture out
 * of the same build. Read per draw call, so it may be called at any time. */
void HDRenderCullOpaque(int on);
// The built level's draw calls, those drawn culled and those drawn two-sided.
void HDRenderCullRanges(int* opaque, int* twoSided);
int HDRenderIntrusionHits(int at);
/* How many of the triangles an entry caught no classic face claims and stand
 * outside the entry's box made one sector smaller in x and z: the art the
 * build hands to the open half, drawn over the whole screen on every frame
 * and never answered per frame. Same terms as HDRenderIntrusionHits. */
int HDRenderIntrusionBand(int at);
// How many of a group's triangles stand inside some OTHER room, all three
// passes together. Non-zero is the whole of "this room needs a portal
// window"; zero is every other room in the game.
int HDRenderGroupIntrudingTriangles(int group);
void HDRenderLevelEnd();

// --- the frame -------------------------------------------------------------
//
// FrameBegin saves every device state the classic path sets or relies on and
// puts the device into the HD path's own state; FrameEnd puts every one of
// them back, bit for bit. Everything between the two is HD. A forgotten state
// is garbage on the screen, so the states are read back from the device and
// written back unchanged rather than listed by judgement.
void HDRenderFrameBegin(const D3DMATRIX* view, const D3DMATRIX* projection,
	const HDFrameLighting* frame = 0);
void HDRenderFrameEnd();

/* Whether a bracket is open right now.
 *
 * HDRenderFrameBegin refuses to nest and silently does nothing when one is
 * already open. The pickup
 * display opens a bracket of its own in the middle of S_OutputPolyList
 * (specific\hd\hd_object.cpp, the immediate draw), so it has to know the
 * answer before it tells DrawInventoryItemMe to leave the classic polygons
 * out: an object that suppresses its own classic draw and then finds no
 * bracket to draw in is drawn by nobody. */
bool HDRenderInFrame();

// The bracket alone, with no room in it. FrameBegin and FrameEnd are these
// two plus the room path's own buffers, shaders and constants; hd_model.cpp
// draws inside the very same bracket, because there is one HD path and one
// list of states to save. A caller that opens the
// bracket itself must close it itself, and must not nest it inside a
// FrameBegin.
void HDRenderStateSave();
void HDRenderStateRestore();

/* The portal window as a scissor rectangle.
 *
 * The classic path draws a room only where its own portal walk says the room
 * can be seen: `r->left / top / right / bottom`, a rectangle in backbuffer
 * pixels, worked out in game\draw.cpp (SetRoomBounds / GetRoomBounds) and
 * handed to specific\drawroom.cpp as clip_left..clip_bottom. Without it, in a
 * level whose rooms overlap in space - ANDREA1's 39 and 53 stand in the same
 * world coordinates - the second room's art comes through the first room's
 * wall. The depth buffer cannot answer that: both rooms really are at that
 * depth.
 *
 * So an HD room draw is scissored to its own window. `left/top` are the
 * first pixel drawn and `right/bottom` the first pixel not drawn, which is
 * Direct3D's own rule for D3DRS_SCISSORTESTENABLE; the caller does the
 * rounding and the margin, because it is the one that knows what the engine's
 * inclusive window meant.
 *
 * HDRenderScissorOff puts the test back off, which is the state the whole HD
 * bracket starts in. The rectangle and the enable are both part of the state
 * bracket - HDRenderStateSave reads them back and HDRenderStateRestore writes
 * them back - so a caller that forgets to switch it off still cannot leave the
 * classic path a device it did not have.
 *
 * HDRenderScissorAvailable is the device cap (D3DPRASTERCAPS_SCISSORTEST).
 * Every card since about 2002 has it; on one that does not, the two calls
 * above do nothing at all and the HD rooms draw unscissored - overlapping art
 * shows through, but the screen is never black. */
bool HDRenderScissorAvailable();
void HDRenderScissor(int left, int top, int right, int bottom);
void HDRenderScissorOff();

/* --- the projected shadow's map ---------------------------------------------
 *
 * A render target texture HD_SHADOW_MAP texels a side in D3DFMT_R32F and a
 * depth surface of its own beside it (hd_shadow.h has the arithmetic,
 * hd_model.cpp the draw). Both live in D3DPOOL_DEFAULT: HDRenderShutdown
 * releases them, which is the path every device loss and every device end
 * takes (HDLevelDeviceLost before a Reset, DXClose before a new device), and
 * HDRenderShadowTargetReady makes them again when they are next asked for.
 *
 * HDRenderShadowTargetReady makes the pair when it does not exist yet and
 * answers whether it exists. A device that refuses the format, the depth
 * format, their match or the creation is not asked again until the device
 * changes; the answer is then false and the blob stays.
 *
 * HDRenderShadowCastBegin, inside an open bracket: remembers the render
 * target, the depth surface, the viewport, the scissor rectangle, the render
 * states the pass sets, the shaders, the declaration, stream 0, the indices,
 * texture 0 and the constants c0 - c166 of the vertex file and c4 of the pixel
 * file; sets the map and its depth surface; clears the colour to 1 (nothing
 * cast) and the depth to 1. False, with nothing changed, when the target does
 * not exist or no bracket is open. HDRenderShadowCastEnd puts every one of
 * them back and releases every reference the Get calls added. The map is
 * never bound as a texture between the two. */
struct IDirect3DTexture9;

enum
{
	HD_SHADOW_TARGET_NONE = 0,	// never asked for on this device
	HD_SHADOW_TARGET_MADE,
	HD_SHADOW_TARGET_REFUSED,
	HD_SHADOW_TARGET_RELEASED
};

/* What the target is, for the log. `reason` is "ok" or what refused it:
 * no_device, format (R32F as a render target texture), depth (neither D24X8
 * nor D16), match (CheckDepthStencilMatch), create (CreateTexture), surface
 * (GetSurfaceLevel), depth_create (CreateDepthStencilSurface). `events`
 * grows by one with every change of state. */
struct HDShadowTargetInfo
{
	int state;
	const char* reason;
	const char* format;
	const char* depth;
	int size;
	int events;
};

bool HDRenderShadowTargetReady();
void HDRenderShadowTargetRelease();
void HDRenderShadowTargetInfo(HDShadowTargetInfo* out);
bool HDRenderShadowCastBegin();
void HDRenderShadowCastEnd();
IDirect3DTexture9* HDRenderShadowMap();

/* The second map, the one the other casters are drawn into: the same kind of
 * pair, HD_SHADOW_OTHERS_MAP texels a side (hd_shadow.h), with a life of its
 * own beside hers.
 *
 * HDRenderShadowOthersReady makes it when it does not exist yet and answers
 * whether it exists; a device that refuses it is not asked again until the
 * device changes, and her map keeps running - only the other casters keep
 * their blobs. HDRenderShadowTargetRelease releases it with hers, so every
 * road that releases hers (a refusal of hers, HDRenderShutdown before a
 * Reset or a new device) releases it too; HDRenderShadowOthersRelease
 * releases it alone. HDRenderShadowOthersInfo is HDRenderShadowTargetInfo's
 * twin. HDRenderShadowOthersCastBegin opens the cast bracket on it exactly
 * as HDRenderShadowCastBegin does on hers - one bracket at a time - and
 * HDRenderShadowCastEnd closes either. HDRenderShadowOthersMap is the
 * texture, null while a bracket is open. */
bool HDRenderShadowOthersReady();
void HDRenderShadowOthersRelease();
void HDRenderShadowOthersInfo(HDShadowTargetInfo* out);
bool HDRenderShadowOthersCastBegin();
IDirect3DTexture9* HDRenderShadowOthersMap();

/* Which of the three passes a call is to draw.
 *
 * The first two write depth and the third does not, and that one fact decides
 * the whole draw order of a frame. Passes 0 and 1 sort themselves out against
 * anything else that writes depth, whatever order they are drawn in; pass 2 -
 * additive, ONE / ONE, D3DRS_ZWRITEENABLE FALSE - leaves the depth buffer as
 * it found it, so any depth-writing draw that comes after it and stands
 * behind it in the world passes the depth test and paints over the glow.
 *
 * Room-major drawing - all three passes of room A, then all three of room B -
 * therefore loses every glow of room A that hangs in front of room B, which
 * on the screen is a lamp cone cut off along the portal's edge. So the frame
 * is drawn pass-major instead: SOLID for every room and every model first,
 * then GLOW for every room and every model, after the classic buckets. See
 * hd_level.cpp, HDFrameDraw and HDFrameDrawGlow. */
#define HD_PASS_OPAQUE		0x1
#define HD_PASS_ALPHA		0x2
#define HD_PASS_ADDITIVE	0x4
/* The see-through half of the alpha pass. The alpha
 * pass holds two different things: the cut-outs, which are solid where they
 * are drawn at all and write depth like any wall, and the glass - material 14
 * - which is translucent and must be drawn after everything opaque, with no
 * depth write, or the wall behind it paints over it. So it is a phase of its
 * own, drawn in the glow phase beside the additive pass. A level with no
 * material 14 in its alpha pass never draws anything here. */
#define HD_PASS_BLEND		0x8
/* The clear floors of the alpha pass: the triangles whose page is part-way
 * clear in every texel (HDPageCacheSlotClear, hd_pageclass.h) - RICH2's
 * marble over the upside-down copy of its room. Blended like the glass, but
 * a phase of its own at the end of the solid one: after every room's opaque
 * art, which is what the copy beneath is, and before the models and the
 * classic buckets, which stand on the floor and must be drawn over it. It
 * writes depth, because it is a floor: everything drawn after it is sorted
 * against it as against any floor - the glass and the glows of the copy
 * beneath stay under it, and a shadow 16 units above it is drawn over it. A
 * level with no such page draws nothing here. */
#define HD_PASS_CLEAR		0x10
#define HD_PASS_SOLID		(HD_PASS_OPAQUE | HD_PASS_ALPHA)
#define HD_PASS_ALL		(HD_PASS_SOLID | HD_PASS_ADDITIVE | HD_PASS_BLEND | \
	HD_PASS_CLEAR)

/* Which half of a group a call is to draw.
 *
 * A group's triangles are sorted into those that stand inside some other
 * room - see HDIntrusion above - and all the rest. The two never share a draw
 * call, because they are drawn
 * with different scissor states: the intruding ones inside the portal window
 * the walk gave this room, and everything else over the whole screen, which
 * is what the classic path does with every polygon that straddles the
 * window's rim.
 *
 * The pieces of the intruding art this frame's HDRenderPairWhole says are
 * drawn whole go with the open half, the rest with the intruding one.
 *
 * HD_WINDOW_ALL is one unscissored call for the whole group; every room but
 * a handful is drawn with it and pays nothing. */
#define HD_WINDOW_ALL		0
#define HD_WINDOW_OPEN		1
#define HD_WINDOW_INTRUDING	2
/* The split into the art inside the room's own box and the overhang outside
 * it, for the diagnostic tool alone (HDRenderSplitOverhang). The engine never
 * asks for these: the overhang is drawn over the whole screen with everything
 * else and has no draw calls of its own - splitting it off costs about ten
 * per cent more draw calls, and scissoring the inside half cuts a black band
 * across ANDREA2's courtyard. */
#define HD_WINDOW_INSIDE	3
#define HD_WINDOW_OVERHANG	4
/* One stand range alone (HDRenderRoomStand). Every other window draws a
 * stand range only while HDRenderStandWith says so, and with the open half -
 * it is never windowed with the group's intruding art. */
#define HD_WINDOW_STAND		5

// One room, the passes the mask names, world matrix as the classic path would
// set it (the room's origin translation). False when the group is missing or
// empty - the caller then draws that room the classic way.
bool HDRenderRoomMask(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes,
	int window = HD_WINDOW_ALL);

// All three passes - HDRenderRoomMask with HD_PASS_ALL.
bool HDRenderRoom(int group, const D3DMATRIX* world, const HDRoomLighting* lighting = 0);

// The same for a single pass; only the diagnostic tool needs it.
bool HDRenderRoomPass(int group, int pass, const D3DMATRIX* world,
	const HDRoomLighting* lighting = 0);

/* --- the projected shadow's receive -----------------------------------------
 *
 * The HD room groups near her drawn a second time, after the map has been
 * cast: RoomVS with the same constants and the same ranges as their opaque
 * and alpha passes and their clear floors, the depth test EQUAL, no depth
 * write, and hd_shadow_receive.hlsl's pixel shader, whose factor multiplies
 * the picture (source ZERO, destination SRCCOLOR, colour write RGB only).
 *
 * HDRenderShadowReceiveReady - the receive shader exists on this device;
 * HDRenderShadowReceive2Ready - its two-map variant does.
 *
 * HDRenderShadowReceiveBegin, inside an open bracket and outside the cast
 * bracket, with the map made: remembers the render states it sets, sampler
 * stage 3 and the pixel shader; sets the room path's vertex shader,
 * declaration, stream and indices, the receive shader with its
 * HD_SHADOW_RECEIVE_COUNT registers from HD_SHADOW_RECEIVE_FIRST on
 * (hd_shadow.h, HDShadowReceiveConstants), the map on stage 3 (point,
 * clamp) and the states above; switches the scissor test off; and where the
 * depth surface has a stencil, clears it and lets each pixel be darkened
 * once. False, with nothing changed, otherwise.
 *
 * HDRenderShadowReceive2Begin, for a frame whose second map was drawn and
 * with the second map made: the same, with the two-map variant in place of
 * the receive shader, the second map on stage 4 (point, clamp; its states
 * and texture remembered too) and HD_SHADOW_RECEIVE2_COUNT further
 * registers from HD_SHADOW_RECEIVE2_FIRST on (hd_shadow.h,
 * HDShadowReceiveOthersConstants). HDRenderRoomShadow and
 * HDRenderShadowReceiveEnd serve both; End puts stage 4 back as well.
 *
 * HDRenderRoomShadow - one group again, in the window HDRenderRoomMask drew
 * it with; the caller sets the scissor that went with it. False when no
 * receive is open or the group is missing or empty.
 *
 * HDRenderShadowReceiveEnd puts back what Begin remembered, and the map
 * leaves stage 3. The vertex constants the receive writes are the ones a
 * room draw writes, and its pixel registers are its own.
 *
 * HDRenderShadowReceiveCalls - draw calls since the last Begin; and
 * HDRenderShadowReceiveStencil - whether that Begin found a stencil. */
bool HDRenderShadowReceiveReady();
bool HDRenderShadowReceive2Ready();
bool HDRenderShadowReceiveBegin(const float* constants);
bool HDRenderShadowReceive2Begin(const float* constants, const float* others);
bool HDRenderRoomShadow(int group, const D3DMATRIX* world, const HDRoomLighting* lighting,
	int window = HD_WINDOW_ALL);
/* HDRenderRoomShadow for one stand range alone, as HDRenderRoomStand drew
 * it: the same matrix, lighting, depth push and range; the caller sets the
 * holder's window. */
bool HDRenderRoomShadowStand(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, int index, bool behind);
void HDRenderShadowReceiveEnd();
int HDRenderShadowReceiveCalls();
bool HDRenderShadowReceiveStencil();

/* The copy behind a mirror's glass, inside an open receive (hd_shadow.h,
 * "the mirror's image"):
 *
 * HDRenderShadowReceiveMirrorReady - the mirror's variant of the receive
 * shader (hd_shadow_receive.hlsl, MirrorReceivePS) exists on this device.
 *
 * HDRenderShadowReceiveMirrorBegin - the variant in place of the receive's
 * shader, with the mirror's two blocks (her HD_SHADOW_RECEIVE_COUNT registers
 * and the second map's HD_SHADOW_RECEIVE2_COUNT, made with HDShadowMirrorView;
 * the second silenced in a frame whose second map was not drawn) and the
 * glass's plane (HDShadowMirrorPlane) in HD_SHADOW_RECEIVE_MIRROR. The next
 * HDRenderRoomShadow calls of the mirror's group then darken its pixels
 * behind the glass alone and mark them in the stencil, so the group's
 * ordinary draw after HDRenderShadowReceiveMirrorEnd leaves them be. False,
 * with nothing changed, without an open receive, without the variant, and
 * without a stencil guard - the copy then takes the ordinary receive.
 *
 * HDRenderShadowReceiveMirrorEnd - the receive's own shader and blocks back;
 * HDRenderShadowReceiveEnd calls it for a draw left open. */
bool HDRenderShadowReceiveMirrorReady();
bool HDRenderShadowReceiveMirrorBegin(const float* hers, const float* others,
	const float* plane);
void HDRenderShadowReceiveMirrorEnd();

/* --- the projected shadow on the HD models -----------------------------------
 *
 * The models' receive bracket, around hd_model.h's receive draw: the
 * entries of the frame list drawn a second time after the models' solid
 * pass, with the model's own vertex shader and constants, the depth test
 * EQUAL, no depth write, and hd_shadow_receive.hlsl's ModelReceivePS, whose
 * factor multiplies the picture (source ZERO, destination SRCCOLOR, colour
 * write RGB only).
 *
 * HDRenderShadowModelsBegin, inside an open bracket and outside every other
 * receive or cast bracket, with `hers` and `others` the maps drawn in this
 * frame (at least one, each made): remembers the receive's render states
 * and the scissor test, sampler stages 3 and 4 with their textures, texture
 * 0, the shaders, the declaration, stream 0, the indices, the vertex
 * registers c0 - c166 and the pixel registers c4 - c7 and c96 - c107; binds
 * her map on stage 3 and the second on stage 4 (point, clamp) - a map not
 * drawn in this frame is bound to nothing; sets the states above and
 * switches the scissor test off; and where the depth surface has a stencil
 * lets each pixel be darkened once, through bit 1 of it (the value 2), which
 * the rooms' receive leaves 0 - a bracket whose rooms' receive cleared
 * nothing clears the stencil here. False, with nothing changed, otherwise.
 *
 * HDRenderShadowModelsBlocks - her block (HD_SHADOW_RECEIVE_COUNT
 * registers) and the second map's (HD_SHADOW_RECEIVE2_COUNT) for the next
 * draws, uploaded from HD_SHADOW_RECEIVE_FIRST on when they differ from the
 * last; a block made to give nothing (hd_shadow.h,
 * HDShadowReceiveWithoutHer, HDShadowReceiveWithoutOthers) for a map the
 * entry does not take.
 *
 * HDRenderShadowModelsEnd puts back everything Begin remembered and
 * releases every reference its Get calls added.
 *
 * HDRenderShadowModelsStencil - whether the last Begin found a stencil. */
bool HDRenderShadowModelsBegin(bool hers, bool others);
void HDRenderShadowModelsBlocks(const float* hers, const float* others);
void HDRenderShadowModelsEnd();
bool HDRenderShadowModelsStencil();

/* The sky dome. How many groups past the last room this
 * level has - 0 for a level with no sky, 1 for every level that has one, 2
 * for TITLE - and the call that draws them all, camera centred, behind
 * everything. See the note in hd_render.cpp for what the group holds and
 * why the sky gets its own slice of the depth buffer. `world` is the matrix
 * the classic path builds for its own horizon mesh: the camera position, no
 * rotation, no scale. False when the level has no sky group or none of them
 * drew. */
int HDRenderSkyGroups();
bool HDRenderSky(const D3DMATRIX* world);

/* --- the statics ------------------------------------------------------------
 *
 * Whether the .TRG draws anything where this static mesh stands. Room local,
 * in classic units, against a 256-unit occupancy of the group's own vertices
 * built while the index buffer is filled. False means the room seam would
 * make the static vanish without putting anything in its place - 21 such
 * statics in ANDREA1 alone - and the caller then draws it the classic way.
 *
 * The destructible pieces: a .TRG part whose material carries bit 15 is
 * one copy of a destructible object, the counter in bits 8 to 14 says which
 * copy, and its triangles are kept in draw calls of their own so that one
 * copy can be switched off. `Info` hands out a piece's room-local box so the
 * caller can pair it with the classic MESH_INFO standing there; `Hide` is
 * what MESH_INFO::Flags bit 0 turns into once Lara has shot it. See the note
 * beside HDDestructible in hd_render.cpp for the measurement. */
bool HDRenderGroupHasGeometry(int group, const float* mins, const float* maxs);

/* How much of the group's art stands in a room-local box.
 *
 * The counting form of the call above, for two questions a yes/no cannot
 * answer:
 *
 *   The statics. "Is there any art in this box" is too weak a reason to take
 *   a static away: ANDREA2's palm has its trunk in one room and its crown in
 *   the room above, and the crown's box catches four cells of 768 - the wall
 *   behind it, not the palm - so the crown would be suppressed with nothing
 *   in its place. The caller asks how much and compares it with the box's
 *   own size.
 *
 *   The overlap list. Whose art stands inside whose room. `interiorOnly`
 *   counts only the cells of triangles that are inside the group's own room
 *   box - the overhang is drawn without a window anyway, so it cannot be the
 *   reason to give one to a room.
 *
 * The box is room local, in the group's own frame. */
int HDRenderGroupCellsIn(int group, const float* mins, const float* maxs,
	int interiorOnly);

/* Which art draws the place of one static mesh (the rule: hd_cover.h, over
 * HD_STATIC_SHARE). The caller hands, per group of the level - group i is
 * room i, as at the build - whether it is drawn HD, the state of the room it
 * is (HD_STAND_ROOM_*, CLASSIC for a room drawn HD), the room's carrier or
 * -1, and the room's origin in x and z; and the static's box and its own
 * mesh's vertices where they stand, both local to its room `room` (x and z
 * from the room's origin, y world). The answer comes back with what it was
 * made of: `own` the cells of the box that hold the room's own group's art
 * and `boxCells` the box's cells; `sure` the vertices the carrier's stand
 * ranges drawn in every frame that lists the room hold, of `verts`; `group`
 * and `groupVerts` the other groups' art at the vertices, the most first (at
 * most HD_STATIC_MAX_GROUPS of them), `holds` which of them hold the static.
 * One function for the engine and for RoomProbe, so the two answer alike. */
struct HDStaticGroup
{
	unsigned char hd;
	unsigned char state;
	short carrier;
	float originX, originZ;
};

struct HDStaticArt
{
	int answer;		// HD_STATIC_ART, HD_STATIC_GROUPS or HD_STATIC_CLASSIC
	int own;
	int boxCells;
	int verts;
	int carrier;	// the room's carrier asked, or -1
	int sure;
	int groupCount;
	int group[HD_STATIC_MAX_GROUPS];
	int groupVerts[HD_STATIC_MAX_GROUPS];
	unsigned char holds[HD_STATIC_MAX_GROUPS];
	int flat;		// HDStaticFlat: no other group asked
	/* For HD_STATIC_GROUPS: the piece made of the first holder's triangles
	 * at the static's place (HDRenderStaticPiece) or -1, that holder (-1
	 * where no piece was asked), its triangles, the static's vertices its own
	 * cells hold, and the
	 * triangles of the runs it reaches into that go on past
	 * HD_STATIC_PIECE_OPEN (hd_cover.h) - a piece is made only with none. */
	int piece;
	int pieceGroup;
	int pieceTris;
	int pieceHeld;
	int pieceOpen;
	// The statics of its room held by the same group with no piece of their
	// own (HDRenderStaticPiecesWhole): its piece is dropped while there are.
	int pieceOthers;
};

/* `place` is the static's entry of the list handed to HDRenderSetStaticPlaces,
 * or -1 for none, which asks no piece. */
void HDRenderStaticArt(int room, const float* mins, const float* maxs,
	const float* verts, int vertCount, const HDStaticGroup* groups,
	int groupCount, int place, HDStaticArt* out);

/* The places of the level's static meshes, for the pieces: every static's box
 * as StaticBox makes it (hd_level.cpp), x and z moved by its room's origin to
 * world units, y as it is (a TR5 room's y origin is 0), and its room. Handed
 * before HDRenderLevelBegin, whose index fill notes the art near each place
 * (hd_cover.h, HD_STATIC_PIECE_REACH), and borrowed until
 * HDRenderStaticPiecesUpload; a place whose box is empty (maxs below mins) is
 * no place. Null or 0 notes nothing and makes no piece. */
struct HDStaticPlace
{
	int room;
	float mins[3];
	float maxs[3];
};

void HDRenderSetStaticPlaces(const HDStaticPlace* places, int count);

/* The answers of a level's statics, `rooms` their rooms: the statics of one
 * room held by one group are drawn as pieces all of them or none, so an
 * object the art models out of several statics - ANDY1's hut in room 225 -
 * is never half art and half classic. A static whose piece goes keeps the
 * classic fallback; `pieceOthers` says why. */
void HDRenderStaticPiecesWhole(const int* rooms, HDStaticArt* arts, int count);

/* After the last HDRenderStaticArt of the level: the pieces' index buffer,
 * and the noted triangles let go. Returns the number of pieces. */
int HDRenderStaticPiecesUpload();

// Whether a piece is there to draw.
bool HDRenderStaticPieceReady(int piece);

/* One piece, drawn as its group draws its own ranges - its vertices, pages,
 * matrix constants and lighting - with the matrix of its group's frame, the
 * passes the mask names; and the same triangles in the projected shadow's
 * receive, inside its Begin and End. */
bool HDRenderStaticPiece(int piece, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes);
bool HDRenderStaticPieceShadow(int piece, const D3DMATRIX* world,
	const HDRoomLighting* lighting);
// How many triangles of the group stand outside its own room box, all three
// passes together. A group whose whole art is outside is
// a group that draws nothing in its own room, and the caller keeps that room
// classic.
int HDRenderGroupOverhangTriangles(int group);

/* Whether the group draws anything in a given horizontal plane. Room local,
 * classic units, `slack` the height tolerance. It is what
 * decides whether the CLASSIC water surface of a room has to be drawn on top
 * of the HD one: measured over the fifteen files, 61 of 93 reflecting rooms
 * have no .TRG art in the plane their classic water lives in - every VCI pool
 * among them - and there the HD room shows the pool floor with nothing over
 * it. See hd_level.cpp, BuildWaterSurfaces. */
bool HDRenderGroupFlatArt(int group, int y, int x0, int z0, int x1, int z1,
	int slack = 32);
/* Whether the group builds the water itself: the same question over the
 * material 16 triangles alone - the remaster's own still water surface. A
 * yes here is final for that face and needs no majority behind it:
 * the classic face and the .TRG's sheet would stand one to twenty units apart
 * and that piece of pool would carry two surfaces. It is not a verdict on the
 * whole room, though - the room keeps every face the sheet does not cover, or
 * a sheet over two sectors would empty a pool of twenty-nine. See
 * BuildWaterSurfaces. */
bool HDRenderGroupWaterArt(int group, int y, int x0, int z0, int x1, int z1,
	int slack = 32);
// How many distinct horizontal planes that group's art covers - for the log.
int HDRenderGroupFlatPlanes(int group);
// Whether the group has a clear floor to draw in HD_PASS_CLEAR - false for
// every group of a level without a part-way clear page, so the frame asks
// no room of such a level for the phase.
bool HDRenderGroupHasClear(int group);
// How many of the group's draw calls take the sheen (ResolveSheen).
int HDRenderGroupSheenRanges(int group);
int HDRenderDestructibleCount();
bool HDRenderDestructibleInfo(int at, int* group, unsigned int* material,
	float* mins, float* maxs);
void HDRenderDestructibleHide(int at, bool hidden);

int HDRenderGroupCount();
int HDRenderGroupTriangles(int group, int pass);	// pass < 0 = all three
int HDRenderGroupRanges(int group, int pass);

// A handful of the group's own vertex positions, room local, spread evenly
// over its index run - the numbers the world-matrix proof is run on. The
// .TRG itself is freed at the end of HDRenderLevelBegin and the vertex buffer
// is write-only, so these are kept aside while the buffer is filled: at most
// HD_RENDER_SAMPLES per group, which is 192 KiB over a whole level.
// Returns how many vertices were written (3 shorts each).
#define HD_RENDER_SAMPLES 128
int HDRenderGroupSampleCount(int group);
int HDRenderGroupSamples(int group, short* xyz, int maxVerts);
// The same samples with their normals, their baked light and their water
// marks - what the parity proof needs to run both formulas on real data.
int HDRenderGroupSampleData(int group, HDRenderSample* out, int maxVerts);
// The room-local bounding box of a group, from the vertices its own indices
// name. False when the group draws nothing.
bool HDRenderGroupBounds(int group, float* mins, float* maxs);

// What moves in one room. False for a group that is not
// there; a group that draws nothing answers with all zeroes.
bool HDRenderGroupMotionInfo(int group, HDRenderGroupMotion* out);
int HDRenderDrawCalls();			// since the last FrameBegin
// Of those, the opaque draw calls that ran with the alpha test on
// because their .TRG material is a cut-out one (leaves, gratings, nets).
int HDRenderCutoutCalls();
// Of those, the draw calls that picked their page out of a material 13
// flipbook rather than out of their own vertices.
int HDRenderFlipbookCalls();
// And the draw calls that turned a material 15 book, whatever page they
// showed - since the last FrameBegin, so one bracket's worth. The frame line
// (HDLOG frame book_calls) adds the solid bracket's opaque books and the glow
// bracket's additive ones.
int HDRenderBookCalls();
// And how many of them carried a material's gloss (materials 3 and 17).
int HDRenderGlossCalls();
// And how many were drawn on the gloss variant, with the sheen ResolveSheen
// gave the draw call - zero while tomb5.hd_reflect is off.
int HDRenderSheenCalls();

/* Which surfaces shine (hd_gloss_math.h, the four sources).
 *
 * HDRenderGlossFile names the override file for the next HDRenderLevelBegin -
 * hd_gloss.txt, beside the executable in the game's working directory, is
 * what hd_level.cpp hands in - and a null or empty path reads none. The file
 * is read at every level build, so an edit shows at the next level load.
 *
 * After a build, HDRenderGlossLevelInfo says what the build made of it:
 * the table's lines, the file's pages (-1 when there was no file) and its
 * bad lines, the parts each source named (a part counts under every source
 * that names one of its pages), the lamps that went into the light field and
 * the light cubes the .TRG holds for the floor squares (0 where their count
 * is not the squares'), the pages that shine and the draw calls that take
 * the gloss variant; the light field (hd_gloss_field.h) - its cells along x,
 * y and z (all 0 when the level has none), the cells a lamp reaches, the
 * weak ones, the bytes of its two textures and the build's time in
 * milliseconds; and the draw calls that keep the plain variant although
 * their page names a gloss, per reason (HDRenderSheenSkipped). One
 * HDRenderGlossPage per page and source that shines, with its triangles. */

/* The draw calls of a level that keep the plain variant, per reason: the
 * water surfaces (the ripple) and the sky band (flat light) whose page names
 * a gloss, and every clear floor's, which keeps it by its class. The
 * caustic, the scroll, the flipbook and the book are carried by the gloss
 * variant and keep no draw call plain; their counts stay 0. */
struct HDRenderSheenSkipped
{
	int ripple;
	int caustic;
	int flat;
	int scroll;
	int flipbook;
	int book;
	int clear;
};

struct HDRenderGlossLevel
{
	int tableLines;
	int filePages;
	int fileBad;
	int partsMat;
	int partsSeed;
	int partsTable;
	int partsFile;
	int lamps;
	int cubes;
	int pages;
	int draws;
	int fieldDims[3];
	int fieldLit;
	int fieldWeak;
	int fieldBytes;
	float fieldMs;
	HDRenderSheenSkipped skipped;
};

struct HDRenderGlossPage
{
	int page;
	int cls;		// HD_GLOSS_*
	int source;		// HD_GLOSS_FROM_*
	float strength;
	float power;
	float f0;
	int tris;
};

void HDRenderGlossFile(const char* path);
/* Every draw call that can take the sheen at all takes it at strength 1 and
 * F0 1, with its own exponent where its page names one and metal's where it
 * does not - the upper bound of the sheen, for RoomProbe's -glossmax. 0 by
 * default and in the engine always. Read at the next build. */
void HDRenderGlossOverride(int max);
/* RoomProbe's -glossfield picture: every draw call on the gloss variant
 * draws the light field in place of its surface - 1 its colour times its
 * one-sidedness, 2 its direction biased into a colour - and 0 the surface,
 * which is what the engine always draws. Takes effect at the next frame. */
void HDRenderGlossFieldView(int mode);
/* RoomProbe's -fieldsight: the next build's light field takes the sight test
 * by the rooms (hd_gloss_field.h, HDGlossFieldSight) - a lamp lights a cell
 * only where a room holding the cell is `nearby` the lamp's room, `nearby` a
 * rooms x rooms table as that struct reads it. Null, which is what the engine
 * always uses, builds the field without the test. */
void HDRenderGlossFieldSight(const unsigned char* nearby, int rooms);
void HDRenderGlossLevelInfo(HDRenderGlossLevel* out);
int HDRenderGlossPageCount();
bool HDRenderGlossPageInfo(int at, HDRenderGlossPage* out);
// And how many drew a clear floor (HD_PASS_CLEAR) - since the last
// FrameBegin; the solid bracket is the one that draws them.
int HDRenderClearCalls();
// And how many drew a still water surface - a material 16 part with a
// ripple on it.
int HDRenderWaterCalls();

// The bytes one room vertex takes in the vertex buffer. Named here because the
// texture budget has to price a level's buffers before they are made.
int HDRenderVertexBytes();

// --- the projection --------------------------------------------------------

// The engine's own SetupZRange (specific\3dmath.cpp), as a function without
// the engine. mznear / mzfar are the values in the float pipeline's units -
// 20 and 20480 for TR5, which is what InitWindow hands SetupZRange before the
// W2V shift is taken off again.
void HDRenderZConstants(double mznear, double mzfar, double* a, double* b, double* boo);

// The matrix whose depth after the w division is exactly a - b / z_view.
// persp is f_persp, centerX / centerY are f_centerx / f_centery, and a / b are
// the engine's f_a / f_b as they stand AFTER SetupZRange (f_b already
// negated).
void HDRenderMakeProjection(double persp, double centerX, double centerY,
	double a, double b, D3DMATRIX* out);

// Fills up to `rows` samples between mznear and mzfar and returns the worst
// absolute difference between the classic depth and the matrix depth. This is
// the numerical proof that the two paths write the same depth; anything above
// about 1e-5 means the shared depth buffer is a fiction and the HD path needs
// its own answer.
//
// persp is f_persp. It cancels out algebraically - the classic path divides
// by it and multiplies by it again - but it does so in single precision and
// in two steps, so leaving it out of the test would test a formula instead of
// the code.
double HDRenderSelfTest(double mznear, double mzfar, double persp,
	HDRenderZSample* rows, int rowCount);

// What the device actually compares depth with, read back from it rather than
// assumed: ZENABLE (1 = D3DZB_TRUE, 2 = D3DZB_USEW), ZFUNC, ZWRITEENABLE, and
// the viewport's MinZ / MaxZ. Every one of them changes the meaning of the
// numbers HDRenderSelfTest compares, so the proof names them.
void HDRenderDepthTruth(IDirect3DDevice9* device, unsigned long* zenable,
	unsigned long* zfunc, unsigned long* zwrite, float* minZ, float* maxZ);

// How many instruction slots the card allows a ps_3_0 shader
// (D3DCAPS9::MaxPixelShader30InstructionSlots). The room shader is 476 of them
// and shader model 3 guarantees 512, but a card that reports less would refuse
// CreatePixelShader with no useful message, so the number is read and logged
// rather than assumed.
unsigned long HDRenderPixelSlots();

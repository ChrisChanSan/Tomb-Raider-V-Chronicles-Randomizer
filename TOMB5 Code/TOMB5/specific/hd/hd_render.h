/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp, hd_load 1919-2256 and hd_draw_room
 * 2259-2312). The renderer under it is new: TROne is OpenGL 3.3 with vertex
 * arrays, texture arrays and runtime GLSL, this is Direct3D 9 with a vertex
 * declaration, one texture per page and compiled bytecode, beside a
 * fixed-function neighbour that must find its state exactly as it left it.
 * See THIRD-PARTY.md.
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
 * carried to the HD vertices by position - the same road TROne takes for its
 * own water flag (game_renderer.cpp:2127-2188, a per room hash of packed
 * int16 x/y/z).
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
	unsigned char nx, ny, nz;	// biased by 127, as on disk
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

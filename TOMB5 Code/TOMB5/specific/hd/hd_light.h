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

#include "hd_render.h"

// The parity block.
//
// Two implementations of the same room vertex colour, on the processor,
// beside each other:
//
//   HDLightClassic  a transcription of specific\drawroom.cpp,
//                   aRoomletTransformLight - longs, truncations, statement
//                   order and all. This is what the classic path paints.
//   HDLightShader   a transcription of specific\hd\hd_shader\hd_room.hlsl -
//                   floats, the 127.5 bridge, the same order. This is what
//                   the card paints in an HD room.
//
// Feed both the same vertex with light = 2 * prelight / 255 and the two have
// to agree about every CONTRIBUTION: the remaster's baked light covers the
// same 0..2 the classic overbright covers, so the two scales meet and each
// term lands in the same units.
//
// They part company in exactly one place, on purpose. Above a colour of 128
// the classic path stops multiplying the texture and starts ADDING, because a
// fixed function pipeline has one diffuse byte and one specular byte to fit a
// brightness of 2 into. A shader has floats and keeps multiplying, which is
// what the HD render path already draws and what the remaster's own art
// expects. So the result carries BOTH numbers:
//
//   HDLightResult::f          the contribution sum in the classic path's own
//                             0..255, before its split and before the bulbs.
//                             THIS is what has to agree - budget 2 of 255.
//   HDLightResult::diffuse    what finally multiplies the texel, and
//   HDLightResult::specular   what is finally added to it. These agree below
//                             128 and diverge above it, by design, and the
//                             difference is reported so the deviation is a
//                             measured number and not a claim.
//
// Nothing in this file touches Direct3D or a single engine global, on purpose:
// the diagnostic tool RoomProbe links it as it stands, and so does the engine.

struct HDParityVertex
{
	float x, y, z;			// room local, classic units
	float nx, ny, nz;		// unit normal, as the roomlet keeps it
	unsigned long prelight;		// the classic baked vertex colour
	unsigned char bits;		// the water marks, HDWaterMark::bits
};

// Everything both implementations need that is not per vertex. `view` is the
// engine's D3DMView for this room - view * world, row major, 16 floats.
struct HDParityScene
{
	float view[16];
	float originX, originY, originZ;
	float surfaceY;
	int underwater;
	int choppy;
	int surfaceGate;
	int redAlert;
	float sel1[4];
	const void* waterTable;		// 64 WATERTAB records
	float wibble;
	float distanceFogStart;		// tomb5.distance_fog * 1024
	const HDLightSource* lights;
	int lightCount;
	const HDFogBulb* bulbs;
	int bulbCount;
};

struct HDLightResult
{
	float f[3];		// the contribution sum, classic 0..255, before the split
	float bulbDensity;	// the summed fog bulb intensity, before it is applied
	float bulbColour[3];
	float diffuse[3];	// what multiplies the texel, 0..2
	float specular[3];	// what is added to it, 0..1
};

void HDLightClassic(const HDParityScene* scene, const HDParityVertex* v,
	HDLightResult* out);
void HDLightShader(const HDParityScene* scene, const HDParityVertex* v,
	HDLightResult* out);

// The worst absolute difference over `count` vertices. worstF is in the
// classic path's 0..255 and is the parity that matters; worstFinal is in 0..1
// on a white texel and carries the deliberate deviation above 128. Either may
// be null. Returns the vertex count used.
int HDLightParity(const HDParityScene* scene, const HDParityVertex* verts, int count,
	double worstF[3], double worstFinal[3]);

// A stand-in for the engine's WaterTable, for a tool that has no engine.
// `dst` takes 22 * 64 records of four bytes, the same layout WATERTAB has.
//
// NOT byte identical with the engine's: init_water_table (gamemain.cpp)
// draws its random column out of the C runtime's rand() in a rejection loop,
// and reproducing that would be reproducing a particular CRT. The shape is
// the same - the same sine, the same four shimmer / choppy / abs ladders - and
// that is all a picture or a parity run needs, because both sides of a parity
// run read the same table. The ENGINE never uses this function: it hands its
// own row over (hd_level.cpp).
void HDLightBuildWaterTable(void* dst);

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

/* The water seen from below, the arithmetic without a device
 * (hd_render.cpp, "under the water"): the eye's depth under the surface the
 * extinction measures its path against, and the window of the sky a flat
 * surface lets through. ModernTest checks both (water_under).
 *
 * HDWaterUnderDepth: `fallback` is 1 for a water room no sheet lies over
 * (its body is the level's largest), `deep` 1 in a level that takes the
 * deep look, `eyeY` and `surface` world y (down), `none` the depth that
 * puts every point in the water (kUnderNoSurface). A fallback room has no
 * surface over it - except under the deep look, whose water is one sea:
 * there the body's surface counts wherever it lies above the eye. An eye
 * over it keeps the whole way: the distance down to a surface under the eye
 * would give less extinction the farther the eye stands from the sea's own
 * surface over it (in JOBY4 the basin of room 7 stands for that one, drawn
 * by room 19 16,000 units higher). Anywhere else the depth is the eye's
 * under the body's surface. */
inline float HDWaterUnderDepth(int fallback, int deep, float eyeY,
	float surface, float none)
{
	const bool sea = fallback && deep && eyeY > surface;

	return fallback && !sea ? none : eyeY - surface;
}

/* The cosine of the half angle of the window a flat surface opens over the
 * eye (Snell's window): rays from the air bend toward the normal by the
 * ratio of the two refractive indices, so all the sky above the surface
 * comes in within asin(1 / n) of straight up and outside it the surface
 * turns the water's own light back. n = 1.333 for water: 48.6 degrees. */
inline float HDWaterSnellCos(float n)
{
	const float s = n > 1.0f ? 1.0f / n : 1.0f;

	return sqrtf(1.0f - s * s);
}

/* Whether the classic flat layers stay out under an HD sky (SkyDrawPhase,
 * hd_render.cpp HDRenderSkyCoversLayers): where the HD sky stands for them
 * - a cloud sheet in it, or a band in the opaque pass (TITLE) - and in a
 * level that takes the deep look with the sheets on, whatever its sky.
 * The deep look's water is one sea seen from below, and its script's
 * layers are that sea's surface on the level's own classic page (JOBY4:
 * the PSX sky's dark teal SKY_GRAPHICS tiles, or the bright surface page
 * with the PSX sky off); the surface over it is the remaster's art of the
 * sea and the look of HDRenderWaterLook, which fills the band's open
 * poles. Without the sheets (tomb5.hd_water 0) no look is drawn, and the
 * layers stay to cover the poles. `sheet`, `opaqueBand` and `deep` are 0
 * or 1; `deep` is 1 only for the deep look with the sheets on. */
inline bool HDSkyLayersLeftOut(int sheet, int opaqueBand, int deep)
{
	return sheet != 0 || opaqueBand != 0 || deep != 0;
}

/* The sea seen from below on the game's path (HDRenderWaterLook): the
 * depth of its surface over the eye. The surface is the water's top over
 * the dive - the highest ceiling of a square of any of the level's water
 * rooms (`top`, world y, down; JOBY4 -5,120, the deep look's own top), 867
 * units over the remaster's art of the sea's surface (room 19, y -4,253)
 * and so a flat stand-in for that art seen from below. At least one unit,
 * so that the eye ray always meets it above the eye. */
inline float HDWaterLookDepth(float eyeY, float top)
{
	const float d = eyeY - top;

	return d > 1.0f ? d : 1.0f;
}

/* How far under the water's top the art of the sea's surface may reach
 * highest and still be that surface (HDWaterSurfaceRoom), in units: a free
 * choice of two squares, between the 851 of JOBY4's sea and the 3,840 of
 * the next water room's art there, and short of the 4,864 of RICH3's one
 * water room, whose art is its own floor. */
static const float kHDWaterSurfaceReach = 2048.0f;

/* The room whose art is the sea's surface over the dive, in a level that
 * takes the deep look. The remaster lays that surface as one sheet over the
 * whole dive in the art of a single room, far over the room's own squares
 * and out of reach of the portals of the rooms the dive is swum in: JOBY4's
 * room 19 is a room of the sea floor (squares at y 15,872 to 17,152) whose
 * art reaches y -4,269 and spans the level. So the room is found by its
 * art: of the `count` rooms, the water room (`wet` 1) whose art (`hasArt`
 * 1) reaches highest, `artTop` the world y of its highest point (y counts
 * down), when that point lies at most `reach` units under the water's top
 * - the highest ceiling of a square of any water room, `ceilTop` each
 * room's own highest (1.0e9 for a room without a square), the top the deep
 * look measures its surface from. A tie goes to the lower number; -1 where
 * no water room's art reaches that high. */
inline int HDWaterSurfaceRoom(const int* wet, const float* ceilTop,
	const int* hasArt, const float* artTop, int count, float reach)
{
	float top = 1.0e9f;
	int best = -1;

	for (int r = 0; r < count; r++)
		if (wet[r] && ceilTop[r] < top)
			top = ceilTop[r];

	for (int r = 0; r < count; r++)
		if (wet[r] && hasArt[r] && artTop[r] <= top + reach &&
			(best < 0 || artTop[r] < artTop[best]))
			best = r;

	return best;
}

/* The look's eleven numbers on the game's path, in the order of
 * RoomProbe's -underlookknobs: the disc's, the shafts' and the underside's
 * strengths, the surface light's reach along the eye ray in units, the
 * disc's power, the shafts' cell size in units, the noise's floor (the
 * share of the cells that pass no shaft), the shafts' reach under the
 * surface and the march's length in units, the way in units over which
 * the waves' slope is kept whole and the window rim's width in cosine
 * (hd_water_look.hlsl, gLookMore; 0 for either: whole, and 0.08). */
enum
{
	HD_LOOK_DISC, HD_LOOK_SHAFTS, HD_LOOK_UNDERSIDE, HD_LOOK_REACH,
	HD_LOOK_POWER, HD_LOOK_CELL, HD_LOOK_FLOOR, HD_LOOK_DEEP, HD_LOOK_LENGTH,
	HD_LOOK_WAVES, HD_LOOK_RIM, HD_LOOK_KNOBS
};

/* Free choices, set by eye on RoomProbe -underlook pictures of JOBY4's
 * dive (rooms 33, 123 and 6, looking 46, 43 and 19 degrees up, and room
 * 123 turned 50 up) for the look of that sea from below: a soft bright
 * surface overhead, faint shafts fanning down into dark water. The eyes
 * there stand 18,800 to 20,200 units under the surface (y 13,667 to
 * 15,044), so the shafts reach 40,000 units down (0.63 of their light left
 * at the shallowest eye, where 6,000 left 0.04); the water's sigma_s
 * (0.000444 a unit) leaves a twentieth of the light 6,700 units along the
 * ray, so the march stops at 5,000 and its 24 steps fall 208 units apart;
 * cells of 1,600 units, of which 62 per cent pass no shaft, give a few
 * broad soft columns where 700 and 0.55 gave an even haze. The remaster's
 * surface art and the band alone are already brighter overhead than dark
 * water, so the look adds little: the disc faint (0.2), the shafts at 8
 * and the underside a glow of 0.12, which add 2 to 9 of 255 to the mean
 * luma at those eyes. The waves' slope is kept whole over 1,500 units of
 * the way and the rim spread over 0.3 of cosine, inside the window's own
 * 0.339 (from its critical cosine 0.661 to 1), so that 19,000 units up the
 * waves draw no noise or dark streaks across the lit window and its middle
 * still takes the whole light. */
inline void HDWaterLookKnobs(float* knobs)
{
	knobs[HD_LOOK_DISC] = 0.2f;
	knobs[HD_LOOK_SHAFTS] = 8.0f;
	knobs[HD_LOOK_UNDERSIDE] = 0.12f;
	knobs[HD_LOOK_REACH] = 40000.0f;
	knobs[HD_LOOK_POWER] = 600.0f;
	knobs[HD_LOOK_CELL] = 1600.0f;
	knobs[HD_LOOK_FLOOR] = 0.62f;
	knobs[HD_LOOK_DEEP] = 40000.0f;
	knobs[HD_LOOK_LENGTH] = 5000.0f;
	knobs[HD_LOOK_WAVES] = 1500.0f;
	knobs[HD_LOOK_RIM] = 0.3f;
}

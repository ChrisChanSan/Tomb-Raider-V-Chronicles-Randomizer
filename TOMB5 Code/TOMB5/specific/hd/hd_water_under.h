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

/* Whether a level takes the deep look (hd_render.cpp, "the deep look"): its
 * water rooms no sheet lies over - `fallback`, the rooms that take the
 * level's largest body - outnumber the rooms under a sheet (`sheeted`) more
 * than `share` times, and the water has a sea's surface over the dive
 * (`surface`, HDWaterSurfaceRoom's answer, -1 for none). A dive looks up to
 * a surface; a flooded room's water stands under its own ceiling. JOBY4:
 * 132 against 2 and room 19; RICH3: 1 against 0 and none. */
inline bool HDWaterDeepLook(int fallback, int sheeted, int share,
	int surface)
{
	return fallback > share * sheeted && surface >= 0;
}

/* The wet floor over a polished floor (hd_render.cpp, "the polished floor
 * under the water"), the arithmetic without a device; ModernTest checks
 * both (wet_polish).
 *
 * HDWetCellOnFace: whether a triangle - its corners' x in `ax` and z in
 * `az` - overlaps the square cell x0 .. x0 + size, z0 .. z0 + size held
 * `inset` in from every side, so a triangle that only meets the cell's edge
 * does not count: separating axes over the cell's two axes and the
 * triangle's three edges. A triangle with no area overlaps nothing.
 *
 * HDWetGivesWay: whether a wet point on a polished floor at height `y`
 * gives its own mirror to the marble floors' this frame: one of the
 * frame's first `drawn` mirrored planes (`planeY`, at most `count` of
 * them) lies within `reach` units of it. */
inline bool HDWetCellOnFace(const float* ax, const float* az, float x0,
	float z0, float size, float inset)
{
	const float bx0 = x0 + inset, bx1 = x0 + size - inset;
	const float bz0 = z0 + inset, bz1 = z0 + size - inset;

	if ((ax[0] < bx0 && ax[1] < bx0 && ax[2] < bx0) ||
		(ax[0] > bx1 && ax[1] > bx1 && ax[2] > bx1) ||
		(az[0] < bz0 && az[1] < bz0 && az[2] < bz0) ||
		(az[0] > bz1 && az[1] > bz1 && az[2] > bz1))
		return false;

	for (int e = 0; e < 3; e++)
	{
		const int n = (e + 1) % 3, o = (e + 2) % 3;
		const float nx = az[n] - az[e], nz = ax[e] - ax[n];
		const float side = nx * (ax[o] - ax[e]) + nz * (az[o] - az[e]);
		float most = -1.0e30f;

		if (side == 0.0f)
			return false;

		// The cell's corner reaching furthest to the triangle's side.
		for (int c = 0; c < 4; c++)
		{
			const float cx = (c & 1) ? bx1 : bx0;
			const float cz = (c & 2) ? bz1 : bz0;
			const float d = (side < 0.0f ? -1.0f : 1.0f) *
				(nx * (cx - ax[e]) + nz * (cz - az[e]));

			most = d > most ? d : most;
		}

		if (most < 0.0f)
			return false;
	}

	return true;
}

inline bool HDWetGivesWay(int y, const int* planeY, int drawn, int count,
	int reach)
{
	for (int k = 0; k < drawn && k < count; k++)
	{
		const int d = planeY[k] - y;

		if (d <= reach && d >= -reach)
			return true;
	}

	return false;
}

/* The look's twenty-five numbers on the game's path, in the order of
 * RoomProbe's -underlookknobs: the disc's, the shafts' and the underside's
 * strengths, the surface light's reach along the eye ray in units, the
 * disc's power, the shafts' cell size in units, the noise's floor (the
 * share of the cells that pass no shaft), the shafts' reach under the
 * surface and the march's length in units, the way in units over which
 * the waves' slope is kept whole and the window rim's width in cosine
 * (hd_water_look.hlsl, gLookMore; 0 for either: whole, and 0.08); then the
 * seven of the sea's sheet in the art's place (HDWaterSeaOver): the
 * disc's, the shafts' and the underside's strengths there, the sheet's
 * own, its ripple's contrast, the share the shafts' noise lights it by and
 * the weight the art keeps; then the disc's power there (0: taken
 * from the light's height, HDWaterGlowPowerUnder), and the largest turn
 * in radians and the largest share of height by which each of the shafts'
 * cells breaks the sheet's ripple (hd_water_look.hlsl, SeaRipple); then
 * the sheet's reach and fade: the share of the margin past the level's
 * water it reaches (HDWaterSheetMargin; 0: the art's box alone), the way
 * in units over which its ragged edge comes in, the share of the
 * extinction's fog along it (HDWaterSheetFog; 0: none), and whether the
 * extinction counts the way up to the sheet's plane where no sheet of the
 * level lies over the eye's room and the eye is under the stand-in's
 * surface (1) or to the stand-in's surface (0; HDWaterUnderSeaDepth);
 * last, the most the shafts' light under the sheet may reach in a pixel,
 * in times their mean light over the same way (HDWaterShaftBound; 0: no
 * bound). */
enum
{
	HD_LOOK_DISC, HD_LOOK_SHAFTS, HD_LOOK_UNDERSIDE, HD_LOOK_REACH,
	HD_LOOK_POWER, HD_LOOK_CELL, HD_LOOK_FLOOR, HD_LOOK_DEEP, HD_LOOK_LENGTH,
	HD_LOOK_WAVES, HD_LOOK_RIM, HD_LOOK_SEA_DISC, HD_LOOK_SEA_SHAFTS,
	HD_LOOK_SEA_UNDERSIDE, HD_LOOK_SHEET, HD_LOOK_RIPPLE, HD_LOOK_PATCHES,
	HD_LOOK_ART, HD_LOOK_SEA_POWER, HD_LOOK_SEA_TURN, HD_LOOK_SEA_SWELL,
	HD_LOOK_SEA_MARGIN, HD_LOOK_SEA_RAGGED, HD_LOOK_SEA_FOG,
	HD_LOOK_SEA_PLANE, HD_LOOK_SEA_PEAK, HD_LOOK_KNOBS
};

/* The shafts at the eye's depth (hd_water_look.hlsl, LookPS). The march
 * follows the light's way back up from each of its samples to the surface,
 * and a value noise of the cells' hash there says whether a shaft passes
 * (LookShaft). Every sample of the march lies about as deep as the eye, so
 * its way back up ends near one point of the surface, the eye's depth
 * along the light from the eye: the march's 5,000 units of way move that
 * end by a few cells at most, and the whole picture's shafts are those few
 * cells' hash - all lit, up to 1 over the cells' mean (18.6 times it at a
 * floor of 0.62), or all dark, as the cells drift with the clock. Lit, the
 * shafts at the sheet's strength burn the light toward the sun to white.
 * So the shafts' light in a pixel is held to a number of times the light
 * the cells' mean gives over the same samples: the sunlit photograph's
 * brightest over its open water's median, 2.49, taken as the most the
 * term may stand over its own typical. It holds the term, not the
 * picture, whose own peak over its median is another number. A dark or a
 * partly lit patch keeps its own light and its pattern, so the picture
 * still turns between its lit and its dark state as the cells drift.
 *
 * HDWaterShaftHash: the cells' hash, hd_room.hlsl's CellHash (its first
 * number) for one cell, 0 to 1.
 *
 * HDWaterShaftMean: the mean of LookShaft's light over the hash's cells -
 * `cells` by `cells` of them from cell 0, `places` by `places` points in
 * each, the smoothstep blend of the four corners' hash, above `floor`
 * only, squared. 0 for no cells or a floor of 1 or more.
 *
 * HDWaterShaftBound: the march's sum `sum` held to `peak` times the cells'
 * mean light `mean` times the sum of the samples' own weights `weights`
 * (the reach under the surface and the extinction along the eye ray); the
 * sum as it is for a peak of 0 or less. */
inline float HDWaterShaftHash(float i, float j)
{
	const float a0 = i * (422.0f / 4096.0f) + j * (1531.0f / 4096.0f);
	const float b0 = j * (481.0f / 4096.0f) + i * (965.0f / 4096.0f);
	const float a = a0 - floorf(a0);
	const float b = b0 - floorf(b0);
	const float h = a * (b + 3.71f) * 7.13f + b;

	return h - floorf(h);
}

inline float HDWaterShaftMean(float floor, int cells, int places)
{
	if (cells <= 0 || places <= 0 || floor >= 1.0f)
		return 0.0f;

	double sum = 0.0;

	for (int i = 0; i < cells; i++)
		for (int j = 0; j < cells; j++)
		{
			const float h00 = HDWaterShaftHash((float)i, (float)j);
			const float h10 = HDWaterShaftHash((float)i + 1.0f, (float)j);
			const float h01 = HDWaterShaftHash((float)i, (float)j + 1.0f);
			const float h11 = HDWaterShaftHash((float)i + 1.0f,
				(float)j + 1.0f);

			for (int p = 0; p < places; p++)
				for (int q = 0; q < places; q++)
				{
					const float fx = (p + 0.5f) / places;
					const float fz = (q + 0.5f) / places;
					const float sx = fx * fx * (3.0f - 2.0f * fx);
					const float sz = fz * fz * (3.0f - 2.0f * fz);
					const float v = (h00 + (h10 - h00) * sx) * (1.0f - sz) +
						(h01 + (h11 - h01) * sx) * sz;
					float a = (v - floor) / (1.0f - floor);

					a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
					sum += a * a;
				}
		}

	return (float)(sum / ((double)cells * cells * places * places));
}

inline float HDWaterShaftBound(float sum, float weights, float mean,
	float peak)
{
	if (peak <= 0.0f)
		return sum;

	const float most = peak * mean * weights;

	return sum < most ? sum : most;
}

/* The power of the cosine to the light whose glow falls to half its peak
 * `halfDeg` degrees from the light's direction: cos^p(half) = 1/2. The
 * disc of the look is pow(cos, p) (hd_water_look.hlsl); 600 halves within
 * 2.7 degrees, a spot. 0 or less for a half angle outside 0 to 90. */
inline float HDWaterGlowPower(float halfDeg)
{
	const float c = cosf(halfDeg * 0.017453293f);

	return halfDeg > 0.0f && halfDeg < 90.0f ? logf(0.5f) / logf(c) : 0.0f;
}

/* The half angle in the water of the sunlit photograph's glow round the
 * sun: 178 pixels at 960 wide under the game's own perspective. */
static const float kHDWaterGlowHalf = 22.5f;

/* The disc's power where the sheet lies, for a glow that halves `halfDeg`
 * degrees from the light in the picture, under the water. The disc is
 * pow(cos, p) of the eye's ray out through the surface against the light
 * (hd_water_look.hlsl, LookPS): an angle in the air. Out of the water a
 * ray's way from the light's grows - n times across the plane of the
 * light, more along it - so the angle in the air of each way `halfDeg`
 * round the light's way in the water (360 of them, those that leave
 * through the window), meaned, is the half angle the power is taken at
 * (HDWaterGlowPower). `up` is the light's height in the air, the sine of
 * its elevation; n 1.333 as the shader's. HDWaterGlowPower(halfDeg) for a
 * light at or under the horizon or no way out; 0 for a half angle outside
 * 0 to 90. */
inline float HDWaterGlowPowerUnder(float halfDeg, float up)
{
	if (!(halfDeg > 0.0f && halfDeg < 90.0f))
		return 0.0f;

	if (!(up > 0.0f && up <= 1.0f))
		return HDWaterGlowPower(halfDeg);

	const float n = 1.333f;
	const float flat = sqrtf(1.0f - up * up);
	// The light's way in the water (x along the light, y up) and its square.
	const float wx = flat / n, wy = sqrtf(1.0f - wx * wx);
	const float ux = wy, uy = -wx;
	const float a = halfDeg * 0.017453293f;
	const float ca = cosf(a), sa = sinf(a);
	double sum = 0.0;
	int count = 0;

	for (int k = 0; k < 360; k++)
	{
		const float ph = (k + 0.5f) * (6.2831853f / 360.0f);
		const float dx = ca * wx + sa * cosf(ph) * ux;
		const float dy = ca * wy + sa * cosf(ph) * uy;
		const float dz = sa * sinf(ph);
		const float h = sqrtf(dx * dx + dz * dz);
		const float s = h * n;

		if (dy <= 0.0f || s >= 1.0f)
			continue;

		// Out of the water: the same way round, its horizontal n times.
		float c = (h > 0.0f ? dx / h * s * flat : 0.0f) +
			sqrtf(1.0f - s * s) * up;

		c = c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c;
		sum += acosf(c);
		count++;
	}

	if (!count)
		return HDWaterGlowPower(halfDeg);

	return HDWaterGlowPower((float)(sum / count) * 57.29578f);
}

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
 * still takes the whole light.
 *
 * Where the eye has the sea's surface over it (HDWaterSeaOver) the art
 * gives way (weight 0) and the look draws the sea's sheet in its place,
 * set on RoomProbe pictures at eleven eyes of the dive (eight looking 19
 * to 85 degrees up, three under the stand-in's edge), measured where the
 * art lay, against two photographs of a sea from below taken as look
 * references only (cut to the photograph, 960 wide). The art's page is
 * grey; its turquoise is the vertices' colour (red 0; hue 175 to 188 in
 * the pictures), the photographs' hue 201 and 207: the sheet takes the
 * water's own colour (the look's light; 198 to 202 with the band).
 * Their pattern's contrast (the luma's spread over 16-pixel windows over
 * the mean luma) is 0.071 and 0.156, the game's own pictures of the art
 * (the player cut out) 0.118: 0.60 to 1.33 of it is the band each eye's
 * 0.058 to 0.114 is taken into. The ripple is 0.8 of the sheet's light,
 * the softest step of 0.05 that holds every eye at or over the band's
 * soft end (0.75 leaves the eye that takes the most glow under it); the
 * sheet 0.10 of the water's light, half again where a shaft leaves it
 * (1.5), so the water between the glow and the shafts stays dark.
 *
 * And the look is stronger there, so that it carries the picture: the
 * photographs' brightest tenth is 7.7 times their darkest, the art's 1.5
 * to 2.3. The shafts at 24 and the underside at 0.24 take it to 1.5 to
 * 3.0, the brightest at the eyes that face the light, with the mean luma
 * at most 96 of 255, under the brighter photograph's 116, and the
 * pattern's contrast to 0.038 to 0.087, inside each eye's band.
 *
 * The disc there is a glow, not a spot: the sunlit photograph's glow
 * round the sun (smoothed over 6 pixels) falls to half 178 pixels from its
 * peak at 960 wide - 22.5 degrees in the water under the game's own
 * perspective (kHDWaterGlowHalf) - and by at most 2.7 a pixel; its peak is
 * 2.49 times the photograph's median luma. The disc's cosine is taken in
 * the air, where that ring lies wider, so its power comes from the
 * light's height (0 here: HDWaterGlowPowerUnder, 3.57 for the sea's light
 * 22 degrees up; the cosine's power at 22.5 itself, 8.75, halves the
 * disc 89 to 102 pixels out). The power 600 of the disc off the sheet
 * halves within 12 pixels and falls by up to 27 a pixel at its rim. The
 * strength: at the power 8.75 the largest whole number from 8 down whose
 * peak lies under the power 600's at every eye the disc is in (4 of 4)
 * and whose peak over the open water's median lies under the
 * photograph's 2.49 at the three eyes that face it from open water is 4;
 * the same light spread to the power 3.57 (the cosine's light over the
 * sky goes as one over the power plus 1) is 1.88, and the largest half
 * step down from it that keeps every eye under the sheet in its band is
 * 1.5 (1.75 takes the eye looking 50 degrees up to the north under its
 * band's soft end).
 *
 * The ripple's two rows are broken per cell of the shafts' grid
 * (SeaRipple) by a phase, a turn of up to 0.9 radians and a loss of up to
 * half the height. Measured on the sheet alone at fifteen eyes, each
 * 192-pixel window's spectrum: its peak over its mean (the regularity) and
 * the share of the peak's ring within 15 degrees of the peak's direction
 * (the straightness) are 242 and 0.43, the unbroken rows' 737 and 0.81,
 * the two photographs' 253 and 341, 0.40 and 0.39: the turn and the loss
 * are the steps of 0.3 and 0.5 whose two numbers both lie by the
 * photographs' (a turn of 0.6 leaves a straightness of 0.51, 1.2 takes
 * the regularity to 237). The luma's step across a cell's border is 0.94
 * to 1.15 times the step inside a cell, the unbroken rows' 0.99 to 1.15:
 * no seam.
 *
 * The sheet reaches past the level's water rooms by the margin of
 * HDWaterSheetMargin (JOBY4's water rooms run 14,300 units further south
 * than the art), its edge ragged over 4,096 units, and the fog along it
 * takes it into the water's colour past 45 degrees from straight up:
 * within 4,096 units of the margin's edge it adds under 1 of luma at every
 * eye that sees it (4 of 30). The fog's share is the largest halving step
 * from 1 that keeps every eye under the sheet in its band (24 of the dive
 * and the flight): 1/32 (0.62 to 0.96 of the art's spread; 1/16 leaves the
 * eye looking level 468 under the basin at 0.52, 1/4 four eyes down to
 * 0.14, where the fog flattens the far sheet along the horizon). With the
 * plane (HDWaterUnderSeaDepth) an eye 68 units under the stand-in's surface
 * sees the rooms over its horizon in the water's colour, hue 199 against
 * the water's 200 and 0.8 of luma apart (counted to the stand-in hue 188,
 * 7.7 apart).
 *
 * The shafts' light in a pixel under the sheet is held to 2.49 times the
 * light the cells' mean gives over the same samples (HDWaterShaftBound),
 * the sunlit photograph's peak over its open water's median, taken for
 * the term. From an eye of the dive looking 26 degrees up toward the
 * light the march's samples meet the surface within a few cells; where
 * those are lit, the term unheld at the sheet's strength burns a quarter
 * of the picture to white. Held, no eye of the dive burns at any of 30
 * clocks 2 seconds apart, and they stay in their band. The bound is the
 * photograph's number, not a measured step: 3 and 4 burn nowhere either
 * and only lift the lit pictures. */
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
	knobs[HD_LOOK_SEA_DISC] = 1.5f;
	knobs[HD_LOOK_SEA_SHAFTS] = 24.0f;
	knobs[HD_LOOK_SEA_UNDERSIDE] = 0.24f;
	knobs[HD_LOOK_SHEET] = 0.10f;
	knobs[HD_LOOK_RIPPLE] = 0.8f;
	knobs[HD_LOOK_PATCHES] = 1.5f;
	knobs[HD_LOOK_ART] = 0.0f;
	knobs[HD_LOOK_SEA_POWER] = 0.0f;
	knobs[HD_LOOK_SEA_TURN] = 0.9f;
	knobs[HD_LOOK_SEA_SWELL] = 0.5f;
	knobs[HD_LOOK_SEA_MARGIN] = 1.0f;
	knobs[HD_LOOK_SEA_RAGGED] = 4096.0f;
	knobs[HD_LOOK_SEA_FOG] = 0.03125f;
	knobs[HD_LOOK_SEA_PLANE] = 1.0f;
	knobs[HD_LOOK_SEA_PEAK] = 2.49f;
}

/* The sea's surface seen from below in the art's place (hd_render.cpp,
 * "the sea's sheet"): whether this frame's eye has the sea's surface over
 * it, so that the look draws the sheet and the art gives way. `caustic` 1
 * with the sheets on, `deep` 1 in a level that takes the deep look,
 * `under` 1 with the eye under the water, `depth` the extinction's depth
 * (HDWaterUnderDepth) and `none` the depth that puts every point in the
 * water: an eye over the stand-in's surface, outside the dive, has none
 * over it, and its picture stays the art's, as dark as the whole way of
 * water leaves it. */
inline bool HDWaterSeaOver(int caustic, int deep, int under, float depth,
	float none)
{
	return caustic != 0 && deep != 0 && under != 0 && depth < none;
}

/* Whether the art gives way this frame: the eye has the sea over it
 * (`over`, HDWaterSeaOver), the look has drawn the sheet since the level's
 * water was built (`drawn`), and it drew it with the clock of this frame's
 * brackets (`drew`, against `now` and the one `before`). `drawn` is its
 * own flag so that no starting value of the clocks can stand for a draw:
 * a frame whose look drew no sheet keeps the art. */
inline bool HDWaterSeaGivesWay(bool over, bool drawn, float drew, float now,
	float before)
{
	return over && drawn && (drew == now || drew == before);
}

/* The way through the water the extinction counts to a point `t` units
 * along a ray whose upward cosine is `up`, under a surface `depth` units
 * over the eye, less the clear zone `clear` round the eye - the room
 * shader's UnderWater (hd_room.hlsl) for a point above the eye, which the
 * art of the sea's surface was dimmed by and the sheet is dimmed by in
 * its place. Never below 0. */
inline float HDWaterSheetPath(float t, float up, float depth, float clear)
{
	const float d = up > 1.0e-3f && depth / up < t ? depth / up : t;

	return d - clear > 0.0f ? d - clear : 0.0f;
}

/* How far in from the edge of the art's box the sheet comes in, in units:
 * four squares, a free choice - from below the box's edge is a straight
 * line across the far water, and the art over the dive's rooms lies
 * inside it. */
static const float kHDWaterSheetEdge = 4096.0f;

/* The extinction's depth where the level takes the deep look and its sea's
 * sheet is found (`sheet` 1, its plane at world y `plane`): an eye in a
 * water room no sheet of the level lies over (`fallback` 1) under the
 * stand-in's surface (`surface`) counts its way up to the plane, as the
 * dive is one sea under one surface; anywhere else HDWaterUnderDepth's
 * depth, so an eye over the stand-in keeps the whole way and the sea's
 * surface fades out over the playable area. Counted against the stand-in
 * (JOBY4's basin of room 7, 16,000 units under the plane), every point
 * higher than it would take the way only up to it, and from the dive the
 * rooms over it would show without the water's colour. */
inline float HDWaterUnderSeaDepth(int fallback, int deep, float eyeY,
	float surface, float none, int sheet, float plane)
{
	if (fallback && deep && sheet && eyeY > surface && eyeY > plane)
		return eyeY - plane;

	return HDWaterUnderDepth(fallback, deep, eyeY, surface, none);
}

/* The depth the sheet's own way is counted from (HDWaterSheetPath): the
 * eye's under the stand-in's surface, `surface` world y, at least 1 - the
 * art of the sea's surface takes its light by that way under the
 * stand-in, and the sheet takes the art's light. */
inline float HDWaterSheetDepth(float eyeY, float surface)
{
	const float d = eyeY - surface;

	return d > 1.0f ? d : 1.0f;
}

/* How far the sheet reaches past the level's water, in units: the height
 * of its plane over the lowest floor of a water room (`floor`, `plane`
 * world y), so that from any eye in the water its edge lies 45 degrees or
 * more from straight up, and the way over which the fog of the water's
 * own extinction (`sigma` a unit, times the fog's share) leaves a
 * twentieth of the light (HDWaterSheetFog), so that past 45 degrees it has
 * faded before its edge. The height alone for no extinction. */
inline float HDWaterSheetMargin(float floor, float plane, float sigma)
{
	const float h = floor > plane ? floor - plane : 0.0f;

	return sigma > 0.0f ? h + logf(20.0f) / sigma : h;
}

/* The sheet's box in world x0, x1, z0, z1: the art's box (`art`) and the
 * level's water rooms' (`water`) together, grown by `margin` on every
 * side; the art's alone where `water` is empty (x1 < x0). */
inline void HDWaterSheetBox(const float* art, const float* water,
	float margin, float* out)
{
	const bool wet = water[1] >= water[0] && water[3] >= water[2];

	out[0] = (wet && water[0] < art[0] ? water[0] : art[0]) - margin;
	out[1] = (wet && water[1] > art[1] ? water[1] : art[1]) + margin;
	out[2] = (wet && water[2] < art[2] ? water[2] : art[2]) - margin;
	out[3] = (wet && water[3] > art[3] ? water[3] : art[3]) + margin;
}

/* The fog along the sheet (hd_water_look.hlsl): the way that the eye ray
 * to a point of the sheet `along` units from the eye along the plane runs
 * past the eye's own height under the plane, `height`, times `share` -
 * added to the sheet's way through the water, so that the sheet fades into
 * the water's own colour from 45 degrees from straight up toward the
 * horizon, as the extinction thickens with the way. Never below 0. */
inline float HDWaterSheetFog(float along, float height, float share)
{
	return along > height ? (along - height) * share : 0.0f;
}

/* The sheet's ripple, the body's two longest wave rows' height over the
 * sum of their two heights (`rows` four numbers a row, kx, kz, omega,
 * height): -1 to 1 whatever the rows' size; 0 for rows without a
 * height. */
inline float HDWaterRippleScale(const float* rows)
{
	const float h = fabsf(rows[3]) + fabsf(rows[7]);

	return h > 0.0f ? 1.0f / h : 0.0f;
}

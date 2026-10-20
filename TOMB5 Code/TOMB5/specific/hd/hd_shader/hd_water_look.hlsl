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

/* The sea seen from below (hd_render.cpp: HDRenderWaterLook on the game's
 * path, drawn right after the sky in a level with the deep look, and
 * RoomProbe's study HDRenderWaterProbeLook): the whole picture, added to
 * it (twice: the disc and the underside behind everything, the shafts over
 * it), with three parts of light the eye under the water would see
 * looking up -
 *
 *   the sun's disc through the surface: the eye ray meets the surface at
 *   the eye's depth over its upward cosine, the surface's normal there
 *   comes from the body's own wave rows (WaveSum, hd_room.hlsl), the ray
 *   bends out into the air by Snell's law (refract; nothing outside the
 *   window) and the disc is a power of its cosine to the light;
 *   the underside lit from above: inside the window the surface carries
 *   the sky's light, brighter toward straight up;
 *   the shafts: the ray is marched from the eye; at each step the light's
 *   way in the water is followed back up to the surface, and a value noise
 *   of the cells' hash (CellHash) there says whether a shaft passes - the
 *   places of the shafts are a hash, never a curve.
 *
 * Both fade with the way: the surface's light over gLookLight.w units of
 * the eye ray, the shafts with the extinction's own sigma_s and over
 * gLookShaft.z units under the surface. Far up the waves are finer than a
 * pixel, and their normals there are drawn as noise and as dark streaks
 * where they tip a ray out of the window; so their slope is held to
 * gLookMore.x / t of itself, t the way to the surface (0: kept whole).
 * Nothing of the frame is left at its clear for these draws to fill: the
 * sky band's poles are closed by its own caps (hd_sky_cap.h). */
#include "hd_room.hlsl"

/* c200 - c202 gLookAxis   the view's axes in the world: world x, y, z of a
 *                          view direction are its dots with these
 * c203        gLookPix    1 / persp twice, the picture's centre in pixels
 * c204        gLookEye    the eye (world, y down), w its depth under the
 *                          surface (the frame's c136.w)
 * c205        gLookSun    toward the light (world, y down), w the disc's
 *                          power
 * c206        gLookGain   the disc, the shafts, the underside, the clock
 * c207        gLookLight  the light's colour, w the surface light's reach
 * c208 - c211 gLookWave   the body's first four wave rows
 * c212        gLookSigma  the extinction (the frame's c134)
 * c213        gLookShaft  the cells' size, the noise's floor, the shafts'
 *                          reach under the surface, the march's length
 * c214        gLookMore   x the way over which the waves' slope is kept
 *                          whole (0 always whole), y the window rim's
 *                          width in cosine (0 the study's 0.08), z the
 *                          eye's depth under the art of the sea's
 *                          surface (0: no sheet), w the depth the
 *                          sheet's own way is counted from (the eye's
 *                          under the stand-in's surface, HDWaterSheetDepth)
 * c215        gLookBox    the sheet's box, world x0, x1, z0, z1: the
 *                          art's and the level's water rooms', the margin
 *                          past them (HDWaterSheetBox, hd_water_under.h)
 * c216        gLookSea    the sheet's strength, its ripple's contrast,
 *                          the clear zone (the frame's c145.x) and one
 *                          over the two longest rows' heights
 * c217        gLookEdge   x the way in from the box's edge over which
 *                          the sheet's ragged edge comes in, in units, y
 *                          the share the shafts' own noise lights the
 *                          sheet by, z the share of the fog along the
 *                          sheet (HDWaterSheetFog), w the cells' mean
 *                          light of the shafts (HDWaterShaftMean; 0 off
 *                          the sheet)
 * c218        gLookBreak  the sheet's ripple broken per cell (SeaRipple):
 *                          x one over the cells' size, y the largest turn
 *                          of a cell's rows in radians, z the largest share
 *                          of their height a cell loses, w the most the
 *                          shafts' light reaches in a pixel in times the
 *                          cells' mean light over the same samples
 *                          (HDWaterShaftBound, hd_water_under.h; 0 off
 *                          the sheet: no bound) */
float4 gLookAxis[3] : register(c200);
float4 gLookPix : register(c203);
float4 gLookEye : register(c204);
float4 gLookSun : register(c205);
float4 gLookGain : register(c206);
float4 gLookLight : register(c207);
float4 gLookWave[4] : register(c208);
float4 gLookSigma : register(c212);
float4 gLookShaft : register(c213);
float4 gLookMore : register(c214);
float4 gLookBox : register(c215);
float4 gLookSea : register(c216);
float4 gLookEdge : register(c217);
float4 gLookBreak : register(c218);

struct LOOK_OUT
{
	float4 pos : POSITION;
};

// The whole picture: four corners already in clip space.
LOOK_OUT LookVS(float4 pos : POSITION)
{
	LOOK_OUT o;

	o.pos = pos;
	return o;
}

/* Whether a shaft passes the surface at world x, z: a value noise of the
 * cells' hash over cells gLookShaft.x wide, its smoothstep blend, above the
 * floor gLookShaft.y only, squared; the cells drift slowly with the clock.
 * Cell numbers into the hash, never places. */
float LookShaft(float2 xz, float t)
{
	const float2 c = xz / gLookShaft.x + t * float2(0.05f, 0.03f);
	const float2 i = floor(c);
	const float2 f = c - i;
	const float2 s = f * f * (3.0f - 2.0f * f);
	float4 second;
	const float4 h = CellHash(float4(i.x, i.x + 1.0f, i.x, i.x + 1.0f),
		float4(i.y, i.y, i.y + 1.0f, i.y + 1.0f), second);
	const float v = lerp(lerp(h.x, h.y, s.x), lerp(h.z, h.w, s.x), s.y);
	const float above = saturate((v - gLookShaft.y) / (1.0f - gLookShaft.y));

	return above * above;
}

/* A value noise of the cells' hash (CellHash) at `c`, in cells: the four
 * corners' numbers blended by the smoothstep of the place, 0 to 1. */
float CellNoise(float2 c)
{
	const float2 i = floor(c);
	const float2 f = c - i;
	const float2 s = f * f * (3.0f - 2.0f * f);
	float4 second;
	const float4 h = CellHash(float4(i.x, i.x + 1.0f, i.x, i.x + 1.0f),
		float4(i.y, i.y, i.y + 1.0f, i.y + 1.0f), second);

	return lerp(lerp(h.x, h.y, s.x), lerp(h.z, h.w, s.x), s.y);
}

/* The sheet's ripple: the body's two longest rows, broken per cell so that
 * its crests neither run straight nor repeat. Each cell of the shafts'
 * grid (1 / gLookBreak.x wide) turns both rows by up to gLookBreak.y,
 * shifts them by a phase of its own and keeps 1 - gLookBreak.z to 1 of
 * their height - three numbers of the cells' hash (CellHash: 16 and 256
 * times the first, 16 times the second), never a curve. The four cells
 * round the point are blended by the smoothstep of its place in the cell,
 * so no seam is left, and the blend is held to one cell's spread (over the
 * root of its weights' squares). Units of height, -1 to 1 times the two
 * rows' heights' sum. */
float SeaRipple(float2 p, float t)
{
	const float2 c = p * gLookBreak.x;
	const float2 i = floor(c);
	const float2 f = c - i;
	const float2 s = f * f * (3.0f - 2.0f * f);
	float4 second;
	const float4 h = CellHash(float4(i.x, i.x + 1.0f, i.x, i.x + 1.0f),
		float4(i.y, i.y, i.y + 1.0f, i.y + 1.0f), second);
	float4 sn, cs;

	sincos((frac(h * 16.0f) * 2.0f - 1.0f) * gLookBreak.y, sn, cs);

	const float4 phase = frac(second * 16.0f) * 6.2831853f;
	const float4 keep = 1.0f - gLookBreak.z * frac(h * 256.0f);
	// A row turned by an angle: cos times k.p plus sin times k's normal.p.
	const float2 k0 = gLookWave[0].xy, k1 = gLookWave[1].xy;
	const float4 a = cs * dot(p, k0) + sn * dot(p, float2(-k0.y, k0.x)) -
		gLookWave[0].z * t + phase;
	const float4 b = cs * dot(p, k1) + sn * dot(p, float2(-k1.y, k1.x)) -
		gLookWave[1].z * t + phase;
	const float4 w = float4((1.0f - s.x) * (1.0f - s.y), s.x * (1.0f - s.y),
		(1.0f - s.x) * s.y, s.x * s.y);

	return dot(w, keep * (gLookWave[0].w * sin(a) + gLookWave[1].w *
		sin(b))) * rsqrt(dot(w, w));
}

float4 LookPS(float2 vpos : VPOS) : COLOR0
{
	const float3 vd = float3((vpos.x + 0.5f - gLookPix.z) * gLookPix.x,
		(vpos.y + 0.5f - gLookPix.w) * gLookPix.y, 1.0f);
	const float3 v = normalize(float3(dot(vd, gLookAxis[0].xyz),
		dot(vd, gLookAxis[1].xyz), dot(vd, gLookAxis[2].xyz)));
	// Up positive from here on: the engine's y counts down.
	const float3 vu = float3(v.x, -v.y, v.z);
	const float3 sun = normalize(float3(gLookSun.x, -gLookSun.y,
		gLookSun.z));
	const float D = gLookEye.w;
	float3 col = 0.0f;
	float far = gLookShaft.w;

	if (vu.y > 1.0e-3f)
	{
		const float t = D / vu.y;
		const float2 hit = gLookEye.xz + v.xz * t;
		float2 slope;

		WaveSum(hit, gLookGain.w, gLookWave[0], gLookWave[1], gLookWave[2],
			gLookWave[3], slope);

		if (gLookMore.x > 0.0f)
			slope *= saturate(gLookMore.x / t);

		const float3 n = normalize(float3(-slope.x, 1.0f, -slope.y));
		const float3 a = refract(vu, -n, 1.333f);
		/* The window's rim, soft: from the critical cosine (0.661, 48.6
		 * degrees, hd_water_under.h) over gLookMore.y of cosine inward
		 * (0: 0.08, the study's), so that the ripples do not punch hard
		 * holes in it. */
		const float rim = gLookMore.y > 0.0f ? gLookMore.y : 0.08f;
		const float win = dot(a, a) > 0.5f ?
			saturate((dot(vu, n) - 0.661f) / rim) : 0.0f;
		const float toward = saturate(dot(a, sun));
		const float disc = pow(toward, gLookSun.w) +
			0.08f * pow(toward, 8.0f);
		const float sky = 0.35f + 0.65f * saturate(a.y);

		col += gLookLight.rgb * exp(-t / gLookLight.w) * win *
			(gLookGain.x * disc + gLookGain.z * sky);
		far = min(far, t);
	}

	/* The sea's sheet in the place of its art, where the eye has that
	 * surface over it (gLookMore.z, 0 elsewhere): the water's own colour,
	 * dimmed as the room shader dimmed the art (UnderWater: the way under
	 * the stand-in's surface less the clear zone) and by the fog along it,
	 * over its box (the art's and the water rooms', the margin past them)
	 * and fading in from a ragged edge, its ripple the body's two longest
	 * rows broken per cell (SeaRipple), brighter where a shaft leaves it
	 * (LookShaft). */
	if (gLookMore.z > 0.0f && vu.y > 1.0e-3f)
	{
		const float ta = gLookMore.z / vu.y;
		const float2 at = gLookEye.xz + v.xz * ta;
		/* The edge ragged: where it comes in moves by up to its own way,
		 * a value noise of the cells' hash over cells that wide. */
		const float edge = min(min(at.x - gLookBox.x, gLookBox.y - at.x),
			min(at.y - gLookBox.z, gLookBox.w - at.y));
		const float inside = saturate((edge - gLookEdge.x *
			CellNoise(at / gLookEdge.x)) / gLookEdge.x);
		/* The fog along the sheet (HDWaterSheetFog): the way past the eye's
		 * own height under the plane counts too. */
		const float along = ta * sqrt(saturate(1.0f - vu.y * vu.y));
		const float path = max(min(ta, gLookMore.w / vu.y) - gLookSea.z,
			0.0f) + max(along - gLookMore.z, 0.0f) * gLookEdge.z;
		const float h = clamp(SeaRipple(at, gLookGain.w) * gLookSea.w, -1.0f,
			1.0f);

		col += gLookLight.rgb * exp(-path * gLookSigma.rgb) * gLookSea.x *
			inside * (1.0f + gLookSea.y * h) *
			(1.0f + gLookEdge.y * LookShaft(at, gLookGain.w));
	}

	// The light's way in the water, the flat surface's bend of it.
	const float3 down = refract(-sun, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.333f);
	const float3 back = -down / max(-down.y, 1.0e-3f);
	// Forward scattering: brighter looking toward the light.
	const float phase = 0.25f + 0.75f * pow(saturate(dot(vu, -down)), 4.0f);
	float4 second;
	const float jitter = CellHash(vpos.xxxx, vpos.yyyy, second).x;
	const float step = far / 24.0f;
	float sum = 0.0f;
	float weights = 0.0f;

	[loop]
	for (int k = 0; k < 24; k++)
	{
		const float s = (k + jitter) * step;
		const float under = D - vu.y * s;
		const float2 at = gLookEye.xz + v.xz * s + back.xz * under;

		sum += LookShaft(at, gLookGain.w) * exp(-under / gLookShaft.z) *
			exp(-s * gLookSigma.w);
		weights += exp(-under / gLookShaft.z) * exp(-s * gLookSigma.w);
	}

	/* Under the sheet the march's samples meet the surface within a few
	 * cells, and the whole picture's shafts take those cells' hash: held
	 * to gLookBreak.w times the cells' mean light (gLookEdge.w) over the
	 * same samples (HDWaterShaftBound, hd_water_under.h); off the sheet
	 * both are 0 and the sum stays as it is. */
	if (gLookBreak.w > 0.0f)
		sum = min(sum, gLookBreak.w * gLookEdge.w * weights);

	col += gLookLight.rgb * gLookGain.y * phase * sum * step /
		gLookShaft.w;
	return float4(col, 0.0f);
}

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
 * A third draw on the game's path fills what the frame left at its clear
 * (the sky band's open poles, past its last ring at 75 degrees) with
 * gLookFill, the water's scattered colour: the colour a view ray under
 * the water takes when it meets nothing. */
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
 *                          width in cosine (0 the study's 0.08)
 * c215        gLookFill   the colour added before the three parts (0 but
 *                          in the fill's draw) */
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
float4 gLookFill : register(c215);

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
	float3 col = gLookFill.rgb;
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

	// The light's way in the water, the flat surface's bend of it.
	const float3 down = refract(-sun, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.333f);
	const float3 back = -down / max(-down.y, 1.0e-3f);
	// Forward scattering: brighter looking toward the light.
	const float phase = 0.25f + 0.75f * pow(saturate(dot(vu, -down)), 4.0f);
	float4 second;
	const float jitter = CellHash(vpos.xxxx, vpos.yyyy, second).x;
	const float step = far / 24.0f;
	float sum = 0.0f;

	[loop]
	for (int k = 0; k < 24; k++)
	{
		const float s = (k + jitter) * step;
		const float under = D - vu.y * s;
		const float2 at = gLookEye.xz + v.xz * s + back.xz * under;

		sum += LookShaft(at, gLookGain.w) * exp(-under / gLookShaft.z) *
			exp(-s * gLookSigma.w);
	}

	col += gLookLight.rgb * gLookGain.y * phase * sum * step /
		gLookShaft.w;
	return float4(col, 0.0f);
}

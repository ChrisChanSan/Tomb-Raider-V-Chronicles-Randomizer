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

/* The projected shadow of the HD Lara and of the things near her: the
 * arithmetic.
 *
 * Five pieces, each a pure function of its arguments: the KEY LIGHT, one
 * direction and one darkness - a fixed slant and a fixed darkness, which the
 * lights that light her turn and deepen by the table's PULL; the FOLLOWING,
 * which draws both where her movement through the light puts them at once
 * and blends a change of the lights over game time so that it never steps;
 * the BOX, an orthographic volume round her seen along that direction, with
 * the one mapping from a point to its place in a shadow map - one box for
 * her own map, and a larger one round it for the second map, which the
 * other casters are drawn into; the GATE, which says whether the projected
 * shadow runs in a frame; and the RULES of who else casts, whose other face
 * says whose blob gives way, and of which maps an HD model takes - with the
 * reflected view a mirror's image takes them through.
 *
 * This header includes no engine header on purpose, like hd_softbody.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp includes it and asserts on every
 * function below without a device or a level, and ModelProbe builds its map
 * matrices through the same functions. hd_laradraw.cpp gathers and steps,
 * hd_model.cpp and hd_render.cpp draw.
 *
 * Space: the engine's world, y growing downward. A direction is the way the
 * light TRAVELS, so straight down is (0, 1, 0). */

/* ------------------------------------------------------------ the table -- */

/* Every number that decides how the shadow looks, in one place.
 *
 * With PULL at 0 the shadow's light is one fixed light for the whole game:
 * FALLBACK_SLANT off straight down, from the side FALLBACK_X/_Z name, at the
 * darkness DARK * LOW, whatever lights her list holds - the shadow moves
 * only as she does. W_REF, W_FALLBACK, SLANT_MAX and TAU shape what the
 * lights add, and matter only while PULL is above 0.
 *
 *   MAP         texels a side of her square map. More is a sharper outline
 *               and a dearer pass; the box below over this is the size of a
 *               texel in the world (4 units).
 *   BOX         her box's width and height across the light, units. Larger
 *               holds a longer shadow and spends the texels more thinly;
 *               2048 holds her standing outline with room on every side.
 *   DEPTH       the box's length along the light, units: what lies further
 *               from her middle than half of it neither casts nor receives.
 *   DARK        how much a pixel loses in the darkest shadow the lights can
 *               give, 0..1, where all of them agree and their weight reaches
 *               W_REF. The fixed light's shadow is DARK * LOW of it. Higher
 *               is a blacker shadow.
 *   LOW         the part of DARK the shadow has where no light gives a
 *               direction - with PULL 0 everywhere. At 0.75 the shadow loses
 *               0.34 of a pixel's colour and keeps 0.66: about the darkness
 *               the lamps give her with PULL 1 over 45 seconds of ANDREA2
 *               (0.28 to 0.43, 0.32 on average). Higher is a darker shadow, 1
 *               is DARK itself.
 *   PULL        how much the lights of her list turn the shadow away from
 *               the fixed slant and deepen it, 0..1: every light's weight is
 *               multiplied by it, in the direction's sum and in the darkness
 *               alike. 0 is the fixed light; 1 the lights' full part. 0,
 *               because the lamps of the levels light the walls - low, close
 *               and one at a time - and each throws the shadow somewhere
 *               else: over 45 seconds of ANDREA2 at PULL 1 her list holds no
 *               lamp or one, the direction turns by more than 40 degrees
 *               from one second to the next 27 times in 44 (104 at most),
 *               and it stands within a degree of SLANT_MAX in 19 of them.
 *   W_REF       the summed light weight (luminance times falloff, times
 *               PULL) from which the direction counts fully. Higher needs
 *               brighter lamps before the shadow reaches its full darkness.
 *               Only while PULL is above 0.
 *   W_FALLBACK  the weight of the fixed slant added to the lights' sum, so
 *               the direction turns smoothly as lights come and go. Higher
 *               pulls every shadow towards the fixed slant. Only while PULL
 *               is above 0.
 *   FALLBACK_SLANT  the fixed slant, degrees off straight down. 0 puts the
 *               shadow under her feet; more lays it beside them, her height
 *               times the tangent long: 0.58 of it at 30, a little more than
 *               half her height.
 *   SLANT_MAX   the steepest slant off straight down a shadow may have,
 *               degrees. Higher allows longer shadows from low lamps; the
 *               shadow's length grows with the tangent, 1.43 of her height
 *               at 55. Only while PULL is above 0, or for a FALLBACK_SLANT
 *               above it, which it holds to itself.
 *   TAU         the time constant with which a change of the lights blends
 *               in, game seconds: after TAU it has gone 63 per cent of the
 *               way, after three TAU 95. Longer is softer and slower. Her
 *               own movement through the light is drawn at once whatever it
 *               is. Only while PULL is above 0: at 0 the lights change
 *               nothing, so nothing blends.
 *   FULL, END   below her lowest node: a receiver up to FULL units below
 *               takes the whole shadow, from END on none, smooth between.
 *               Receivers never cast, so a floor her shadow falls on cannot
 *               hide the floor beneath it, nor a wall the floor beyond it:
 *               END is what keeps her outline off a storey under her, and
 *               off the floor of the room beside, which lies as deep below
 *               her as the wall's foot. Counted on the thirteen levels'
 *               classic faces (ModelProbe -shadowsurvey: rays along the
 *               light from her body on every floor sector, a face behind
 *               the first face a ray meets taking her shadow through it):
 *               behind a face more than a block below her feet such faces
 *               outnumber the first faces 2.4 to 1, between half a block and
 *               a block they are as many. At 512 and 1024 half a block takes
 *               all of it and a block none: the weight such faces take
 *               through a wall falls by 89 per cent and through a floor by
 *               54, the first faces below her feet lose 28 per cent of
 *               theirs, all of them more than half a block below her - the
 *               foot of a ledge she stands on. Larger lets her shadow follow
 *               a drop further and a storey under her take it again.
 *   EDGE        the outer part of the box, of its half width, over which the
 *               shadow fades out, so the box's edge is never seen.
 *   FACING      the n.l from which a face takes the whole shadow; a face
 *               turned away from the light (n.l at 0 or below) takes none.
 *   BIAS        units a receiver must lie behind the cast depth to be in
 *               shadow. Receivers never cast, so it covers arithmetic only;
 *               more is safer and lets a shadow start a little later.
 *
 * The second map, the one the other casters - the creatures and the items
 * the classic engine gives a blob - are drawn into: the same light and the
 * same basis as hers, a larger box round the same centre.
 *
 *   OTHERS_MAP    texels a side of the second map. With OTHERS_BOX it gives
 *                 the size of a texel in the world, 4 units at 2048, the
 *                 texel of her map, so a creature's outline is as soft as
 *                 hers; fewer is a softer, blockier outline and a cheaper
 *                 map (16 MiB of colour and 16 of depth at 2048).
 *   OTHERS_BOX    the second box's width and height across the light,
 *                 units. A caster further from her than half of it and
 *                 OTHERS_MARGIN of it casts nothing and keeps no blob
 *                 either, and the edge term has faded its shadow out before
 *                 the box's side; larger reaches further
 *                 and spends the texels more thinly. 8192 is four sectors to
 *                 every side of her.
 *   OTHERS_MARGIN how far beyond the second box's sides a caster's place may
 *                 lie and the caster still be drawn into the map: a part of
 *                 OTHERS_BOX beyond the four sides across the light, and the
 *                 same part of OTHERS_DEPTH beyond the near and far side
 *                 along it. Its parts that reach into the box cast there and
 *                 the edge term, or OTHERS_FADE along the light, fades them,
 *                 so a creature leaving the box loses its shadow gradually
 *                 on every side, also on the two its upper body leans in
 *                 from its feet. 0.1 is 819 units across the light: at the
 *                 fixed slant a head up to 2160 units above the feet.
 *                 Smaller cuts a tall creature's shadow off where it leaves
 *                 the box on those two sides; larger draws casters of which
 *                 no part reaches into the box.
 *   OTHERS_DEPTH  the second box's length along the light, units: no part
 *                 of a caster further from her centre along the light than
 *                 half of it casts. 8192 holds a storey above and below her.
 *                 Its near and far side (4096 units along the light from her
 *                 centre, 4730 straight above or below her at the fixed
 *                 slant) are faded over by OTHERS_FADE.
 *   OTHERS_FADE   the part of OTHERS_DEPTH next to each of the box's near
 *                 and far side over which a caster's shadow fades out along
 *                 the light: a part of a caster at that side casts nothing,
 *                 one OTHERS_FADE of the depth in from it casts in full,
 *                 linear between. A caster's place is held up to
 *                 OTHERS_MARGIN past those sides as past the other four, so
 *                 a creature crossing one of them loses its shadow
 *                 gradually and never in one frame. 0.05 is 410 units, where
 *                 a creature 760 units tall spans 658 along a 30 degree
 *                 light; OTHERS_MARGIN past the side its 658 lie wholly
 *                 outside the box, where nothing casts any more. Smaller
 *                 shows a harder end of the shadow where a creature climbs
 *                 or falls out of the box; larger lets a creature's shadow
 *                 thin out already on the storey above or below her.
 *   OTHERS_FULL, OTHERS_END  how far behind a caster along the light a
 *                 receiver still takes its shadow: whole up to OTHERS_FULL
 *                 units, none from OTHERS_END on, smooth between. The blob
 *                 carriers of the thirteen levels stand 691 to 809 units
 *                 tall in their rest pose (Tr5Level::frameBounds), the
 *                 sniper 907 - 1047 along a 30 degree light, within 23 units
 *                 of OTHERS_FULL - so the floor at a man's feet takes his
 *                 whole outline; the skeleton (1199), the hydra (1249) and
 *                 the Roman god (1546) lose its top on their own floor. A
 *                 floor straight below a creature's feet takes nothing from
 *                 1108 units down, about a block: through a wall or a floor
 *                 the weight such faces take falls by 99 and 74 per cent
 *                 against 2560 (ModelProbe -shadowsurvey, as for FULL and
 *                 END), the first faces lose 10. Larger lets the shadow
 *                 reach a lower floor and the room beside, smaller cuts a
 *                 man's head off his shadow. */
#define HD_SHADOW_MAP			512
#define HD_SHADOW_BOX			2048.0f
#define HD_SHADOW_DEPTH			2048.0f
#define HD_SHADOW_DARK			0.45f
#define HD_SHADOW_LOW			0.75f
#define HD_SHADOW_PULL			0.0f
#define HD_SHADOW_W_REF			0.5f
#define HD_SHADOW_W_FALLBACK		0.15f
#define HD_SHADOW_FALLBACK_SLANT	30.0f
#define HD_SHADOW_SLANT_MAX		55.0f
#define HD_SHADOW_TAU			0.5f
#define HD_SHADOW_FULL			512.0f
#define HD_SHADOW_END			1024.0f
#define HD_SHADOW_EDGE			0.1f
#define HD_SHADOW_FACING		0.2f
#define HD_SHADOW_BIAS			8.0f
#define HD_SHADOW_OTHERS_MAP		2048
#define HD_SHADOW_OTHERS_BOX		8192.0f
#define HD_SHADOW_OTHERS_MARGIN		0.1f
#define HD_SHADOW_OTHERS_DEPTH		8192.0f
#define HD_SHADOW_OTHERS_FADE		0.05f
#define HD_SHADOW_OTHERS_FULL		1024.0f
#define HD_SHADOW_OTHERS_END		1280.0f

/* The machinery under the table: these keep the following honest and are
 * not a matter of taste.
 *
 *   GAP         a frame further than this from the last one, game seconds,
 *               snaps instead of blending: a hitch, a load, a long pause.
 *               hd_softbody.h's HD_SOFT_GAP, for the same reason.
 *   SPEED, SLACK  the fastest she can travel, units a second, and a margin
 *               on top, units: a reference point further than SPEED * dt +
 *               SLACK from the last one has been put somewhere (a teleport,
 *               a cutscene's edge) and snaps. hd_softbody.h's numbers.
 *   AGREE_EPS   added to the summed weight under the agreement of the
 *               directions, so no light at all divides nothing by nothing.
 *   NEAR_LAMP   a lamp nearer to her reference point than this, units, has
 *               no direction there; its weight still counts.
 *   FALLBACK_X, _Z  the compass bearing of the fixed slant, a unit vector in
 *               the world's x-z plane. */
#define HD_SHADOW_GAP			0.25f
#define HD_SHADOW_SPEED			9000.0f
#define HD_SHADOW_SLACK			128.0f
#define HD_SHADOW_AGREE_EPS		1.0e-4f
#define HD_SHADOW_NEAR_LAMP		1.0f
#define HD_SHADOW_FALLBACK_X		0.70710678f
#define HD_SHADOW_FALLBACK_Z		0.70710678f

/* The option's value that asks for the projected shadow; 1..4 are the four
 * blob shadows of S_PrintShadow. */
#define HD_SHADOW_MODE_PROJECTED	5

/* The light kinds, the numbers HD_OBJLIGHT_SUN / _POINT / _SPOT have in
 * hd_model.h. */
#define HD_SHADOW_SUN			0
#define HD_SHADOW_POINT			1
#define HD_SHADOW_SPOT			2

/* At most this many lights are weighed; the engine's two lists of an item
 * hold far fewer. */
#define HD_SHADOW_MAX_LIGHTS		32

/* ----------------------------------------------------------- the helpers -- */

inline float hd_shadow_dot(const float* a, const float* b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float hd_shadow_len(const float* a)
{
	return sqrtf(hd_shadow_dot(a, a));
}

inline bool hd_shadow_finite(float f)
{
	return f == f && f < 3.0e38f && f > -3.0e38f;
}

inline float hd_shadow_saturate(float f)
{
	return f > 0.0f ? (f < 1.0f ? f : 1.0f) : 0.0f;
}

/* The fixed slant: FALLBACK_SLANT off straight down, towards FALLBACK_X/_Z. */
inline void HDShadowFallback(float* d)
{
	const float a = HD_SHADOW_FALLBACK_SLANT * (3.14159265f / 180.0f);
	const float s = sinf(a);

	d[0] = s * HD_SHADOW_FALLBACK_X;
	d[1] = cosf(a);
	d[2] = s * HD_SHADOW_FALLBACK_Z;
}

/* `v` made a unit vector; the fixed slant for a vector of no length or one
 * that is not a number. */
inline void hd_shadow_unit(float* v)
{
	const float l = hd_shadow_len(v);

	if (!(l > 1.0e-6f) || !hd_shadow_finite(l))
	{
		HDShadowFallback(v);
		return;
	}

	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
}

/* --------------------------------------------------------- the key light -- */

/* One light as the key light weighs it, in world space:
 *
 *   kind     HD_SHADOW_SUN, _POINT or _SPOT
 *   x, y, z  a sun's travel direction (any length), a point's or a spot's
 *            position
 *   r, g, b  its colour as the engine's list holds it, 0..1
 *   range    a point's Outer, a spot's Cutoff, units; not read for a sun */
struct HDShadowLight
{
	int kind;
	float x, y, z;
	float r, g, b;
	float range;
};

/* The key light of one frame.
 *
 *   dir       the unit direction the shadow is thrown in
 *   strength  DARK * lerp(LOW, 1, agree)
 *   weight    the summed weight of the lights counted, before the pull
 *   agree     0..1: how far the lights agree on one direction, times how far
 *             their weight times the pull reaches W_REF; 0 at pull 0
 *   slant     degrees between dir and straight down
 *   lights    lights with a weight above 0, before the pull */
struct HDShadowKey
{
	float dir[3];
	float strength;
	float weight;
	float agree;
	float slant;
	int lights;
};

/* Rec. 709's luminance of a colour. */
inline float HDShadowLuminance(float r, float g, float b)
{
	return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* A point's or a spot's falloff at a distance, the model shader's own term
 * (hd_model.hlsl, ModelVS: rad = max(0, 1 - dist * invRange), invRange 1 /
 * Outer for a point and 1 / Cutoff for a spot). Distance over range against
 * the falloff:
 *
 *   0  1      0.25  0.75      0.5  0.5      1 and over  0
 *
 * A range of 0 or less, or a distance that is not a number, gives 0. */
inline float HDShadowFalloff(float dist, float range)
{
	if (!(range > 0.0f) || !hd_shadow_finite(dist))
		return 0.0f;

	const float f = 1.0f - dist / range;

	return f > 0.0f ? f : 0.0f;
}

/* The angle between `d` and straight down, degrees. */
inline float HDShadowSlant(const float* d)
{
	const float l = hd_shadow_len(d);

	if (!(l > 0.0f))
		return 0.0f;

	float c = d[1] / l;

	c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
	return acosf(c) * (180.0f / 3.14159265f);
}

/* The slant limit, on a unit vector in place: a direction more than
 * SLANT_MAX off straight down - an upward one included - is turned back to
 * the limit with its compass bearing kept. A direction with no bearing at
 * all (straight up) takes the fixed slant's. True when it was turned. */
inline bool HDShadowLimit(float* d)
{
	const float a = HD_SHADOW_SLANT_MAX * (3.14159265f / 180.0f);
	const float cosMax = cosf(a);

	if (d[1] >= cosMax)
		return false;

	float h[2] = { d[0], d[2] };
	float hl = sqrtf(h[0] * h[0] + h[1] * h[1]);

	if (!(hl > 1.0e-6f))
	{
		h[0] = HD_SHADOW_FALLBACK_X;
		h[1] = HD_SHADOW_FALLBACK_Z;
		hl = 1.0f;
	}

	const float s = sinf(a);

	d[0] = s * h[0] / hl;
	d[1] = cosMax;
	d[2] = s * h[1] / hl;
	return true;
}

/* The key light's target for a reference point P on her, with `pull` the
 * lights' part, held to 0..1 - the table's PULL unless the caller gives one.
 *
 * Every light gives a weight w = luminance * falloff at P (1 for a sun) and
 * a direction d, the way it travels at P: a sun's own, and P minus the lamp,
 * made unit, for a point or a spot; the key light uses pull * w. The
 * direction is the sum of pull * w * d plus W_FALLBACK times the fixed
 * slant, made unit and held to the slant limit - continuous in P - and the
 * fixed slant itself where that sum is nothing. The strength follows how far
 * the lights agree: |sum pull w d| / sum pull w, times sum pull w over W_REF,
 * both held to 0..1. At pull 0 every product is 0, so the target is the
 * fixed slant at DARK * LOW, the same bits for any lights and any P. */
inline void HDShadowKeyTarget(const HDShadowLight* lights, int count, const float* P,
	HDShadowKey* out, float pull = HD_SHADOW_PULL)
{
	const float k = hd_shadow_saturate(pull);
	float sum[3] = { 0.0f, 0.0f, 0.0f };
	float weight = 0.0f;
	float pulled = 0.0f;
	int used = 0;

	for (int i = 0; lights && i < count && i < HD_SHADOW_MAX_LIGHTS; i++)
	{
		const HDShadowLight& l = lights[i];
		const float lum = HDShadowLuminance(l.r, l.g, l.b);
		float d[3] = { 0.0f, 0.0f, 0.0f };
		float w = 0.0f;

		if (!hd_shadow_finite(lum) || !(lum > 0.0f))
			continue;

		if (l.kind == HD_SHADOW_SUN)
		{
			const float v[3] = { l.x, l.y, l.z };
			const float len = hd_shadow_len(v);

			if (!(len > 1.0e-6f) || !hd_shadow_finite(len))
				continue;

			d[0] = v[0] / len;
			d[1] = v[1] / len;
			d[2] = v[2] / len;
			w = lum;
		}
		else if (l.kind == HD_SHADOW_POINT || l.kind == HD_SHADOW_SPOT)
		{
			const float v[3] = { P[0] - l.x, P[1] - l.y, P[2] - l.z };
			const float dist = hd_shadow_len(v);

			w = lum * HDShadowFalloff(dist, l.range);

			if (dist > HD_SHADOW_NEAR_LAMP && hd_shadow_finite(dist))
			{
				d[0] = v[0] / dist;
				d[1] = v[1] / dist;
				d[2] = v[2] / dist;
			}
		}
		else
			continue;

		if (!(w > 0.0f) || !hd_shadow_finite(w))
			continue;

		const float wp = k * w;

		sum[0] += wp * d[0];
		sum[1] += wp * d[1];
		sum[2] += wp * d[2];
		weight += w;
		pulled += wp;
		used++;
	}

	float fb[3];

	HDShadowFallback(fb);

	/* A sum of nothing leaves the fixed slant as HDShadowFallback gives
	 * it, so the fixed light does not depend on the rounding of a unit
	 * vector made of a fraction of itself. */
	if (sum[0] == 0.0f && sum[1] == 0.0f && sum[2] == 0.0f)
	{
		for (int i = 0; i < 3; i++)
			out->dir[i] = fb[i];
	}
	else
	{
		out->dir[0] = sum[0] + HD_SHADOW_W_FALLBACK * fb[0];
		out->dir[1] = sum[1] + HD_SHADOW_W_FALLBACK * fb[1];
		out->dir[2] = sum[2] + HD_SHADOW_W_FALLBACK * fb[2];
		hd_shadow_unit(out->dir);
	}

	HDShadowLimit(out->dir);

	const float agree = hd_shadow_saturate(hd_shadow_len(sum) / (pulled + HD_SHADOW_AGREE_EPS)) *
		hd_shadow_saturate(pulled / HD_SHADOW_W_REF);

	out->agree = agree;
	out->strength = HD_SHADOW_DARK * (HD_SHADOW_LOW + (1.0f - HD_SHADOW_LOW) * agree);
	out->weight = weight;
	out->slant = HDShadowSlant(out->dir);
	out->lights = used;
}

/* ------------------------------------------------------- the following -- */

/* The key light as it is drawn: the frame's target plus an offset that holds
 * back what a change of the lights moved.
 *
 * The target turns for two reasons. She moves through the light: that is
 * where the shadow lies in this very frame, and it is drawn at once. The
 * lights themselves change - another room's list comes in, a lamp goes out
 * or fades: that is blended, so neither the direction nor the darkness ever
 * steps. To tell the two apart, HDShadowFollowLights weighs the frame's
 * lights at the last step's reference point before each step; against the
 * last step's target that is what the lights alone changed, at one place,
 * and the step takes it into the offset, which falls off along
 * exp(-t / TAU). At PULL 0 the target is the same in every frame, the offset
 * stays 0 and the target is drawn as it is.
 *
 *   primed    0 until the first step, and after a reset; the next step snaps
 *   dir       the drawn direction, a unit vector
 *   strength  the drawn strength
 *   at        the reference point of the last step, for the jump test and
 *             for the measurement of the lights
 *   snaps     snaps since the caller last cleared it, for the log
 *   last, lastStrength  the target of the last step
 *   offset, offsetStrength  the drawn values minus the target, the direction
 *             before it is made unit: 0 while the lights stay as they are
 *   back, backStrength  the frame's lights weighed at `at`
 *             (HDShadowFollowLights)
 *   measured  1 while `back` holds a measurement the next step has not used
 *   lag       the largest angle between the drawn direction and the target,
 *             degrees, since the caller last cleared it, for the log */
struct HDShadowFollow
{
	int primed;
	float dir[3];
	float strength;
	float at[3];
	int snaps;
	float last[3];
	float lastStrength;
	float offset[3];
	float offsetStrength;
	float back[3];
	float backStrength;
	int measured;
	float lag;
};

inline void HDShadowFollowReset(HDShadowFollow* f)
{
	f->primed = 0;
	f->measured = 0;
}

/* How much of an offset is left after `dt` game seconds: exp(-dt / TAU),
 * the exact solution of v' = -v / TAU over dt. Steps multiply to the same
 * value at any frame rate, because exp(-a) exp(-b) = exp(-(a + b)). 1 for
 * dt <= 0. */
inline float HDShadowDecay(float dt)
{
	if (!(dt > 0.0f))
		return 1.0f;

	return expf(-dt / HD_SHADOW_TAU);
}

/* The measurement before a step: the frame's lights - the same ones its
 * target is made of - weighed at the last step's reference point,
 * HDShadowKeyTarget at `at` with the same pull as the target. Nothing before
 * the first step, which snaps. */
inline void HDShadowFollowLights(HDShadowFollow* f, const HDShadowLight* lights, int count,
	float pull = HD_SHADOW_PULL)
{
	f->measured = 0;

	if (!f->primed)
		return;

	HDShadowKey back;

	HDShadowKeyTarget(lights, count, f->at, &back, pull);

	for (int i = 0; i < 3; i++)
		f->back[i] = back.dir[i];

	f->backStrength = back.strength;
	f->measured = 1;
}

/* One step of the following, once per drawn main frame, with the frame's
 * target and reference point P; HDShadowFollowLights comes right before it.
 *
 *   a snap      the offset is cleared, so the target is drawn as it is: the
 *               first step after a reset (level start, savegame, a frame in
 *               which the projected shadow did not run), dt beyond GAP or not
 *               a number, and P further from the last step's than SPEED * dt
 *               + SLACK.
 *   otherwise   the jump is back - last, what the lights alone changed since
 *               the last step, measured at the last step's P, and the offset
 *               becomes (offset - jump) * HDShadowDecay(dt). A step without a
 *               measurement cannot tell her movement from the lights and
 *               counts the whole change of the target as the lights': back is
 *               then the target itself.
 *   dt <= 0     the jump is taken and nothing decays: a frame drawn twice,
 *               or the game standing still (the clock is hd_softbody.h's
 *               HDSoftClock, which the pause and the inventory do not
 *               advance), so nothing leaps when the clock runs again.
 *
 * The drawn direction is target + offset made unit and held to the slant
 * limit - the target itself where the offset is nothing or the sum has no
 * length - and the drawn strength is target + offset held to 0..DARK. With
 * the lights unchanged the jump is 0 and the shadow lies where the light
 * throws it in every frame, standing still the frame she stands still; a
 * change of the lights starts from what was drawn and reaches the new target
 * along exp(-t / TAU), t from the last step that saw the old lights, the
 * same at every frame rate.
 *
 * True when it snapped. */
inline bool HDShadowFollowStep(HDShadowFollow* f, const HDShadowKey* target, const float* P,
	float dt)
{
	bool snap = !f->primed || !hd_shadow_finite(dt) || dt > HD_SHADOW_GAP;

	if (!snap)
	{
		const float d[3] = { P[0] - f->at[0], P[1] - f->at[1], P[2] - f->at[2] };
		const float jump = hd_shadow_len(d);
		const float reach = HD_SHADOW_SPEED * (dt > 0.0f ? dt : 0.0f) + HD_SHADOW_SLACK;

		if (!hd_shadow_finite(jump) || jump > reach)
			snap = true;
	}

	const bool measured = f->measured != 0;

	f->measured = 0;

	for (int i = 0; i < 3; i++)
		f->at[i] = P[i];

	if (snap)
	{
		for (int i = 0; i < 3; i++)
			f->offset[i] = 0.0f;

		f->offsetStrength = 0.0f;
		f->primed = 1;
		f->snaps++;
	}
	else
	{
		const float k = HDShadowDecay(dt);
		const float* back = measured ? f->back : target->dir;
		const float backStrength = measured ? f->backStrength : target->strength;
		bool numbers = true;

		for (int i = 0; i < 3; i++)
		{
			f->offset[i] = (f->offset[i] - (back[i] - f->last[i])) * k;
			numbers = numbers && hd_shadow_finite(f->offset[i]);
		}

		f->offsetStrength = (f->offsetStrength - (backStrength - f->lastStrength)) * k;

		/* An offset that is not a number is dropped, so the target
		 * is drawn. */
		if (!numbers || !hd_shadow_finite(f->offsetStrength))
		{
			for (int i = 0; i < 3; i++)
				f->offset[i] = 0.0f;

			f->offsetStrength = 0.0f;
		}
	}

	for (int i = 0; i < 3; i++)
		f->last[i] = target->dir[i];

	f->lastStrength = target->strength;

	float d[3] = { target->dir[0], target->dir[1], target->dir[2] };

	if (f->offset[0] != 0.0f || f->offset[1] != 0.0f || f->offset[2] != 0.0f)
	{
		const float sum[3] = { d[0] + f->offset[0], d[1] + f->offset[1], d[2] + f->offset[2] };
		const float l = hd_shadow_len(sum);

		if (l > 1.0e-6f && hd_shadow_finite(l))
		{
			for (int i = 0; i < 3; i++)
				d[i] = sum[i] / l;

			HDShadowLimit(d);
		}
	}

	for (int i = 0; i < 3; i++)
		f->dir[i] = d[i];

	const float s = target->strength + f->offsetStrength;

	f->strength = s > 0.0f ? (s < HD_SHADOW_DARK ? s : HD_SHADOW_DARK) : 0.0f;

	/* The lag for the log: the angle as atan2 of the cross product's length
	 * over the dot product, which keeps its precision near 0. */
	const float* t = target->dir;
	const float cr[3] = { d[1] * t[2] - d[2] * t[1], d[2] * t[0] - d[0] * t[2],
		d[0] * t[1] - d[1] * t[0] };
	const float lag = atan2f(hd_shadow_len(cr), hd_shadow_dot(d, t)) * (180.0f / 3.14159265f);

	if (lag > f->lag)
		f->lag = lag;

	return snap;
}

/* --------------------------------------------------------- the arrival -- */

/* Where the projected shadow starts to run - the option stepped to
 * Projected, the level drawn HD with it, Lara walking from a room drawn
 * classic into an HD one, the map made again after a lost device - it comes
 * in over FADE_IN game seconds, and the blobs it takes the place of go out
 * over the same time: the drawn strength is the following's times
 * HDShadowPresence of the time it has run without a break, every blob that
 * gives way to it is drawn meanwhile with its alpha times 1 minus that, so
 * the floor shows one shadow's worth at every step, never both whole and
 * never none.
 *
 *   FADE_IN   0.25 seconds: 7.5 frames at 30 a second, 15 at 60, where the
 *             change was one frame; half of TAU, so the shadow arrives
 *             quicker than a change of the lights blends in. A free choice,
 *             not yet seen in the game.
 *
 * Not on the way out: a frame the gate refuses draws no map, so there is
 * nothing to fade out, and the blobs are drawn whole from that frame on. */
#define HD_SHADOW_FADE_IN		0.25f

/* How far the shadow has come in after `t` game seconds of running:
 * 3 f^2 - 2 f^3 for f = t / FADE_IN held to 0..1 - the curve that leaves 0
 * and reaches 1 with no slope, as a fog bulb's rim softens (hd_room.hlsl,
 * BulbIntensity). HDShadowDecay's curve, exp(-t / TAU), suits a change that
 * may be undone halfway; it reaches its end only in the limit, and a blob
 * weighted by it would never quite go. 0 at t <= 0, exactly 1 from FADE_IN
 * on, so a shadow that has run that long is drawn at the following's
 * strength to the bit. */
inline float HDShadowPresence(float t)
{
	const float f = hd_shadow_saturate(t / HD_SHADOW_FADE_IN);

	return f * f * (3.0f - 2.0f * f);
}

/* One frame of the arrival, once per drawn frame: `t` the game seconds the
 * shadow has run without a break, `runs` whether it runs in this frame, `dt`
 * the frame's game time. A frame it does not run in sets `t` back to 0; one
 * it runs in adds `dt` (a frame drawn twice and a paused game add nothing),
 * up to FADE_IN. The presence of the frame, HDShadowPresence(t). */
inline float HDShadowArrive(float* t, bool runs, float dt)
{
	if (!runs || !hd_shadow_finite(*t))
	{
		*t = 0.0f;
		return 0.0f;
	}

	if (dt > 0.0f && hd_shadow_finite(dt) && *t < HD_SHADOW_FADE_IN)
		*t += dt;

	return HDShadowPresence(*t);
}

/* A blob's vertex colour with its alpha - the top byte of the engine's
 * D3DCOLOR, which is the blob's darkness - times `weight`, rounded to the
 * nearest byte. A weight of 1 or more gives the colour as it is, so a blob
 * that does not fade is drawn as it always is, to the bit. */
inline unsigned long HDShadowBlobAlpha(unsigned long colour, float weight)
{
	if (!(weight < 1.0f))
		return colour;

	const float w = weight > 0.0f ? weight : 0.0f;
	const unsigned long a = (unsigned long)((float)(colour >> 24) * w + 0.5f);

	return (colour & 0x00FFFFFFul) | (a << 24);
}

/* --------------------------------------------------------------- the box -- */

/* The map's volume in the world.
 *
 *   fwd       the light's direction
 *   right     normalise(fwd x (1, 0, 0)): its length is sqrt(fwd.y^2 +
 *             fwd.z^2), at least cos(SLANT_MAX) = 0.57 while the slant
 *             limit holds, so it never vanishes and never flips sign as the
 *             direction turns
 *   up        fwd x right; (right, up, fwd) is a rotation of the world's
 *             axes
 *   centre    the middle between her lowest and highest node, its right and
 *             up components snapped to whole texels
 *   mid       the same middle before the snap
 *   lowest, highest  the y of her lowest node (the largest y) and of her
 *             highest
 *   extent    the box's width and height across the light, units
 *   depth     its length along the light, units
 *   map       texels a side of the map it is drawn into
 *
 * The last three are the box's own size, so one set of functions below
 * serves both maps: her box (HDShadowBoxMake) has BOX, DEPTH and MAP, the
 * second map's (HDShadowBoxAround) OTHERS_BOX, OTHERS_DEPTH and OTHERS_MAP.
 * Every division by them is the division by the table's number it stands
 * for, so her box gives the same bits through them as through the
 * numbers. */
struct HDShadowBox
{
	float right[3];
	float up[3];
	float fwd[3];
	float centre[3];
	float mid[3];
	float lowest;
	float highest;
	float extent;
	float depth;
	int map;
};

inline void HDShadowBasis(const float* dir, float* right, float* up, float* fwd)
{
	for (int i = 0; i < 3; i++)
		fwd[i] = dir[i];

	hd_shadow_unit(fwd);

	right[0] = 0.0f;
	right[1] = fwd[2];
	right[2] = -fwd[1];

	const float l = hd_shadow_len(right);

	if (l > 1.0e-6f)
	{
		right[1] /= l;
		right[2] /= l;
	}
	else
	{
		/* Only a direction along the world's x axis comes here, which the
		 * slant limit never lets through. */
		right[1] = 0.0f;
		right[2] = -1.0f;
	}

	up[0] = fwd[1] * right[2] - fwd[2] * right[1];
	up[1] = fwd[2] * right[0] - fwd[0] * right[2];
	up[2] = fwd[0] * right[1] - fwd[1] * right[0];
}

/* The box's centre out of its middle: the middle's right and up components
 * snapped to whole texels of the box's own map (extent / map units), its
 * component along the light kept. The snap: with a steady light every point
 * of the world keeps the same place inside its texel while she moves, so the
 * outline does not crawl. */
inline void hd_shadow_box_snap(HDShadowBox* b)
{
	const double texel = (double)b->extent / (double)b->map;
	double cr = 0.0, cu = 0.0, cf = 0.0;

	for (int i = 0; i < 3; i++)
	{
		cr += (double)b->mid[i] * b->right[i];
		cu += (double)b->mid[i] * b->up[i];
		cf += (double)b->mid[i] * b->fwd[i];
	}

	cr = floor(cr / texel + 0.5) * texel;
	cu = floor(cu / texel + 0.5) * texel;

	for (int i = 0; i < 3; i++)
		b->centre[i] = (float)(cr * b->right[i] + cu * b->up[i] + cf * b->fwd[i]);
}

/* Her box: for a direction and her nodes' world positions, three floats
 * each, BOX wide and high, DEPTH long, drawn into a map MAP texels a side.
 * False with no node. */
inline bool HDShadowBoxMake(HDShadowBox* b, const float* dir, const float* nodes, int count)
{
	if (!nodes || count <= 0)
		return false;

	int lo = 0, hi = 0;

	for (int n = 1; n < count; n++)
	{
		if (nodes[n * 3 + 1] > nodes[lo * 3 + 1])
			lo = n;

		if (nodes[n * 3 + 1] < nodes[hi * 3 + 1])
			hi = n;
	}

	HDShadowBasis(dir, b->right, b->up, b->fwd);

	for (int i = 0; i < 3; i++)
		b->mid[i] = 0.5f * (nodes[lo * 3 + i] + nodes[hi * 3 + i]);

	b->lowest = nodes[lo * 3 + 1];
	b->highest = nodes[hi * 3 + 1];
	b->extent = HD_SHADOW_BOX;
	b->depth = HD_SHADOW_DEPTH;
	b->map = HD_SHADOW_MAP;
	hd_shadow_box_snap(b);
	return true;
}

/* A box of another size round her box: the same light, the same basis, the
 * same middle and the same nodes' heights, `extent` wide and high, `depth`
 * long, drawn into a map of `map` texels a side, its centre snapped to whole
 * texels of that map the way hers is. With texels of her size (OTHERS_BOX /
 * OTHERS_MAP is BOX / MAP, 4 units) the centre is hers to the bit. The
 * second map's box is HDShadowBoxAround(her, OTHERS_BOX, OTHERS_DEPTH,
 * OTHERS_MAP). */
inline void HDShadowBoxAround(HDShadowBox* out, const HDShadowBox* her, float extent,
	float depth, int map)
{
	*out = *her;
	out->extent = extent;
	out->depth = depth;
	out->map = map;
	hd_shadow_box_snap(out);
}

/* Where a world point lies in the map: (u, v) in texture coordinates, 0..1
 * across the box, and depth 0..1 from the box's near side (towards the
 * light) to its far side. The box's centre lands on (0.5, 0.5, 0.5) and one
 * texel along `right` is 1 / map in u; along `up`, v falls.
 *
 * The receive side looks a point up with exactly this; the cast side
 * rasterises with HDShadowCastMatrix, which puts the point whose (u, v) this
 * gives into the texel whose centre is (u, v) - Direct3D 9's half-texel
 * rule, see there. */
inline void HDShadowMapUVD(const HDShadowBox* b, const float* p, float* out)
{
	const float d[3] = { p[0] - b->centre[0], p[1] - b->centre[1], p[2] - b->centre[2] };

	out[0] = 0.5f + hd_shadow_dot(d, b->right) / b->extent;
	out[1] = 0.5f - hd_shadow_dot(d, b->up) / b->extent;
	out[2] = 0.5f + hd_shadow_dot(d, b->fwd) / b->depth;
}

/* Whether a world point lies inside a box grown by `margin` on all six
 * sides - a part of its extent on each of the four across the light, and
 * the same part of its depth on the near and the far side along it: its u,
 * v and depth (HDShadowMapUVD) all within -margin..1 + margin, the edges
 * included; false for a point that is not a number. With margin 0 it is the
 * box itself. The second map asks it of a caster's place with OTHERS_MARGIN:
 * the place is one point, the caster's feet, and the parts above them lie
 * further in on two of the box's sides across the light, where they still
 * cast after the feet have left it, and nearer the light than the feet,
 * where they still cast after the feet have crossed the far side. */
inline bool HDShadowBoxHolds(const HDShadowBox* b, const float* p, float margin)
{
	float uvd[3];

	HDShadowMapUVD(b, p, uvd);

	for (int i = 0; i < 3; i++)
		if (!(uvd[i] >= -margin && uvd[i] <= 1.0f + margin))
			return false;

	return true;
}

/* A world point out of a view-space one, for a world-to-view of
 * view = R p + t with R a rotation, row-major. */
inline void HDShadowViewToWorld(const float* R, const float* t, const float* v, float* out)
{
	const float d[3] = { v[0] - t[0], v[1] - t[1], v[2] - t[2] };

	for (int j = 0; j < 3; j++)
		out[j] = R[0 * 3 + j] * d[0] + R[1 * 3 + j] * d[1] + R[2 * 3 + j] * d[2];
}

/* HDShadowMapUVD as one matrix on VIEW space: (vx, vy, vz, 1) * m = (u, v,
 * depth, 1), row-major with the row vector on the left, as a D3DMATRIX is
 * used. `R` and `t` are the frame's world-to-view, view = R p + t. Worked
 * in double: the camera and the box may stand far from the world's origin,
 * and the constants are differences of those large positions. */
inline void HDShadowViewToMap(const HDShadowBox* b, const float* R, const float* t, float* m)
{
	const float* axis[3] = { b->right, b->up, b->fwd };
	const double scale[3] = { 1.0 / b->extent, -1.0 / b->extent, 1.0 / b->depth };

	for (int i = 0; i < 16; i++)
		m[i] = 0.0f;

	for (int col = 0; col < 3; col++)
	{
		/* The axis in view space, R a; and dot(p - centre, a) for
		 * p = R^T (v - t) is dot(v, R a) - dot(t, R a) - dot(centre, a). */
		double ra[3];
		double c = 0.0;

		for (int i = 0; i < 3; i++)
		{
			ra[i] = (double)R[i * 3 + 0] * axis[col][0] +
				(double)R[i * 3 + 1] * axis[col][1] + (double)R[i * 3 + 2] * axis[col][2];
			c -= (double)t[i] * ra[i] + (double)b->centre[i] * axis[col][i];
		}

		for (int i = 0; i < 3; i++)
			m[i * 4 + col] = (float)(ra[i] * scale[col]);

		m[12 + col] = (float)(0.5 + c * scale[col]);
	}

	m[15] = 1.0f;
}

/* The cast side's projection: view space into the map's clip space,
 * row-major, row vector on the left - the matrix the model shader takes in
 * place of the frame's projection.
 *
 * Direct3D 9 centres a pixel on whole screen coordinates while a texel's
 * centre is at (i + 0.5) / map, so a clip x of 2u - 1 would land half a texel
 * off the lookup. The clip position is therefore
 *
 *   x = 2u - 1 - 1 / map,   y = 1 - 2v + 1 / map,   z = depth,   w = 1
 *
 * which the viewport turns into the screen position u * map - 0.5: the texel
 * whose centre is (u, v). `map` is the box's own. */
inline void HDShadowCastMatrix(const HDShadowBox* b, const float* R, const float* t, float* m)
{
	float uvd[16];

	HDShadowViewToMap(b, R, t, uvd);

	for (int row = 0; row < 4; row++)
	{
		m[row * 4 + 0] = 2.0f * uvd[row * 4 + 0];
		m[row * 4 + 1] = -2.0f * uvd[row * 4 + 1];
		m[row * 4 + 2] = uvd[row * 4 + 2];
		m[row * 4 + 3] = uvd[row * 4 + 3];
	}

	m[12] += -1.0f - 1.0f / (float)b->map;
	m[13] += 1.0f + 1.0f / (float)b->map;
}

/* -------------------------------------------------------------- the gate -- */

/* Why the projected shadow does not run in a frame, in the order asked:
 *
 *   OPTION        the option does not ask for it
 *   HD_OFF        the level is not drawn HD (hd_enable off among others)
 *   CLASSIC_LARA  Lara is not drawn as the HD composite
 *   CLASSIC_ROOM  the room she is in is drawn classic and keeps a classic
 *                 face she can stand on (hd_level.h, HDRoomShadowHD)
 *   TARGET        the map, its depth surface or its shaders do not exist on
 *                 this device - refused, or not made
 *
 * The target is asked last, because asking for it makes it. */
enum
{
	HD_SHADOW_GATE_OK = 0,
	HD_SHADOW_GATE_OPTION,
	HD_SHADOW_GATE_HD_OFF,
	HD_SHADOW_GATE_CLASSIC_LARA,
	HD_SHADOW_GATE_CLASSIC_ROOM,
	HD_SHADOW_GATE_TARGET
};

inline int HDShadowGate(long stored, bool hdLevel, bool hdLara, bool hdRoom, bool target)
{
	if (stored != HD_SHADOW_MODE_PROJECTED)
		return HD_SHADOW_GATE_OPTION;

	if (!hdLevel)
		return HD_SHADOW_GATE_HD_OFF;

	if (!hdLara)
		return HD_SHADOW_GATE_CLASSIC_LARA;

	if (!hdRoom)
		return HD_SHADOW_GATE_CLASSIC_ROOM;

	if (!target)
		return HD_SHADOW_GATE_TARGET;

	return HD_SHADOW_GATE_OK;
}

inline const char* HDShadowGateWord(int gate)
{
	switch (gate)
	{
	case HD_SHADOW_GATE_OK: return "ok";
	case HD_SHADOW_GATE_OPTION: return "option";
	case HD_SHADOW_GATE_HD_OFF: return "hd_off";
	case HD_SHADOW_GATE_CLASSIC_LARA: return "classic_lara";
	case HD_SHADOW_GATE_CLASSIC_ROOM: return "classic_room";
	case HD_SHADOW_GATE_TARGET: return "target";
	default: return "?";
	}
}

/* ------------------------------------------------------ the blob's mode -- */

/* What tomb5.shadow_mode draws. The stored value is the option as the menu
 * and the registry hold it; the EFFECTIVE value is what a frame draws with
 * it:
 *
 *   1 original, 2 circle, 3 faded circle, 4 PSX   the blob of S_PrintShadow,
 *                 for Lara and for every other item, in HD as without it
 *   5 projected   Lara's projected shadow where it runs; every blob that is
 *                 still drawn - an item's that does not give way to the
 *                 second map (HDShadowBlobGivesWay), and Lara's in a frame
 *                 the projected shadow does not run in - is the faded circle
 *
 * Any other stored value is handed on as it is, so S_PrintShadow draws for
 * it what it always drew (its circle). */
#define HD_SHADOW_MODE_ORIGINAL	1
#define HD_SHADOW_MODE_CIRCLE		2
#define HD_SHADOW_MODE_FADED		3
#define HD_SHADOW_MODE_PSX		4

/* The mode every blob is drawn with: the stored one, with 5 drawn as the
 * faded circle. The one function every reader of the option in
 * specificfx.cpp asks. */
inline long HDShadowBlobMode(long stored)
{
	return stored == HD_SHADOW_MODE_PROJECTED ? HD_SHADOW_MODE_FADED : stored;
}

/* The mode Lara is drawn with in a frame whose gate answered `gate`
 * (HDShadowGate): 5 exactly when the projected shadow runs, her blob's mode
 * otherwise. */
inline long HDShadowEffectiveMode(long stored, int gate)
{
	return stored == HD_SHADOW_MODE_PROJECTED && gate == HD_SHADOW_GATE_OK
		? HD_SHADOW_MODE_PROJECTED : HDShadowBlobMode(stored);
}

/* The log's word for an effective mode. S_PrintShadow draws its plain circle
 * for 2 and for every value it has no branch of its own for, so those are
 * all "circle". */
inline const char* HDShadowModeWord(long effective)
{
	switch (effective)
	{
	case HD_SHADOW_MODE_ORIGINAL: return "original";
	case HD_SHADOW_MODE_FADED: return "faded";
	case HD_SHADOW_MODE_PSX: return "psx";
	case HD_SHADOW_MODE_PROJECTED: return "projected";
	default: return "circle";
	}
}

/* ---------------------------------------------------------- the receive -- */

/* The HD room groups near her are drawn a second time with a pixel shader of
 * its own (hd_shader/hd_shadow_receive.hlsl): the same vertex shader, the
 * same constants and the same ranges as their opaque and alpha passes, the
 * depth test EQUAL, and the picture multiplied by
 *
 *     1 - strength * lookup * edge * height * facing
 *
 * In a frame whose second map was drawn the shader is the two-map variant
 * (Receive2PS in the same file), and the factor is
 *
 *     1 - max(strength * hers, strength * theirs)
 *
 * with `hers` the product above without the strength, and `theirs` the
 * second map's lookup weighted tap by tap with the distance term and the
 * fade along the light (HDShadowLookupBehind), times the edge term of the
 * second box and the same facing term. The max leaves a floor under two overlapping shadows as
 * dark as under one, and where nothing of the second map falls it is the
 * one-map factor itself.
 *
 * The functions below are those formulas on the processor, term by term, for
 * ModernTest and for the probes; the shader is their transcription. */

/* The edge term at (u, v): 1 inside the box, falling to 0 linearly over the
 * outer EDGE of its half width - 0.05 of the map's side at EDGE 0.1 - and 0
 * outside it, so the box's border is never seen. */
inline float HDShadowEdgeTerm(float u, float v)
{
	const float a = u < 1.0f - u ? u : 1.0f - u;
	const float b = v < 1.0f - v ? v : 1.0f - v;

	return hd_shadow_saturate((a < b ? a : b) / (0.5f * HD_SHADOW_EDGE));
}

/* The height term for a receiver `below` units below her lowest node (y
 * grows downward, so a receiver above that node has a negative `below`): 1
 * down to FULL, 0 from END on, linear between - the floor of a storey under
 * her takes no shadow through its ceiling. */
inline float HDShadowHeightTerm(float below)
{
	return hd_shadow_saturate((HD_SHADOW_END - below) / (HD_SHADOW_END - HD_SHADOW_FULL));
}

/* The facing term for n.l, the unit normal of the face against the unit
 * direction towards the light: 0 for a face turned away (a ceiling, the far
 * side of a wall), 1 from FACING on, linear between. */
inline float HDShadowFacingTerm(float ndotl)
{
	return hd_shadow_saturate(ndotl / HD_SHADOW_FACING);
}

/* The distance term of one tap of the second map, for a receiver `behind`
 * units further along the light than the caster that tap holds: 1 up to
 * OTHERS_FULL, 0 from OTHERS_END on, linear between. It stands in for her
 * height term, which measures from her lowest node: the second map holds
 * many casters at many heights, so each tap measures from its own. */
inline float HDShadowBehindTerm(float behind)
{
	return hd_shadow_saturate((HD_SHADOW_OTHERS_END - behind) /
		(HD_SHADOW_OTHERS_END - HD_SHADOW_OTHERS_FULL));
}

/* The fade along the light of one tap of the second map, for the depth
 * `tap` (0..1 of the box) its caster stands at: 0 at the box's near and far
 * side, 1 from OTHERS_FADE in from either, linear between - and exactly 1 in
 * the box's middle, so a caster there casts as it would without it. A
 * caster crossing either side thins out over OTHERS_FADE of the depth
 * instead of losing its shadow in one frame. Worked as a product with the
 * reciprocal, as the shader works it. */
inline float HDShadowFadeTerm(float tap)
{
	const float edge = tap < 1.0f - tap ? tap : 1.0f - tap;

	return hd_shadow_saturate(edge * (1.0f / HD_SHADOW_OTHERS_FADE));
}

/* The weights of the four taps along one axis for a position `f` (0..1)
 * inside its texel: 1 - f, 1, 1, f on the texels i - 1, i, i + 1 and i + 2,
 * i the texel whose centre is at or before the position. They sum to 3, so
 * the sixteen of a block sum to 9; each is linear in f and the block slides
 * over by one texel exactly when f wraps from 1 to 0, so the lookup is
 * continuous in the position - no step where a texel ends. */
inline void HDShadowTapWeights(float f, float* w)
{
	w[0] = 1.0f - f;
	w[1] = 1.0f;
	w[2] = 1.0f;
	w[3] = f;
}

/* The lookup of her map, as the receive shader does it, on a map of `size` x
 * `size` depths held row by row (row 0 at v = 0): the 4 x 4 texels round
 * (u, v), clamped at the map's border as the sampler's CLAMP clamps them; a
 * texel casts when it is not the clear value 1 and lies nearer the light
 * than `depth` minus the bias. The weighted share of casting texels, 0..1. */
inline float HDShadowLookup(const float* map, int size, float u, float v, float depth)
{
	if (!map || size <= 0)
		return 0.0f;

	const float s = u * (float)size - 0.5f;
	const float r = v * (float)size - 0.5f;
	const float bs = floorf(s);
	const float br = floorf(r);
	float wx[4], wy[4];

	HDShadowTapWeights(s - bs, wx);
	HDShadowTapWeights(r - br, wy);

	float limit = depth - HD_SHADOW_BIAS / HD_SHADOW_DEPTH;

	limit = limit < 1.0f ? limit : 1.0f;

	float sum = 0.0f;

	for (int j = 0; j < 4; j++)
		for (int k = 0; k < 4; k++)
		{
			int x = (int)bs + k - 1;
			int y = (int)br + j - 1;

			x = x < 0 ? 0 : (x >= size ? size - 1 : x);
			y = y < 0 ? 0 : (y >= size ? size - 1 : y);

			if (map[y * size + x] < limit)
				sum += wx[k] * wy[j];
		}

	return sum * (1.0f / 9.0f);
}

/* The lookup of the second map, as the two-map shader does it: the same
 * sixteen taps, weights, clamp and clear value as HDShadowLookup, the bias
 * in the units of a box `depthUnits` long (OTHERS_DEPTH for the second box),
 * and every casting tap weighted with the distance term of how far the
 * receiver lies behind that tap's caster, (depth - tap) * depthUnits units
 * (HDShadowBehindTerm), and with the fade of the tap's own depth along the
 * light (HDShadowFadeTerm). 0..1. */
inline float HDShadowLookupBehind(const float* map, int size, float u, float v, float depth,
	float depthUnits)
{
	if (!map || size <= 0 || !(depthUnits > 0.0f))
		return 0.0f;

	const float s = u * (float)size - 0.5f;
	const float r = v * (float)size - 0.5f;
	const float bs = floorf(s);
	const float br = floorf(r);
	float wx[4], wy[4];

	HDShadowTapWeights(s - bs, wx);
	HDShadowTapWeights(r - br, wy);

	float limit = depth - HD_SHADOW_BIAS / depthUnits;

	limit = limit < 1.0f ? limit : 1.0f;

	float sum = 0.0f;

	for (int j = 0; j < 4; j++)
		for (int k = 0; k < 4; k++)
		{
			int x = (int)bs + k - 1;
			int y = (int)br + j - 1;

			x = x < 0 ? 0 : (x >= size ? size - 1 : x);
			y = y < 0 ? 0 : (y >= size ? size - 1 : y);

			const float tap = map[y * size + x];

			if (tap < limit)
				sum += wx[k] * wy[j] * (HDShadowBehindTerm((depth - tap) * depthUnits) *
					HDShadowFadeTerm(tap));
		}

	return sum * (1.0f / 9.0f);
}

/* The factor the two-map shader writes, for her part and the second map's
 * part, each its lookup times its terms, both 0..1: 1 - strength times the
 * larger of the two, worked as the smaller of the two factors 1 - strength *
 * hers and 1 - strength * theirs, as the shader works it. Where the second
 * map gives nothing it is her factor itself; where two shadows overlap the
 * floor is as dark as under the darker one, never darker. */
inline float HDShadowReceiveFactor(float strength, float hers, float theirs)
{
	const float herFactor = 1.0f - strength * hers;
	const float theirFactor = 1.0f - strength * theirs;

	return herFactor < theirFactor ? herFactor : theirFactor;
}

/* The pixel registers of the receive shader, in this order from
 * HD_SHADOW_RECEIVE_FIRST on (hd_shadow_receive.hlsl's table):
 *
 *   +0 .. +2  u, v and depth as dot(float4(viewPos, 1), row): the columns of
 *             HDShadowViewToMap, so receiver and caster share one mapping
 *   +3        the height term before its saturate, as a plane in view space:
 *             (END - (y - lowest)) / (END - FULL), y the world y of the point
 *   +4        xyz the unit direction towards the light in view space, w
 *             1 / FACING
 *   +5        x the strength, y 1 / (EDGE / 2), z BIAS in depth units, w the
 *             map's side in texels
 *   +6        x 1 / the map's side
 *
 * The depth unit and the map's side are the box's own (extent, depth, map),
 * MAP and DEPTH for hers. `R` and `t` are the frame's world-to-view,
 * view = R p + t. */
#define HD_SHADOW_RECEIVE_FIRST		96
#define HD_SHADOW_RECEIVE_COUNT		7

inline void HDShadowReceiveConstants(const HDShadowBox* b, const float* R, const float* t,
	float strength, float* regs)
{
	float m[16];

	HDShadowViewToMap(b, R, t, m);

	for (int i = 0; i < HD_SHADOW_RECEIVE_COUNT * 4; i++)
		regs[i] = 0.0f;

	for (int c = 0; c < 3; c++)
	{
		regs[c * 4 + 0] = m[0 * 4 + c];
		regs[c * 4 + 1] = m[1 * 4 + c];
		regs[c * 4 + 2] = m[2 * 4 + c];
		regs[c * 4 + 3] = m[3 * 4 + c];
	}

	/* World y = sum_i R[i][1] (v_i - t_i); worked in double for the same
	 * reason as HDShadowViewToMap - the constant is a difference of large
	 * positions. */
	const double span = (double)HD_SHADOW_END - (double)HD_SHADOW_FULL;
	double ty = 0.0;

	for (int i = 0; i < 3; i++)
	{
		regs[12 + i] = (float)(-(double)R[i * 3 + 1] / span);
		ty += (double)t[i] * R[i * 3 + 1];
	}

	regs[15] = (float)(((double)HD_SHADOW_END + b->lowest + ty) / span);

	for (int i = 0; i < 3; i++)
		regs[16 + i] = -(R[i * 3 + 0] * b->fwd[0] + R[i * 3 + 1] * b->fwd[1] +
			R[i * 3 + 2] * b->fwd[2]);

	regs[19] = 1.0f / HD_SHADOW_FACING;
	regs[20] = strength;
	regs[21] = 1.0f / (0.5f * HD_SHADOW_EDGE);
	regs[22] = HD_SHADOW_BIAS / b->depth;
	regs[23] = (float)b->map;
	regs[24] = 1.0f / (float)b->map;
}

/* Her block made to give nothing, for a frame whose second map was drawn and
 * her own was not, and for an entry that does not take her map (her own
 * pieces): the u row reads -1 at every pixel, so her edge term, and with it
 * her part of the factor, is 0, and the two-map shader never reads her
 * map - neither one an earlier frame left in her target nor her own
 * outline on herself. */
inline void HDShadowReceiveWithoutHer(float* regs)
{
	regs[0] = 0.0f;
	regs[1] = 0.0f;
	regs[2] = 0.0f;
	regs[3] = -1.0f;
}

/* The two-map shader's further pixel registers, the second map's own, in
 * this order from HD_SHADOW_RECEIVE2_FIRST on, right after her block
 * (hd_shadow_receive.hlsl's table):
 *
 *   +0 .. +2  u, v and depth of the second map as dot(float4(viewPos, 1),
 *             row): the columns of HDShadowViewToMap of the second box
 *   +3        x BIAS in the second box's depth units, y the second map's
 *             side in texels, z 1 / that side
 *   +4        the distance term as a line in the depth difference of a tap:
 *             x depth / (OTHERS_END - OTHERS_FULL), y OTHERS_END /
 *             (OTHERS_END - OTHERS_FULL); the term is saturate(y - x *
 *             (depth - tap)); z 1 / OTHERS_FADE, the fade along the light
 *             saturate(min(tap, 1 - tap) * z) (HDShadowFadeTerm)
 *
 * The strength, the edge's scale and the direction towards the light are
 * her block's: one light throws both shadows. `R` and `t` are the frame's
 * world-to-view, view = R p + t. */
#define HD_SHADOW_RECEIVE2_FIRST	(HD_SHADOW_RECEIVE_FIRST + HD_SHADOW_RECEIVE_COUNT)
#define HD_SHADOW_RECEIVE2_COUNT	5

inline void HDShadowReceiveOthersConstants(const HDShadowBox* b, const float* R, const float* t,
	float* regs)
{
	float m[16];

	HDShadowViewToMap(b, R, t, m);

	for (int i = 0; i < HD_SHADOW_RECEIVE2_COUNT * 4; i++)
		regs[i] = 0.0f;

	for (int c = 0; c < 3; c++)
	{
		regs[c * 4 + 0] = m[0 * 4 + c];
		regs[c * 4 + 1] = m[1 * 4 + c];
		regs[c * 4 + 2] = m[2 * 4 + c];
		regs[c * 4 + 3] = m[3 * 4 + c];
	}

	const float span = HD_SHADOW_OTHERS_END - HD_SHADOW_OTHERS_FULL;

	regs[12] = HD_SHADOW_BIAS / b->depth;
	regs[13] = (float)b->map;
	regs[14] = 1.0f / (float)b->map;
	regs[16] = b->depth / span;
	regs[17] = HD_SHADOW_OTHERS_END / span;
	regs[18] = 1.0f / HD_SHADOW_OTHERS_FADE;
}

/* The second map's block made to give nothing, for an entry that does not
 * take the second map or a frame whose second map was not drawn: the u row
 * reads -1 at every pixel, so the second box's edge term, and with it the
 * second map's part of the factor, is 0 and its lookup is never taken - the
 * twin of HDShadowReceiveWithoutHer. */
inline void HDShadowReceiveWithoutOthers(float* regs)
{
	regs[0] = 0.0f;
	regs[1] = 0.0f;
	regs[2] = 0.0f;
	regs[3] = -1.0f;
}

/* The world box a receiver must touch to take any of the shadow: the box's
 * cross-section along the light (the box's own extent), from half its depth
 * on the light's side of her middle (where her casting parts end) down to
 * where her height term ends (lowest + END), as an axis-aligned box. A room
 * group whose own box misses it is not drawn again. Generous on purpose: the
 * corners of a slanted prism's box hold more than the prism. */
inline void HDShadowReach(const HDShadowBox* b, float* mins, float* maxs)
{
	const float half = 0.5f * b->extent;
	const float bottom = b->lowest + HD_SHADOW_END;
	const float spread = half * (fabsf(b->right[1]) + fabsf(b->up[1]));
	const float down = b->fwd[1] > 0.1f ? b->fwd[1] : 0.1f;
	const float nearF = -0.5f * b->depth;
	float farF = (bottom - b->centre[1] + spread) / down;

	farF = farF > nearF ? farF : nearF;

	for (int i = 0; i < 3; i++)
	{
		mins[i] = 3.0e38f;
		maxs[i] = -3.0e38f;
	}

	for (int corner = 0; corner < 8; corner++)
	{
		const float a = (corner & 1) ? half : -half;
		const float c = (corner & 2) ? half : -half;
		const float f = (corner & 4) ? farF : nearF;

		for (int i = 0; i < 3; i++)
		{
			const float p = b->centre[i] + a * b->right[i] + c * b->up[i] + f * b->fwd[i];

			mins[i] = p < mins[i] ? p : mins[i];
			maxs[i] = p > maxs[i] ? p : maxs[i];
		}
	}

	maxs[1] = maxs[1] < bottom ? maxs[1] : bottom;
}

/* The same for the second box: its cross-section along the light (its own
 * extent), from its near side to OTHERS_END beyond its far side - a caster
 * stands anywhere inside the box, and a receiver takes its shadow up to
 * OTHERS_END behind it along the light (HDShadowBehindTerm) - as an
 * axis-aligned box. Her height term plays no part. */
inline void HDShadowReachOthers(const HDShadowBox* b, float* mins, float* maxs)
{
	const float half = 0.5f * b->extent;
	const float nearF = -0.5f * b->depth;
	const float farF = 0.5f * b->depth + HD_SHADOW_OTHERS_END;

	for (int i = 0; i < 3; i++)
	{
		mins[i] = 3.0e38f;
		maxs[i] = -3.0e38f;
	}

	for (int corner = 0; corner < 8; corner++)
	{
		const float a = (corner & 1) ? half : -half;
		const float c = (corner & 2) ? half : -half;
		const float f = (corner & 4) ? farF : nearF;

		for (int i = 0; i < 3; i++)
		{
			const float p = b->centre[i] + a * b->right[i] + c * b->up[i] + f * b->fwd[i];

			mins[i] = p < mins[i] ? p : mins[i];
			maxs[i] = p > maxs[i] ? p : maxs[i];
		}
	}
}

/* Whether two axis-aligned boxes overlap, edges included. */
inline bool HDShadowBoxesTouch(const float* amin, const float* amax, const float* bmin,
	const float* bmax)
{
	for (int i = 0; i < 3; i++)
		if (amax[i] < bmin[i] || bmax[i] < amin[i])
			return false;

	return true;
}

/* ------------------------------------------------------ who else casts -- */

/* The other casters: every item the classic engine gives a blob (its
 * object's shadow_size is not 0) is drawn into the second map instead,
 * where it can be, and its blob gives way. One rule with two faces, as pure
 * functions of the facts of one item in one frame:
 *
 *   runs      the projected shadow runs in this frame (HDShadowFrameDecide)
 *   herStep   her main draw stepped the key light and made both boxes in
 *             this frame; she is drawn before the items, so it is known
 *             when an item's blob is asked about
 *   target    the second map, its depth surface and its shaders exist on
 *             this device
 *   hdModel   the item is drawn as an HD model in this frame - what
 *             HDObjectBegin decides for it
 *   mirrored  the item is a mirror's reflection
 *   blob      its object's shadow_size is not 0
 *   inside    its place lies inside the second box grown by OTHERS_MARGIN on
 *             all six sides (HDShadowBoxHolds)
 *
 * HDShadowBlobGivesWay says whether the item's blob is left out; the item
 * itself is then drawn into the second map exactly when it also has a blob
 * and stands inside the grown box (HDShadowOtherCasts) - whether the picture
 * draws it or not (HDShadowItemWalk). Inside it an item with a blob therefore
 * has always the one or the other, both only while the shadow comes in and
 * the blob goes out (HDShadowArrive); beyond it the item has
 * neither, and no part of a caster up to OTHERS_MARGIN of the box from its
 * place reaches into the box, so the edge term across the light and the fade
 * along it (HDShadowFadeTerm) have faded its projected shadow out already on
 * every side. */
inline bool HDShadowBlobGivesWay(bool runs, bool herStep, bool target, bool hdModel,
	bool mirrored)
{
	return runs && herStep && target && hdModel && !mirrored;
}

inline bool HDShadowOtherCasts(bool blob, bool runs, bool herStep, bool target, bool hdModel,
	bool mirrored, bool inside)
{
	return blob && inside && HDShadowBlobGivesWay(runs, herStep, target, hdModel, mirrored);
}

/* What game\draw.cpp's item draw does with an item's node walk
 * (DrawAnimatingItem), out of four facts of the item in the frame:
 *
 *   hdModel   it is drawn as an HD model (HDObjectBegin)
 *   clipped   its bounds lie outside the view or its portal window - the
 *             clip test answered 0, and the picture draws nothing of it
 *   gaveWay   its blob gave way to the second map (HDShadowBlobGivesWay)
 *   inside    its place lies inside the grown second box (HDShadowBoxHolds
 *             with OTHERS_MARGIN)
 *
 *   DRAWN    an item that is not clipped: its nodes are walked and drawn,
 *            HD or classic, as they always are
 *   UNSEEN   an HD item clipped away whose blob gave way and whose place lies
 *            inside: its nodes are walked for the second map alone - cast
 *            into it and drawn in no pass - so its shadow, which reaches 440
 *            to 900 units beyond its bounds along the light, lies on the
 *            floor before the item itself comes into the view; the classic
 *            engine draws the blob of such an item too, the blob's call stands
 *            before the clip
 *   NONE     every other clipped item: no node is walked */
enum
{
	HD_SHADOW_WALK_NONE = 0,
	HD_SHADOW_WALK_DRAWN,
	HD_SHADOW_WALK_UNSEEN
};

inline int HDShadowItemWalk(bool hdModel, bool clipped, bool gaveWay, bool inside)
{
	if (!clipped)
		return HD_SHADOW_WALK_DRAWN;

	return hdModel && gaveWay && inside ? HD_SHADOW_WALK_UNSEEN : HD_SHADOW_WALK_NONE;
}

/* ------------------------------------------------- who takes the shadow -- */

/* The HD models take the maps as the rooms do: after their solid pass every
 * entry of the frame list that can touch a map's reach is drawn again with
 * the model receive shader (hd_shadow_receive.hlsl, ModelReceivePS), the
 * depth test EQUAL, and the picture multiplied by the two-map factor. Which
 * maps an entry takes is a fact of what it is, never of whether it was cast
 * in this frame, so a creature does not change its look as it walks into
 * the second box:
 *
 *   LARA    her pieces, in third and in first person: the second map only -
 *           her own map would lay her outline on herself
 *   BLOB    an item whose object has a classic blob (its shadow_size; for a
 *           reflection the value the mirror's loop hands over before it zeroes
 *           the field for the draw): her map only - the second map is the one
 *           it is cast into
 *   OTHER   every other HD item - a door, a crate, a pickup: both maps
 *   NONE    what never takes a shadow: the inventory ring and the pickup
 *           display
 *
 * A mirror's reflection takes by the same class, through its mirror's
 * blocks (HDShadowMirrorView): `mirrored` says the entry is one,
 * `mirrorBlocks` that its mirror's blocks exist in this frame - its mirror is
 * known and seen from its room's side; a reflection without them takes
 * nothing. A map not drawn in this frame is taken by no one. The answer is
 * the maps the entry takes, HD_SHADOW_TAKES_HERS and HD_SHADOW_TAKES_OTHERS
 * or'd; 0 is an entry that is not drawn again. The block of a map an entry does not
 * take is silenced (HDShadowReceiveWithoutHer, HDShadowReceiveWithoutOthers),
 * so one shader serves every class and never reads a map it is not to read. */
enum
{
	HD_SHADOW_RECEIVER_NONE = 0,
	HD_SHADOW_RECEIVER_LARA,
	HD_SHADOW_RECEIVER_BLOB,
	HD_SHADOW_RECEIVER_OTHER
};

#define HD_SHADOW_TAKES_HERS		1
#define HD_SHADOW_TAKES_OTHERS		2

inline int HDShadowReceiverTakes(int receiver, bool herDrawn, bool othersDrawn, bool mirrored,
	bool mirrorBlocks)
{
	if (mirrored && !mirrorBlocks)
		return 0;

	int takes = 0;

	if (herDrawn && (receiver == HD_SHADOW_RECEIVER_BLOB || receiver == HD_SHADOW_RECEIVER_OTHER))
		takes |= HD_SHADOW_TAKES_HERS;

	if (othersDrawn && (receiver == HD_SHADOW_RECEIVER_LARA ||
		receiver == HD_SHADOW_RECEIVER_OTHER))
		takes |= HD_SHADOW_TAKES_OTHERS;

	return takes;
}

/* Whether a sphere touches an axis-aligned box, edges included: the
 * models' pass asks it of an entry's bounding sphere in the world and a
 * map's reach (HDShadowReach, HDShadowReachOthers). False for a sphere that
 * is not a number. */
inline bool HDShadowSphereTouches(const float* centre, float radius, const float* mins,
	const float* maxs)
{
	float sum = 0.0f;

	for (int i = 0; i < 3; i++)
	{
		const float c = centre[i];
		const float d = c < mins[i] ? mins[i] - c : (c > maxs[i] ? c - maxs[i] : 0.0f);

		if (!hd_shadow_finite(c))
			return false;

		sum += d * d;
	}

	return hd_shadow_finite(radius) && sum <= radius * radius;
}

/* ------------------------------------------------- the mirror's image -- */

/* A mirror (hd_render.h, HDMirror) shows the world reflected in its plane:
 * behind the glass stands a copy of the room, and the reflections of Lara
 * and the items are drawn into it (game\mirror.cpp). The shadows in that
 * image are the shadows in front of the glass, reflected - the same two maps,
 * looked up at the reflected place.
 *
 * For the plane of constant `axis` (0 x, 2 z) at `plane`, a point p' of the
 * image is the reflection of p = S p' + c, S the diagonal that negates the
 * axis and c the vector with 2 * plane on it. A view-space point v = R p' + t
 * of the image therefore stands for the world point S R^T (v - t) + c, and
 * that is the world point a world-to-view of
 *
 *     R' = R S,   t' = t - R S c
 *
 * gives back for v (HDShadowViewToWorld): every block of the receive made
 * with R', t' in place of the frame's R, t - her rows, the height plane, the
 * direction towards the light, the second map's rows - is the block for a
 * pixel of the image. It holds for both halves of the image alike: the
 * copy's art, drawn through the real view at p', and a reflection, whose
 * node matrices the mirror's view puts at the view position of p' for a
 * point p of its body. Applied twice it gives R, t back.
 *
 * `R` and `t` are the frame's world-to-view, view = R p + t; worked in double
 * for the reason HDShadowViewToMap is. */
inline void HDShadowMirrorView(const float* R, const float* t, int axis, float plane,
	float* outR, float* outT)
{
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			outR[i * 3 + j] = j == axis ? -R[i * 3 + j] : R[i * 3 + j];

		outT[i] = (float)((double)t[i] - 2.0 * (double)plane * (double)outR[i * 3 + axis]);
	}
}

/* The glass's plane as the pixel shader's clip of the copy, in view space: a
 * view-space point v with dot(float4(v, 1), out) >= 0 lies on the copy's side
 * of the plane, `side` -1 for a copy at smaller coordinates than the plane and
 * +1 for one at larger (HDMirror's own). The art of a mirror's room group
 * behind the plane is the copy; in front of it stands the room. */
inline void HDShadowMirrorPlane(const float* R, const float* t, int axis, float plane,
	int side, float* out)
{
	double d = -(double)plane;

	for (int i = 0; i < 3; i++)
	{
		out[i] = (float)side * R[i * 3 + axis];
		d -= (double)R[i * 3 + axis] * (double)t[i];
	}

	out[3] = (float)((double)side * d);
}

/* Whether the camera sees a mirror from its room's side: the camera's world
 * position, -R^T t, on the side of the plane away from the copy. A mirror
 * seen from behind shows no image (game\mirror.cpp, Draw_Mirror_Items, asks
 * the same of the classic view). */
inline bool HDShadowMirrorFaces(const float* R, const float* t, int axis, float plane, int side)
{
	double camera = 0.0;

	for (int i = 0; i < 3; i++)
		camera -= (double)R[i * 3 + axis] * (double)t[i];

	return (camera - (double)plane) * (double)side < 0.0;
}

/* The receive pixel register of the mirror's variant of the rooms' receive
 * (hd_shadow_receive.hlsl, MirrorReceivePS): the plane above, right after the
 * second map's block. */
#define HD_SHADOW_RECEIVE_MIRROR	(HD_SHADOW_RECEIVE2_FIRST + HD_SHADOW_RECEIVE2_COUNT)

/* ------------------------------------------------------------ the frame -- */

/* One drawn frame's projected shadow, filled by hd_laradraw.cpp at Lara's
 * main draw and read by the passes in hd_level.cpp.
 *
 *   runs        1 when the projected shadow runs in this frame
 *   gate        HD_SHADOW_GATE_*, why it does not
 *   target      the key light's target this frame
 *   dir, strength  the drawn key light (HDShadowFollow's), the strength
 *               times `presence`
 *   box         her map's volume
 *   viewToMap   view space -> (u, v, depth, 1) of her map, HDShadowViewToMap
 *   castClip    view space -> her map's clip space, HDShadowCastMatrix
 *   R, t        the frame's world-to-view, view = R p + t
 *   snapped     1 when the following snapped in this frame
 *   stepped     1 when her main draw stepped the key light in this frame and
 *               made both boxes; 0 from HDLaraDrawFrameReset on
 *   others      the second map's volume, HDShadowBoxAround her box
 *   othersViewToMap, othersCastClip  the same two matrices for the second
 *               map
 *   presence    how far the shadow has come in (HDShadowArrive): `strength`
 *               is the following's times it, and a blob that gives way is
 *               drawn with its alpha times 1 minus it; 0 in a frame it does
 *               not run in */
struct HDShadowFrame
{
	int runs;
	int gate;
	HDShadowKey target;
	float dir[3];
	float strength;
	HDShadowBox box;
	float viewToMap[16];
	float castClip[16];
	float R[9];
	float t[3];
	int snapped;
	int stepped;
	HDShadowBox others;
	float othersViewToMap[16];
	float othersCastClip[16];
	float presence;
};

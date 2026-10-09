/*
 * Modified 2026 by ChrisChanSan.
 *
 * The fade near the eye and the |N.V| term below are TROne's rules for its
 * glow material: TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931.
 * Translated to HLSL for Direct3D 9; not a line of the original is copied.
 * The edge fade, the end fade and the choice between the file normal and the
 * face normal are this file's own.
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

/* The glow shader: the pixel shader of .TRG material 2's draw calls in the
 * additive pass - the light shafts, the lamp cones and the window and candle
 * glows. hd_render.cpp binds it for those draw calls alone and the room pixel
 * shader for every other one; the vertex shader stays the room's own RoomVS.
 * This file therefore includes hd_room.hlsl and reads the same VS_OUT, the
 * same registers and the same BulbIntensity, and hd_room.hlsl itself is not
 * changed by it.
 *
 * Build. bshader.bat compiles it with
 *
 *     fxc /nologo /T ps_3_0 /E GlowPS /O3 /Vn kHDGlowPS /Fh hd_glow_ps.inc hd_glow.hlsl
 *
 * The stamp on hd_glow_ps.inc is the MD5 of this file. bshader.bat always
 * compiles every shader together, so an edit to hd_room.hlsl, which be.bat's
 * guard sees through the room shader's own stamp, regenerates this .inc too.
 *
 * What it draws is RoomPS in the additive pass - texel, alpha test, vertex
 * light, dynamic lights, the WaterTable term, the distance fog, the ceiling
 * of one, the fog bulbs' darkening, the caustic, the texel alpha, the weight
 * and the swing, in RoomPS's own lines - times four fades, each 1 where it
 * does not apply:
 *
 *   near    smoothstep over the first HD_GLOW_NEAR units from the eye, so a
 *           camera inside a shaft does not look through a stack of cards
 *           that fills the frame: from 3000 -1500 2200 in ANDREA2 room 131
 *           the shaft's additive pass covers 85.6 per cent of the frame.
 *   facing  |N.V|, so a card seen in its own plane - TITLE room 6 from 6013
 *           -151 3599, its streaks converging on the plane's vanishing point
 *           - fades out, and a cylinder of cards - RICH1 room 3's grate
 *           shaft, a flat 1.0 of light at every vertex - reads bright along
 *           its axis and dim at its rim, the chord profile of a solid column.
 *   edge    smoothstep over the outer 15 per cent of the u range the part
 *           uses, for a part that cuts its cards out of the middle of the
 *           page: ANDREA2's window shaft takes u 48..206 of page 3038, whose
 *           columns at 48 and 206 still carry 61 to 64 per cent of the
 *           brightest one, so every card side is a hard line.
 *   ends    smoothstep over up to 512 units at both ends of the asset along
 *           its page-v axis, for an asset whose cards are cut out of the
 *           page in v - the window ANDREA2's shaft falls from and the floor
 *           it ends on.
 *
 * What the level build decides per part - the u range, the normal, the
 * asset, its axis and its ends - is hd_render.cpp's BuildGlowParts.
 *
 * The rest of RoomPS is left out because it is zero on these draw calls: the
 * ripple (gRipple.y is non-zero for material 16 alone), the grazing gloss
 * (gSpec.x is 0 in the additive pass, MaterialGloss is asked for the two
 * surface passes only) and the bulbs' added colour (gPass.w is 0 in the
 * additive pass). */

#include "hd_room.hlsl"

/* How near the eye a glow fades out, in world units: one square. */
#define HD_GLOW_NEAR 1024.0f

/* The glow's own constants, per DRAW CALL, written by draw_pass for every
 * material 2 range (hd_render.cpp, GlowConstants):
 *
 *   gGlowAxis  xyz  the asset's page-v axis, a unit vector in the .TRG's own
 *                   frame, where a direction is the same in every room
 *              w    1 / the end fade's length
 *   gGlowEnds  x y  the asset's two ends along that axis, in the drawn
 *                   room's own frame (dot(roomPos, axis) is compared with
 *                   them)
 *              z w  the part's used u range, page units
 *   gGlowMode  x    1 / the edge fade's width, page units
 *              y    1 = |N.V| from the face normal, 0 = from the file normal
 *
 * A fade that does not apply is handed as a range the pixel never leaves:
 * axis 0 and ends -1 and 1 with a length of 1, u -1 and 2 with a width of 1.
 *
 * c77 to c79 are registers no other pixel shader of this renderer reads; the
 * room shader holds literals there (its literals sit in c0 to c15, c37 to
 * c39 and c72 to c80, hd_room_ps.inc), which belong to it alone while it is
 * bound, and the model shader's sit in c0 to c19 and c36 to c38. This
 * shader's own literals take c0 to c15 outside c4 and c10, c36 to c39 and
 * c72 to c76 - c36 is gRipple's register, which it does not read, so
 * draw_pass writes gRipple again when it puts the room shader back - and
 * none of the registers the renderer writes while it is bound
 * (hd_glow_ps.inc). */
float4 gGlowAxis : register(c77);
float4 gGlowEnds : register(c78);
float4 gGlowMode : register(c79);

/* smoothstep(0, 1, x) on a distance that is already scaled to the fade. */
float GlowRamp(float x)
{
	x = saturate(x);

	return x * x * (3.0f - 2.0f * x);
}

float4 GlowPS(VS_OUT input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	clip(texel.a - gPass.x);

	/* RoomPS's light, line for line: the vertex light and the dynamic lights
	 * in the classic path's 0..255 units, the WaterTable term, the distance
	 * fog, then the ceiling of one that the additive pass keeps (see "Where
	 * the ceiling is not optional" in hd_room.hlsl). */
	float3 f = input.light * 127.5f;

	[unroll]
	for (int i = 0; i < HD_MAX_LIGHTS; i++)
	{
		if (i >= (int)gCounts.x)
			continue;

		float4 a = gLights[i * 2 + 0];
		float4 b = gLights[i * 2 + 1];

		float3 delta = input.roomPos - a.xyz;
		float dist2 = dot(delta, delta);

		if (dist2 < a.w * a.w)
		{
			float dist = sqrt(max(dist2, 1e-12f));
			float ramp = b.w * (a.w - dist);
			float3 t = input.viewNrm * (delta / dist);

			f += b.rgb * (ramp * (1.0f - (t.x + t.y + t.z)) * 255.0f);
		}
	}

	f += input.waterAdd.xyz;

	// The fog's start, moved out under the water as in RoomPS.
	const float fogFrom = gFog.x + gUnderSigma.w * 1.0e30f;

	if (input.viewPos.z > fogFrom)
		f -= (input.viewPos.z - fogFrom) * gFog.y;

	float3 c = f * (2.0f / 255.0f);

	c = lerp(c, min(c, 1.0f), gPass.y);

	[loop]
	for (int j = 0; j < (int)gCounts.y; j++)
	{
		float4 a = gBulbs[j * 2 + 0];
		float4 b = gBulbs[j * 2 + 1];

		c -= BulbIntensity(input.viewPos, a, b) * (1.0f / 255.0f);
	}

	c = max(c, 0.0f);

	/* The caustic, projected by the face and faded by the pixel's
	 * footprint as RoomPS's plain variant does it (see CausticFace and
	 * HD_CAUSTIC_FOOT), taken before the branch where the gradient is
	 * defined. */
	const float3 footX = ddx(input.roomPos);
	const float3 footY = ddy(input.roomPos);
	const float sharp = saturate(2.0f - sqrt(max(dot(footX, footX),
		dot(footY, footY))) * (1.0f / HD_CAUSTIC_FOOT));

	if (input.waterAdd.w > 0.002f)
	{
		float3 world = input.roomPos + gRoomOrigin.xyz;
		c *= max(0.0f, 1.0f + HD_CAUSTIC_FLOOR * input.waterAdd.w * sharp *
			CausticFace(world, footX, footY, gAdd.y));
	}

	float3 lit = texel.rgb * c;

	lit *= lerp(1.0f, texel.a, gPass.y);

	float weight = gAdd.x;

	if (gAdd.z > 0.0f && gAdd.w > 0.0f)
		weight *= 1.0f + HD_RAY_SWING *
			sin((input.roomPos.x + input.roomPos.z) * HD_RAY_WAVELENGTH +
				gAdd.y * (6.2831853f / gAdd.z));

	lit *= lerp(1.0f, weight, gPass.y);

	// Under the water a glow is dimmed and scatters nothing (UnderWater).
	lit = UnderWater(lit, input.viewPos, 0.0f);

	/* The four fades. */
	float d = length(input.viewPos);
	float near = GlowRamp(d * (1.0f / HD_GLOW_NEAR));

	/* The file normal is the one the remaster baked, interpolated across the
	 * card; the face normal comes out of the screen derivatives of the view
	 * position and is flat over a triangle. The loader picks the face normal
	 * for a part whose file normals disagree with its faces - JOBY2's lamp
	 * cones, ANDREA3's window slabs of page 3232, ANDY2 room 34 - and the file
	 * normal everywhere else, where it is the smoother of the two. Both are
	 * computed and one is kept by the weight, because a derivative inside a
	 * branch is undefined. */
	float3 V = input.viewPos * (-1.0f / max(d, 1e-3f));
	float3 N = lerp(input.viewNrm,
		cross(ddx(input.viewPos), ddy(input.viewPos)), gGlowMode.y);
	float facing = abs(dot(N, V)) * rsqrt(max(dot(N, N), 1e-12f));

	float edge = GlowRamp((input.uv.x - gGlowEnds.z) * gGlowMode.x) *
		GlowRamp((gGlowEnds.w - input.uv.x) * gGlowMode.x);

	float along = dot(input.roomPos, gGlowAxis.xyz);
	float ends = GlowRamp((along - gGlowEnds.x) * gGlowAxis.w) *
		GlowRamp((gGlowEnds.y - along) * gGlowAxis.w);

	return float4(lit * (near * facing * edge * ends), texel.a * gSpec.w);
}

/*
 * Modified 2026 by ChrisChanSan.
 *
 * The lighting rule below - room light is the SUM of the two vertex colour
 * triples, the alpha test lives in the alpha pass ONLY, and the additive pass
 * carries its weight in the texture's alpha - is carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\render\game_renderer.cpp, the GLSL
 * shaders k_hd_vert / k_hd_frag). Translated to HLSL for Direct3D 9; not a
 * line of the original is copied. See THIRD-PARTY.md.
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

/* THE HD ROOM SHADER (#123, stage S1).
 *
 * HOW IT IS BUILT. There is no fxc step in tomb5.vcxproj on purpose - the
 * project must build on a machine with nothing but the Visual Studio C++
 * toolset. The bytecode is checked in beside this file as two C headers, and
 * this file is the source GPLv3 requires to sit next to them. Rebuild them
 * with
 *
 *     Preparation\Tools\Diagnose\bshader.bat
 *
 * which is exactly these two calls:
 *
 *     fxc /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh hd_room_vs.inc hd_room.hlsl
 *     fxc /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh hd_room_ps.inc hd_room.hlsl
 *
 * fxc.exe comes from the Windows SDK (Windows Kits\10\bin\<version>\x86).
 * Nothing of it is linked in: /Fh writes a plain byte array, and
 * CreateVertexShader / CreatePixelShader take it as it is. No d3dx, no
 * runtime compiler, no extra DLL in the package.
 *
 * THE VERTEX IS THE ONE ON DISK. A .TRG vertex is 20 bytes (Trg.h) and it
 * keeps that size and that meaning on the card - only the three colour bytes
 * of each pair are written back to front, for the reason below:
 *
 *   +0   SHORT4    x, y, z, sway     POSITION   room local, classic units
 *   +8   UBYTE4    nx, ny, nz, layer NORMAL     biased by 127; layer unused
 *                                               here (one page per draw call)
 *   +12  D3DCOLOR  b, g, r, u        COLOR0     first light triple + u
 *   +16  D3DCOLOR  b, g, r, v        COLOR1     second light triple + v
 *
 * D3DDECLTYPE_D3DCOLOR reads the four bytes as one little-endian DWORD in
 * 0xAARRGGBB order and hands the shader (R, G, B, A) - so the byte at +0 is
 * BLUE, the byte at +3 is alpha, and the shader gets the triple as .rgb the
 * right way round. hd_render.cpp writes the pair in that order (c1b, c1g,
 * c1r, u) and this file reads .rgb: exactly ONE swap between the file and
 * the pixel. Two swaps - the tempting "the bytes arrive as .bgra, so read
 * .bgr" - cancel out and paint the whole level with red and blue exchanged,
 * which looks like a plausible picture and is the reason this paragraph is
 * this long.
 *
 * The alpha byte, .a, is the texture coordinate, already divided by 255,
 * which is exactly the scale it wants (TROne: u / 255.0f). That the two
 * coordinates sit in two different colours' alpha bytes is the remaster's
 * own packing, not ours.
 *
 * THE LIGHT. light = colour1 * roomShadow + colour2, and the pixel is
 * texel * light. A SUM, not a product - TRForge measured that over all 144
 * .TRG files, and getting it wrong costs a factor of 2.65 in brightness.
 * roomShadow is 1 until shadow maps exist (stage S2).
 *
 * THE THREE PASSES are the three index runs of a .TRG part, in this order:
 *
 *   0 opaque    no alpha test. The alpha channel of an opaque tile carries a
 *               gloss/sparkle MASK, not coverage - testing on it blanks whole
 *               floors (TROne, measured on a TR1 snow floor).
 *   1 alpha     clip(a - 0.5), the punch-through pass.
 *   2 additive  colour * alpha, blended ONE/ONE with the depth write off.
 *
 * Both switches ride in one constant so the shader never branches: gPass.x is
 * 1 in the alpha pass and 0 otherwise (clip(0) discards nothing), gPass.y is
 * 1 in the additive pass and 0 otherwise.
 */

/* ------------------------------------------------------------ constants -- */

float4x4 gWorldViewProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = room shadow,
 * w = spare (stage S2 puts the distance fog start here). */
float4 gPass : register(c4);

sampler2D sPage : register(s0);

/* --------------------------------------------------------------- vertex -- */

struct VS_IN
{
	float4 pos    : POSITION;	/* x, y, z, sway */
	float4 normal : NORMAL;		/* nx, ny, nz, layer - 0..255, biased */
	float4 c0     : COLOR0;		/* .rgb light 1, .a = u */
	float4 c1     : COLOR1;		/* .rgb light 2, .a = v */
};

struct VS_OUT
{
	float4 pos   : POSITION;
	float2 uv    : TEXCOORD0;
	/* The light travels as a TEXCOORD, not as a COLOR: a colour register is
	 * clamped to 0..1 on shader model 3 hardware, and the sum of two triples
	 * reaches 2. Clamping it here would quietly cap every bright room. */
	float3 light : TEXCOORD1;
};

VS_OUT RoomVS(VS_IN input)
{
	VS_OUT output;

	output.pos = mul(float4(input.pos.xyz, 1.0f), gWorldViewProj);
	output.uv = float2(input.c0.a, input.c1.a);
	output.light = input.c0.rgb * gPass.z + input.c1.rgb;

	return output;
}

/* ---------------------------------------------------------------- pixel -- */

float4 RoomPS(VS_OUT input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	/* Alpha pass only - see the note about the gloss mask above. */
	clip(gPass.x * (texel.a - 0.5f));

	float3 lit = texel.rgb * input.light;

	/* Additive pass only: an unweighted glow washes the whole frame out. */
	lit *= lerp(1.0f, texel.a, gPass.y);

	return float4(lit, texel.a);
}

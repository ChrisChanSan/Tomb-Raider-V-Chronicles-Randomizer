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

/* The sky's own shaders, for RoomProbe's measurements of the sky
 * (hd_render.cpp, "The sky dome"); the game draws none of them.
 *
 * LayerVS and LayerPS draw one of the classic flat layers the way the classic
 * path draws it (specificfx.cpp, DrawFlatSky, draw type 4): a quad carrying
 * one page, the page times the vertex colour CalcColorSplit makes of the
 * layer's colour plus its specular half, the page's own alpha over what is
 * behind, no depth test. RoomProbe hands in either the level's classic sky
 * page or the HD page the game borrows for it.
 *
 * Build. bshader.bat compiles it,
 *
 *     fxc /nologo /T vs_3_0 /E LayerVS /O3 /Vn kHDSkyLayerVS
 *         /Fh hd_sky_layer_vs.inc hd_sky.hlsl
 *     fxc /nologo /T ps_3_0 /E LayerPS /O3 /Vn kHDSkyLayerPS
 *         /Fh hd_sky_layer_ps.inc hd_sky.hlsl
 *
 * and stamps the .inc files with the MD5 of this file.
 *
 * Constants. Vertex: c0 - c3 gLayerWvp, the quad's world, view and
 * projection. Pixel: c0 gLayerDiffuse (the colour's diffuse half, 0..1),
 * c1 gLayerSpecular (its specular half, added). Sampler s0, the page. */

float4x4 gLayerWvp : register(c0);
float4 gLayerDiffuse : register(c0);
float4 gLayerSpecular : register(c1);
sampler2D sLayerPage : register(s0);

struct LayerVertex
{
	float4 pos : POSITION;
	float2 uv : TEXCOORD0;
};

LayerVertex LayerVS(float3 pos : POSITION, float2 uv : TEXCOORD0)
{
	LayerVertex o;

	o.pos = mul(float4(pos, 1.0f), gLayerWvp);
	o.uv = uv;
	return o;
}

float4 LayerPS(float2 uv : TEXCOORD0) : COLOR
{
	const float4 texel = tex2D(sLayerPage, uv);

	return float4(saturate(texel.rgb * gLayerDiffuse.rgb +
		gLayerSpecular.rgb), texel.a);
}

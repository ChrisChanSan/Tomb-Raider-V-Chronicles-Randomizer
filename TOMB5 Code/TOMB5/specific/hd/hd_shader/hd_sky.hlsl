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

/* The cloud layer, RoomProbe's alone (-skycloud): a measurement of what a
 * volumetric layer would cost, never drawn by the game. CloudVS hands the
 * screen's corners through; CloudPS walks each view ray through a slab of
 * air above the camera, samples a density field built from hashed lattice
 * values, and composes what it meets front to back over the sky behind.
 *
 *     fxc /nologo /T vs_3_0 /E CloudVS /O3 /Vn kHDSkyCloudVS
 *         /Fh hd_sky_cloud_vs.inc hd_sky.hlsl
 *     fxc /nologo /T ps_3_0 /E CloudPS /O3 /Vn kHDSkyCloudPS
 *         /Fh hd_sky_cloud_ps.inc hd_sky.hlsl
 *
 * Constants, pixel side. c0 and c1 the view's x and y axes in world space
 * divided by the projection's scales on them, c2 its forward axis - the ray
 * of a pixel at (x, y) in -1..1, y up the screen, is c2 + x c0 + y c1, in
 * the engine's axes (y down). c3: x the slab's floor over the camera, y
 * its thickness, z the wind's travel along x (UpdateSky's counter, units),
 * w the lattice cell. c4: x the coverage (the field's value below which
 * the air is clear), y the density per unit path, z the fade towards the
 * horizon (the share of the ray's rise over which it comes in). c5 the
 * colour at the slab's top, c6 at its floor. */

float4 gCloudRight : register(c0);
float4 gCloudUp : register(c1);
float4 gCloudForward : register(c2);
float4 gCloudSlab : register(c3);
float4 gCloudShape : register(c4);
float4 gCloudTop : register(c5);
float4 gCloudFloor : register(c6);

struct CloudVertex
{
	float4 pos : POSITION;
	float2 screen : TEXCOORD0;
};

CloudVertex CloudVS(float2 corner : POSITION)
{
	CloudVertex o;

	o.pos = float4(corner, 1.0f, 1.0f);
	o.screen = corner;
	return o;
}

/* A lattice point's value in 0..1, a hash of its three integer
 * coordinates: folded into 0..63 per axis (the field repeats every 64 cells,
 * past any distance the slab is walked to), then scrambled by two rounds in
 * which each axis is multiplied by a linear function of another and folded
 * back into 0..1. Arithmetic only - ps_3_0 has no integers - and no
 * trigonometry. */
float CloudHash(float3 cell)
{
	float3 p = cell - 64.0f * floor(cell * (1.0f / 64.0f));

	p = frac(p * float3(0.1361f, 0.1187f, 0.1543f) + 0.317f);
	p = frac(p * (p.yzx * 47.31f + 13.73f));
	p = frac(p * (p.zxy * 31.17f + 7.91f));
	return frac(p.x + p.y * 1.618f + p.z * 2.718f);
}

// The field's value at `q` (cells): the eight hashed corners, smoothly
// interpolated.
float CloudValue(float3 q)
{
	const float3 c = floor(q);
	float3 f = q - c;

	f = f * f * (3.0f - 2.0f * f);

	const float a = lerp(CloudHash(c), CloudHash(c + float3(1, 0, 0)), f.x);
	const float b = lerp(CloudHash(c + float3(0, 1, 0)),
		CloudHash(c + float3(1, 1, 0)), f.x);
	const float d = lerp(CloudHash(c + float3(0, 0, 1)),
		CloudHash(c + float3(1, 0, 1)), f.x);
	const float e = lerp(CloudHash(c + float3(0, 1, 1)),
		CloudHash(c + float3(1, 1, 1)), f.x);

	return lerp(lerp(a, b, f.y), lerp(d, e, f.y), f.z);
}

#define HD_CLOUD_STEPS 16

float4 CloudPS(float2 screen : TEXCOORD0) : COLOR
{
	const float3 ray = normalize(gCloudForward.xyz + screen.x *
		gCloudRight.xyz + screen.y * gCloudUp.xyz);
	const float rise = -ray.y;

	clip(rise - 0.02f);

	const float t0 = gCloudSlab.x / rise;
	const float t1 = (gCloudSlab.x + gCloudSlab.y) / rise;
	const float dt = (t1 - t0) / HD_CLOUD_STEPS;
	float3 colour = 0.0f;
	float clear = 1.0f;

	[loop]
	for (int i = 0; i < HD_CLOUD_STEPS; i++)
	{
		const float t = t0 + (i + 0.5f) * dt;
		const float3 p = ray * t;
		const float h = (-p.y - gCloudSlab.x) / gCloudSlab.y;
		const float3 q = float3(p.x + gCloudSlab.z, p.y, p.z) / gCloudSlab.w;
		const float field = CloudValue(q) * 0.65f +
			CloudValue(q * 2.03f) * 0.35f;
		const float shape = 4.0f * h * (1.0f - h);
		const float density = saturate(field - gCloudShape.x) * shape *
			gCloudShape.y;
		const float taken = 1.0f - exp(-density * dt);

		colour += clear * taken * lerp(gCloudFloor.rgb, gCloudTop.rgb, h);
		clear *= 1.0f - taken;
	}

	const float fade = saturate((rise - 0.02f) / gCloudShape.z);

	return float4(colour * fade, (1.0f - clear) * fade);
}

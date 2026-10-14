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

/* The springs (hd_spring.h holds their rules): a level's springs built on
 * its water bodies, their rings handed to the sheets once a frame, and the
 * column and the bubbles drawn after the sheets with the water's cards.
 * The first part reads the level only through hd_render.h and is linked
 * by RoomProbe as well (compiled there with HD_SPRING_TOOL); the last part
 * is the engine's: the level's begin and end, the frame's two calls and
 * the device's loss. With tomb5.hd_water 0 no sheet is built and nothing
 * of this runs; with tomb5.hd_motion 0 the bubbles hold where they are,
 * the column stands and the sheets draw no ring. */
#ifndef HD_SPRING_TOOL
#include "../../tomb5/pch.h"
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#endif

#include "hd_spring.h"
#include "hd_render.h"

#include "hd_shader/hd_room_card_vs.inc"
#include "hd_shader/hd_spring_column_ps.inc"
#include "hd_shader/hd_spring_bubble_ps.inc"

#include <algorithm>
#include <stdio.h>

namespace
{
	std::vector<HDSpring> g_springs;
	// Per spring: the clock up to which its rings went to the sheets.
	std::vector<double> g_fed;
	// The clock the bubbles stand at while tomb5.hd_motion is off.
	double g_held = 0.0;

	IDirect3DDevice9* g_made_on = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_column_ps = 0;
	IDirect3DPixelShader9* g_bubble_ps = 0;

	// RoomProbe's timing of the draw, and what the last draw drew.
	bool g_timing = false;
	double g_gpu_ms = -1.0;
	int g_drawn_columns = 0;
	int g_drawn_bubbles = 0;
	// RoomProbe -springparts: 1 the columns, 2 the bubbles, 3 both.
	int g_parts = 3;

	/* How far a quad fades into what stands behind it, units: the
	 * cards' 64 halved for a bubble, a free choice; the column's soft
	 * edge where it meets the floor or the sheet behind it the same. */
	const float kSoft = 32.0f;
	/* The ratio's floor (hd_spring.hlsl, ColumnPS): a dark texel moved
	 * next to a black one would make a ratio of thousands; at 0.02 of
	 * white the bend of a dark floor stays a bend. And the pattern's cell,
	 * 48 units: two to five cells across the column. Free choices, set by
	 * eye on the round's RoomProbe pictures. */
	const float kRatioFloor = 0.02f;
	const float kCell = 48.0f;
	/* The bubbles' share over the water's own, and how far the rings
	 * go back when a spring first rings or the clock jumps: past the
	 * rings' 3.5 s life (hd_render.cpp, kRingLife), so a picture at a
	 * moment shows every ring alive then. */
	const float kBubbleShare = 0.9f;
	const double kRingBack = 4.0;

	// A card's corner, as hd_render.cpp's CardVertex: CardVS reads it.
	struct Vertex
	{
		float x, y, z;
		float u, v, cx, cz;
		float r, g, b, a;
		float light;
	};

	const DWORD kFVF = D3DFVF_XYZ | D3DFVF_TEX3 | D3DFVF_TEXCOORDSIZE4(0) |
		D3DFVF_TEXCOORDSIZE4(1) | D3DFVF_TEXCOORDSIZE1(2);

	void release_shaders()
	{
		if (g_vs) { g_vs->Release(); g_vs = 0; }
		if (g_column_ps) { g_column_ps->Release(); g_column_ps = 0; }
		if (g_bubble_ps) { g_bubble_ps->Release(); g_bubble_ps = 0; }
		g_made_on = 0;
	}

	// The shaders on the frame's device, made again when the device is new.
	bool shaders(IDirect3DDevice9* device)
	{
		if (device != g_made_on)
		{
			release_shaders();

			if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomCardVS,
				&g_vs)))
				g_vs = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringColumnPS, &g_column_ps)))
				g_column_ps = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringBubblePS, &g_bubble_ps)))
				g_bubble_ps = 0;

			g_made_on = device;
		}

		return g_vs && g_column_ps && g_bubble_ps;
	}

	// Row vectors: a point through a 4x4 that keeps w at one.
	void through(const float* m, const float* p, float* out)
	{
		for (int k = 0; k < 3; k++)
			out[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] +
				m[12 + k];
	}

	// a times b, row vectors.
	void multiply(const float* a, const float* b, float* out)
	{
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] +
					a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
	}

	// The world point a row-vector world-to-view matrix sends to the eye.
	void eye_of(const float* m, float* eye)
	{
		const float a = m[0], b = m[1], c = m[2];
		const float d = m[4], e = m[5], f = m[6];
		const float g = m[8], h = m[9], i = m[10];
		const float det = a * (e * i - f * h) - b * (d * i - f * g) +
			c * (d * h - e * g);

		if (fabsf(det) < 1.0e-12f)
		{
			eye[0] = eye[1] = eye[2] = 0.0f;
			return;
		}

		const float inv[9] =
		{
			(e * i - f * h) / det, (c * h - b * i) / det,
			(b * f - c * e) / det, (f * g - d * i) / det,
			(a * i - c * g) / det, (c * d - a * f) / det,
			(d * h - e * g) / det, (b * g - a * h) / det,
			(a * e - b * d) / det
		};

		for (int j = 0; j < 3; j++)
			eye[j] = -(m[12] * inv[j] + m[13] * inv[3 + j] +
				m[14] * inv[6 + j]);
	}

	/* The square of a body that holds world x, z and draws a sheet: its
	 * body, surface and depth. */
	bool square_under(float x, float z, int* body, int* surface, int* depth)
	{
		std::vector<HDRenderWaterSquare> squares;

		for (int b = 0; b < HDRenderWaterBodies(); b++)
		{
			const int n = HDRenderWaterBodySquares(b, 0, 0, 0);

			if (n <= 0)
				continue;

			squares.resize((size_t)n);
			HDRenderWaterBodySquares(b, &squares[0], n, 0);

			for (const HDRenderWaterSquare& q : squares)
				if (q.sheet && x >= (float)q.x && x < (float)(q.x + 1024) &&
					z >= (float)q.z && z < (float)(q.z + 1024))
				{
					*body = b;
					*surface = q.y;
					*depth = q.depth;
					return true;
				}
		}

		return false;
	}

	/* A spring onto its water: its body, the sheet over it and the floor
	 * under it (a table row's own floor where it gives one, else the
	 * square's). A ripple row of hd_water.txt whose source lies within a
	 * quarter of a square of the spring rings that place already: the
	 * spring then rings no more, so one place does not ring twice. False
	 * where no sheet lies over the place. */
	bool place(HDSpring& s, std::vector<int>& marked)
	{
		int body = -1, surface = 0, depth = 0;

		if (!square_under(s.x, s.z, &body, &surface, &depth))
			return false;

		s.body = body;
		s.surfaceY = surface;

		if (s.floorY <= surface)
			s.floorY = surface + depth;

		HDRenderWaterBody info;

		s.rings = 1;

		if (HDRenderWaterBodyInfo(body, &info) && info.spring &&
			std::find(marked.begin(), marked.end(), body) == marked.end())
		{
			const float dx = info.springAt[0] - s.x;
			const float dz = info.springAt[1] - s.z;

			s.rings = dx * dx + dz * dz <= 256.0f * 256.0f ? 0 : 1;
		}

		if (!info.spring)
		{
			HDRenderWaterBodySpring(body, s.x, s.z);
			marked.push_back(body);
		}

		return true;
	}
}

void HDSpringClear()
{
	g_springs.clear();
	g_fed.clear();
	g_held = 0.0;
}

/* The level's springs: the table's rows for this level, each laid onto
 * its water; a row whose place has no sheet is left out. */
void HDSpringBuild(const char* stem)
{
	HDSpringClear();

	if (!stem)
		return;

	std::vector<HDSpring> springs;

	for (const HDSpringRow& r : kHDSprings)
	{
		if (_stricmp(r.stem, stem))
			continue;

		HDSpring s = {};

		strncpy(s.stem, stem, sizeof(s.stem) - 1);
		s.room = r.room;
		s.x = r.x;
		s.z = r.z;
		s.floorY = r.floorY;
		s.strength = r.strength;
		s.period = r.period;
		s.body = -1;
		s.how = HD_SPRING_TABLE;
		s.seed = HDSpringSeed(stem, s.x, s.z);
		springs.push_back(s);
	}

	std::vector<int> marked;

	for (HDSpring& s : springs)
		if (place(s, marked))
			g_springs.push_back(s);

	g_fed.assign(g_springs.size(), -1.0e30);
}

// RoomProbe -spring: one more spring at world x, z, laid as the others.
bool HDSpringAdd(const char* stem, float x, float z, float strength)
{
	HDSpring s = {};
	std::vector<int> marked;

	strncpy(s.stem, stem ? stem : "", sizeof(s.stem) - 1);
	s.room = -1;
	s.x = x;
	s.z = z;
	s.strength = strength;
	s.period = HDSpringRulePeriod(strength);
	s.body = -1;
	s.how = HD_SPRING_FILE;
	s.seed = HDSpringSeed(s.stem, x, z);

	if (!place(s, marked))
		return false;

	g_springs.push_back(s);
	g_fed.push_back(-1.0e30);
	return true;
}

int HDSpringCount()
{
	return (int)g_springs.size();
}

const HDSpring* HDSpringAt(int i)
{
	return i >= 0 && i < (int)g_springs.size() ? &g_springs[(size_t)i] : 0;
}

/* The rings of the frame, once a frame before the sheets draw: every ring
 * a spring within kHDSpringRingReach of the eye started since the last
 * frame, and the small ring of each bubble that reached the sheet since
 * then and pops (HDSpringBubblePops), on the frame's clock. Nothing while
 * tomb5.hd_motion is off: the sheets draw no ring then. */
void HDSpringRings(float eyeX, float eyeY, float eyeZ)
{
	HDRenderSpringFrame f;

	if (g_springs.empty() || !HDRenderWaterSpringFrame(&f) || !f.motion)
		return;

	const double t = f.clock;

	for (size_t i = 0; i < g_springs.size(); i++)
	{
		const HDSpring& s = g_springs[i];
		double& fed = g_fed[i];

		if (fed > t + 0.1 || fed < t - kRingBack)
			fed = t - kRingBack;

		const float dx = s.x - eyeX, dz = s.z - eyeZ;
		const float dy = (float)s.surfaceY - eyeY;

		if (dx * dx + dy * dy + dz * dz >
			kHDSpringRingReach * kHDSpringRingReach)
		{
			fed = t;
			continue;
		}

		for (int j = (int)floor(fed / s.period) - 1;
			s.rings && j <= (int)floor(t / s.period) + 1; j++)
		{
			const double born = HDSpringRingBorn(s, j);

			if (born > fed && born <= t)
				HDRenderWaterRingAdd(s.x, (float)s.surfaceY, s.z,
					HDSpringRingSize(s.strength), born, 0);
		}

		const float rate = HDSpringBubbleRate(s);
		const double longest = (double)(s.floorY - s.surfaceY) /
			kHDSpringRiseMin;

		for (int k = (int)floor((fed - longest) * rate) - 1;
			rate > 0.0f && k <= (int)floor(t * rate) + 1; k++)
		{
			HDSpringBubbleAt b;
			float size = 0.0f;

			HDSpringBubble(s, k, t, &b);

			const double top = HDSpringBubbleBorn(s, k) + b.top;

			if (top <= fed || top > t || !HDSpringBubblePops(s, k, &size) ||
				!HDSpringBubble(s, k, top - 1.0e-3, &b))
				continue;

			HDRenderWaterRingAdd(b.x, (float)s.surfaceY, b.z, size, top, 1);
		}

		fed = t;
	}
}

void HDSpringTiming(int on, double* ms)
{
	if (on >= 0)
		g_timing = on != 0;

	if (ms)
		*ms = g_gpu_ms;
}

void HDSpringParts(int mask)
{
	g_parts = mask;
}

void HDSpringDrawn(int* columns, int* bubbles)
{
	if (columns)
		*columns = g_drawn_columns;

	if (bubbles)
		*bubbles = g_drawn_bubbles;
}

/* The column and the bubbles of every spring in view, after the sheets:
 * `toFrame` takes the world into the frame's world, as for the cards
 * (hd_render.cpp, HDRenderWaterCardsDraw). The column needs the frame's
 * copy; without it only the bubbles draw. */
void HDSpringFrameDraw(const float* toFrame)
{
	HDRenderSpringFrame f;

	g_drawn_columns = g_drawn_bubbles = 0;
	g_gpu_ms = -1.0;

	if (g_springs.empty() || !toFrame || !HDRenderWaterSpringFrame(&f) ||
		!f.width || !f.height || !shaders(f.device))
		return;

	if (f.motion)
		g_held = f.clock;

	const double t = f.motion ? (double)f.clock : g_held;
	float m[16];
	float eye[3];

	multiply(toFrame, f.view, m);
	eye_of(m, eye);

	const float focal = f.proj[0] * 0.5f * (float)f.width;
	std::vector<Vertex> column, bubble;

	for (const HDSpring& s : g_springs)
	{
		const float depth = (float)(s.floorY - s.surfaceY);
		const float bw[3] = { s.x, (float)s.floorY, s.z };
		const float tw[3] = { s.x, (float)s.surfaceY, s.z };
		float b[3], top[3];

		through(m, bw, b);
		through(m, tw, top);

		/* The column, a quad standing on the jet's line and turned to the
		 * eye, in eight rows up its height; left out where an end lies
		 * behind the eye. It fades as the eye looks along its line, where
		 * the quad turns edge-on, and as its half-width at mid height
		 * falls from kHDSpringFadeFrom to kHDSpringFadeTo pixels. */
		if ((g_parts & 1) && f.copy && depth > 0.0f && b[2] > 1.0f &&
			top[2] > 1.0f)
		{
			const float ax[3] = { top[0] - b[0], top[1] - b[1],
				top[2] - b[2] };
			const float mid[3] = { (b[0] + top[0]) * 0.5f,
				(b[1] + top[1]) * 0.5f, (b[2] + top[2]) * 0.5f };
			float side[3] = { ax[1] * mid[2] - ax[2] * mid[1],
				ax[2] * mid[0] - ax[0] * mid[2],
				ax[0] * mid[1] - ax[1] * mid[0] };
			const float sl = sqrtf(side[0] * side[0] + side[1] * side[1] +
				side[2] * side[2]);
			const float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] +
				ax[2] * ax[2]);
			const float ml = sqrtf(mid[0] * mid[0] + mid[1] * mid[1] +
				mid[2] * mid[2]);
			const float sine = al > 0.0f && ml > 0.0f ? sl / (al * ml) : 0.0f;
			const float across = HDSpringHalfWidth(s, depth * 0.5f) * focal /
				mid[2];
			float weight = s.strength *
				fminf(fmaxf((sine - 0.2f) / 0.3f, 0.0f), 1.0f) *
				fminf(fmaxf((across - kHDSpringFadeTo) /
				(kHDSpringFadeFrom - kHDSpringFadeTo), 0.0f), 1.0f);

			if (weight > 0.0f && sl > 0.0f)
			{
				for (int k = 0; k < 3; k++)
					side[k] /= sl;

				Vertex row[9][2];

				for (int r = 0; r <= 8; r++)
				{
					const float along = (float)r / 8.0f;
					const float h = depth * along;
					const float hw = HDSpringHalfWidth(s, h);

					for (int e = 0; e < 2; e++)
					{
						const float u = e ? 1.0f : -1.0f;
						Vertex& v = row[r][e];

						v.x = b[0] + ax[0] * along + side[0] * hw * u;
						v.y = b[1] + ax[1] * along + side[1] * hw * u;
						v.z = b[2] + ax[2] * along + side[2] * hw * u;
						v.u = u;
						v.v = along;
						v.cx = hw * u;
						v.cz = h;
						v.r = kHDSpringBend * s.strength * focal /
							fmaxf(v.z, 1.0f);
						v.g = weight;
						v.b = (float)fmod(t * kHDSpringJet, 4096.0 * kCell);
						v.a = kHDSpringNear;
						v.light = 1.0f;
					}
				}

				for (int r = 0; r < 8; r++)
				{
					const Vertex quad[6] = { row[r][0], row[r][1],
						row[r + 1][1], row[r][0], row[r + 1][1],
						row[r + 1][0] };

					column.insert(column.end(), quad, quad + 6);
				}

				g_drawn_columns++;
			}
		}

		// The bubbles alive at the clock, each a card facing the eye.
		const float rate = HDSpringBubbleRate(s);

		if (!(g_parts & 2) || rate <= 0.0f || depth <= 0.0f)
			continue;

		float tint[3] = { 1.0f, 1.0f, 1.0f }, light = 1.0f;
		float absorb[3] = { 0.0f, 0.0f, 0.0f };

		HDRenderWaterSpringLook(s.body, tint, &light, absorb);

		const double longest = (double)depth / kHDSpringRiseMin;

		for (int k = (int)floor((t - longest) * rate) - 1;
			k <= (int)floor(t * rate) + 1; k++)
		{
			HDSpringBubbleAt at;

			if (!HDSpringBubble(s, k, t, &at))
				continue;

			const float w[3] = { at.x, at.y, at.z };
			float p[3];

			through(m, w, p);

			if (p[2] < 1.0f || at.radius * focal / p[2] < 0.75f)
				continue;

			/* Seen from over the water, the water over the bubble on the
			 * eye's way to it takes its share per channel, as the sheet's
			 * absorption takes the floor's (hd_render.cpp, gWaterAbsorb);
			 * from under the water the shader's extinction does. */
			float keep[3] = { 1.0f, 1.0f, 1.0f };

			if (eye[1] < (float)s.surfaceY)
			{
				const float ex = at.x - eye[0], ey = at.y - eye[1],
					ez = at.z - eye[2];
				const float path = sqrtf(ex * ex + ey * ey + ez * ez) *
					(at.y - (float)s.surfaceY) / fmaxf(ey, 1.0f);

				for (int c = 0; c < 3; c++)
					keep[c] = expf(-path * absorb[c]);
			}

			static const float corner[6][2] = { { -1, -1 }, { 1, -1 },
				{ 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

			for (int c = 0; c < 6; c++)
			{
				Vertex v;

				v.x = p[0] + corner[c][0] * at.radius;
				v.y = p[1] + corner[c][1] * at.radius;
				v.z = p[2];
				v.u = v.v = 0.0f;
				v.cx = corner[c][0];
				v.cz = corner[c][1];
				v.r = tint[0] * keep[0];
				v.g = tint[1] * keep[1];
				v.b = tint[2] * keep[2];
				v.a = at.share * kBubbleShare * (keep[0] + keep[1] +
					keep[2]) / 3.0f;
				v.light = light;
				bubble.push_back(v);
			}

			g_drawn_bubbles++;
		}
	}

	if (column.empty() && bubble.empty())
		return;

	IDirect3DDevice9* d = f.device;

	// The device's state as the frame left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex[2] = { 0, 0 };
	IDirect3DVertexBuffer9* stream = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
		D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_FOGENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE
	};
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
	};
	const DWORD stage[2] = { 5, 6 };
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));
	DWORD saved[stateCount], savedSamp[2][sampCount];

	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	d->GetVertexDeclaration(&decl);
	d->GetFVF(&fvf);
	d->GetStreamSource(0, &stream, &streamOffset, &streamStride);

	for (int i = 0; i < stateCount; i++)
		d->GetRenderState(states[i], &saved[i]);

	for (int k = 0; k < 2; k++)
	{
		d->GetTexture(stage[k], &tex[k]);

		for (int i = 0; i < sampCount; i++)
			d->GetSamplerState(stage[k], samps[i], &savedSamp[k][i]);
	}

	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	if (g_timing &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3])))
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	// The projection alone in c0 - c3, transposed as the renderer does.
	float columns[16];

	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			columns[c * 4 + r] = f.proj[r * 4 + c];

	const bool soft = f.depth != 0;
	const float screen[4] = { 1.0f / (float)f.width, 1.0f / (float)f.height,
		f.proj[10], -f.proj[14] };
	const float mode[4] = { soft ? 1.0f : 0.0f, 1.0f / kSoft, kRatioFloor,
		kCell };

	d->SetVertexShader(g_vs);
	d->SetFVF(kFVF);
	d->SetVertexShaderConstantF(0, columns, 4);
	d->SetPixelShaderConstantF(92, screen, 1);
	d->SetPixelShaderConstantF(93, mode, 1);
	d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	d->SetRenderState(D3DRS_FOGENABLE, FALSE);
	d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
	d->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	for (int k = 0; k < 2; k++)
	{
		const DWORD filter = k ? D3DTEXF_POINT : D3DTEXF_LINEAR;

		d->SetSamplerState(stage[k], D3DSAMP_MINFILTER, filter);
		d->SetSamplerState(stage[k], D3DSAMP_MAGFILTER, filter);
		d->SetSamplerState(stage[k], D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		d->SetSamplerState(stage[k], D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		d->SetSamplerState(stage[k], D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		d->SetSamplerState(stage[k], D3DSAMP_SRGBTEXTURE, FALSE);
	}

	d->SetTexture(5, f.copy);
	d->SetTexture(6, f.depth);

	if (!column.empty())
	{
		d->SetPixelShader(g_column_ps);
		d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
		d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(column.size() / 3),
			&column[0], sizeof(Vertex));
	}

	if (!bubble.empty())
	{
		d->SetPixelShader(g_bubble_ps);
		d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(bubble.size() / 3),
			&bubble[0], sizeof(Vertex));
	}

	if (q[3])
	{
		UINT64 a = 0, b = 0, freq = 0;
		BOOL disjoint = TRUE;

		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		q[3]->Issue(D3DISSUE_END);

		while (q[0]->GetData(&disjoint, sizeof(disjoint),
			D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[3]->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
			S_FALSE) {}

		if (!disjoint && freq && b > a)
			g_gpu_ms = (double)(b - a) * 1000.0 / (double)freq;
	}

	for (int i = 0; i < 4; i++)
		if (q[i])
			q[i]->Release();

	for (int i = 0; i < stateCount; i++)
		d->SetRenderState(states[i], saved[i]);

	for (int k = 0; k < 2; k++)
	{
		for (int i = 0; i < sampCount; i++)
			d->SetSamplerState(stage[k], samps[i], savedSamp[k][i]);

		d->SetTexture(stage[k], tex[k]);

		if (tex[k])
			tex[k]->Release();
	}

	d->SetStreamSource(0, stream, streamOffset, streamStride);
	d->SetFVF(fvf);
	d->SetVertexDeclaration(decl);
	d->SetVertexShader(vs);
	d->SetPixelShader(ps);

	if (stream) stream->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
}

void HDSpringRelease()
{
	release_shaders();
}

#ifndef HD_SPRING_TOOL
/* ------------------------------------------------------------ the engine */
#include "hd_level.h"			// HDLevelActive
#include "../function_stubs.h"		// Log, gLogEnabled
#include "../../tomb5/tomb5.h"		// tomb5.hd_water
#include "../../game/camera.h"		// camera.pos, the eye

/* At the level's build, after the sheets and before the life, which
 * reads the springs as its bodies' springs (HDRenderWaterBody::spring). */
void HDSpringLevelBegin(const char* stem)
{
	HDSpringClear();

	if (!HDLevelActive() || !tomb5.hd_water || !HDRenderWaterSheetsBuilt())
		return;

	HDSpringBuild(stem);
}

void HDSpringLevelEnd()
{
	HDSpringClear();
}

// Once a frame, after the game's ripples went to the sheets.
void HDSpringFeed()
{
	if (!HDLevelActive())
		return;

	HDSpringRings((float)camera.pos.x, (float)camera.pos.y,
		(float)camera.pos.z);
}

// After the water's cards, with their matrix (hd_level.cpp, WorldToFrame).
void HDSpringDraw(const float* toFrame)
{
	if (!HDLevelActive())
		return;

	HDSpringFrameDraw(toFrame);
}

// The shaders belong to the device that is going away.
void HDSpringDeviceLost()
{
	HDSpringRelease();
}
#endif

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
#include "hd_light.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
	struct WaterTab
	{
		signed char shimmer;
		signed char choppy;
		unsigned char random;
		unsigned char abs;
	};

	inline long ClrR(unsigned long c) { return (long)((c >> 16) & 0xFF); }
	inline long ClrG(unsigned long c) { return (long)((c >> 8) & 0xFF); }
	inline long ClrB(unsigned long c) { return (long)(c & 0xFF); }

	inline float Square(float v) { return v * v; }

	// (long)v >> shift: truncate towards zero, then an ARITHMETIC shift.
	inline long ShiftDown(float v, int bits)
	{
		return (long)v >> bits;
	}

	inline const WaterTab* Row(const void* table)
	{
		static const WaterTab zero[64] = {};
		return table ? (const WaterTab*)table : zero;
	}

	// The WaterTable's two step lookup (drawroom.cpp:357-361). `wibbleShift`
	// is 2 for everything but the Red Alert gas rooms, which use 3.
	const WaterTab& Sample(const HDParityScene* s, float x, float y, float z, int wibbleShift)
	{
		const WaterTab* row = Row(s->waterTable);

		const long ix = ShiftDown(x + s->originX, 6);
		const long iy = ShiftDown(y + s->originY, 6);
		const long iz = ShiftDown(z + s->originZ, 7);
		const unsigned char rnd = row[(ix + iy + iz) & 0x3F].random;

		return row[(((long)s->wibble >> wibbleShift) + rnd) & 0x3F];
	}

	void Transform(const float* m, float x, float y, float z, float out[3])
	{
		out[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
		out[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
		out[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
	}

	void Rotate(const float* m, float x, float y, float z, float out[3])
	{
		out[0] = x * m[0] + y * m[4] + z * m[8];
		out[1] = x * m[1] + y * m[5] + z * m[9];
		out[2] = x * m[2] + y * m[6] + z * m[10];
	}

	/* The classic path's fog bulb intensity (drawroom.cpp:553-668), in the
	 * original's own shape - including the branch that leaves `vec` holding
	 * the unit direction and so returns one density for a bulb behind the
	 * camera. */
	/* `soften` is the shader's rim weight and NOTHING the classic path has:
	 * false gives the classic answer to the bit, true gives
	 * what hd_room.hlsl now computes. The two call sites below pass one each,
	 * which is what makes -paritycheck able to say how far the deviation
	 * reaches - see the note in the shader for why it is there. */
	float BulbIntensity(const float stash[3], const HDFogBulb& bulb,
		bool soften = false)
	{
		const float rad = bulb.rad;
		const float sqrad = rad * rad;
		const float P[3] = { bulb.x, bulb.y, bulb.z };
		const float sqlen = Square(P[0]) + Square(P[1]) + Square(P[2]);

		/* The classic path's 45 degree cull (drawroom.cpp:632) belongs to the
		 * classic answer and to nothing else: in a pixel shader it draws a
		 * hard line down the side of a 16:9 or first person frame - see the
		 * long note beside it in hd_room.hlsl. What the shader keeps of it is
		 * the exact half: a sphere entirely behind the camera plane meets no
		 * forward ray. */
		if (soften)
		{
			if (P[2] + rad <= 0)
				return 0;
		}
		else if (!(stash[2] + rad > 0 &&
			fabsf(stash[0]) - rad < fabsf(stash[2]) &&
			fabsf(stash[1]) - rad < fabsf(stash[2])))
			return 0;

		float vec[3] = { 0, 0, 0 };
		float vec2[3] = { 0, 0, 0 };
		float val = Square(P[0] - stash[0]) + Square(P[1] - stash[1]) + Square(P[2] - stash[2]);

		if (sqlen < sqrad)
		{
			if (val < sqrad)
			{
				vec2[0] = stash[0];
				vec2[1] = stash[1];
				vec2[2] = stash[2];
			}
			else
			{
				val = 1.0f / sqrtf(Square(stash[0]) + Square(stash[1]) + Square(stash[2]));
				vec2[0] = val * stash[0];
				vec2[1] = val * stash[1];
				vec2[2] = val * stash[2];

				const float val2 = P[0] * vec2[0] + P[1] * vec2[1] + P[2] * vec2[2];
				val = val2 + sqrtf(sqrad - (sqlen - Square(val2)));
				vec2[0] *= val;
				vec2[1] *= val;
				vec2[2] *= val;
			}
		}
		else if (val < sqrad)
		{
			vec2[0] = stash[0];
			vec2[1] = stash[1];
			vec2[2] = stash[2];

			val = 1.0f / sqrtf(Square(stash[0]) + Square(stash[1]) + Square(stash[2]));
			vec[0] = val * stash[0];
			vec[1] = val * stash[1];
			vec[2] = val * stash[2];

			const float val2 = P[0] * vec[0] + P[1] * vec[1] + P[2] * vec[2];
			val = val2 - sqrtf(sqrad - (sqlen - Square(val2)));
			vec[0] *= val;
			vec[1] *= val;
			vec[2] *= val;
		}
		else
		{
			val = Square(stash[0]) + Square(stash[1]) + Square(stash[2]);
			float val2 = 1.0f / sqrtf(val);
			vec[0] = val2 * stash[0];
			vec[1] = val2 * stash[1];
			vec[2] = val2 * stash[2];

			val2 = P[0] * vec[0] + P[1] * vec[1] + P[2] * vec[2];

			if (val2 > 0)
			{
				float val3 = Square(val2);

				if (val > val3)
				{
					val = sqlen - val3;

					if (val < sqrad)
					{
						val3 = sqrtf(sqrad - val);

						val = val2 - val3;
						vec2[0] = val * vec[0];
						vec2[1] = val * vec[1];
						vec2[2] = val * vec[2];

						val = val2 + val3;
						vec[0] *= val;
						vec[1] *= val;
						vec[2] *= val;
					}
					else
					{
						vec[0] = vec[1] = vec[2] = 0;
						vec2[0] = vec2[1] = vec2[2] = 0;
					}
				}
			}
		}

		const float chord = sqrtf(Square(vec2[0] - vec[0]) +
			Square(vec2[1] - vec[1]) + Square(vec2[2] - vec[2]));

		if (!soften)
			return chord * bulb.density;

		// The same three lines the shader runs - see hd_room.hlsl, where
		// the note beside kRim says why it is 0.35 and not the 1.0 first
		// tried.
		const float kRim = 0.35f;
		const float span = 2.0f * rad > 1e-4f ? 2.0f * rad : 1e-4f;
		float t = chord / span;

		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		t = t / kRim;
		t = t < 0 ? 0 : (t > 1 ? 1 : t);

		return chord * bulb.density * (t * t * (3.0f - 2.0f * t));
	}

	// Which of the three baked marks the room flags select, and whether the
	// vertex has to sit on the surface plane - the shader's dot(bits, sel1)
	// and the gate beside it, on the processor.
	float ShimmerOn(const HDParityScene* s, const HDParityVertex* v, float y)
	{
		const float bits[4] =
		{
			(float)(v->bits & 1),
			(float)((v->bits >> 1) & 1),
			(float)((v->bits >> 2) & 1),
			1.0f		// the constant fourth flag byte
		};

		float on = bits[0] * s->sel1[0] + bits[1] * s->sel1[1] +
			bits[2] * s->sel1[2] + bits[3] * s->sel1[3];

		if (s->surfaceGate)
			on *= fabsf(y - s->surfaceY) < 0.5f ? 1.0f : 0.0f;

		return on;
	}
}

/* ------------------------------------------------- the classic reference -- */

void HDLightClassic(const HDParityScene* scene, const HDParityVertex* v,
	HDLightResult* out)
{
	memset(out, 0, sizeof(*out));

	float pos[3] = { v->x, v->y, v->z };

	if (scene->choppy && (v->bits & 1))
		pos[1] += Sample(scene, pos[0], pos[1], pos[2], 2).choppy;

	float stash[3];
	Transform(scene->view, pos[0], pos[1], pos[2], stash);

	float n[3];
	Rotate(scene->view, v->nx, v->ny, v->nz, n);

	float fR = 0, fG = 0, fB = 0;
	long sR = 0, sG = 0, sB = 0;

	for (int j = 0; j < scene->lightCount; j++)
	{
		const HDLightSource& light = scene->lights[j];
		float lPos[3] =
		{
			pos[0] - light.x,
			pos[1] - light.y,
			pos[2] - light.z
		};

		float val = Square(lPos[0]) + Square(lPos[1]) + Square(lPos[2]);

		if (val < Square(light.falloff))
		{
			val = sqrtf(val);
			const float val2 = (1.0f / light.falloff) * (light.falloff - val);

			lPos[0] = n[0] * (1.0f / val * lPos[0]);
			lPos[1] = n[1] * (1.0f / val * lPos[1]);
			lPos[2] = n[2] * (1.0f / val * lPos[2]);

			val = val2 * (1.0f - (lPos[0] + lPos[1] + lPos[2]));
			fR += light.r * val;
			fG += light.g * val;
			fB += light.b * val;
		}
	}

	fR = ClrR(v->prelight) + fR * 255.0f;
	fG = ClrG(v->prelight) + fG * 255.0f;
	fB = ClrB(v->prelight) + fB * 255.0f;

	long cR = (long)fR;
	long cG = (long)fG;
	long cB = (long)fB;

	const float shimmerOn = ShimmerOn(scene, v, pos[1]);
	const bool onSurface = fabsf(pos[1] - scene->surfaceY) < 0.5f;

	if (scene->underwater)
	{
		const long col = -2 * Sample(scene, pos[0], pos[1], pos[2], 2).choppy;
		cR += col;
		cG += col;
		cB += col;
	}
	else if (shimmerOn > 0.5f)
	{
		const WaterTab& e = Sample(scene, pos[0], pos[1], pos[2], 2);
		const long col = (e.shimmer + e.abs) << 3;
		cR += col;
		cG += col;
		cB += col;
	}
	else if (scene->redAlert && !onSurface)
	{
		const WaterTab& e = Sample(scene, pos[0], pos[1], pos[2], 3);
		const long col = (e.shimmer + e.abs) << 3;
		cG += labs(col);
	}

	if (scene->distanceFogStart > 0 && stash[2] > scene->distanceFogStart)
	{
		const float val = (stash[2] - scene->distanceFogStart) *
			((1.0f / scene->distanceFogStart) * 255.0f);
		cR -= (long)val;
		cG -= (long)val;
		cB -= (long)val;
	}

	out->f[0] = (float)cR;
	out->f[1] = (float)cG;
	out->f[2] = (float)cB;

	if (cR - 128 <= 0) cR <<= 1; else { sR = (cR - 128) >> 1; cR = 255; }
	if (cG - 128 <= 0) cG <<= 1; else { sG = (cG - 128) >> 1; cG = 255; }
	if (cB - 128 <= 0) cB <<= 1; else { sB = (cB - 128) >> 1; cB = 255; }

	if (scene->bulbCount)
	{
		long col = 0;

		for (int j = 0; j < scene->bulbCount; j++)
		{
			const float intensity = BulbIntensity(stash, scene->bulbs[j]);

			if (intensity)
			{
				col += (long)intensity;
				sR += (long)(intensity * scene->bulbs[j].r);
				sG += (long)(intensity * scene->bulbs[j].g);
				sB += (long)(intensity * scene->bulbs[j].b);

				out->bulbDensity += intensity;
				out->bulbColour[0] += intensity * scene->bulbs[j].r;
				out->bulbColour[1] += intensity * scene->bulbs[j].g;
				out->bulbColour[2] += intensity * scene->bulbs[j].b;
			}
		}

		cR -= col;
		cG -= col;
		cB -= col;
	}

	if (sR > 255) sR = 255; else if (sR < 0) sR = 0;
	if (sG > 255) sG = 255; else if (sG < 0) sG = 0;
	if (sB > 255) sB = 255; else if (sB < 0) sB = 0;
	if (cR > 255) cR = 255; else if (cR < 0) cR = 0;
	if (cG > 255) cG = 255; else if (cG < 0) cG = 0;
	if (cB > 255) cB = 255; else if (cB < 0) cB = 0;

	out->diffuse[0] = cR / 255.0f;
	out->diffuse[1] = cG / 255.0f;
	out->diffuse[2] = cB / 255.0f;
	out->specular[0] = sR / 255.0f;
	out->specular[1] = sG / 255.0f;
	out->specular[2] = sB / 255.0f;
}

/* ------------------------------------------------ the shader, emulated ---- */

void HDLightShader(const HDParityScene* scene, const HDParityVertex* v,
	HDLightResult* out)
{
	memset(out, 0, sizeof(*out));

	float pos[3] = { v->x, v->y, v->z };

	// RoomVS, step 1.
	if (scene->choppy && (v->bits & 1))
		pos[1] += Sample(scene, pos[0], pos[1], pos[2], 2).choppy;

	// RoomVS, step 2.
	const float atSurface = fabsf(pos[1] - scene->surfaceY) < 0.5f ? 1.0f : 0.0f;
	const float shimmerOn = ShimmerOn(scene, v, pos[1]);
	const float gasOn = (scene->redAlert ? 1.0f : 0.0f) * (1.0f - atSurface);

	float waterAdd[3] = { 0, 0, 0 };

	if (scene->underwater)
	{
		const float col = -2.0f * Sample(scene, pos[0], pos[1], pos[2], 2).choppy;
		waterAdd[0] = waterAdd[1] = waterAdd[2] = col;
	}
	else if (shimmerOn > 0.5f)
	{
		const WaterTab& e = Sample(scene, pos[0], pos[1], pos[2], 2);
		const float col = ((float)e.shimmer + (float)e.abs) * 8.0f;
		waterAdd[0] = waterAdd[1] = waterAdd[2] = col;
	}
	else if (gasOn > 0.5f)
	{
		const WaterTab& e = Sample(scene, pos[0], pos[1], pos[2], 3);
		waterAdd[1] = fabsf(((float)e.shimmer + (float)e.abs) * 8.0f);
	}

	float view[3];
	Transform(scene->view, pos[0], pos[1], pos[2], view);

	float nrm[3];
	Rotate(scene->view, v->nx, v->ny, v->nz, nrm);

	/* RoomPS. The bridge: the remaster's baked light reaches 2, the classic
	 * overbright reaches 2, so light = 2 * prelight / 255 and f = prelight. */
	float f[3] =
	{
		2.0f * ClrR(v->prelight) / 255.0f * 127.5f,
		2.0f * ClrG(v->prelight) / 255.0f * 127.5f,
		2.0f * ClrB(v->prelight) / 255.0f * 127.5f
	};

	for (int i = 0; i < scene->lightCount && i < HD_MAX_LIGHTS; i++)
	{
		const HDLightSource& a = scene->lights[i];
		const float delta[3] = { pos[0] - a.x, pos[1] - a.y, pos[2] - a.z };
		const float dist2 = Square(delta[0]) + Square(delta[1]) + Square(delta[2]);

		if (dist2 < a.falloff * a.falloff)
		{
			const float dist = sqrtf(dist2);
			const float ramp = (a.falloff != 0 ? 1.0f / a.falloff : 0.0f) * (a.falloff - dist);
			const float t0 = nrm[0] * (delta[0] / dist);
			const float t1 = nrm[1] * (delta[1] / dist);
			const float t2 = nrm[2] * (delta[2] / dist);
			const float lit = ramp * (1.0f - (t0 + t1 + t2)) * 255.0f;

			f[0] += a.r * lit;
			f[1] += a.g * lit;
			f[2] += a.b * lit;
		}
	}

	f[0] += waterAdd[0];
	f[1] += waterAdd[1];
	f[2] += waterAdd[2];

	if (scene->distanceFogStart > 0 && view[2] > scene->distanceFogStart)
	{
		const float take = (view[2] - scene->distanceFogStart) *
			(255.0f / scene->distanceFogStart);
		f[0] -= take;
		f[1] -= take;
		f[2] -= take;
	}

	float c[3], s[3];

	// The classic split with its ceiling taken off - see hd_room.hlsl.
	for (int k = 0; k < 3; k++)
	{
		out->f[k] = f[k];
		c[k] = f[k] * (2.0f / 255.0f);
		s[k] = 0.0f;
	}

	for (int j = 0; j < scene->bulbCount && j < HD_MAX_BULBS; j++)
	{
		// The SHADER's answer, so with the A4 rim weight.
		const float intensity = BulbIntensity(view, scene->bulbs[j], true);

		out->bulbDensity += intensity;

		for (int k = 0; k < 3; k++)
		{
			const float colour = k == 0 ? scene->bulbs[j].r
				: (k == 1 ? scene->bulbs[j].g : scene->bulbs[j].b);

			out->bulbColour[k] += intensity * colour;
			c[k] -= intensity * (1.0f / 255.0f);
			s[k] += intensity * colour * (1.0f / 255.0f);
		}
	}

	for (int k = 0; k < 3; k++)
	{
		out->diffuse[k] = c[k] < 0 ? 0 : c[k];
		out->specular[k] = s[k] < 0 ? 0 : (s[k] > 1 ? 1 : s[k]);
	}
}

/* -------------------------------------------------------------- the run -- */

int HDLightParity(const HDParityScene* scene, const HDParityVertex* verts, int count,
	double worstF[3], double worstFinal[3])
{
	if (worstF)
		worstF[0] = worstF[1] = worstF[2] = 0;
	if (worstFinal)
		worstFinal[0] = worstFinal[1] = worstFinal[2] = 0;

	if (!scene || !verts || count <= 0)
		return 0;

	for (int i = 0; i < count; i++)
	{
		HDLightResult a, b;

		HDLightClassic(scene, &verts[i], &a);
		HDLightShader(scene, &verts[i], &b);

		for (int k = 0; k < 3; k++)
		{
			// The contribution, in the classic path's own 0..255.
			const double df = fabs((double)a.f[k] - (double)b.f[k]);

			// What a WHITE texel would come out as, which is where the
			// deliberate deviation above 128 shows.
			const double finalA = (double)a.diffuse[k] + (double)a.specular[k];
			const double finalB = (double)b.diffuse[k] + (double)b.specular[k];
			const double dfinal = fabs(finalA - finalB);

			if (worstF && df > worstF[k])
				worstF[k] = df;
			if (worstFinal && dfinal > worstFinal[k])
				worstFinal[k] = dfinal;
		}
	}

	return count;
}

/* --------------------------------------------------- the stand-in table -- */

void HDLightBuildWaterTable(void* dst)
{
	if (!dst)
		return;

	WaterTab(*table)[64] = (WaterTab(*)[64])dst;

	static const short shimmerLadder[4] = { 31, 63, 95, 127 };
	static const short choppyLadder[4] = { 16, 53, 90, 127 };
	static const unsigned char absLadder[4] = { 4, 8, 12, 16 };

	std::srand(121197);

	for (int i = 0; i < 64; i++)
	{
		/* rcossin_tbl (global\math_tbls.h:269) is sin and cos interleaved,
		 * scaled by 4096 - NOT by 32767, which is the scale a 16 bit sine
		 * table usually has and the one this line had first. The difference
		 * is a factor of eight on every water effect: the choppy column runs
		 * to +-16 with 4096 and to +-128 with 32767, and the picture out of
		 * the tool went from a moving swell to television static. Measured:
		 * rcossin_tbl[128] is 401, and sin(2 pi / 64) * 4096 is 401.4. */
		const short sSin = (short)(sin(i * 6.283185307179586 / 64.0) * 4096.0);

		table[0][i].shimmer = (signed char)((63 * sSin) >> 15);
		table[0][i].choppy = (signed char)((16 * sSin) >> 12);
		table[0][i].abs = 0;

		table[1][i].shimmer = (signed char)((32 * sSin) >> 15);
		table[1][i].choppy = 0;
		table[1][i].abs = (unsigned char)-3;

		table[2][i].shimmer = (signed char)((64 * sSin) >> 15);
		table[2][i].choppy = 0;
		table[2][i].abs = 0;

		table[3][i].shimmer = (signed char)((96 * sSin) >> 15);
		table[3][i].choppy = 0;
		table[3][i].abs = 4;

		table[4][i].shimmer = (signed char)((127 * sSin) >> 15);
		table[4][i].choppy = 0;
		table[4][i].abs = 8;

		for (int j = 0, k = 5; j < 4; j++, k += 4)
			for (int m = 0; m < 4; m++)
			{
				table[k + m][i].shimmer = (signed char)(-((sSin * shimmerLadder[m]) >> 15));
				table[k + m][i].choppy = (signed char)((sSin * choppyLadder[j]) >> 12);
				table[k + m][i].abs = absLadder[m];
			}

		for (int row = 0; row < 21; row++)
			table[row][i].random = (unsigned char)(std::rand() & 0xFC);
	}
}

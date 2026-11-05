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
#include "../tomb5/pch.h"
#include "output.h"
#include "../specific/3dmath.h"
#include "zrange_math.h"		// ZFAR_SHIPPED - the corners' count
#include "../game/newinv2.h"
#include "../specific/d3dmatrix.h"
#include "lighting.h"
#include "function_table.h"
#include "../game/gameflow.h"
#include "../game/effects.h"
#include "specificfx.h"
#include "drawroom.h"
#include "function_stubs.h"
#include "profile.h"			// present-phase timing (PROFLOG)
#include "hd/hd_level.h"
#include "time.h"
#include "dxshell.h"
#include "polyinsert.h"
#include "winmain.h"
#include "../game/tomb4fx.h"
#include "LoadSave.h"
#include "../game/delstuff.h"
#include "../game/deltapak.h"
#include "file.h"
#include "texture.h"
#include "../game/camera.h"
#include "../game/spotcam.h"
#include "../game/control.h"
#include "../game/effect2.h"
#include "../game/lara.h"
#include "gamemain.h"
#include "../game/draw.h"
#include "../game/health.h"
#include "../game/text.h"
#include "../game/interpolate.h"	// the in-between (tween) frame counters
#include "../tomb5/tomb5.h"

D3DTLVERTEX aVertexBuffer[1024];

long aGlobalSkinMesh;
long GlobalAlpha = 0xFF000000;
long GlobalAmbient;
float AnimatingTexturesV[16][8][3];
float aBoundingBox[24];

static ENVUV aMappedEnvUV[256];
static ENVUV SkinENVUV[40][12];
static D3DTLVERTEX SkinVerts[40][12];
static short SkinClip[40][12];

void S_DrawPickup(short object_number)
{
	long x, y;

	phd_LookAt(0, 1024, 0, 100, 0, 200, 0);
	aSetViewMatrix();

	x = phd_winwidth - GetFixedScale(80) + PickupX;
	y = phd_winheight - GetFixedScale(75);
	DrawThreeDeeObject2D(x, y, convert_obj_to_invobj(object_number), 128, 0, (GnFrameCounter & 0x7F) << 9, 0, 0, 1);
}

void aTransformLightClipMesh(MESH_DATA* mesh)
{
	POINTLIGHT_STRUCT* point;
	SUNLIGHT_STRUCT* sun;
	SPOTLIGHT_STRUCT* spot;
	FOGBULB_STRUCT* bulb;
	FVECTOR pos;
	FVECTOR stash;
	FVECTOR vec;
	FVECTOR vec2;
	short* clip;
	float fR, fG, fB, val, val2, val3, zv, intensity;
	static float DistanceFogStart, iDistanceFogStart;
	long cR, cG, cB, sR, sG, sB, col;
	short clipFlag;

	clip = clipflags;
	DistanceFogStart = tomb5.distance_fog * 1024.0F;
	iDistanceFogStart = 1.0F / DistanceFogStart;

	for (int i = 0; i < mesh->nVerts; i++)
	{
		pos.x = mesh->aVtx[i].x * D3DMView._11 + mesh->aVtx[i].y * D3DMView._21 + mesh->aVtx[i].z * D3DMView._31 + D3DMView._41;
		pos.y = mesh->aVtx[i].x * D3DMView._12 + mesh->aVtx[i].y * D3DMView._22 + mesh->aVtx[i].z * D3DMView._32 + D3DMView._42;
		pos.z = mesh->aVtx[i].x * D3DMView._13 + mesh->aVtx[i].y * D3DMView._23 + mesh->aVtx[i].z * D3DMView._33 + D3DMView._43;

		if (TotalNumLights)
		{
			fR = (float)aAmbientR;
			fG = (float)aAmbientG;
			fB = (float)aAmbientB;

			if (NumPointLights)
			{
				for (int j = 0; j < NumPointLights; j++)
				{
					point = &PointLights[j];
					val = (point->vec.x * mesh->aVtx[i].nx + point->vec.y * mesh->aVtx[i].ny + point->vec.z * mesh->aVtx[i].nz + 1.0F) * 0.5F;
					val *= val;

					if (val > 0)
					{
						val *= point->rad;
						fR += val * point->r;
						fG += val * point->g;
						fB += val * point->b;
					}
				}
			}

			if (NumSpotLights)
			{
				for (int j = 0; j < NumSpotLights; j++)
				{
					spot = &SpotLights[j];
					val = spot->vec.x * mesh->aVtx[i].nx + spot->vec.y * mesh->aVtx[i].ny + spot->vec.z * mesh->aVtx[i].nz;

					if (val > 0)
					{
						val *= spot->rad;
						fR += val * spot->r;
						fG += val * spot->g;
						fB += val * spot->b;
					}
				}
			}

			if (NumSunLights)
			{
				for (int j = 0; j < NumSunLights; j++)
				{
					sun = &SunLights[j];
					val = sun->vec.x * mesh->aVtx[i].nx + sun->vec.y * mesh->aVtx[i].ny + sun->vec.z * mesh->aVtx[i].nz;

					if (val > 0)
					{
						if (!InventoryActive)	//fucking shit
							val *= 0.75F;
						else
							val += val;

						fR += val * sun->r;
						fG += val * sun->g;
						fB += val * sun->b;
					}
				}
			}

			cR = (long)fR;
			cG = (long)fG;
			cB = (long)fB;
		}
		else
		{
			cR = aAmbientR;
			cG = aAmbientG;
			cB = aAmbientB;
		}

		if (pos.z > DistanceFogStart)
		{
			val = (pos.z - DistanceFogStart) * (iDistanceFogStart * 255.0F);
			cR -= (long)val;
			cG -= (long)val;
			cB -= (long)val;
		}

		sR = 0;
		sG = 0;
		sB = 0;

		if (cR - 128 <= 0)
			cR <<= 1;
		else
		{
			sR = (cR - 128) >> 1;
			cR = 255;
		}

		if (cG - 128 <= 0)
			cG <<= 1;
		else
		{
			sG = (cG - 128) >> 1;
			cG = 255;
		}

		if (cB - 128 <= 0)
			cB <<= 1;
		else
		{
			sB = (cB - 128) >> 1;
			cB = 255;
		}

		stash.x = pos.x;
		stash.y = pos.y;
		stash.z = pos.z;
		aVertexBuffer[i].tu = pos.x;
		aVertexBuffer[i].tv = pos.y;
		clipFlag = 0;

		if (pos.z < f_mznear)
			clipFlag = -128;
		else
		{
			zv = f_mpersp / pos.z;
			pos.x = pos.x * zv + f_centerx;
			pos.y = pos.y * zv + f_centery;
			aVertexBuffer[i].rhw = f_moneopersp * zv;

			if (pos.x < clip_left)
				clipFlag++;
			else if (pos.x > clip_right)
				clipFlag += 2;

			if (pos.y < clip_top)
				clipFlag += 4;
			else if (pos.y > clip_bottom)
				clipFlag += 8;
		}

		*clip++ = clipFlag;

		aVertexBuffer[i].sx = pos.x;
		aVertexBuffer[i].sy = pos.y;
		aVertexBuffer[i].sz = pos.z;
		col = 0;

		if (NumFogBulbs)
		{
			for (int j = 0; j < NumFogBulbs; j++)
			{
				bulb = &FogBulbs[j];
				intensity = 0;

				if (bulb->rad + stash.z > 0 && fabs(stash.x) - bulb->rad < fabs(stash.z) && fabs(stash.y) - bulb->rad < fabs(stash.z))
				{
					vec.x = 0;
					vec.y = 0;
					vec.z = 0;
					vec2.x = 0;
					vec2.y = 0;
					vec2.z = 0;
					val = SQUARE(bulb->pos.x - stash.x) + SQUARE(bulb->pos.y - stash.y) + SQUARE(bulb->pos.z - stash.z);

					if (bulb->sqlen < bulb->sqrad)
					{
						if (val < bulb->sqrad)
						{
							vec2.x = stash.x;
							vec2.y = stash.y;
							vec2.z = stash.z;
						}
						else
						{
							val = 1.0F / sqrt(SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z));
							vec2.x = val * stash.x;
							vec2.y = val * stash.y;
							vec2.z = val * stash.z;
							val2 = vec2.x * bulb->pos.x + vec2.y * bulb->pos.y + vec2.z * bulb->pos.z;
							val = val2 + sqrt(bulb->sqrad - (bulb->sqlen - SQUARE(val2)));
							vec2.x *= val;
							vec2.y *= val;
							vec2.z *= val;
						}
					}
					else if (val < bulb->sqrad)
					{
						vec2.z = stash.z;
						vec2.x = stash.x;
						vec2.y = stash.y;
						val = 1.0F / sqrt(SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z));
						vec.x = val * stash.x;
						vec.y = val * stash.y;
						vec.z = val * stash.z;
						val2 = vec.x * bulb->pos.x + vec.y * bulb->pos.y + vec.z * bulb->pos.z;
						val = val2 - sqrt(bulb->sqrad - (bulb->sqlen - SQUARE(val2)));
						vec.x *= val;
						vec.y *= val;
						vec.z *= val;
					}
					else
					{
						val = SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z);
						val2 = 1.0F / sqrt(val);
						vec.x = val2 * stash.x;
						vec.y = val2 * stash.y;
						vec.z = val2 * stash.z;
						val2 = bulb->pos.x * vec.x + bulb->pos.y * vec.y + bulb->pos.z * vec.z;

						if (val2 > 0)
						{
							val3 = SQUARE(val2);

							if (val > val3)
							{
								val = bulb->sqlen - val3;

								if (val < bulb->sqrad)
								{
									val3 = sqrtf(bulb->sqrad - val);
									val = val2 - val3;
									vec2.x = val * vec.x;
									vec2.y = val * vec.y;
									vec2.z = val * vec.z;
									val = val2 + val3;
									vec.x *= val;
									vec.y *= val;
									vec.z *= val;
								}
								else
								{
									vec.x = 0;
									vec.y = 0;
									vec.z = 0;
									vec2.x = 0;
									vec2.y = 0;
									vec2.z = 0;
								}
							}
						}
					}

					intensity = sqrt(SQUARE(vec2.x - vec.x) + SQUARE(vec2.y - vec.y) + SQUARE(vec2.z - vec.z)) * bulb->d;
				}

				if (intensity)
				{
					col += (long)intensity;
					sR += long(intensity * bulb->r);
					sG += long(intensity * bulb->g);
					sB += long(intensity * bulb->b);
				}
			}

			cR -= col;
			cG -= col;
			cB -= col;
		}

		if (sR > 255)
			sR = 255;
		else if (sR < 0)
			sR = 0;

		if (sG > 255)
			sG = 255;
		else if (sG < 0)
			sG = 0;

		if (sB > 255)
			sB = 255;
		else if (sB < 0)
			sB = 0;

		if (cR > 255)
			cR = 255;
		else if (cR < 0)
			cR = 0;

		if (cG > 255)
			cG = 255;
		else if (cG < 0)
			cG = 0;

		if (cB > 255)
			cB = 255;
		else if (cB < 0)
			cB = 0;

		aVertexBuffer[i].color = GlobalAlpha | RGBONLY(cR, cG, cB);
		aVertexBuffer[i].specular = RGBA(sR, sG, sB, 0xFF);
	}
}

void aTransformLightPrelightClipMesh(MESH_DATA* mesh)
{
	FOGBULB_STRUCT* bulb;
	DYNAMIC* light;
	FVECTOR pos;
	FVECTOR lPos;
	FVECTOR stash;
	FVECTOR vec;
	FVECTOR vec2;
	short* clip;
	float x, y, z, val, val2, val3, zv, intensity;
	static float DistanceFogStart, iDistanceFogStart;
	long sR, sG, sB, cR, cG, cB, pR, pG, pB, col;
	short clipFlag;

	clip = clipflags;
	pR = (StaticMeshShade & 0x1F) << 3;
	pG = ((StaticMeshShade >> 5) & 0x1F) << 3;
	pB = ((StaticMeshShade >> 10) & 0x1F) << 3;
	DistanceFogStart = tomb5.distance_fog * 1024.0F;
	iDistanceFogStart = 1.0F / DistanceFogStart;

	for (int i = 0; i < mesh->nVerts; i++)
	{
		pos.x = mesh->aVtx[i].x * D3DMView._11 + mesh->aVtx[i].y * D3DMView._21 + mesh->aVtx[i].z * D3DMView._31 + D3DMView._41;
		pos.y = mesh->aVtx[i].x * D3DMView._12 + mesh->aVtx[i].y * D3DMView._22 + mesh->aVtx[i].z * D3DMView._32 + D3DMView._42;
		pos.z = mesh->aVtx[i].x * D3DMView._13 + mesh->aVtx[i].y * D3DMView._23 + mesh->aVtx[i].z * D3DMView._33 + D3DMView._43;

		cR = CLRR(mesh->aVtx[i].prelight);
		cG = CLRG(mesh->aVtx[i].prelight);
		cB = CLRB(mesh->aVtx[i].prelight);
		cR = (cR * pR) >> 8;
		cG = (cG * pG) >> 8;
		cB = (cB * pB) >> 8;

		if (tomb5.static_lighting)
		{
			for (int j = 0; j < 32; j++)
			{
				light = &dynamics[j];

				if (light->on)
				{
					x = light->x - lGlobalMeshPos.x;
					y = light->y - lGlobalMeshPos.y;
					z = light->z - lGlobalMeshPos.z;
					lPos.x = aLightMatrix._11 * x + aLightMatrix._12 * y + aLightMatrix._13 * z;
					lPos.y = aLightMatrix._21 * x + aLightMatrix._22 * y + aLightMatrix._23 * z;
					lPos.z = aLightMatrix._31 * x + aLightMatrix._32 * y + aLightMatrix._33 * z;
					val = sqrt(SQUARE(lPos.x - mesh->aVtx[i].x) + SQUARE(lPos.y - mesh->aVtx[i].y) + SQUARE(lPos.z - mesh->aVtx[i].z)) * 1.7F;

					if (val <= light->falloff)
					{
						val2 = (light->falloff - val) / light->falloff;
						cR += long(val2 * light->r);
						cG += long(val2 * light->g);
						cB += long(val2 * light->b);
					}
				}
			}
		}

		if (pos.z > DistanceFogStart)
		{
			val = (pos.z - DistanceFogStart) * (iDistanceFogStart * 255.0F);
			cR -= (long)val;
			cG -= (long)val;
			cB -= (long)val;
		}

		sR = 0;
		sG = 0;
		sB = 0;

		if (cR - 128 <= 0)
			cR <<= 1;
		else
		{
			sR = (cR - 128) >> 1;
			cR = 255;
		}

		if (cG - 128 <= 0)
			cG <<= 1;
		else
		{
			sG = (cG - 128) >> 1;
			cG = 255;
		}

		if (cB - 128 <= 0)
			cB <<= 1;
		else
		{
			sB = (cB - 128) >> 1;
			cB = 255;
		}

		stash.x = pos.x;
		stash.y = pos.y;
		stash.z = pos.z;
		aVertexBuffer[i].tu = pos.x;
		aVertexBuffer[i].tv = pos.y;
		clipFlag = 0;

		if (pos.z < f_mznear)
			clipFlag = -128;
		else
		{
			zv = f_mpersp / pos.z;
			pos.x = pos.x * zv + f_centerx;
			pos.y = pos.y * zv + f_centery;
			aVertexBuffer[i].rhw = f_moneopersp * zv;

			if (pos.x < clip_left)
				clipFlag++;
			else if (pos.x > clip_right)
				clipFlag += 2;

			if (pos.y < clip_top)
				clipFlag += 4;
			else if (pos.y > clip_bottom)
				clipFlag += 8;
		}

		*clip++ = clipFlag;

		aVertexBuffer[i].sx = pos.x;
		aVertexBuffer[i].sy = pos.y;
		aVertexBuffer[i].sz = pos.z;
		col = 0;

		if (NumFogBulbs)
		{
			for (int j = 0; j < NumFogBulbs; j++)
			{
				bulb = &FogBulbs[j];
				intensity = 0;

				if (bulb->rad + stash.z > 0 && fabs(stash.x) - bulb->rad < fabs(stash.z) && fabs(stash.y) - bulb->rad < fabs(stash.z))
				{
					vec.x = 0;
					vec.y = 0;
					vec.z = 0;
					vec2.x = 0;
					vec2.y = 0;
					vec2.z = 0;
					val = SQUARE(bulb->pos.x - stash.x) + SQUARE(bulb->pos.y - stash.y) + SQUARE(bulb->pos.z - stash.z);

					if (bulb->sqlen < bulb->sqrad)
					{
						if (val < bulb->sqrad)
						{
							vec2.x = stash.x;
							vec2.y = stash.y;
							vec2.z = stash.z;
						}
						else
						{
							val = 1.0F / sqrt(SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z));
							vec2.x = val * stash.x;
							vec2.y = val * stash.y;
							vec2.z = val * stash.z;
							val2 = vec2.x * bulb->pos.x + vec2.y * bulb->pos.y + vec2.z * bulb->pos.z;
							val = val2 + sqrt(bulb->sqrad - (bulb->sqlen - SQUARE(val2)));
							vec2.x *= val;
							vec2.y *= val;
							vec2.z *= val;
						}
					}
					else if (val < bulb->sqrad)
					{
						vec2.z = stash.z;
						vec2.x = stash.x;
						vec2.y = stash.y;
						val = 1.0F / sqrt(SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z));
						vec.x = val * stash.x;
						vec.y = val * stash.y;
						vec.z = val * stash.z;
						val2 = vec.x * bulb->pos.x + vec.y * bulb->pos.y + vec.z * bulb->pos.z;
						val = val2 - sqrt(bulb->sqrad - (bulb->sqlen - SQUARE(val2)));
						vec.x *= val;
						vec.y *= val;
						vec.z *= val;
					}
					else
					{
						val = SQUARE(stash.x) + SQUARE(stash.y) + SQUARE(stash.z);
						val2 = 1.0F / sqrt(val);
						vec.x = val2 * stash.x;
						vec.y = val2 * stash.y;
						vec.z = val2 * stash.z;
						val2 = bulb->pos.x * vec.x + bulb->pos.y * vec.y + bulb->pos.z * vec.z;

						if (val2 > 0)
						{
							val3 = SQUARE(val2);

							if (val > val3)
							{
								val = bulb->sqlen - val3;

								if (val < bulb->sqrad)
								{
									val3 = sqrtf(bulb->sqrad - val);
									val = val2 - val3;
									vec2.x = val * vec.x;
									vec2.y = val * vec.y;
									vec2.z = val * vec.z;
									val = val2 + val3;
									vec.x *= val;
									vec.y *= val;
									vec.z *= val;
								}
								else
								{
									vec.x = 0;
									vec.y = 0;
									vec.z = 0;
									vec2.x = 0;
									vec2.y = 0;
									vec2.z = 0;
								}
							}
						}
					}

					intensity = sqrt(SQUARE(vec2.x - vec.x) + SQUARE(vec2.y - vec.y) + SQUARE(vec2.z - vec.z)) * bulb->d;
				}

				if (intensity)
				{
					col += (long)intensity;
					sR += long(intensity * bulb->r);
					sG += long(intensity * bulb->g);
					sB += long(intensity * bulb->b);
				}
			}

			cR -= col;
			cG -= col;
			cB -= col;
		}

		if (sR > 255)
			sR = 255;
		else if (sR < 0)
			sR = 0;

		if (sG > 255)
			sG = 255;
		else if (sG < 0)
			sG = 0;

		if (sB > 255)
			sB = 255;
		else if (sB < 0)
			sB = 0;

		if (cR > 255)
			cR = 255;
		else if (cR < 0)
			cR = 0;

		if (cG > 255)
			cG = 255;
		else if (cG < 0)
			cG = 0;

		if (cB > 255)
			cB = 255;
		else if (cB < 0)
			cB = 0;

		aVertexBuffer[i].color = RGBONLY(cR, cG, cB) | GlobalAlpha;
		aVertexBuffer[i].specular = RGBA(sR, sG, sB, 0xFF);
	}
}

long S_GetObjectBounds(short* bounds)
{
	FVECTOR vtx[8];
	float xMin, xMax, yMin, yMax, zMin, zMax, numZ, xv, yv, zv;

	if (aMXPtr[M23] >= f_mzfar && !outside)
		return 0;

	xMin = bounds[0];
	xMax = bounds[1];
	yMin = bounds[2];
	yMax = bounds[3];
	zMin = bounds[4];
	zMax = bounds[5];

	vtx[0].x = xMin;
	vtx[0].y = yMin;
	vtx[0].z = zMin;

	vtx[1].x = xMax;
	vtx[1].y = yMin;
	vtx[1].z = zMin;

	vtx[2].x = xMax;
	vtx[2].y = yMax;
	vtx[2].z = zMin;

	vtx[3].x = xMin;
	vtx[3].y = yMax;
	vtx[3].z = zMin;

	vtx[4].x = xMin;
	vtx[4].y = yMin;
	vtx[4].z = zMax;

	vtx[5].x = xMax;
	vtx[5].y = yMin;
	vtx[5].z = zMax;

	vtx[6].x = xMax;
	vtx[6].y = yMax;
	vtx[6].z = zMax;

	vtx[7].x = xMin;
	vtx[7].y = yMax;
	vtx[7].z = zMax;

	xMin = (float)0x3FFFFFFF;
	xMax = (float)-0x3FFFFFFF;
	yMin = (float)0x3FFFFFFF;
	yMax = (float)-0x3FFFFFFF;
	numZ = 0;

	for (int i = 0; i < 8; i++)
	{
		zv = vtx[i].x * aMXPtr[M20] + vtx[i].y * aMXPtr[M21] + vtx[i].z * aMXPtr[M22] + aMXPtr[M23];

		/* The corners between the near plane and ZFAR_SHIPPED, not the far
		 * plane: the answer for an object nearer than 20,480 units does not
		 * depend on the far plane - a box reaching past 20,480 is drawn
		 * clipped (-1) without the window test, where a count on the far
		 * plane would window test it - and one past 20,480, which the
		 * origin test above lets through, is drawn clipped as past the
		 * plane in an outside frame. */
		if(zv > f_mznear && zv < (float)ZFAR_SHIPPED)
		{
			numZ++;
			zv /= f_mpersp;

			if (!zv)
				zv = 1;

			zv = 1 / zv;
			xv = zv * (vtx[i].x * aMXPtr[M00] + vtx[i].y * aMXPtr[M01] + vtx[i].z * aMXPtr[M02] + aMXPtr[M03]);

			if (xv < xMin)
				xMin = xv;

			if (xv > xMax)
				xMax = xv;

			yv = zv * (vtx[i].x * aMXPtr[M10] + vtx[i].y * aMXPtr[M11] + vtx[i].z * aMXPtr[M12] + aMXPtr[M13]);

			if (yv < yMin)
				yMin = yv;

			if (yv > yMax)
				yMax = yv;
		}
	}

	xMin += f_centerx;
	xMax += f_centerx;
	yMin += f_centery;
	yMax += f_centery;

	if (numZ < 8 || xMin < 0 || yMin < 0 || phd_winxmax < xMax || phd_winymax < yMax)
		return -1;

	if (phd_right >= xMin && phd_bottom >= yMin && phd_left <= xMax && phd_top <= yMax)
		return 1;
	else
		return 0;
}

void S_AnimateTextures(long n)
{
	TEXTURESTRUCT* tex;
	TEXTURESTRUCT tex2;
	short* range;
	float voff;
	static long comp;
	short nRanges, nRangeFrames;
	static short v;

	for (comp += n; comp > 5; comp -= 5)
	{
		nRanges = *aranges;
		range = aranges + 1;

		for (int i = 0; i < nRanges; i++)
		{
			nRangeFrames = *range++;

			if (i < nAnimUVRanges && gfUVRotate)
			{
				while (nRangeFrames > 0)
				{
					range++;
					nRangeFrames--;
				}
			}
			else
			{
				tex2 = textinfo[*range];

				while (nRangeFrames > 0)
				{
					textinfo[range[0]] = textinfo[range[1]];
					range++;
					nRangeFrames--;
				}

				textinfo[*range] = tex2;
			}

			range++;
		}
	}

	if (gfUVRotate)
	{
		range = aranges + 1;
		v = (v - gfUVRotate * (n >> 1)) & 0x1F;

		for (int i = 0; i < nAnimUVRanges; i++)
		{
			nRangeFrames = *range++;

			while (nRangeFrames >= 0)
			{
				tex = &textinfo[range[0]];
				voff = v * (1.0F / 256.0F);
				tex->v1 = voff + AnimatingTexturesV[i][nRangeFrames][0];
				tex->v2 = voff + AnimatingTexturesV[i][nRangeFrames][0];
				tex->v3 = voff + AnimatingTexturesV[i][nRangeFrames][0] + 0.125F;
				tex->v4 = voff + AnimatingTexturesV[i][nRangeFrames][0] + 0.125F;
				range++;
				nRangeFrames--;
			}
		}
	}
}

// MESHLOG counter: how often aCheckMeshClip discards a mesh completely.
// DrawAnimatingItem reads it before and after drawing a thinker; if it rose,
// the body died here rather than at the bounds check.
long aMeshClipRejects;

// Set by DrawAnimatingItem while a thinker is being drawn; only then does
// the discard branch below write its CLIPDETAIL line. -1 = off.
long aClipReportObj = -1;

long aCheckMeshClip(MESH_DATA* mesh)
{
	float* stash;
	short* checkBox;
	float left, right, top, bottom, front, back;
	float x, y, z, bx, by, bz, zv;
	long tooNear, offScreen;

	if (aGlobalSkinMesh)
		return 2;

	left = 10000.0F;
	right = -10000.0F;
	top = 10000.0F;
	bottom = -10000.0F;
	front = 10000.0F;
	back = -10000.0F;
	stash = aBoundingBox;
	checkBox = CheckClipBox;
	tooNear = 0;
	offScreen = 0;

	for (int i = 0; i < 8; i++)
	{
		bx = mesh->bbox[checkBox[0]];
		by = mesh->bbox[checkBox[1]];
		bz = mesh->bbox[checkBox[2]];
		checkBox += 3;

		x = bx * D3DMView._11 + by * D3DMView._21 + bz * D3DMView._31 + D3DMView._41;
		y = bx * D3DMView._12 + by * D3DMView._22 + bz * D3DMView._32 + D3DMView._42;
		z = bx * D3DMView._13 + by * D3DMView._23 + bz * D3DMView._33 + D3DMView._43;
		stash[0] = x;
		stash[1] = y;
		stash[2] = z;
		stash += 3;

		if (z < f_mznear)
		{
			tooNear++;
			z = f_mznear;
		}

		zv = f_mpersp / z;
		x = zv * x + f_centerx;
		y = zv * y + f_centery;

		if (x < left)
			left = x;

		if (x > right)
			right = x;

		if (y < top)
			top = y;

		if (y > bottom)
			bottom = y;

		if (z < front)
			front = z;

		if (z > back)
			back = z;

		if (left < f_left)
		{
			left = f_left;
			offScreen++;
		}

		if (right > f_right)
		{
			right = f_right;
			offScreen++;
		}

		if (top < f_top)
		{
			top = f_top;
			offScreen++;
		}

		if (bottom > f_bottom)
		{
			bottom = f_bottom;
			offScreen++;
		}
	}

	if (tooNear == 8 || left > f_right || right < f_left || top > f_bottom || bottom < f_top)
	{
		// CLIPDETAIL: when a mesh is discarded here at a full-screen clip
		// window, the projection is delivering bad values rather than a
		// genuine off-screen case. This line records where the eight
		// corners projected to, how many lay in front of the near plane,
		// and where the mesh sits relative to the camera per the view
		// matrix.
		//
		// The counter only increments while logging is on, so MESHLOG's
		// before/after comparison does not drift over a long session for
		// nothing.
		if (gLogEnabled)
		{
			aMeshClipRejects++;

			if (aClipReportObj >= 0)
			{
				static long lastDetail;

				if (GlobalCounter - lastDetail > 15 || GlobalCounter < lastDetail)
				{
					lastDetail = GlobalCounter;
					// aMXPtr[M23] is logged alongside for comparison:
					// S_GetObjectBounds judges visibility from the
					// a-stack, this test from D3DMView.
					Log("CLIPDETAIL obj=%d near=%d L=%d R=%d T=%d B=%d front=%d back=%d camera=%d/%d/%d aM23=%d box=%d,%d,%d..%d,%d,%d",
						(int)aClipReportObj, (int)tooNear, (int)left, (int)right,
						(int)top, (int)bottom, (int)front, (int)back,
						(int)D3DMView._41, (int)D3DMView._42, (int)D3DMView._43,
						(int)aMXPtr[M23],
						(int)mesh->bbox[0], (int)mesh->bbox[1], (int)mesh->bbox[2],
						(int)mesh->bbox[3], (int)mesh->bbox[4], (int)mesh->bbox[5]);
				}
			}
		}

		return 0;
	}

	if (tooNear || offScreen)
		return 1;

	return 2;
}

HRESULT DDCopyBitmap(LPDIRECT3DSURFACE9 surf, HBITMAP hbm, long x, long y, long dx, long dy)
{
	HDC hdc;
	HDC hdc2;
	BITMAP bitmap;
	D3DSURFACE_DESC desc;
	HRESULT result;
	long l, t;

	if (!hbm || !surf)
		return E_FAIL;

	hdc = CreateCompatibleDC(0);

	if (!hdc)
		OutputDebugString("createcompatible dc failed\n");

	SelectObject(hdc, hbm);
	GetObject(hbm, sizeof(BITMAP), &bitmap);

	if (!dx)
		dx = bitmap.bmWidth;

	if (!dy)
		dy = bitmap.bmHeight;

	surf->GetDesc(&desc);
	l = 0;
	t = 0;
	result = surf->GetDC(&hdc2);

	if (result == DD_OK)
	{
		StretchBlt(hdc2, l, t, desc.Width, desc.Height, hdc, x, y, dx, dy, SRCCOPY);
		surf->ReleaseDC(hdc2);
	}

	DeleteDC(hdc);
	return result;
}

HRESULT _LoadBitmap(LPDIRECT3DSURFACE9 surf, LPCSTR name)
{
	HBITMAP hBitmap;
	HRESULT result;

	hBitmap = (HBITMAP)LoadImage(GetModuleHandle(0), name, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	if (!hBitmap)
		hBitmap = (HBITMAP)LoadImage(0, name, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);

	if (!hBitmap)
	{
		OutputDebugString("handle is null\n");
		return E_FAIL;
	}

	result = DDCopyBitmap(surf, hBitmap, 0, 0, 0, 0);

	if (result != DD_OK)
		OutputDebugString("ddcopybitmap failed\n");

	DeleteObject(hBitmap);
	return result;
}

static HRESULT _LoadBitmapToBackBuffer(LPCSTR name)
{
	LPDIRECT3DSURFACE9 back;
	HRESULT result;

	if (FAILED(App.dx.lpD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return E_FAIL;

	result = _LoadBitmap(back, name);
	back->Release();
	return result;
}

void do_boot_screen(long language)
{
	Log(__FUNCTION__);

	switch (language)
	{
	case ENGLISH:
	case DUTCH:
		_LoadBitmapToBackBuffer("uk.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("uk.bmp");
		break;

	case FRENCH:
		_LoadBitmapToBackBuffer("france.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("france.bmp");
		break;

	case GERMAN:
		_LoadBitmapToBackBuffer("germany.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("germany.bmp");
		break;

	case ITALIAN:
		_LoadBitmapToBackBuffer("italy.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("italy.bmp");
		break;

	case SPANISH:
		_LoadBitmapToBackBuffer("spain.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("spain.bmp");
		break;

	case US:
		_LoadBitmapToBackBuffer("usa.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("usa.bmp");
		break;

	case JAPAN:
		_LoadBitmapToBackBuffer("japan.bmp");
		S_DumpScreen();
		_LoadBitmapToBackBuffer("japan.bmp");
		break;
	}
}

void aCalcColorSplit(long col, long* pC, long* pS)
{
	long cR, cG, cB, sR, sG, sB;

	sR = 0;
	sG = 0;
	sB = 0;
	cR = CLRR(col);
	cG = CLRG(col);
	cB = CLRB(col);

	if (cR - 128 <= 0)
		cR <<= 1;
	else
	{
		sR = (cR - 128) >> 1;
		cR = 255;
	}

	if (cG - 128 <= 0)
		cG <<= 1;
	else
	{
		sG = (cG - 128) >> 1;
		cG = 255;
	}

	if (cB - 128 <= 0)
		cB <<= 1;
	else
	{
		sB = (cB - 128) >> 1;
		cB = 255;
	}

	if (cR < 0)
		cR = 0;
	else if (cR > 255)
		cR = 255;

	if (cG < 0)
		cG = 0;
	else if (cG > 255)
		cG = 255;

	if (cB < 0)
		cB = 0;
	else if (cB > 255)
		cB = 255;

	if (sR < 0)
		sR = 0;
	else if (sR > 255)
		sR = 255;

	if (sG < 0)
		sG = 0;
	else if (sG > 255)
		sG = 255;

	if (sB < 0)
		sB = 0;
	else if (sB > 255)
		sB = 255;

	*pC = RGBONLY(cR, cG, cB);
	*pS = RGBONLY(sR, sG, sB);
}

/* The 30 Hz cap. Sync() (specific/time.cpp) counts whole 1/60 s ticks -
 * there is no "thirty" anywhere in that file. The `< 2` below is what turns
 * sixty into thirty; gPresentMinTicks names that constant so DrawPhaseGame
 * can lower it to one for its own present and nobody else's. See the note
 * in output.h for why the default has to stay two. */
long gPresentMinTicks = 2;

/* The display-bound rate (fps_mode 2), and the two ways it can fail.
 *
 * fps_mode 2 means "as often as the panel refreshes". The engine does not
 * measure that rate and wait for it - it hands the waiting to Direct3D:
 * with D3DPRESENT_INTERVAL_ONE the present itself blocks until the display
 * is ready, so the loop runs at 60, 120 or 144 by itself and the
 * `while (!Sync());` spin is never entered.
 *
 * The mode is therefore only active with a device that really honours that
 * interval. Two things can take it away:
 *
 *   The option is off, or the adapter does not offer the interval. DXCreate
 *   knows this and DXVSyncActive() answers it. Without a blocking present
 *   there is nothing left to bound the loop, and an uncapped frame rate can
 *   damage the GPU, so the mode falls back to fps_mode 1 (the 60 Hz spin)
 *   and logs that once.
 *
 *   The driver overrides it. A control panel set to "vsync off, application
 *   controlled: no" leaves the device claiming the interval while Present
 *   returns at once. That cannot be asked, only measured - which is what
 *   the watchdog below does, with the same fallback. */
static bool g_vsyncSaidOnce = false;	// the "needs VSync" line, once a session
static bool g_vsyncBroken = false;	// measured: the present does not wait
static __int64 g_wdFreq = 0;
static __int64 g_wdLast = 0;
static long g_wdFast = 0;

long ActiveFpsMode()
{
	if (tomb5.fps_mode >= 2)
	{
		if (DXVSyncActive() && !g_vsyncBroken)
			return 2;

		if (!g_vsyncSaidOnce)
		{
			g_vsyncSaidOnce = true;
			Log("frame rate: \"Display\" needs VSync and the present does not wait - running at 60");
		}

		return 1;
	}

	return (long)tomb5.fps_mode;
}

long TargetPresentHz()
{
	const long mode = ActiveFpsMode();

	if (mode == 2)
		return DXDisplayHz();

	return mode == 1 ? 60 : 30;
}

/* The slot the present that just ran asked for, in milliseconds - the one
 * definition, so FPSLOG (specific/hd/hd_level.cpp) and PROFLOG
 * (specific/profile.cpp) cannot drift apart on it.
 *
 * gPresentMinTicks is how many 1/60 s ticks S_DumpScreen waited for, so two
 * ticks is 33.3 ms and one is 16.7. Zero is the display-bound frame: it
 * waited for no tick at all because the present waited for the panel, so
 * its slot is the panel's own period. */
double PresentSlotMs()
{
	if (gPresentMinTicks <= 0)
	{
		const long hz = TargetPresentHz();

		return 1000.0 / (double)(hz > 0 ? hz : 60);
	}

	return (double)gPresentMinTicks * 1000.0 / 60.0;
}

/* Is the present really waiting? Called once per display-bound present.
 *
 * A present that returns in less than half the panel's period did not wait
 * for the panel: with the interval honoured they land on the period, and
 * half of it is far outside anything jitter can reach. One of those means
 * nothing; six hundred in a row means the driver has overridden the
 * interval and the engine is running uncapped without ever having asked to.
 * That is ten seconds' worth at 60 Hz, four at 144 Hz - long enough that
 * nothing normal trips it, short enough to stay a moment rather than a
 * session.
 *
 * Once it has fired it stays fired for the session: the fallback takes the
 * frame back to the 60 Hz spin, and the spin means this function is not
 * called again, so nothing could measure a recovery, and a flag that went
 * back and forth would spend hundreds of frames uncapped on every swing. A
 * restart is the way back.
 *
 * A minimised window is not a broken driver: present on an iconic window
 * can return at once with nothing to compose against, so that run is
 * dropped rather than counted, and coming back from the taskbar does not
 * cost the player their setting.
 *
 * Costs one QueryPerformanceCounter and one IsIconic call per present, and
 * only in this mode. */
static void NoteDisplayBoundPresent()
{
	__int64 now;
	double ms, period;

	if (!g_wdFreq && !QueryPerformanceFrequency((LARGE_INTEGER*)&g_wdFreq))
		return;

	if (App.hWnd && IsIconic(App.hWnd))
	{
		g_wdLast = 0;
		g_wdFast = 0;
		return;
	}

	QueryPerformanceCounter((LARGE_INTEGER*)&now);

	if (g_wdLast)
	{
		ms = 1000.0 * (double)(now - g_wdLast) / (double)g_wdFreq;
		period = 1000.0 / (double)DXDisplayHz();

		if (ms < period * 0.5)
			g_wdFast++;
		else
			g_wdFast = 0;

		if (g_wdFast > 600 && !g_vsyncBroken)
		{
			g_vsyncBroken = true;
			Log("frame rate: the present is not waiting for the display (%.2f ms against %.2f) "
				"- the frame rate falls back to 60 for this session", ms, period);
		}
	}

	g_wdLast = now;
}

/* How many ticks DrawPhaseGame's own present may wait for.
 *
 * Two ticks (30 Hz) is required by any menu drawn once per present while
 * walked with debounced input written once per tick: the title screen,
 * level select, the whole options tree and the Tomb5 pages in
 * tomb5/troyestuff.cpp all read dbinput once per call inside DoTitle's
 * `TitleOptions(); DrawPhaseGame(); ControlPhase(nframes);` loop
 * (game/gameflow.cpp), against dbinput's own once-per-tick refresh in
 * ControlPhase's `for (framecount += nframes; framecount > 0; framecount -=
 * 2)` loop (game/control.cpp). At one tick per present every second
 * ControlPhase runs no tick at all, dbinput keeps standing, and the next
 * TitleOptions reads the same press a second time: one tap of Down moves
 * the cursor two rows.
 *
 * The title now shields itself from this instead of needing the slow rate:
 * on the call that would read an edge for the second time, its input is
 * shadowed to zero for the duration (game/gameflow.cpp, TitleInputShadow),
 * and the debounce formula in specific/input.cpp keeps dbinput's own
 * previous value because the shadow is put back the moment TitleOptions
 * returns. The title can therefore run at any present rate.
 *
 * `InventoryActive` stays as a guard even though the inventory's own modal
 * loop (game/newinv2.cpp) no longer reaches DrawPhaseGame - it paces its
 * own present through InvFrameMinTicks (game/inventory_interp.cpp) instead.
 * The check here is what keeps a future path into DrawPhaseGame from a menu
 * from picking up 60 Hz by accident.
 *
 * Everything else in the game loop is safe by construction: DoLevel reads
 * no input at all outside ControlPhase, and every other menu in the build -
 * the pause menu, the death menu, the load/save screen, the special
 * features - runs its own modal loop with its own S_UpdateInput at the
 * default two ticks. */
long GameFrameMinTicks()
{
	const long mode = ActiveFpsMode();

	if (mode == 0)
		return 2;

	if (InventoryActive)
		return 2;

	/* Zero does not mean "as fast as possible" - it means "do not wait for a
	 * tick, because the present already waits". fps_mode 2 is only active
	 * with a device that blocks in Present (see ActiveFpsMode), so the spin
	 * below has nothing left to do and the frame is paced by the panel. At
	 * 144 Hz, two frames out of three come back from Sync() with no whole
	 * tick at all, which is correct: ControlPhase(0) runs no logic step and
	 * the frame is drawn from the clock instead (game/interpolate.cpp,
	 * InterpTickFraction). */
	return mode == 2 ? 0 : 1;
}

long S_DumpScreen()
{
	long n;

	/* Was this a 30 Hz present?
	 *
	 * Every modal loop in the build comes through here with the default two
	 * ticks - the inventory, the load/save screen, the death menu, the load
	 * screen's fade loop - and so does DrawPhaseGame at the title. Their
	 * per-present counters step once per present, so the in-between-frame
	 * latch must be down while they run. It is put down here, at the one
	 * place every present in the game passes through, rather than at each
	 * loop.
	 *
	 * Set before the spin, not after: a modal loop's counters run in
	 * S_OutputPolyList, i.e. before this call, so clearing it here opens
	 * the way from the loop's second iteration onwards. The first
	 * iteration can still inherit `true` from the last game frame and skip
	 * one step - one sixtieth of a second on a loading screen. */
	if (gPresentMinTicks >= 2)
		InterpNoteClassicPresent();

	/* GnFrameCounter is read as a clock, not as a frame number:
	 * game/text.cpp blinks a string on `GnFrameCounter & 8`, S_DrawPickup
	 * spins the pickup on `(GnFrameCounter & 0x7F) << 9`, and the inventory
	 * reads it too. At 60 Hz, unguarded, every one of those would run at
	 * double speed. In a modal loop the latch is down (see above) and this
	 * steps once per present exactly as before. */
	if (!InterpIsTweenFrame())
		GnFrameCounter++;

	if (gPresentMinTicks <= 0)
	{
		/* The display-bound present: the clock is read on the other side
		 * of it, not before.
		 *
		 * Every other present in the engine reads the clock first and then
		 * flips, because the wait is the read: `while (n < min) while
		 * (!Sync());` is the pacing and the measurement in one. Here the
		 * wait is inside Present - D3DPRESENT_INTERVAL_ONE holds it until
		 * the panel is ready - so a Sync() taken before it would miss the
		 * whole of that wait: the ticks spent waiting would be credited to
		 * the next frame instead, a frame late, and ControlPhase would run
		 * its logic step a frame after the tick it belongs to.
		 *
		 * That is not a rounding error, it is the difference between a
		 * smooth mode and a stuttering one. The blend factor is
		 * `(framecount + subTick + 1) / 2` (interpolate_math.h) and it
		 * stays inside [0,1) - never clamped to the newest state, i.e.
		 * never stalling - exactly as long as the accumulator and the
		 * sub-tick remainder are read from the same moment. Reading the
		 * clock after the flip makes them so: every whole tick that has
		 * passed is in the accumulator and what is left over is under one
		 * tick. Read before the flip, up to a whole frame's worth of time
		 * would sit in neither of them, the factor would ask for a state
		 * the logic has not reached, and the picture would stand still for
		 * a frame at every logic step.
		 *
		 * The watchdog runs here because this branch is the one place in
		 * the engine that waits for nothing of its own: if the present is
		 * not really waiting either, nothing bounds the loop.
		 *
		 * PROF_SPIN stays empty in this mode; PROF_FLIP carries the wait,
		 * which is the honest place for it - the CPU is inside the driver,
		 * not burning a core in a loop of ours. */
		NoteDisplayBoundPresent();

		ProfBegin(PROF_FLIP);
		_EndScene();
		DXShowFrame();
		ProfEnd(PROF_FLIP);

		n = Sync();
	}
	else
	{
		// Not display-bound: the watchdog's interval is dropped so that a
		// menu in the middle of a session is not measured as one enormous
		// frame when the game comes back to it.
		g_wdLast = 0;

		n = Sync();

		while (n < gPresentMinTicks)
		{
			ProfBegin(PROF_SPIN);
			while (!Sync());	//wait for sync
			ProfEnd(PROF_SPIN);
			n++;
		}

		ProfBegin(PROF_FLIP);
		_EndScene();
		DXShowFrame();
		ProfEnd(PROF_FLIP);
	}

	ProfPresent();
	App.dx.DoneBlit = 1;
	return n;
}

long S_DumpScreenFrame()
{
	long n;

	// The loading bar's own present, which runs at up to 60. It is not a
	// game frame, so the in-between latch goes down here too.
	InterpNoteClassicPresent();

	n = Sync();

	while (n < 1)
	{
		while (!Sync());	//wait for sync
		n++;
	}

	GnFrameCounter++;
	_EndScene();
	DXShowFrame();
	ProfPresent();
	App.dx.DoneBlit = 1;
	return n;
}

void StashSkinVertices(long node)
{
	D3DTLVERTEX* v;
	D3DTLVERTEX* d;
	short* cf;
	uchar* vns;

	vns = SkinVertNums[node];
	cf = (short*)&SkinClip[node];
	d = (D3DTLVERTEX*)&SkinVerts[node];
	v = aVertexBuffer;

	while (1)
	{
		if (*vns == 0xFF)	// the row's end; 0..254 name a vertex
			break;

		d->sx = v[*vns].sx;
		d->sy = v[*vns].sy;
		d->sz = v[*vns].sz;
		d->rhw = v[*vns].rhw;
		d->color = v[*vns].color;
		d->specular = v[*vns].specular;
		d->tu = v[*vns].tu;
		d->tv = v[*vns].tv;
		*cf++ = clipflags[*vns];
		d++;
		vns++;
	}
}

void SkinVerticesToScratch(long node)
{
	D3DTLVERTEX* v;
	D3DTLVERTEX* d;
	short* cf;
	char* vns;

	vns = (char*)&ScratchVertNums[node];
	cf = (short*)&SkinClip[node];
	d = (D3DTLVERTEX*)&SkinVerts[node];
	v = aVertexBuffer;

	while (1)
	{
		if (*vns < 0)
			break;

		v[*vns].sx = d->sx;
		v[*vns].sy = d->sy;
		v[*vns].sz = d->sz;
		v[*vns].rhw = d->rhw;
		v[*vns].color = d->color;
		v[*vns].specular = d->specular;
		v[*vns].tu = d->tu;
		v[*vns].tv = d->tv;
		clipflags[*vns] = *cf++;
		d++;
		vns++;
	}
}

void StashSkinNormals(long node)
{
	ENVUV* d;
	uchar* vns;

	vns = SkinVertNums[node];
	d = &SkinENVUV[node][0];

	while (1)
	{
		if (*vns == 0xFF)	// the row's end; 0..254 name a vertex
			break;

		d->u = aMappedEnvUV[*vns].u;
		d->v = aMappedEnvUV[*vns].v;
		d++;
		vns++;
	}
}

void SkinNormalsToScratch(long node)
{
	ENVUV* s;
	char* vns;

	vns = (char*)&ScratchVertNums[node];
	s = &SkinENVUV[node][0];

	while (1)
	{
		if (*vns < 0)
			break;

		aMappedEnvUV[*vns].u = s->u;
		aMappedEnvUV[*vns].v = s->v;
		s++;
		vns++;
	}
}

void S_InitialisePolyList()
{
	D3DRECT r;

	r.x1 = App.dx.rViewport.left;
	r.y1 = App.dx.rViewport.top;
	r.y2 = App.dx.rViewport.top + App.dx.rViewport.bottom;
	r.x2 = App.dx.rViewport.left + App.dx.rViewport.right;

	// Clear the depth buffer here too, at the start of the frame.
	//
	// Without this, the depth buffer was cleared only once per frame, in
	// S_OutputPolyList, before the scope mask. The mask then wrote its
	// depths in and nobody cleared them until the next frame, so the next
	// frame's world was tested against the previous frame's mask depths
	// and discarded wherever the mask had been.
	//
	// While the scope stays on this goes unnoticed, because the mask
	// arrives anew at the same spot every frame and covers exactly what it
	// blocks. Only in the one frame where it drops away does the imprint
	// show: the world appears only inside the circle, while outside it the
	// sky remains, since the sky is drawn without a depth test.
	//
	// The clear in S_OutputPolyList stays where it is: it keeps the mask,
	// the recording overlay and the fade above the scene.
	//
	// Timed as PROF_CLEAR for a pause line, with -log alone (profile.h).
	ProfBegin(PROF_CLEAR);
	DXAttempt(App.dx.lpD3DDevice->Clear(1, &r,
		(App.dx.Flags & DXF_ZBUFFER) ? (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER) : D3DCLEAR_TARGET,
		0, 1.0F, 0));
	ProfEnd(PROF_CLEAR);

	_BeginScene();
	InitBuckets();
	InitialiseSortList();

	// The HD rooms of this frame are collected in InsertRoom and drawn in
	// S_OutputPolyList, so their list is emptied here beside the buckets
	// and the sort list, by the same function and for the same reason.
	HDFrameReset();
}

void S_OutputPolyList()
{
	D3DRECT r;
	long h;
	char buf[128];

	WinFrameRate();
	App.dx.lpD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);

	if (resChangeCounter)
	{
		sprintf(buf, "%dx%d", App.dx.dwRenderWidth, App.dx.dwRenderHeight);
		PrintString(8, App.dx.dwRenderHeight - 8, 6, buf, 0);
		/* This counts logic ticks, not drawn frames.
		 *
		 * A rate-scaled step (`TargetPresentHz() / App.fps`) breaks at the
		 * display-bound rate: the ratio is an integer division, so a
		 * measured rate a hair above the target truncates the step to zero
		 * and the caption never leaves the screen - with the panel pacing
		 * the frames, that is not a corner case, it happens every second
		 * reading.
		 *
		 * Counting 120 logic ticks instead avoids the problem: it is four
		 * seconds at any frame rate, with no measured rate in the
		 * calculation, and at fps_mode 0 (one step per frame at 30 fps) it
		 * is the same 120 frames vanilla counted. */
		if (!InterpIsTweenFrame())
			resChangeCounter--;

		if (resChangeCounter < 0)
			resChangeCounter = 0;
	}

	/* The HD rooms of this frame, and why they go here.
	 *
	 * The frame's depth buffer is cleared in S_InitialisePolyList, and
	 * nothing has drawn into it since except the sky, which SkyDrawPhase
	 * draws with ZWRITEENABLE off (specific/specificfx.cpp, OutputSky) - so
	 * the sky owns no depth and the HD walls paint over it, exactly as the
	 * classic room polygons did.
	 *
	 * Then come the classic buckets: opaque items, Lara, the statics of
	 * every room that stayed classic. They are depth tested against the HD
	 * walls now in the buffer, and the projection matrix behind those walls
	 * writes the same sz the classic path writes for the same point in
	 * space, so the test is exact and needs no bias (hd_render.cpp,
	 * HDRenderSelfTest). After the buckets, the sort list draws the
	 * see-through polygons over both.
	 *
	 * The frame draws in two phases. HDFrameDraw is everything of the HD
	 * path that writes depth - every room's opaque and alpha-tested passes
	 * and every HD object's - and it goes before the buckets. HDFrameDrawGlow
	 * is the additive pass of the same rooms and objects; it writes no
	 * depth, so it has to come after every opaque draw in the frame, or the
	 * last opaque thing drawn paints over it - without this ordering a wall
	 * lamp's glow cone is cut off along a portal. hd_level.cpp's own note
	 * over HDFrameDraw carries the full argument and the order.
	 *
	 * The other depth clear, further down this function, separates the
	 * world from the binocular mask, the pickup display and the fade, and
	 * still runs after everything above it, including this block. */
	ProfBegin(PROF_HD_DRAW);
	HDFrameDraw();
	ProfEnd(PROF_HD_DRAW);

	if (App.dx.Flags & DXF_ZBUFFER)
		DrawBuckets();		// PROF_BUCKETS sits inside it - three call sites

	ProfBegin(PROF_HD_GLOW);
	HDFrameDrawGlow();
	ProfEnd(PROF_HD_GLOW);

	if (gfLevelId == LVL5_TITLE)
	{
		Fade();

		if (App.dx.Flags & DXF_ZBUFFER)
			DrawSortList();
	}

	SortPolyList(SortCount, SortList);
	DrawSortList();

	if (App.dx.Flags & DXF_ZBUFFER)
	{
		r.x1 = App.dx.rViewport.left;
		r.y1 = App.dx.rViewport.top;
		r.x2 = App.dx.rViewport.left + App.dx.rViewport.right;
		r.y2 = App.dx.rViewport.top + App.dx.rViewport.bottom;
		DXAttempt(App.dx.lpD3DDevice->Clear(1, &r, D3DCLEAR_ZBUFFER, 0, 1.0F, 0));
	}

	if ((BinocularRange || SCOverlay || SniperOverlay) && !MonoScreenOn)
	{
		InitialiseSortList();
		DrawBinoculars();
		DrawSortList();
	}

	if (pickups[CurrentPickup].life != -1 && !MonoScreenOn && !GLOBAL_playing_cutseq)
	{
		InitialiseSortList();
		S_DrawPickup(pickups[CurrentPickup].object_number);
		SortPolyList(SortCount, SortList);
		DrawSortList();
	}

	InitialiseSortList();

	if (FadeScreenHeight)
	{
		h = long((float)phd_winymax / 256.0F) * FadeScreenHeight;
		DrawPsxTile(0, phd_winwidth | (h << 16), 0x62FFFFFF, 0, 0);
		DrawPsxTile(phd_winheight - h, phd_winwidth | (h << 16), 0x62FFFFFF, 0, 0);
	}

	if (gfLevelId != LVL5_TITLE)
	{
		Fade();

		if (FlashFader)
		{
			DrawFlash();

			/* The white flash a weapon or a pickup throws over the screen.
			 * FlashFader is its brightness and it walks down by two per
			 * drawn frame, so at 60 Hz the flash would be over in half the
			 * time; the flash itself is still drawn on every frame. */
			if (FlashFader && !InterpIsTweenFrame())
				FlashFader -= 2;
		}

		DrawSortList();
	}

	if (DoFade == 1)
	{
		InitialiseSortList();
		DoScreenFade();
		DrawSortList();
	}
}

void S_InsertRoom(ROOM_INFO* r, long a)
{
	InsertRoom(r);
}

static void RGB_M(ulong& c, long m)	//Original was a macro.
{
	long r, g, b, a;

	a = CLRA(c);
	r = (CLRR(c) * m) >> 8;
	g = (CLRG(c) * m) >> 8;
	b = (CLRB(c) * m) >> 8;
	c = RGBA(r, g, b, a);
}

void phd_PutPolygons(short* objptr, long clipstatus)
{
	MESH_DATA* mesh;
	ENVUV* envuv;
	ACMESHVERTEX* vtx;
	TEXTURESTRUCT* pTex;
	TEXTURESTRUCT tex;
	short* quad;
	short* tri;
	long clrbak[4];
	long spcbak[4];
	long num;
	ushort drawbak;
	bool envmap;

	aSetViewMatrix();
	mesh = (MESH_DATA*)objptr;

	if (!objptr)
		return;

	clip_left = f_left;
	clip_top = f_top;
	clip_right = f_right;
	clip_bottom = f_bottom;

	if (!aCheckMeshClip(mesh))
		return;

	lGlobalMeshPos.x = mesh->bbox[3] - mesh->bbox[0];
	lGlobalMeshPos.y = mesh->bbox[4] - mesh->bbox[1];
	lGlobalMeshPos.z = mesh->bbox[5] - mesh->bbox[2];
	SuperResetLights();

	if (GlobalAmbient)
	{
		aAmbientR = CLRR(GlobalAmbient);
		aAmbientG = CLRG(GlobalAmbient);
		aAmbientB = CLRB(GlobalAmbient);
		GlobalAmbient = 0;
	}
	else
	{
		if (mesh->prelight)
			InitDynamicLighting(current_item);
		else
			InitObjectLighting(current_item);

		InitObjectFogBulbs();
	}

	if (mesh->aFlags & 2)
		aTransformLightPrelightClipMesh(mesh);
	else
		aTransformLightClipMesh(mesh);

	if (mesh->aFlags & 1)
	{
		envuv = aMappedEnvUV;
		vtx = mesh->aVtx;

		for (int i = 0; i < mesh->nVerts; i++, envuv++, vtx++)
		{
			envuv->u = (vtx->nx * D3DMView._11 + vtx->ny * D3DMView._21 + vtx->nz * D3DMView._31) * 0.25F + 0.25F;
			envuv->v = (vtx->nx * D3DMView._12 + vtx->ny * D3DMView._22 + vtx->nz * D3DMView._32) * 0.25F + 0.25F;
		}
	}

	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		pTex = &textinfo[quad[4] & 0x7FFF];
		envmap = 0;
		drawbak = pTex->drawtype;

		if (quad[5] & 1)
			pTex->drawtype = 2;

		if (quad[5] & 2)
		{
			envmap = 1;
			tex.drawtype = 2;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[quad[0]].u;
			tex.v1 = aMappedEnvUV[quad[0]].v;
			tex.u2 = aMappedEnvUV[quad[1]].u;
			tex.v2 = aMappedEnvUV[quad[1]].v;
			tex.u3 = aMappedEnvUV[quad[2]].u;
			tex.v3 = aMappedEnvUV[quad[2]].v;
			tex.u4 = aMappedEnvUV[quad[3]].u;
			tex.v4 = aMappedEnvUV[quad[3]].v;
			num = (quad[5] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			if (!pTex->drawtype)
				AddQuadZBuffer(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
			else if (pTex->drawtype <= 2)
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);

			if (envmap)
			{
				clrbak[0] = aVertexBuffer[quad[0]].color;
				clrbak[1] = aVertexBuffer[quad[1]].color;
				clrbak[2] = aVertexBuffer[quad[2]].color;
				clrbak[3] = aVertexBuffer[quad[3]].color;
				spcbak[0] = aVertexBuffer[quad[0]].specular;
				spcbak[1] = aVertexBuffer[quad[1]].specular;
				spcbak[2] = aVertexBuffer[quad[2]].specular;
				spcbak[3] = aVertexBuffer[quad[3]].specular;
				RGB_M(aVertexBuffer[quad[0]].color, num);
				RGB_M(aVertexBuffer[quad[1]].color, num);
				RGB_M(aVertexBuffer[quad[2]].color, num);
				RGB_M(aVertexBuffer[quad[3]].color, num);
				RGB_M(aVertexBuffer[quad[0]].specular, num);
				RGB_M(aVertexBuffer[quad[1]].specular, num);
				RGB_M(aVertexBuffer[quad[2]].specular, num);
				RGB_M(aVertexBuffer[quad[3]].specular, num);
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], &tex, 0);
				aVertexBuffer[quad[0]].color = clrbak[0];
				aVertexBuffer[quad[1]].color = clrbak[1];
				aVertexBuffer[quad[2]].color = clrbak[2];
				aVertexBuffer[quad[3]].color = clrbak[3];
				aVertexBuffer[quad[0]].specular = spcbak[0];
				aVertexBuffer[quad[1]].specular = spcbak[1];
				aVertexBuffer[quad[2]].specular = spcbak[2];
				aVertexBuffer[quad[3]].specular = spcbak[3];
			}
		}
		else
		{
			pTex->drawtype = 7;
			AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
		}

		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		pTex = &textinfo[tri[3] & 0x7FFF];
		envmap = 0;
		drawbak = pTex->drawtype;

		if (tri[4] & 1)
			pTex->drawtype = 2;

		if (tri[4] & 2)
		{
			envmap = 1;
			tex.drawtype = 2;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[tri[0]].u;
			tex.v1 = aMappedEnvUV[tri[0]].v;
			tex.u2 = aMappedEnvUV[tri[1]].u;
			tex.v2 = aMappedEnvUV[tri[1]].v;
			tex.u3 = aMappedEnvUV[tri[2]].u;
			tex.v3 = aMappedEnvUV[tri[2]].v;
			num = (tri[4] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			if (!pTex->drawtype)
				AddTriZBuffer(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
			else if (pTex->drawtype <= 2)
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);

			if (envmap)
			{
				clrbak[0] = aVertexBuffer[tri[0]].color;
				clrbak[1] = aVertexBuffer[tri[1]].color;
				clrbak[2] = aVertexBuffer[tri[2]].color;
				spcbak[0] = aVertexBuffer[tri[0]].specular;
				spcbak[1] = aVertexBuffer[tri[1]].specular;
				spcbak[2] = aVertexBuffer[tri[2]].specular;
				RGB_M(aVertexBuffer[tri[0]].color, num);
				RGB_M(aVertexBuffer[tri[1]].color, num);
				RGB_M(aVertexBuffer[tri[2]].color, num);
				RGB_M(aVertexBuffer[tri[0]].specular, num);
				RGB_M(aVertexBuffer[tri[1]].specular, num);
				RGB_M(aVertexBuffer[tri[2]].specular, num);
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], &tex, 0);
				aVertexBuffer[tri[0]].color = clrbak[0];
				aVertexBuffer[tri[1]].color = clrbak[1];
				aVertexBuffer[tri[2]].color = clrbak[2];
				aVertexBuffer[tri[0]].specular = spcbak[0];
				aVertexBuffer[tri[1]].specular = spcbak[1];
				aVertexBuffer[tri[2]].specular = spcbak[2];
			}
		}
		else
		{
			pTex->drawtype = 7;
			AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		}

		pTex->drawtype = drawbak;
	}
}

void phd_PutPolygonsSkyMesh(short* objptr, long clipstatus)
{
	TEXTURESTRUCT* pTex;
	MESH_DATA* mesh;
	short* quad;
	short* tri;
	static float num;
	ushort drawbak;

	num = 0;
	mesh = (MESH_DATA*)objptr;
	aSetViewMatrix();
	SuperResetLights();
	aAmbientR = 128;
	aAmbientG = 128;
	aAmbientB = 128;
	clip_top = f_top;
	clip_bottom = f_bottom;
	clip_left = f_left;
	clip_right = f_right;
	aTransformLightClipMesh(mesh);
	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		pTex = &textinfo[quad[4] & 0x7FFF];
		drawbak = pTex->drawtype;

		if (quad[5] & 1)
		{
			if (gfLevelFlags & GF_HORIZONCOLADD)
				pTex->drawtype = 2;
			else
			{
				if (App.dx.Flags & DXF_ZBUFFER)
				{
					aVertexBuffer[quad[0]].color = 0;
					aVertexBuffer[quad[1]].color = 0;
					aVertexBuffer[quad[2]].color = 0xFF000000;
					aVertexBuffer[quad[3]].color = 0xFF000000;
					pTex->drawtype = 3;
				}
				else
				{
					aVertexBuffer[quad[0]].color = 0;
					aVertexBuffer[quad[1]].color = 0;
					aVertexBuffer[quad[2]].color = 0;
					aVertexBuffer[quad[3]].color = 0;
					pTex->drawtype = 0;
				}
			}
		}
		else
			pTex->drawtype = 4;

		aVertexBuffer[quad[0]].rhw = (f_mpersp / f_mzfar) * f_moneopersp;
		aVertexBuffer[quad[1]].rhw = (f_mpersp / f_mzfar) * f_moneopersp;
		aVertexBuffer[quad[2]].rhw = (f_mpersp / f_mzfar) * f_moneopersp;
		aVertexBuffer[quad[3]].rhw = (f_mpersp / f_mzfar) * f_moneopersp;
		AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);

		if (aVertexBuffer[quad[0]].sy > num)
			num = aVertexBuffer[quad[0]].sy;

		if (aVertexBuffer[quad[1]].sy > num)
			num = aVertexBuffer[quad[1]].sy;

		if (aVertexBuffer[quad[2]].sy > num)
			num = aVertexBuffer[quad[2]].sy;

		if (aVertexBuffer[quad[3]].sy > num)
			num = aVertexBuffer[quad[3]].sy;

		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		pTex = &textinfo[tri[3] & 0x7FFF];
		drawbak = pTex->drawtype;
		pTex->drawtype = 4;
		AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		pTex->drawtype = drawbak;
	}

	num = (float)phd_centery;
}

void phd_PutPolygonsPickup(short* objptr, float x, float y, long color)
{
	MESH_DATA* mesh;
	ENVUV* envuv;
	ACMESHVERTEX* vtx;
	TEXTURESTRUCT* pTex;
	TEXTURESTRUCT tex;
	short* quad;
	short* tri;
	float val, xbak, ybak;
	long clrbak[4];
	long spcbak[4];
	long num;
	ushort drawbak;
	bool envmap;

	aSetViewMatrix();
	mesh = (MESH_DATA*)objptr;
	lGlobalMeshPos.x = mesh->bbox[3] - mesh->bbox[0];
	lGlobalMeshPos.y = mesh->bbox[4] - mesh->bbox[1];
	lGlobalMeshPos.z = mesh->bbox[5] - mesh->bbox[2];
	SuperResetLights();
	clip_left = f_left;
	clip_top = f_top;
	clip_right = f_right;
	clip_bottom = f_bottom;
	SunLights[0].r = (float)CLRR(color);
	SunLights[0].g = (float)CLRG(color);
	SunLights[0].b = (float)CLRB(color);
	val = 1.0F / sqrt(12500.0F);
	SunLights[0].vec.x = (D3DMView._12 * -50.0F + D3DMView._13 * -100.0F) * val;	//x is 0
	SunLights[0].vec.y = (D3DMView._22 * -50.0F + D3DMView._23 * -100.0F) * val;
	SunLights[0].vec.z = (D3DMView._32 * -50.0F + D3DMView._33 * -100.0F) * val;
	NumSunLights = 1;
	TotalNumLights = 1;
	aAmbientR = 8;
	aAmbientG = 8;
	aAmbientB = 8;
	xbak = f_centerx;
	ybak = f_centery;
	f_centerx = x;
	f_centery = y;
	aTransformLightClipMesh(mesh);
	f_centerx = xbak;
	f_centery = ybak;

	if (mesh->aFlags & 1)
	{
		envuv = aMappedEnvUV;
		vtx = mesh->aVtx;

		for (int i = 0; i < mesh->nVerts; i++, envuv++, vtx++)
		{
			envuv->u = (vtx->nx * D3DMView._11 + vtx->ny * D3DMView._21 + vtx->nz * D3DMView._31) * 0.25F + 0.25F;
			envuv->v = (vtx->nx * D3DMView._12 + vtx->ny * D3DMView._22 + vtx->nz * D3DMView._32) * 0.25F + 0.25F;
		}
	}

	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		envmap = 0;
		pTex = &textinfo[quad[4] & 0x7FFF];
		drawbak = pTex->drawtype;

		if (quad[5] & 1)
			pTex->drawtype = 2;

		if (quad[5] & 2)
		{
			envmap = 1;
			tex.drawtype = 2;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[quad[0]].u;
			tex.v1 = aMappedEnvUV[quad[0]].v;
			tex.u2 = aMappedEnvUV[quad[1]].u;
			tex.v2 = aMappedEnvUV[quad[1]].v;
			tex.u3 = aMappedEnvUV[quad[2]].u;
			tex.v3 = aMappedEnvUV[quad[2]].v;
			tex.u4 = aMappedEnvUV[quad[3]].u;
			tex.v4 = aMappedEnvUV[quad[3]].v;
			num = (quad[5] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);

			if (envmap)
			{
				clrbak[0] = aVertexBuffer[quad[0]].color;
				clrbak[1] = aVertexBuffer[quad[1]].color;
				clrbak[2] = aVertexBuffer[quad[2]].color;
				clrbak[3] = aVertexBuffer[quad[3]].color;
				spcbak[0] = aVertexBuffer[quad[0]].specular;
				spcbak[1] = aVertexBuffer[quad[1]].specular;
				spcbak[2] = aVertexBuffer[quad[2]].specular;
				spcbak[3] = aVertexBuffer[quad[3]].specular;
				RGB_M(aVertexBuffer[quad[0]].color, num);
				RGB_M(aVertexBuffer[quad[1]].color, num);
				RGB_M(aVertexBuffer[quad[2]].color, num);
				RGB_M(aVertexBuffer[quad[3]].color, num);
				RGB_M(aVertexBuffer[quad[0]].specular, num);
				RGB_M(aVertexBuffer[quad[1]].specular, num);
				RGB_M(aVertexBuffer[quad[2]].specular, num);
				RGB_M(aVertexBuffer[quad[3]].specular, num);
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], &tex, 0);
				aVertexBuffer[quad[0]].color = clrbak[0];
				aVertexBuffer[quad[1]].color = clrbak[1];
				aVertexBuffer[quad[2]].color = clrbak[2];
				aVertexBuffer[quad[3]].color = clrbak[3];
				aVertexBuffer[quad[0]].specular = spcbak[0];
				aVertexBuffer[quad[1]].specular = spcbak[1];
				aVertexBuffer[quad[2]].specular = spcbak[2];
				aVertexBuffer[quad[3]].specular = spcbak[3];
			}
		}
		else
		{
			pTex->drawtype = 0;
			AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
		}

		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		envmap = 0;
		pTex = &textinfo[tri[3] & 0x7FFF];
		drawbak = pTex->drawtype;

		if (tri[4] & 1)
			pTex->drawtype = 2;

		if (tri[4] & 2)
		{
			envmap = 1;
			tex.drawtype = 2;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[*tri].u;
			tex.v1 = aMappedEnvUV[*tri].v;
			tex.u2 = aMappedEnvUV[tri[1]].u;
			tex.v2 = aMappedEnvUV[tri[1]].v;
			tex.u3 = aMappedEnvUV[tri[2]].u;
			tex.v3 = aMappedEnvUV[tri[2]].v;
			num = (tri[4] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);

			if (envmap)
			{
				clrbak[0] = aVertexBuffer[tri[0]].color;
				clrbak[1] = aVertexBuffer[tri[1]].color;
				clrbak[2] = aVertexBuffer[tri[2]].color;
				spcbak[0] = aVertexBuffer[tri[0]].specular;
				spcbak[1] = aVertexBuffer[tri[1]].specular;
				spcbak[2] = aVertexBuffer[tri[2]].specular;
				RGB_M(aVertexBuffer[tri[0]].color, num);
				RGB_M(aVertexBuffer[tri[1]].color, num);
				RGB_M(aVertexBuffer[tri[2]].color, num);
				RGB_M(aVertexBuffer[tri[0]].specular, num);
				RGB_M(aVertexBuffer[tri[1]].specular, num);
				RGB_M(aVertexBuffer[tri[2]].specular, num);
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], &tex, 0);
				aVertexBuffer[tri[0]].color = clrbak[0];
				aVertexBuffer[tri[1]].color = clrbak[1];
				aVertexBuffer[tri[2]].color = clrbak[2];
				aVertexBuffer[tri[0]].specular = spcbak[0];
				aVertexBuffer[tri[1]].specular = spcbak[1];
				aVertexBuffer[tri[2]].specular = spcbak[2];
			}
		}
		else
		{
			pTex->drawtype = 7;
			AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		}

		pTex->drawtype = drawbak;
	}
}

void phd_PutPolygons_train(short* objptr, long x)
{
	phd_PutPolygons(objptr, x);
}

void phd_PutPolygons_seethrough(short* objptr, long fade)
{
	MESH_DATA* mesh;
	TEXTURESTRUCT* pTex;
	short* quad;
	short* tri;
	ushort drawbak;

	aSetViewMatrix();
	mesh = (MESH_DATA*)objptr;

	if (!objptr)
		return;

	clip_left = f_left;
	clip_top = f_top;
	clip_right = f_right;
	clip_bottom = f_bottom;

	fade--;

	if (fade < 0)
		fade = 0;

	GlobalAlpha = fade << 25;

	if (!aCheckMeshClip(mesh))
		return;

	lGlobalMeshPos.x = mesh->bbox[3] - mesh->bbox[0];
	lGlobalMeshPos.y = mesh->bbox[4] - mesh->bbox[1];
	lGlobalMeshPos.z = mesh->bbox[5] - mesh->bbox[2];
	SuperResetLights();

	if (GlobalAmbient)
	{
		aAmbientR = CLRR(GlobalAmbient);
		aAmbientG = CLRG(GlobalAmbient);
		aAmbientB = CLRB(GlobalAmbient);
		GlobalAmbient = 0;
	}
	else
	{
		if (mesh->prelight)
			InitDynamicLighting(current_item);
		else
			InitObjectLighting(current_item);

		InitObjectFogBulbs();
	}

	if (mesh->aFlags & 2)
		aTransformLightPrelightClipMesh(mesh);
	else
		aTransformLightClipMesh(mesh);

	GlobalAlpha = 0xFF000000;

	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		pTex = &textinfo[quad[4] & 0x7FFF];
		drawbak = pTex->drawtype;
		pTex->drawtype = 3;
		AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		pTex = &textinfo[tri[3] & 0x7FFF];
		drawbak = pTex->drawtype;
		pTex->drawtype = 3;
		AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		pTex->drawtype = drawbak;
	}
}

void phd_PutPolygonsSpcXLU(short* objptr, long clipstatus)
{
	MESH_DATA* mesh;
	TEXTURESTRUCT* pTex;
	short* quad;
	short* tri;
	ushort drawbak;

	aSetViewMatrix();
	mesh = (MESH_DATA*)objptr;

	if (!objptr)
		return;

	clip_left = f_left;
	clip_top = f_top;
	clip_right = f_right;
	clip_bottom = f_bottom;

	if (!aCheckMeshClip(mesh))
		return;

	lGlobalMeshPos.x = mesh->bbox[3] - mesh->bbox[0];
	lGlobalMeshPos.y = mesh->bbox[4] - mesh->bbox[1];
	lGlobalMeshPos.z = mesh->bbox[5] - mesh->bbox[2];
	SuperResetLights();

	if (GlobalAmbient)
	{
		aAmbientR = CLRR(GlobalAmbient);
		aAmbientG = CLRG(GlobalAmbient);
		aAmbientB = CLRB(GlobalAmbient);
		GlobalAmbient = 0;
	}
	else
	{
		if (mesh->prelight)
			InitDynamicLighting(current_item);
		else
			InitObjectLighting(current_item);

		InitObjectFogBulbs();
	}

	if (mesh->aFlags & 2)
		aTransformLightPrelightClipMesh(mesh);
	else
		aTransformLightClipMesh(mesh);

	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		pTex = &textinfo[quad[4] & 0x7FFF];
		drawbak = pTex->drawtype;
		pTex->drawtype = 2;
		AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		pTex = &textinfo[tri[3] & 0x7FFF];
		drawbak = pTex->drawtype;
		pTex->drawtype = 2;
		AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		pTex->drawtype = drawbak;
	}
}

void phd_PutPolygonsSpcEnvmap(short* objptr, long clipstatus)
{
	MESH_DATA* mesh;
	ENVUV* envuv;
	ACMESHVERTEX* vtx;
	TEXTURESTRUCT* pTex;
	TEXTURESTRUCT tex;
	short* quad;
	short* tri;
	long clrbak[4];
	long spcbak[4];
	long num;
	ushort drawbak;
	bool envmap;

	aSetViewMatrix();
	mesh = (MESH_DATA*)objptr;

	if (!objptr)
		return;

	clip_left = f_left;
	clip_top = f_top;
	clip_right = f_right;
	clip_bottom = f_bottom;

	if (!aCheckMeshClip(mesh))
		return;

	lGlobalMeshPos.x = mesh->bbox[3] - mesh->bbox[0];
	lGlobalMeshPos.y = mesh->bbox[4] - mesh->bbox[1];
	lGlobalMeshPos.z = mesh->bbox[5] - mesh->bbox[2];
	SuperResetLights();

	if (GlobalAmbient)
	{
		aAmbientR = CLRR(GlobalAmbient);
		aAmbientG = CLRG(GlobalAmbient);
		aAmbientB = CLRB(GlobalAmbient);
		GlobalAmbient = 0;
	}
	else
	{
		if (mesh->prelight)
			InitDynamicLighting(current_item);
		else
			InitObjectLighting(current_item);

		InitObjectFogBulbs();
	}

	if (mesh->aFlags & 2)
		aTransformLightPrelightClipMesh(mesh);
	else
		aTransformLightClipMesh(mesh);

	if (mesh->aFlags & 1)
	{
		envuv = aMappedEnvUV;
		vtx = mesh->aVtx;

		for (int i = 0; i < mesh->nVerts; i++, envuv++, vtx++)
		{
			envuv->u = (vtx->nx * D3DMView._11 + vtx->ny * D3DMView._21 + vtx->nz * D3DMView._31) * 0.25F + 0.75F;
			envuv->v = (vtx->nx * D3DMView._12 + vtx->ny * D3DMView._22 + vtx->nz * D3DMView._32) * 0.25F + 0.25F;
		}
	}

	quad = mesh->gt4;

	for (int i = 0; i < mesh->ngt4; i++, quad += 6)
	{
		pTex = &textinfo[quad[4] & 0x7FFF];
		envmap = 0;
		drawbak = pTex->drawtype;

		if (quad[5] & 1)
			pTex->drawtype = 2;

		if (quad[5] & 2)
		{
			envmap = 1;
			tex.drawtype = 0;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[quad[0]].u;
			tex.v1 = aMappedEnvUV[quad[0]].v;
			tex.u2 = aMappedEnvUV[quad[1]].u;
			tex.v2 = aMappedEnvUV[quad[1]].v;
			tex.u3 = aMappedEnvUV[quad[2]].u;
			tex.v3 = aMappedEnvUV[quad[2]].v;
			tex.u4 = aMappedEnvUV[quad[3]].u;
			tex.v4 = aMappedEnvUV[quad[3]].v;
			num = (quad[5] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			if (envmap)
			{
				clrbak[0] = aVertexBuffer[quad[0]].color;
				clrbak[1] = aVertexBuffer[quad[1]].color;
				clrbak[2] = aVertexBuffer[quad[2]].color;
				clrbak[3] = aVertexBuffer[quad[3]].color;
				spcbak[0] = aVertexBuffer[quad[0]].specular;
				spcbak[1] = aVertexBuffer[quad[1]].specular;
				spcbak[2] = aVertexBuffer[quad[2]].specular;
				spcbak[3] = aVertexBuffer[quad[3]].specular;
				RGB_M(aVertexBuffer[quad[0]].color, num);
				RGB_M(aVertexBuffer[quad[1]].color, num);
				RGB_M(aVertexBuffer[quad[2]].color, num);
				RGB_M(aVertexBuffer[quad[3]].color, num);
				RGB_M(aVertexBuffer[quad[0]].specular, num);
				RGB_M(aVertexBuffer[quad[1]].specular, num);
				RGB_M(aVertexBuffer[quad[2]].specular, num);
				RGB_M(aVertexBuffer[quad[3]].specular, num);
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], &tex, 0);
				aVertexBuffer[quad[0]].color = clrbak[0];
				aVertexBuffer[quad[1]].color = clrbak[1];
				aVertexBuffer[quad[2]].color = clrbak[2];
				aVertexBuffer[quad[3]].color = clrbak[3];
				aVertexBuffer[quad[0]].specular = spcbak[0];
				aVertexBuffer[quad[1]].specular = spcbak[1];
				aVertexBuffer[quad[2]].specular = spcbak[2];
				aVertexBuffer[quad[3]].specular = spcbak[3];
			}
			else if (!pTex->drawtype)
				AddQuadZBuffer(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
			else if (pTex->drawtype <= 2)
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
		}
		else
		{
			if (envmap)
			{
				tex.drawtype = 7;
				clrbak[0] = aVertexBuffer[quad[0]].color;
				clrbak[1] = aVertexBuffer[quad[1]].color;
				clrbak[2] = aVertexBuffer[quad[2]].color;
				clrbak[3] = aVertexBuffer[quad[3]].color;
				spcbak[0] = aVertexBuffer[quad[0]].specular;
				spcbak[1] = aVertexBuffer[quad[1]].specular;
				spcbak[2] = aVertexBuffer[quad[2]].specular;
				spcbak[3] = aVertexBuffer[quad[3]].specular;
				RGB_M(aVertexBuffer[quad[0]].color, num);
				RGB_M(aVertexBuffer[quad[1]].color, num);
				RGB_M(aVertexBuffer[quad[2]].color, num);
				RGB_M(aVertexBuffer[quad[3]].color, num);
				RGB_M(aVertexBuffer[quad[0]].specular, num);
				RGB_M(aVertexBuffer[quad[1]].specular, num);
				RGB_M(aVertexBuffer[quad[2]].specular, num);
				RGB_M(aVertexBuffer[quad[3]].specular, num);
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], &tex, 0);
				aVertexBuffer[quad[0]].color = clrbak[0];
				aVertexBuffer[quad[1]].color = clrbak[1];
				aVertexBuffer[quad[2]].color = clrbak[2];
				aVertexBuffer[quad[3]].color = clrbak[3];
				aVertexBuffer[quad[0]].specular = spcbak[0];
				aVertexBuffer[quad[1]].specular = spcbak[1];
				aVertexBuffer[quad[2]].specular = spcbak[2];
				aVertexBuffer[quad[3]].specular = spcbak[3];
			}
			else
			{
				pTex->drawtype = 7;
				AddQuadSorted(aVertexBuffer, quad[0], quad[1], quad[2], quad[3], pTex, 0);
			}
		}

		pTex->drawtype = drawbak;
	}

	tri = mesh->gt3;

	for (int i = 0; i < mesh->ngt3; i++, tri += 5)
	{
		pTex = &textinfo[tri[3] & 0x7FFF];
		envmap = 0;
		drawbak = pTex->drawtype;

		if (tri[4] & 1)
			pTex->drawtype = 2;

		if (tri[4] & 2)
		{
			envmap = 1;
			tex.drawtype = 0;
			tex.flag = pTex->flag;
			tex.tpage = ushort(nTextures - 3);
			tex.u1 = aMappedEnvUV[tri[0]].u;
			tex.v1 = aMappedEnvUV[tri[0]].v;
			tex.u2 = aMappedEnvUV[tri[1]].u;
			tex.v2 = aMappedEnvUV[tri[1]].v;
			tex.u3 = aMappedEnvUV[tri[2]].u;
			tex.v3 = aMappedEnvUV[tri[2]].v;
			num = (tri[4] & 0x7C) << 1;
		}

		if (GlobalAlpha == 0xFF000000)
		{
			if (envmap)
			{
				clrbak[0] = aVertexBuffer[tri[0]].color;
				clrbak[1] = aVertexBuffer[tri[1]].color;
				clrbak[2] = aVertexBuffer[tri[2]].color;
				spcbak[0] = aVertexBuffer[tri[0]].specular;
				spcbak[1] = aVertexBuffer[tri[1]].specular;
				spcbak[2] = aVertexBuffer[tri[2]].specular;
				RGB_M(aVertexBuffer[tri[0]].color, num);
				RGB_M(aVertexBuffer[tri[1]].color, num);
				RGB_M(aVertexBuffer[tri[2]].color, num);
				RGB_M(aVertexBuffer[tri[0]].specular, num);
				RGB_M(aVertexBuffer[tri[1]].specular, num);
				RGB_M(aVertexBuffer[tri[2]].specular, num);
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], &tex, 0);
				aVertexBuffer[tri[0]].color = clrbak[0];
				aVertexBuffer[tri[1]].color = clrbak[1];
				aVertexBuffer[tri[2]].color = clrbak[2];
				aVertexBuffer[tri[0]].specular = spcbak[0];
				aVertexBuffer[tri[1]].specular = spcbak[1];
				aVertexBuffer[tri[2]].specular = spcbak[2];
			}
			else if (!pTex->drawtype)
					AddTriZBuffer(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
				else if (pTex->drawtype <= 2)
					AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
		}
		else
		{
			if (envmap)
			{
				tex.drawtype = 7;
				clrbak[0] = aVertexBuffer[tri[0]].color;
				clrbak[1] = aVertexBuffer[tri[1]].color;
				clrbak[2] = aVertexBuffer[tri[2]].color;
				spcbak[0] = aVertexBuffer[tri[0]].specular;
				spcbak[1] = aVertexBuffer[tri[1]].specular;
				spcbak[2] = aVertexBuffer[tri[2]].specular;
				RGB_M(aVertexBuffer[tri[0]].color, num);
				RGB_M(aVertexBuffer[tri[1]].color, num);
				RGB_M(aVertexBuffer[tri[2]].color, num);
				RGB_M(aVertexBuffer[tri[0]].specular, num);
				RGB_M(aVertexBuffer[tri[1]].specular, num);
				RGB_M(aVertexBuffer[tri[2]].specular, num);
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], &tex, 0);
				aVertexBuffer[tri[0]].color = clrbak[0];
				aVertexBuffer[tri[1]].color = clrbak[1];
				aVertexBuffer[tri[2]].color = clrbak[2];
				aVertexBuffer[tri[0]].specular = spcbak[0];
				aVertexBuffer[tri[1]].specular = spcbak[1];
				aVertexBuffer[tri[2]].specular = spcbak[2];
			}
			else
			{
				pTex->drawtype = 7;
				AddTriSorted(aVertexBuffer, tri[0], tri[1], tri[2], pTex, 0);
			}
		}

		pTex->drawtype = drawbak;
	}
}

long GetFixedScale(long unit)
{
	long w, h, x, y;

	w = 640;
	h = 480;
	x = (phd_winwidth > w) ? MulDiv(phd_winwidth, unit, w) : unit;
	y = (phd_winheight > h) ? MulDiv(phd_winheight, unit, h) : unit;
	return x < y ? x : y;
}

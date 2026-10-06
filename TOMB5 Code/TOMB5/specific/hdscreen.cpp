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
// This file is built without the precompiled header (see tomb5.vcxproj, the
// same setting bc7.cpp has), because the tool build below must be able to
// leave pch.h out altogether - and with /Yu the compiler throws away
// everything in front of the pch include, conditionals included.
#ifdef HDSCREEN_NO_ENGINE
#include <stdlib.h>
#include "hdscreen.h"
#else
#include "../tomb5/pch.h"
#include "hdscreen.h"
#include "bc7.h"
#include "function_stubs.h"
#include "../game/gameflow.h"
#endif

// Optional high resolution loading screens.
//
// Chronicles keeps its 21 loading pictures in screens\SCREENS.STR: raw
// 640x480 A1R5G5B5, 614400 bytes each, no header of any kind, picture n at
// offset n * 614400. The remaster ships the same pictures redrawn at
// 1920x1080 as BC7 compressed .DDS files, one per level, named after the
// level: ANDREA1.DDS, JOBY2.DDS, RICH3.DDS and so on, plus TITLE.DDS and four
// localised logos LOGO_DE/EU/FR/US.DDS.
//
// If the player copies those into screens\HD\, this reads them instead. If
// the folder is not there, nothing here happens and the game shows Core's own
// pictures exactly as before - the same deal as the HQ sound bank in
// hqsfx.cpp and the HQ music in audio.cpp. The files are the player's own
// data and are never shipped with this project.
//
// The name is what makes this work under the randomizer. The loading picture
// has to follow the level, not the slot it happens to sit in, and the level's
// own file name out of SCRIPT.DAT is the one thing that always says which
// level is really being loaded.
//
// Direct3D 9 predates BC7 by a decade and cannot sample it, so the blocks are
// unpacked on the CPU in bc7.cpp and handed over as plain 32 bit pixels.

#ifndef HDSCREEN_NO_ENGINE

static void HDScreenPath(char* dest, const char* name)
{
	strcpy(dest, "screens\\HD\\");
	strcat(dest, name);
	strcat(dest, ".DDS");
}

const char* HDScreenLogo()
{
	// Only four logo files exist. English, and everything the remaster did
	// not draw its own logo for, get the European one.
	switch (Gameflow->Language)
	{
	case LNG_GERMAN:
		return "LOGO_DE";

	case LNG_FRENCH:
		return "LOGO_FR";

	case LNG_US:
		return "LOGO_US";

	default:
		return "LOGO_EU";
	}
}

const char* HDScreenLevelName(const char* wadName)
{
	const char* s;

	if (!wadName || !*wadName)
		return 0;

	s = strrchr(wadName, '\\');
	s = s ? s + 1 : wadName;
	if (!*s)
		return 0;

	// The Remaster ships no picture for the Security Breach cutscene;
	// it borrows Red Alert's instead.
	if (!_stricmp(s, "RICHCUT2"))
		return "RICH3";

	return s;
}

ulong* HDScreenRead(const char* name, long maxSide, long* width, long* height)
{
	FILE* fp;
	char path[80];
	uchar* raw;
	ulong* px;
	const char* why;
	long size;

	*width = 0;
	*height = 0;

	if (!name || !*name || strlen(name) > 40)
		return 0;

	HDScreenPath(path, name);
	fp = fopen(path, "rb");

	if (!fp)
		return 0;		// no HD folder, or no picture for this level - normal

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size < 148 || size > 64 * 1024 * 1024)
	{
		Log("HD screen: %s has an implausible size (%d bytes) - using the original picture", path, size);
		fclose(fp);
		return 0;
	}

	raw = (uchar*)malloc(size);

	if (!raw)
	{
		Log("HD screen: no memory for %s - using the original picture", path);
		fclose(fp);
		return 0;
	}

	if (fread(raw, 1, size, fp) != (size_t)size)
	{
		Log("HD screen: %s could not be read - using the original picture", path);
		free(raw);
		fclose(fp);
		return 0;
	}

	fclose(fp);
	px = BC7DecodeDDS(raw, size, maxSide, width, height, &why);
	free(raw);

	if (!px)
	{
		Log("HD screen: %s refused - %s - using the original picture", path, why);
		return 0;
	}

	Log("HD screen: %s decoded, %d x %d", path, *width, *height);
	return px;
}

#endif	// HDSCREEN_NO_ENGINE

// Everything below composes two decoded pictures and touches nothing else -
// no engine, no logging, no allocation - so DdsProbe can compile it and prove
// the result as a PNG without starting the game.

// Alpha below this counts as nothing at all. The remaster's logos fade out
// into a wide, almost empty halo; without a floor the box would be the whole
// 1920x1080 frame and the mark would end up a third of its proper size.
#define HD_LOGO_ALPHA_FLOOR	8

// ...and the alpha the lettering itself starts at. LOGO_DE has 51450 fully
// opaque pixels, so the letters are solid and this threshold is nowhere near
// a cliff: the box at alpha > 128 and the box at alpha > 224 differ by one
// pixel in width and two in height (DdsProbe -logo). See the placement
// comment in HDScreenComposeLogo for why two boxes are needed at all.
#define HD_LOGO_ALPHA_INK	128

// How far down the picture the top edge of the lettering sits, as a divisor
// of the picture height: 1080 / 72 = 15 pixels. Measured off the hand-built
// reference composite (TITLE_DE), whose lettering starts at y = 15.
#define HD_LOGO_TOP_DIV		72

static long HDScreenBoxAbove(const ulong* px, long w, long h, long minAlpha,
	long* x0, long* y0, long* x1, long* y1)
{
	const ulong* row;
	long x, y, loX, loY, hiX, hiY;

	if (!px || w <= 0 || h <= 0)
		return 0;

	loX = w;
	loY = h;
	hiX = -1;
	hiY = -1;

	for (y = 0; y < h; y++)
	{
		row = px + y * w;

		for (x = 0; x < w; x++)
		{
			if ((long)((row[x] >> 24) & 0xFF) <= minAlpha)
				continue;

			if (x < loX)
				loX = x;

			if (x > hiX)
				hiX = x;

			if (y < loY)
				loY = y;

			hiY = y;		// rows are walked top down
		}
	}

	if (hiX < 0)
		return 0;

	*x0 = loX;
	*y0 = loY;
	*x1 = hiX;
	*y1 = hiY;
	return 1;
}

long HDScreenAlphaBox(const ulong* px, long w, long h, long* x0, long* y0, long* x1, long* y1)
{
	return HDScreenBoxAbove(px, w, h, HD_LOGO_ALPHA_FLOOR, x0, y0, x1, y1);
}

// One bilinear tap, returned premultiplied (r*a, g*a, b*a, a). Interpolating
// straight colour across the edge of the mark would drag the colour of the
// transparent pixels next to it - black in these files - into the result and
// leave a dark seam around every letter; premultiplying first is the standard
// cure and costs nothing here. Sampling is clamped to the whole picture, not
// to the box, so the box edge keeps the soft fade that lies just outside it.
static ulong HDScreenSample(const ulong* px, long w, long h, float u, float v)
{
	const ulong* p[4];
	long ix, iy, jx, jy, i;
	float fu, fv, wt[4], r, g, b, a;

	// Truncation is not a floor for negative coordinates, and the left and
	// top edge of the box do sample half a pixel outside it.
	ix = (long)u;

	if ((float)ix > u)
		ix--;

	iy = (long)v;

	if ((float)iy > v)
		iy--;

	fu = u - (float)ix;
	fv = v - (float)iy;
	jx = ix + 1;
	jy = iy + 1;

	if (ix < 0) ix = 0; else if (ix > w - 1) ix = w - 1;
	if (jx < 0) jx = 0; else if (jx > w - 1) jx = w - 1;
	if (iy < 0) iy = 0; else if (iy > h - 1) iy = h - 1;
	if (jy < 0) jy = 0; else if (jy > h - 1) jy = h - 1;

	p[0] = px + iy * w + ix;
	p[1] = px + iy * w + jx;
	p[2] = px + jy * w + ix;
	p[3] = px + jy * w + jx;
	wt[0] = (1.0F - fu) * (1.0F - fv);
	wt[1] = fu * (1.0F - fv);
	wt[2] = (1.0F - fu) * fv;
	wt[3] = fu * fv;
	r = 0;
	g = 0;
	b = 0;
	a = 0;

	for (i = 0; i < 4; i++)
	{
		float ta = (float)((*p[i] >> 24) & 0xFF) * (1.0F / 255.0F);

		a += wt[i] * ta * 255.0F;
		r += wt[i] * ta * (float)((*p[i] >> 16) & 0xFF);
		g += wt[i] * ta * (float)((*p[i] >> 8) & 0xFF);
		b += wt[i] * ta * (float)(*p[i] & 0xFF);
	}

	return ((ulong)(a + 0.5F) << 24) | ((ulong)(r + 0.5F) << 16) |
		((ulong)(g + 0.5F) << 8) | (ulong)(b + 0.5F);
}

long HDScreenLogoSize(const ulong* logo, long lw, long lh, long maxW, long maxH, long* outW, long* outH)
{
	long x0, y0, x1, y1, w, h;

	if (!logo || maxW < 1 || maxH < 1)
		return 0;

	if (!HDScreenAlphaBox(logo, lw, lh, &x0, &y0, &x1, &y1))
		return 0;

	w = x1 - x0 + 1;
	h = y1 - y0 + 1;

	// Shrink to fit, never enlarge. Enlarging here would only blur: the
	// title menu scales the mark to the screen anyway, and it does that from
	// whatever the texture holds - so the texture may as well hold the
	// sharpest thing there is, which is the file's own pixels.
	if (w > maxW || h > maxH)
	{
		if (w * maxH > h * maxW)	// width is the tighter of the two
		{
			h = (h * maxW + w / 2) / w;
			w = maxW;
		}
		else
		{
			w = (w * maxH + h / 2) / h;
			h = maxH;
		}
	}

	if (w < 1)
		w = 1;

	if (h < 1)
		h = 1;

	*outW = w;
	*outH = h;
	return 1;
}

long HDScreenCutLogo(const ulong* logo, long lw, long lh, ulong* dst, long dstPitch, long outW, long outH)
{
	ulong px;
	long sx0, sy0, sx1, sy1;
	long srcW, srcH, x, y, i, a, ch[3];
	float su, sv;

	if (!logo || !dst || outW < 1 || outH < 1 || dstPitch < outW)
		return 0;

	if (!HDScreenAlphaBox(logo, lw, lh, &sx0, &sy0, &sx1, &sy1))
		return 0;

	srcW = sx1 - sx0 + 1;
	srcH = sy1 - sy0 + 1;

	// The same walk HDScreenComposeLogo makes over its destination - one tap
	// per output pixel, sampled at the pixel's centre.
	for (y = 0; y < outH; y++)
	{
		sv = (float)sy0 + ((float)y + 0.5F) * (float)srcH / (float)outH - 0.5F;

		for (x = 0; x < outW; x++)
		{
			su = (float)sx0 + ((float)x + 0.5F) * (float)srcW / (float)outW - 0.5F;
			px = HDScreenSample(logo, lw, lh, su, sv);
			a = (long)(px >> 24);

			// The tap comes back premultiplied, because that is the only way
			// to interpolate across the edge of the mark without dragging the
			// black of the transparent pixels into it. A texture, though, is
			// blended by the device with SRCALPHA / INVSRCALPHA - straight
			// alpha - so the colour has to be divided back out here.
			if (!a)
			{
				dst[y * dstPitch + x] = 0;
				continue;
			}

			ch[0] = ((long)((px >> 16) & 0xFF) * 255 + a / 2) / a;
			ch[1] = ((long)((px >> 8) & 0xFF) * 255 + a / 2) / a;
			ch[2] = ((long)(px & 0xFF) * 255 + a / 2) / a;

			for (i = 0; i < 3; i++)
				if (ch[i] > 255)
					ch[i] = 255;

			dst[y * dstPitch + x] = ((ulong)a << 24) | ((ulong)ch[0] << 16) |
				((ulong)ch[1] << 8) | (ulong)ch[2];
		}
	}

	return 1;
}

// Builds the picture the title screen used to be a separate file for: the
// title picture with the localised wordmark over it, horizontally centred and
// just under the top edge.
//
// The install ships the wordmark alone on a transparent 1920x1080 frame
// (LOGO_DE.DDS and its three siblings) and the title picture without it
// (TITLE.DDS); no file holds the two together. Putting them together used
// to mean a single hand-made composite file; that file
// cannot ship, because it is remaster material - so the engine does it at
// load time instead, out of the player's own two files.
//
// The mark keeps the size it has in its own picture, scaled by the ratio of
// the two picture widths - for the normal case of two 1920x1080 files that is
// exactly 1:1, and the resampling below is then an exact copy. Measured
// against a hand-built reference composite: its mark is 1020 pixels wide,
// matching the logo file's own 1020, at every ink threshold from 2% to 20%
// of the peak - the composite moved the mark rather than scaling it.
//
// Two boxes, not one. What has to be drawn is everything with any alpha at
// all: the lettering sits in a soft black halo that reaches about 20 pixels
// past it on every side. What has to be placed is only what the eye can see,
// the lettering. Anchoring the drawn box instead would hand those 20 pixels
// of invisible halo to the top margin and push the visible mark that much
// further down than the reference composite has it.
long HDScreenComposeLogo(ulong* base, long bw, long bh, const ulong* logo, long lw, long lh, long leftAlign)
{
	ulong* bd;
	ulong px;
	long sx0, sy0, sx1, sy1;
	long ix0, iy0, ix1, iy1;
	long srcW, srcH, dstW, dstH, dstX, dstY;
	long x, y, i, a, ia, ch[4];
	float su, sv;

	if (!base || !logo || bw <= 0 || bh <= 0 || lw <= 0 || lh <= 0)
		return 0;

	if (!HDScreenAlphaBox(logo, lw, lh, &sx0, &sy0, &sx1, &sy1))
		return 0;

	// A logo that is nothing but halo has no lettering to line up on; take
	// the drawn box for both jobs rather than giving up.
	if (!HDScreenBoxAbove(logo, lw, lh, HD_LOGO_ALPHA_INK, &ix0, &iy0, &ix1, &iy1))
	{
		ix0 = sx0;
		iy0 = sy0;
		ix1 = sx1;
		iy1 = sy1;
	}

	srcW = sx1 - sx0 + 1;
	srcH = sy1 - sy0 + 1;
	dstW = (srcW * bw + lw / 2) / lw;
	dstH = (srcH * bw + lw / 2) / lw;		// one uniform scale, from the width

	if (dstW < 1 || dstH < 1)
		return 0;

	// Centre the lettering across the picture and put its top edge one
	// seventysecond of the way down; the halo follows and is clipped where it
	// runs off the picture, exactly as in the reference composite. The US
	// emblem is three lines tall and would sit on Lara's brow when centred;
	// it goes in the top left corner instead, the other three wordmarks top
	// centre. Margin: 1/80 of the width.
	if (leftAlign)
		dstX = bw / 80 - ((ix0 - sx0) * bw + lw / 2) / lw;
	else
		dstX = (bw - ((ix1 - ix0 + 1) * bw + lw / 2) / lw) / 2 - ((ix0 - sx0) * bw + lw / 2) / lw;
	dstY = bh / HD_LOGO_TOP_DIV - ((iy0 - sy0) * bw + lw / 2) / lw;

	for (y = 0; y < dstH; y++)
	{
		if (dstY + y < 0 || dstY + y >= bh)
			continue;

		sv = (float)sy0 + ((float)y + 0.5F) * (float)srcH / (float)dstH - 0.5F;

		for (x = 0; x < dstW; x++)
		{
			if (dstX + x < 0 || dstX + x >= bw)
				continue;

			su = (float)sx0 + ((float)x + 0.5F) * (float)srcW / (float)dstW - 0.5F;
			px = HDScreenSample(logo, lw, lh, su, sv);
			a = (long)(px >> 24);

			if (!a)
				continue;

			// Standard "over": the tap is already premultiplied, so the base
			// only has to be faded by what is left of the alpha. Rounding in
			// the tap can leave a channel one above its own alpha, which
			// would carry into the byte above, so every channel is clamped.
			ia = 255 - a;
			bd = base + (dstY + y) * bw + dstX + x;
			ch[0] = a + (long)((*bd >> 24) & 0xFF) * ia / 255;
			ch[1] = (long)((px >> 16) & 0xFF) + (long)((*bd >> 16) & 0xFF) * ia / 255;
			ch[2] = (long)((px >> 8) & 0xFF) + (long)((*bd >> 8) & 0xFF) * ia / 255;
			ch[3] = (long)(px & 0xFF) + (long)(*bd & 0xFF) * ia / 255;

			for (i = 0; i < 4; i++)
				if (ch[i] > 255)
					ch[i] = 255;

			*bd = ((ulong)ch[0] << 24) | ((ulong)ch[1] << 16) | ((ulong)ch[2] << 8) | (ulong)ch[3];
		}
	}

	return 1;
}

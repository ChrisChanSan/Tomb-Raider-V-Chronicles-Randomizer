/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rule DrawChar's HD branch rests on - widen the sampled window and
 * the quad in the same ratio, so that a letter drawn from the remaster's
 * glyph atlas neither moves nor grows - is TROne's (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp, its "hdtext" glyph
 * draw; specific\hd\hd_fontmatch.h says why the citation carries no line
 * numbers). Written against that description, not copied; everything else
 * in this file, the glyph table and PrintString included, is TR5's own. See
 * THIRD-PARTY.md.
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
#include "text.h"
#include "../specific/specificfx.h"
#include "../specific/polyinsert.h"
#include "../specific/output.h"
#include "../specific/texture.h"
#include "../specific/function_stubs.h"
#include "../specific/3dmath.h"
#include "../specific/gamemain.h"
#include "../specific/hd/hd_font.h"		// the HD font

CVECTOR FontShades[10][32];
long font_height;
long default_font_height;
long GnFrameCounter;
uchar ScaleFlag;

char AccentTable[46][2] =
{
	/* The rows themselves live in game\accent.inc, so that the diagnostic
	 * tool FontProbe composes its umlauts from exactly this table instead
	 * of a copy of it. Nothing about it changed when it moved. */
#include "accent.inc"
};

#pragma warning(push)
#pragma warning(disable : 4838)
#pragma warning(disable : 4309)
static CVECTOR ShadeFromTo[10][2] =
{
	{ {128, 128, 128, 0}, {128, 128, 128, 0} },
	{ {128, 128, 128, 0}, {128, 128, 128, 0} },
	{ {128, 128, 128, 0}, {128, 128, 128, 0} },
	{ {128, 0, 0, 0}, {64, 0, 0, 0} },
	{ {0, 0, 160, 0}, {0, 0, 80, 0} },
	{ {128, 128, 128, 0}, {16, 16, 16, 0} },
	{ {192, 128, 64, 0}, {64, 16, 0, 0} },
	{ {16, 16, 16, 0}, {128, 128, 128, 0} },
	{ {224, 192, 0, 0}, {64, 32, 0, 0} },
	{ {128, 0, 0, 0}, {64, 0, 0, 0} },
};
#pragma warning(pop)

CHARDEF CharDef[106] =
{
	/* The rows themselves live in game\chardef.inc, so that the HD font
	 * and the diagnostic tool FontProbe read exactly these numbers
	 * instead of a copy of them. Nothing about the table changed when it
	 * moved. */
#include "chardef.inc"
};

void DrawChar(long x, long y, ushort col, CHARDEF* def)
{
	D3DTLVERTEX v[4];
	TEXTURESTRUCT tex;
	const HDFontGlyph* hd;
	float u1, v1, u2, v2;
	long x1, y1, x2, y2, top, bottom;

	x1 = x + phd_winxmin;
	x2 = x1 + def->w;
	y1 = y + phd_winymin + def->YOffset;
	y2 = y + phd_winymin + def->h + def->YOffset;
	setXY4(v, x1, y1, x2, y1, x2, y2, x1, y2, (long)f_mznear, clipflags);

	top = *(long*)&FontShades[col][2 * def->TopShade];
	bottom = *(long*)&FontShades[col][2 * def->BottomShade];
	v[0].color = top;
	v[1].color = top;
	v[2].color = bottom;
	v[3].color = bottom;

	top = *(long*)&FontShades[col][(2 * def->TopShade) + 1];
	bottom = *(long*)&FontShades[col][(2 * def->BottomShade) + 1];
	v[0].specular = top;
	v[1].specular = top;
	v[2].specular = bottom;
	v[3].specular = bottom;

	u1 = def->u + (1.0F / 512.0F);
	v1 = def->v + (1.0F / 512.0F);
	u2 = 512.0F / float(phd_winxmax + 1) * (float)def->w * (1.0F / 256.0F) + def->u - (1.0F / 512.0F);
	v2 = 240.0F / float(phd_winymax + 1) * (float)def->h * (1.0F / 256.0F) + def->v - (1.0F / 512.0F);
	tex.u1 = u1;
	tex.v1 = v1;
	tex.u2 = u2;
	tex.v2 = v1;
	tex.u3 = u2;
	tex.v3 = v2;
	tex.u4 = u1;
	tex.v4 = v2;

	tex.drawtype = 1;
	tex.tpage = ushort(nTextures - 2);
	tex.flag = 0;

	/* Draws the same letter out of the remaster's atlas instead, when one
	 * exists. With hd_enable off, HDFontGlyphAt returns nothing and the
	 * quad below is the same as without this branch - same position,
	 * size, FontShades colour and specular, same page. Every line of text
	 * in the game (title menu, ring, options, save list, statistics, HUD,
	 * item names, subtitles) goes through PrintString and therefore here.
	 *
	 * The UV is shifted, not recomputed: the remaster's atlas is the
	 * classic page at eight times the size, so a normalised coordinate on
	 * one is a normalised coordinate on the other, and the glyph sits at
	 * the same place plus a whole number of classic texels (hd_font.cpp
	 * works that number out once per session). Shifting only the window
	 * changes nothing else: the letter cannot grow, shrink or slide,
	 * because no factor was touched. Measured with FontProbe -sample: the
	 * texels a screen pixel walks are the same number to the last bit.
	 *
	 * The quad is widened by the same fraction as the window, which is
	 * TROne's rule: the classic arithmetic samples the rectangle inset by
	 * half a texel on each side, needed because the classic page packs
	 * its glyphs with no gap, and without that inset a letter would bleed
	 * into its neighbour. At eight times the size that inset is four HD
	 * texels, which would cut off the antialiased rim and drop shadow the
	 * remaster paints around every letter (measured: 12.6% of the glyph's
	 * ink). Widening window and quad together keeps that rim visible
	 * while the letter still stands on the same pixel at the same size.
	 *
	 * The margin (hd->mu) is per glyph, from HDFontMargins
	 * (specific\hd\hd_fontmatch.cpp): a uniform margin pulls a slice of
	 * the neighbouring glyph in beside tightly packed marks such as an
	 * apostrophe or a comma, which HDFontMargins avoids by giving each
	 * glyph as much room as it can have and no more.
	 *
	 * The clip flags stay the ones setXY4 computed for the classic quad:
	 * a letter that was wholly on screen stays wholly on screen, the
	 * widened edge is a pixel or two of nearly transparent atlas, and the
	 * rasteriser clips it at the viewport regardless. */
	hd = HDFontGlyphAt((int)(def - CharDef));

	if (hd)
	{
		const float mu = hd->mu;
		const float mx = (u2 - u1) > 0.0F ? float(x2 - x1) * mu / (u2 - u1) : 0.0F;
		const float my = (v2 - v1) > 0.0F ? float(y2 - y1) * mu / (v2 - v1) : 0.0F;

		v[0].sx -= mx;	v[0].sy -= my;
		v[1].sx += mx;	v[1].sy -= my;
		v[2].sx += mx;	v[2].sy += my;
		v[3].sx -= mx;	v[3].sy += my;

		tex.u1 = u1 + hd->du - mu;
		tex.v1 = v1 + hd->dv - mu;
		tex.u2 = u2 + hd->du + mu;
		tex.v2 = v1 + hd->dv - mu;
		tex.u3 = u2 + hd->du + mu;
		tex.v3 = v2 + hd->dv + mu;
		tex.u4 = u1 + hd->du - mu;
		tex.v4 = v2 + hd->dv + mu;
		tex.tpage = HD_FONT_TPAGE;
	}

	nPolyType = 4;
	AddQuadClippedSorted(v, 0, 1, 2, 3, &tex, 0);
}

static void AccentToCode(uchar& s)
{
	if (s == (uchar)'¡')
		s = 128 + 45;
	else if (s == (uchar)'¿')
		s = 128 + 40;
	else if (s == (uchar)'Ñ')
		s = 128 + 37;
	else if (s == (uchar)'ñ')
		s = 128 + 36;
	else if (s == (uchar)'ú')
		s = 128 + 35;
	else if (s == (uchar)'ó')
		s = 128 + 34;
	else if (s == (uchar)'í')
		s = 128 + 33;
	else if (s == (uchar)'á')
		s = 128 + 32;
	else if (s == (uchar)'Ó')
		s = 128 + 28;
	else if (s == (uchar)'Ü')
		s = 128 + 26;
	else if (s == (uchar)'Ö')
		s = 128 + 25;
	else if (s == (uchar)'ÿ')
		s = 128 + 24;
	else if (s == (uchar)'ù')
		s = 128 + 23;
	else if (s == (uchar)'û')
		s = 128 + 22;
	else if (s == (uchar)'ò')
		s = 128 + 21;
	else if (s == (uchar)'ö')
		s = 128 + 20;
	else if (s == (uchar)'ô')
		s = 128 + 19;
	else if (s == (uchar)'Á')
		s = 128 + 17;
	else if (s == (uchar)'É')
		s = 128 + 16;
	else if (s == (uchar)'Â')
		s = 128 + 15;
	else if (s == (uchar)'Ä')
		s = 128 + 14;
	else if (s == (uchar)'ì')
		s = 128 + 13;
	else if (s == (uchar)'î')
		s = 128 + 12;
	else if (s == (uchar)'ï')
		s = 128 + 11;
	else if (s == (uchar)'è')
		s = 128 + 10;
	else if (s == (uchar)'ë')
		s = 128 + 9;
	else if (s == (uchar)'ê')
		s = 128 + 8;
	else if (s == (uchar)'ç')
		s = 128 + 0;
	else if (s == (uchar)'á')
		s = 128 + 6;
	else if (s == (uchar)'à')
		s = 128 + 5;
	else if (s == (uchar)'ä')
		s = 128 + 4;
	else if (s == (uchar)'â')
		s = 128 + 3;
	else if (s == (uchar)'é')
		s = 128 + 2;
	else if (s == (uchar)'ü')
		s = 128 + 1;
}

long GetStringLength(const char* string, long* top, long* bottom)
{
	CHARDEF* def;
	long s, accent, length, lowest, highest, y;

	s = *string++;
	length = 0;
	accent = 0;
	lowest = -1024;
	highest = 1024;

	while (s)
	{
		if (s == '\n')
			break;

		if (s == ' ')
			length += long((float(phd_winxmax + 1) / 640.0F) * 8.0F);
		else if (s == '\t')
		{
			length += 40;

			if (top && highest > -12)
				highest = -12;

			if (bottom && lowest < 2)
				lowest = 2;
		}
		else if (s >= 20)
		{
			AccentToCode((uchar&)s);

			if (s < ' ')
				def = &CharDef[s + 74];
			else
			{
				if (s >= 128 && s <= 173)
				{
					accent = 1;
					s = AccentTable[s - 128][0];
				}

				def = &CharDef[s - '!'];
			}

			if (ScaleFlag)
				length += def->w - def->w / 4;
			else
				length += def->w;

			y = def->YOffset;

			if (top && y < highest)
				highest = def->YOffset;

			if (bottom && def->h + y > lowest)
				lowest = def->h + y;
		}

		s = *string++;
	}

	if (top)
	{
		if (accent)
			highest -= 4;

		*top = highest;
	}

	if (bottom)
		*bottom = lowest;

	return length;
}

void PrintString(long x, long y, uchar col, const char* string, ushort flags)
{
	CHARDEF* def;
	CHARDEF* accent;
	long x2, bottom, l, top, bottom2;
	uchar s;

	if (flags & FF_BLINK && GnFrameCounter & 0x10)
		return;

	ScaleFlag = (flags & FF_SMALL) != 0;
	x2 = GetStringLength(string, 0, &bottom);

	if (flags & FF_CENTER)
		x2 = x - (x2 >> 1);
	else if (flags & FF_RJUSTIFY)
		x2 = x - x2;
	else
		x2 = x;

	s = *string++;

	while (s)
	{
		if (s == '\n')
		{
			if (*string == '\n')
			{
				bottom = 0;
				y += 16;
			}
			else
			{
				l = GetStringLength(string, &top, &bottom2);

				if (flags & FF_CENTER)
					x2 = x - (l >> 1);
				else if (flags & FF_RJUSTIFY)
					x2 = x - l;
				else
					x2 = x;

				y += bottom - top + 2;
				bottom = bottom2;
			}

			s = *string++;
			continue;
		}

		if (s == ' ')
		{
			if (ScaleFlag)
				x2 += 6;
			else
				x2 += long(float(phd_winxmax + 1) / 640.0F * 8.0F);

			s = *string++;
			continue;
		}

		if (s == '\t')
		{
			x2 += 40;
			s = *string++;
			continue;
		}

		if (s < 20)
		{
			col = s - 1;
			s = *string++;
			continue;
		}

		AccentToCode(s);

		if (s >= 128 && s <= 173)
		{
			def = &CharDef[AccentTable[s - 128][0] - '!'];
			accent = &CharDef[AccentTable[s - 128][1] - '!'];
			DrawChar(x2, y, col, def);

			if (AccentTable[s - 128][1] != ' ')
				DrawChar(def->w / 2 + x2 - 3, y + def->YOffset, col, accent);
		}
		else
		{
			if (s < ' ')
				def = &CharDef[s + 74];
			else if (s - '!' < 106)
				def = &CharDef[s - '!'];
			else
			{
				// Glyph outside the PC font: CharDef has 106 entries plus
				// the accent codes 128..173 above; without this check,
				// anything beyond (e.g. the kana indices of the Japanese
				// PSX language file, bytes up to 181) reads past the
				// table. Skip instead of crash; what the font can show
				// is shown.
				s = *string++;
				continue;
			}

			DrawChar(x2, y, col, def);
		}

		if (ScaleFlag)
			x2 += def->w - def->w / 4;
		else
			x2 += def->w;

		s = *string++;
	}

	ScaleFlag = 0;
}

void InitFont()
{
	static CHARDEF copy[106];
	static long init = 1;
	long color, specular;
	ushort r, g, b;
	short h, w, yoff;
	uchar fr, fg, fb, tr, tg, tb;

	for (int i = 0; i < 10; i++)
	{
		fr = ShadeFromTo[i][0].r;
		fg = ShadeFromTo[i][0].g;
		fb = ShadeFromTo[i][0].b;
		tr = ShadeFromTo[i][1].r;
		tg = ShadeFromTo[i][1].g;
		tb = ShadeFromTo[i][1].b;

		for (int j = 0; j < 16; j++)
		{
			r = (ushort)(((tr * j) >> 4) + ((fr * (16 - j)) >> 4));
			g = (ushort)(((tg * j) >> 4) + ((fg * (16 - j)) >> 4));
			b = (ushort)(((tb * j) >> 4) + ((fb * (16 - j)) >> 4));

			if (r > 255)
				r = 255;

			if (g > 255)
				g = 255;

			if (b > 255)
				b = 255;

			aCalcColorSplit(RGBONLY(b, g, r), &color, &specular);

			r = CLRR(color);
			g = CLRG(color);
			b = CLRB(color);
			FontShades[i][j << 1].r = (uchar)r;
			FontShades[i][j << 1].g = (uchar)g;
			FontShades[i][j << 1].b = (uchar)b;
			FontShades[i][j << 1].a = (uchar)0xFF;

			r = CLRR(specular);
			g = CLRG(specular);
			b = CLRB(specular);
			FontShades[i][(j << 1) + 1].r = (uchar)r;
			FontShades[i][(j << 1) + 1].g = (uchar)g;
			FontShades[i][(j << 1) + 1].b = (uchar)b;
			FontShades[i][(j << 1) + 1].a = (uchar)0xFF;
		}
	}

	if (init)
	{
		for (int i = 0; i < 106; i++)
		{
			copy[i].h = CharDef[i].h;
			copy[i].w = CharDef[i].w;
			copy[i].YOffset = CharDef[i].YOffset;
		}

		init = 0;
	}

	for (int i = 0; i < 106; i++)
	{
		h = short((float)copy[i].h * float(phd_winymax / 240.0F));
		w = short((float)copy[i].w * float(phd_winxmax / 512.0F));
		yoff = short((float)copy[i].YOffset * float(phd_winymax / 240.0F));
		CharDef[i].h = h;
		CharDef[i].w = w;
		CharDef[i].YOffset = yoff;
	}

	font_height = GetFixedScale(28);
	default_font_height = font_height;
}

void UpdatePulseColour()
{
	long color, specular;
	static uchar PulseCnt = 0;
	uchar c, r, g, b;

	PulseCnt = (PulseCnt + 1) & 0x1F;

	if (PulseCnt > 16)
		c = -PulseCnt;
	else
		c = PulseCnt;

	c <<= 3;
	aCalcColorSplit(RGBONLY(c, c, c), &color, &specular);

	for (int i = 0; i < 16; i++)
	{
		r = CLRR(color);
		g = CLRG(color);
		b = CLRB(color);
		FontShades[1][i << 1].r = r;
		FontShades[1][i << 1].g = g;
		FontShades[1][i << 1].b = b;

		r = CLRR(specular);
		g = CLRG(specular);
		b = CLRB(specular);
		FontShades[1][(i << 1) + 1].r = r;
		FontShades[1][(i << 1) + 1].g = g;
		FontShades[1][(i << 1) + 1].b = b;
	}
}

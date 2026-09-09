/*
 * Modified 2026 by ChrisChanSan.
 *
 * The idea of finding the classic glyphs in the remaster's atlas by template
 * match, and the rule of widening image and screen in the same ratio so that
 * a letter neither moves nor grows, are TROne's (Copyright 2026 Todesengel,
 * GPLv3, host\render\game_renderer.cpp:7367-7870 and :11211-11269). Written
 * against that description, not copied - and it needs neither their sprite
 * scan nor their "is this an alphabet" gate, because the 106 glyph rectangles
 * are known here (game\chardef.inc). See THIRD-PARTY.md.
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
#include "../../tomb5/pch.h"		// windows.h and d3d9.h, as every engine file has them

#include "hd_font.h"
#include "hd_fontmatch.h"
#include "hd_install.h"
#include "hd_level.h"			// HDEnabled - the one guard
#include "../bc7.h"
#include "../function_stubs.h"		// Log
#include "../winmain.h"			// App.dx.lpD3DDevice
#include "../../global/types.h"		// CHARDEF

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	/* THE 106 RECTANGLES, unscaled. The same include game\text.cpp fills
	 * CharDef[106] from, so the two cannot drift apart - and it has to be
	 * this file's own copy and not CharDef itself, because InitFont
	 * overwrites CharDef's w, h and YOffset in place with the values for the
	 * current screen size and this needs the numbers as the artist drew
	 * them. */
	const CHARDEF kUnscaled[106] =
	{
#include "../../game/chardef.inc"
	};

	const int kGlyphs = (int)(sizeof(kUnscaled) / sizeof(kUnscaled[0]));

	unsigned long* g_page;			// the level's classic font page
	char g_page_md5[33];			// what has been matched
	char g_matched_md5[33];
	HDFontHit g_hit[106];
	HDFontCell g_cell[106];
	HDFontGlyph g_glyph[106];
	int g_found;
	int g_atlas = -1;			// 0 or 1: which FONT<n> won
	bool g_matched;

	IDirect3DTexture9* g_tex;
	long g_tex_side;

	bool read_whole(const char* path, unsigned char** out, unsigned long* size)
	{
		std::FILE* file = std::fopen(path, "rb");
		long length;

		*out = 0;
		*size = 0;

		if (!file)
			return false;

		std::fseek(file, 0, SEEK_END);
		length = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);

		if (length <= 0 || length > 64 * 1024 * 1024)
		{
			std::fclose(file);
			return false;
		}

		*out = (unsigned char*)std::malloc((size_t)length);

		if (!*out || std::fread(*out, 1, (size_t)length, file) != (size_t)length)
		{
			std::free(*out);
			*out = 0;
			std::fclose(file);
			return false;
		}

		std::fclose(file);
		*size = (unsigned long)length;
		return true;
	}

	// "<install>\TEX\FONT<n>[_OG].DDS". HDInstallRoot() is already the game 5
	// folder of the bundle.
	const char* font_path(int which, bool og)
	{
		static char path[300];
		std::snprintf(path, sizeof(path), "%s\\TEX\\FONT%d%s.DDS",
			HDInstallRoot(), which, og ? "_OG" : "");
		return path;
	}

	/* THE MATCH, and which of the two alphabets wins.
	 *
	 * Measured over the shipped bundle: FONT0 finds 93 of the 106 rectangles
	 * and FONT1 finds 22 - so FONT1 is a different alphabet (its 22 are the
	 * letters the two share) and the count is what says so. TROne rejects
	 * theirs with a threshold on the same count; here the two are simply
	 * compared and the better one taken, which needs no threshold at all and
	 * still refuses an installation that has neither.
	 */
	void run_match()
	{
		HDFontCell cells[106];
		HDFontHit hits[2][106];
		unsigned long* atlas[2] = { 0, 0 };
		int found[2] = { 0, 0 };

		g_found = 0;
		g_atlas = -1;
		g_matched = false;
		std::memset(g_hit, 0, sizeof(g_hit));

		for (int i = 0; i < kGlyphs; i++)
			cells[i] = HDFontCellOf(kUnscaled[i].u, kUnscaled[i].v,
				kUnscaled[i].w, kUnscaled[i].h);

		for (int which = 0; which < 2; which++)
		{
			unsigned char* file = 0;
			unsigned long size = 0;
			const char* why = "";

			std::memset(hits[which], 0, sizeof(hits[which]));

			if (!read_whole(font_path(which, true), &file, &size))
			{
				Log("HDLOG font atlas=FONT%d not there (%s)", which, font_path(which, true));
				continue;
			}

			atlas[which] = (unsigned long*)std::malloc(HD_FONT_SIDE * HD_FONT_SIDE * 4);

			if (!atlas[which] || !HDFontOgDecode(file, size, atlas[which], &why))
			{
				Log("HDLOG font atlas=FONT%d refused reason=%s", which, why);
				std::free(file);
				std::free(atlas[which]);
				atlas[which] = 0;
				continue;
			}

			std::free(file);
			found[which] = HDFontMatchAll(atlas[which], g_page, cells, kGlyphs,
				HD_FONT_ACCEPT, hits[which]);
			Log("HDLOG font atlas=FONT%d matched=%d/%d", which, found[which], kGlyphs);
		}

		if (found[0] <= 0 && found[1] <= 0)
		{
			Log("HDLOG font atlas=none matched=0/%d page=%s - text stays classic",
				kGlyphs, g_page_md5);
			std::free(atlas[0]);
			std::free(atlas[1]);
			return;
		}

		g_atlas = found[1] > found[0] ? 1 : 0;
		g_found = found[g_atlas];
		std::memcpy(g_hit, hits[g_atlas], sizeof(g_hit));
		std::memcpy(g_cell, cells, sizeof(g_cell));

		std::free(atlas[0]);
		std::free(atlas[1]);

		/* AND THE NUMBER THE DRAW SEAM NEEDS: the offset from the classic
		 * rectangle to the atlas, in normalised page units. The atlas is the
		 * classic page times eight, so 256ths of the page are the same
		 * fraction of it - which is why DrawChar can add this to a classic UV
		 * and nothing scales.
		 *
		 * The margin is the other number and it cannot be worked out here:
		 * how much room a letter has is a property of the BIG picture, and
		 * only HDFontBuild has that. Until then every glyph gets none, which
		 * is the safe answer and not a wrong one - a glyph drawn without a
		 * margin loses a little of its shadow and nothing else. */
		for (int i = 0; i < kGlyphs; i++)
		{
			g_glyph[i].du = (float)(g_hit[i].x - cells[i].x) / (float)HD_FONT_SIDE;
			g_glyph[i].dv = (float)(g_hit[i].y - cells[i].y) / (float)HD_FONT_SIDE;
			g_glyph[i].mu = 0.0f;
		}

		g_matched = true;
		std::strcpy(g_matched_md5, g_page_md5);

		Log("HDLOG font atlas=FONT%d matched=%d/%d below=%d page=%s",
			g_atlas, g_found, kGlyphs, kGlyphs - g_found, g_page_md5);

		// And every glyph that did NOT make it, with its score, so that a
		// letter drawn classic in the middle of an HD line has a line saying
		// why. The twelve pad icons are the expected ones.
		for (int i = 0; i < kGlyphs; i++)
		{
			if (g_hit[i].ok)
				continue;

			Log("HDLOG font miss glyph=%d '%c' score=%.3f - stays classic",
				i, (i + '!') <= '~' ? (char)(i + '!') : '?', g_hit[i].score);
		}
	}
}

void HDFontLevelPage(const void* page)
{
	char md5[33];

	// The guard. With hd_enable off, or without an installed Remaster, not a
	// byte of the page is even copied and DrawChar never asks anything.
	if (!page || !HDEnabled())
		return;

	if (!g_page)
	{
		g_page = (unsigned long*)std::malloc(HD_FONT_SIDE * HD_FONT_SIDE * 4);

		if (!g_page)
			return;
	}

	std::memcpy(g_page, page, (size_t)HD_FONT_SIDE * HD_FONT_SIDE * 4);
	HDFontMd5(g_page, (unsigned long)HD_FONT_SIDE * HD_FONT_SIDE * 4, md5);

	/* ALL SIXTEEN SHIPPED LEVEL FILES CARRY THE SAME FONT PAGE - measured
	 * with FontProbe -pages, md5 191cfc2fdba5b4c54024361c463d7537 for every
	 * one of them including the title. So the match runs once in a session
	 * and every further level load is this comparison. It is keyed on the
	 * page and not on a flag, because a level file that ever did carry
	 * another font page has to be matched again rather than drawn with a
	 * table that belongs to a different alphabet. */
	if (g_matched && !std::strcmp(md5, g_matched_md5))
	{
		std::strcpy(g_page_md5, md5);
		return;
	}

	std::strcpy(g_page_md5, md5);
	run_match();
}

void HDFontBuild()
{
	unsigned char* file = 0;
	unsigned long size = 0;
	long levels;
	long side = 0, high = 0;

	if (!HDEnabled() || !g_matched || g_atlas < 0)
		return;

	if (g_tex)
		return;			// made once, kept for the whole session

	if (!App.dx.lpD3DDevice)
		return;

	if (!read_whole(font_path(g_atlas, false), &file, &size))
	{
		Log("HDLOG font FONT%d.DDS not there - text stays classic", g_atlas);
		return;
	}

	if (!BC7DDSShape(file, size, &side, &high, &levels) || side != high ||
		side != HD_FONT_HD_SIDE)
	{
		Log("HDLOG font FONT%d.DDS is %ld x %ld, expected %d x %d - text stays classic",
			g_atlas, side, high, HD_FONT_HD_SIDE, HD_FONT_HD_SIDE);
		std::free(file);
		return;
	}

	/* THE WHOLE MIP CHAIN, and it is not optional. At 1920 x 1080 a
	 * thirteen-texel glyph is drawn 58 pixels high out of 104 atlas texels -
	 * the atlas is MINIFIED by 1.8, and at 1280 x 720 by 2.7. Without mips
	 * that is a bilinear undersample and thin strokes crawl; with them and
	 * the LOD bias in HWR_DrawSortList the level lands just under mip 0 and
	 * stays sharp. The chain is six levels, 2048 down to 64, and costs a
	 * third of the 16 MiB the top level costs.
	 *
	 * D3DPOOL_MANAGED, like every classic texture page (CreateTexturePage,
	 * specific\texture.cpp): a managed texture survives a device RESET on its
	 * own, so only a device that is released outright takes it, and that is
	 * what HDFontDeviceLost is for. */
	if (FAILED(App.dx.lpD3DDevice->CreateTexture((UINT)side, (UINT)side,
		(UINT)levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_tex, 0)))
	{
		Log("HDLOG font FONT%d.DDS: no texture (%ld x %ld, %ld levels) - text stays classic",
			g_atlas, side, side, levels);
		g_tex = 0;
		std::free(file);
		return;
	}

	for (long mip = 0; mip < levels; mip++)
	{
		D3DLOCKED_RECT rect;
		const char* why = "";
		long w = 0, h = 0;
		unsigned long* px = BC7DecodeDDSMip(file, size, HD_FONT_HD_SIDE, mip, &w, &h, &why);

		if (!px)
			break;			// the chain ends where whole 4x4 blocks end

		/* THE PER-GLYPH MARGIN, and this is the one place that can work it
		 * out: it needs the big picture, and the big picture exists here and
		 * nowhere else. A uniform margin was tried first and was wrong - the
		 * atlas packs the small marks close together, so the one texel a
		 * capital letter has to spare pulls a slice of the neighbouring tick
		 * in beside an apostrophe and beside a comma, plainly visible in a
		 * rendered line. Every glyph now gets as much as it can have and no
		 * more. */
		if (mip == 0)
		{
			float margin[106];
			int full = 0, none = 0;

			HDFontMargins(px, w, g_page, g_cell, g_hit, kGlyphs, HD_FONT_MARGIN, margin);

			for (int i = 0; i < kGlyphs; i++)
			{
				g_glyph[i].mu = margin[i] / (float)HD_FONT_SIDE;

				if (!g_hit[i].ok)
					continue;

				if (margin[i] >= HD_FONT_MARGIN)
					full++;
				else if (margin[i] <= 0.0f)
					none++;
			}

			Log("HDLOG font margin ceiling=%.2f texel(s): %d glyph(s) get all of "
				"it, %d get none, the rest get what fits", HD_FONT_MARGIN, full, none);
		}

		if (SUCCEEDED(g_tex->LockRect((UINT)mip, &rect, 0, 0)))
		{
			const unsigned char* src = (const unsigned char*)px;
			unsigned char* dst = (unsigned char*)rect.pBits;

			for (long y = 0; y < h; y++)
			{
				std::memcpy(dst, src, (size_t)w * 4);
				src += (size_t)w * 4;
				dst += rect.Pitch;
			}

			g_tex->UnlockRect((UINT)mip);
		}

		std::free(px);
	}

	std::free(file);
	g_tex_side = side;
	Log("HDLOG font atlas=FONT%d texture=%ld x %ld mips=%ld matched=%d/%d page=%s",
		g_atlas, side, side, levels, g_found, kGlyphs, g_page_md5);
}

void HDFontDeviceLost()
{
	if (!g_tex)
		return;

	g_tex->Release();
	g_tex = 0;
	g_tex_side = 0;
	Log("HDLOG font texture released with the device - text classic until the next level");
}

const HDFontGlyph* HDFontGlyphAt(int index)
{
	// Not behind HDEnabled(): this is asked once per drawn character and the
	// three tests below already say no in every case that one would.
	if (!g_tex || !g_matched || index < 0 || index >= kGlyphs)
		return 0;

	if (!g_hit[index].ok)
		return 0;

	return &g_glyph[index];
}

IDirect3DTexture9* HDFontTexture()
{
	return g_tex;
}

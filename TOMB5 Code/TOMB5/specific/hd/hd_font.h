/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The idea of finding the classic glyphs in the remaster's atlas by template
 * match, and the rule of widening image and screen in the same ratio so that
 * a letter neither moves nor grows, are TROne's (Copyright 2026 Todesengel,
 * GPLv3, host\render\game_renderer.cpp:7367-7870 and :11211-11269). Written
 * against that description, not copied. See THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#pragma once

// EVERY LINE OF TEXT THE GAME DRAWS, OUT OF THE REMASTER'S FONT ATLAS
// (#123, fix round Q).
//
// The engine has exactly one font: a 256 x 256 page at the end of Textures[]
// and a table of 106 rectangles on it (game\text.cpp). Title menu, inventory
// ring, options, load and save lists, statistics, HUD counters, item names
// and subtitles all go through PrintString -> DrawChar, so ONE branch in
// DrawChar covers all of them - there is no second text path anywhere.
//
// WHAT THE REMASTER SHIPS (measured, FontProbe -atlas):
//
//   <install>\5\TEX\FONT0_OG.DDS    256 x 256, uncompressed B8G8R8A8. The
//                                   classic glyph bitmaps, re-typeset in
//                                   ASCII order.
//   <install>\5\TEX\FONT0.DDS       2048 x 2048, BC7, six mip levels. The
//                                   same alphabet in the same places at eight
//                                   times the size, REDRAWN - white letters
//                                   with a dark rim and a drop shadow baked
//                                   in. Shrunk back to 256 it differs from
//                                   the _OG picture in 12.6 % of the texels
//                                   of the matched rectangles, so it is a new
//                                   drawing and not a magnified copy.
//   FONT1(.._OG).DDS                a second alphabet. Checked and rejected:
//                                   22 of the 106 rectangles are found in it
//                                   against 93 in FONT0. Which of the two is
//                                   used is decided by that count, per run,
//                                   not by its name.
//
// HOW A GLYPH IS FOUND: the classic rectangle's ink mask is slid over the
// _OG atlas and scored intersection over union (specific\hd\hd_fontmatch.cpp).
// 93 of the 106 rectangles are found, 92 of them at score 1.000 - the
// remaster took the glyph bitmaps over texel for texel. The thirteen that are
// not found are the twelve PlayStation pad icons, which the remaster's atlas
// does not have at all, and the c-cedilla at 0.895; each of those keeps the
// classic picture, on its own, without affecting any other letter.
//
// HOW IT IS DRAWN: the atlas is the classic page times eight, so in
// NORMALISED texture coordinates the same glyph sits at the same place plus a
// whole number of classic texels. DrawChar therefore does not recompute its
// UVs - it SHIFTS them, which is why nothing can move or change size. The
// quad and the window are then both widened by HD_FONT_MARGIN in the same
// ratio, so that the drop shadow is not cut off at the rectangle's edge.
//
// Everything here is dead while the registry setting hd_enable is 0: no file
// is opened, no match runs, HDFontGlyphAt returns nothing and DrawChar takes
// the classic branch it always took.

struct IDirect3DTexture9;

/* THE TPAGE OF A TEXT QUAD DRAWN FROM THE ATLAS.
 *
 * A quad names its texture by an index into Textures[], which holds 256
 * entries, and the sort list carries that index in a `short`. The HD font is
 * NOT in that array - it must not be, because the font, the sky and the two
 * title pages are addressed as nTextures minus a small number and every one
 * of them would move if the count grew (MODIFICATIONS.md, and the same note
 * over the water frame pages in hd_level.cpp).
 *
 * So the quad carries a number that is not an index at all, and
 * HWR_DrawSortList (specific\polyinsert.cpp) turns it into this texture
 * before it can be used as one. 4096 is far outside 0..255 and still fits a
 * signed short with room to spare; nothing else in the engine ever writes a
 * tpage other than an index into Textures[] - LoadTextures refuses a level
 * with more than 256 pages outright (specific\file.cpp). */
#define HD_FONT_TPAGE	4096

// One glyph's two numbers, both in normalised page units - atlas texels
// divided by 256 - so that they can be added straight to a classic UV.
struct HDFontGlyph
{
	// Where the glyph sits on the atlas, as an offset from its classic
	// rectangle.
	float du, dv;

	// How far its window may be opened without touching the letter beside it
	// on the atlas, worked out per glyph by HDFontMargins - 0 for a mark that
	// is packed too tightly to afford any. See HD_FONT_MARGIN.
	float mu;
};

/* The font page of the level that is loading, 256 x 256 A8R8G8B8. Called
 * from LoadTextures (specific\file.cpp) at the line that reads that page, on
 * the LOAD THREAD, and it does no Direct3D at all - it copies the page, and
 * when the page is not the one already matched it runs the match. Measured:
 * all sixteen shipped level files carry the same font page (md5
 * 191cfc2fdba5b4c54024361c463d7537), so the match runs once per session and
 * not once per level - but it is keyed on the page's own md5, so a level file
 * that ever carries a different font page is matched again rather than drawn
 * with the wrong table. */
void HDFontLevelPage(const void* page);

// The Direct3D side: decodes FONT<n>.DDS and makes the texture. MAIN THREAD,
// called beside HDLevelBuild from S_LoadLevelFile, for the reason given there
// - the device belongs to that thread. Does nothing when the texture already
// exists, which after the first level is the normal case.
void HDFontBuild();

// The device is going away (a resolution change, Alt+Enter, the end of the
// session). The texture was made on it and goes with it; the match table and
// the decoded page stay, so the next HDFontBuild is a texture upload and not
// a fresh match.
void HDFontDeviceLost();

// Where glyph `index` sits on the atlas, or 0 for a glyph that has no HD
// picture, while HD is off, or before the texture exists. DrawChar's only
// question.
const HDFontGlyph* HDFontGlyphAt(int index);

// The texture the HD_FONT_TPAGE quads are drawn with, or 0. HWR_DrawSortList's
// only question.
IDirect3DTexture9* HDFontTexture();

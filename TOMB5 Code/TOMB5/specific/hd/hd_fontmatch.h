/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The template match below is TROne's idea (Copyright 2026 Todesengel,
 * GPLv3, host\render\game_renderer.cpp:7367-7870, the block marked
 * "hdtext"): mask the ink of a classic glyph, slide it over the remaster's
 * glyph atlas, score intersection over union and refuse anything under a
 * high threshold, because "a similar character is the wrong character". The
 * code here is written against that description, not copied - and it needs
 * neither their sprite scan nor their "is this an alphabet" gate, because we
 * know which 106 rectangles are glyphs (game\chardef.inc). See THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#pragma once

// WHERE A CLASSIC GLYPH LIVES IN THE REMASTER'S ATLAS (#123, fix round Q).
//
// This file is written against the .DDS definition and deliberately has no
// engine dependencies - not even a Log() - so that the same code can be
// compiled into the diagnostic tool Preparation\Tools\Diagnose\FontProbe.cpp,
// which prints the whole match table and writes a picture of every glyph pair.
// What the tool proves about the matcher is therefore true of the engine's
// matcher as well. bc7.cpp and hdscreen.cpp already have that deal.
//
// THE TWO PICTURES.
//
//   the classic font page   256 x 256, A8R8G8B8, the second of the three
//                           0x40000 blocks at the end of LoadTextures
//                           (specific\file.cpp, "//font"). Byte for byte the
//                           same in all sixteen shipped level files -
//                           measured, md5 191cfc2fdba5b4c54024361c463d7537.
//   <install>\5\TEX\FONT<n>_OG.DDS
//                           256 x 256, uncompressed B8G8R8A8, one mip level,
//                           262 272 bytes. The remaster's own atlas at the
//                           classic size, and an exact 1/8 of FONT<n>.DDS.
//
// The two are NOT the same page: the remaster re-typeset its atlas in ASCII
// order while the classic page packs the glyphs wherever they fitted. What
// they do share is the glyph BITMAPS, and that is what the match finds.

#define HD_FONT_SIDE	256		// both pictures, and one eighth of the HD atlas
#define HD_FONT_HD_SIDE	2048		// FONT<n>.DDS
#define HD_FONT_SCALE	8		// HD_FONT_HD_SIDE / HD_FONT_SIDE

// Alpha at or above this counts as ink. The classic page's alpha is strictly
// binary - measured, 56 867 texels at 0 and 8 669 at 255, nothing between -
// so on that side any threshold in the middle gives the same mask. FONT0_OG
// has a real antialiased ramp (381 texels between 128 and 143), and 96 is
// TROne's number for it.
#define HD_FONT_INK	96

/* HOW MUCH ROOM THE HD GLYPH MAY TAKE AROUND THE CLASSIC RECTANGLE, in
 * classic texels (#123, fix round Q).
 *
 * The classic UV arithmetic samples the glyph rectangle inset by half a texel
 * on each side - the classic page packs its glyphs with no gap at all, and
 * without that inset a letter would bleed into its neighbour. At eight times
 * the size the inset is four HD texels, and it cuts off exactly the
 * antialiased rim and the drop shadow the remaster painted around every
 * letter: measured, 12.6 % of a glyph's ink. So the window is widened AND the
 * quad is widened in the same ratio, which is TROne's rule
 * (game_renderer.cpp:11249-11254) - the letter neither moves nor grows, only
 * more of the atlas around it becomes visible.
 *
 * THIS IS THE CEILING, NOT THE MARGIN. A uniform margin was tried first and
 * looked wrong: the atlas packs the small marks close together, so the same
 * one texel that a capital letter has to spare pulls a slice of the
 * neighbouring tick in beside an apostrophe and beside a comma - plainly
 * visible in a rendered line, at every margin from half a texel upwards. So
 * HDFontMargins works out per glyph how far the window can be opened before
 * it touches anything that is not this letter, and every glyph gets its own
 * answer between 0 and this ceiling.
 *
 * One texel is the ceiling because that is where the gain stops: measured
 * over the 93 matched glyphs, a window widened by 0.5 texels holds 94.5 % of
 * the glyph's ink, by 1.0 texel 97.8 %, by 1.5 texels 99.1 % - and the
 * remaining tenths cost far more foreign ink than they are worth. */
#define HD_FONT_MARGIN	1.0f

// The intersection over union a hit has to reach. TROne's threshold, and
// their reason for it is the whole point of the exercise: a similar
// character is the wrong character, and a wrong glyph on screen is worse
// than a classic one.
#define HD_FONT_ACCEPT	0.94f

// One glyph rectangle on the classic 256 x 256 page.
struct HDFontCell
{
	short x, y, w, h;
};

// Where it was found on the atlas, and how well.
struct HDFontHit
{
	short x, y;		// top left on the _OG atlas, in its own texels
	float score;		// intersection over union, 0 when nothing was found
	bool ok;		// score >= the accept threshold handed in
};

// The cell of one CharDef row. u and v are stored as n/255 (see
// game\chardef.inc) and w/h are the UNSCALED width and height, so this is
// the rectangle the artist drew, not the one the screen shows.
HDFontCell HDFontCellOf(float u, float v, short w, short h);

// An UNCOMPRESSED 256 x 256 B8G8R8A8 .DDS - the shape every FONT*_OG.DDS of
// the remaster has - into `out`, which must hold HD_FONT_SIDE * HD_FONT_SIDE
// pixels of 0xAARRGGBB. Anything else is refused with a reason in *why.
bool HDFontOgDecode(const void* file, unsigned long size, unsigned long* out,
	const char** why);

// The MD5 of a block of bytes as 32 lower case hex digits plus a nul, so the
// log line naming a font page and the diagnostic tool naming the same page
// print the same number. Here rather than beside the caller because both the
// engine and FontProbe have to agree on it.
void HDFontMd5(const void* data, unsigned long size, char* out33);

// The match. `page` is the classic font page and `atlas` the _OG picture,
// both HD_FONT_SIDE squared and 0xAARRGGBB. For every cell the best position
// on the atlas is written to hits[i]; hits[i].ok says whether it reached
// `accept`. Returns how many did.
//
// A cell with no ink at all - which none of the 106 has, but a level file is
// not a promise - scores 0 and is not ok.
int HDFontMatchAll(const unsigned long* atlas, const unsigned long* page,
	const HDFontCell* cells, int count, float accept, HDFontHit* hits);

/* WHITE INK, as against the dark rim and the drop shadow around it. The
 * remaster's big atlas is a redrawn font with a shadow baked in: measured
 * over the 93 matched glyphs, its fully opaque texels have a mean brightness
 * of 224 of 255 and its half-transparent ones a mean of 44. Halfway between
 * the two separates the letter from what is painted around it, and nothing in
 * the atlas sits near the boundary. */
inline bool HDFontIsWhite(unsigned long argb)
{
	return (argb >> 24) >= HD_FONT_INK &&
		((((argb >> 16) & 255) + ((argb >> 8) & 255) + (argb & 255)) / 3) >= 128;
}

/* HOW FAR EACH GLYPH'S WINDOW MAY BE OPENED, in classic texels.
 *
 * `hd` is the decoded FONT<n>.DDS at `side` x `side` (2048), `page` the
 * classic font page, `cells` and `hits` what the match produced. For every
 * glyph that was found, the window is opened one HD texel at a time, from
 * `maxMargin` downwards, and the first width at which it touches no OTHER
 * letter is the answer; `out[i]` is that width in classic texels, and 0 for a
 * glyph that has no room at all or was not found.
 *
 * "Another letter" means another letter's WHITE ink, decided by a flood fill
 * over white alone whose seeds come from the classic page. Three measurements
 * are behind that mouthful, and every one of them was arrived at by getting it
 * wrong first - see the comments in the implementation: a fill over ALPHA runs
 * a letter together with its own drop shadow and then across into the next
 * letter's; the small picture's spacing does not answer for the big one,
 * because the big one is a new drawing whose letters are up to three classic
 * texels larger; and seeding the whole rectangle floods a neighbour that
 * reaches into it, which for the tightly packed marks is exactly what happens.
 *
 * The drift of the classic u - stored as n/255 and read as n/256, so the
 * window sits up to a whole texel to the right of the rectangle - is carried,
 * because that is the window the engine really samples. */
void HDFontMargins(const unsigned long* hd, long side, const unsigned long* page,
	const HDFontCell* cells, const HDFontHit* hits, int count, float maxMargin,
	float* out);

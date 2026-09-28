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
#pragma once

/* How a page's alpha channel is to be read, out of the smallest and the
 * largest alpha of the top mip the page keeps (0..255).
 *
 * This header includes no engine header on purpose:
 * `Preparation\Tools\Diagnose\ModernTest.cpp` includes this file itself and
 * asserts on the function below without a device, a level or a page.
 * specific\hd\hd_pagecache.cpp measures the two numbers while a page is still
 * A8R8G8B8 and keeps the answer per slot; hd_render.cpp asks it for every
 * triangle of the alpha pass.
 *
 *   cutout  some texel lies below the alpha test's reference - 128, the 0.5
 *           draw_pass clips the alpha pass at. The test throws those texels
 *           away, so the alpha is coverage: a grating, a net, a leaf.
 *   clear   every texel passes the test, and the smallest of them is no
 *           higher than HD_PAGE_CLEAR_TOP. The test would draw the whole page
 *           solid, while its alpha says the surface lets a share of what lies
 *           behind it through. RICH2's marble floors are the case: pages
 *           1029, 1045, 1047, 1049, 1053, 1079 and 1081 are alpha 231 to 239
 *           in every texel, and the room stands upside down beneath them as a
 *           copy of itself (the lobby, room 8: 657 triangles of floor over
 *           2,895 of copy).
 *   opaque  everything else: a page at or near 255 everywhere.
 *
 * HD_PAGE_CLEAR_TOP is measured and not a taste. Over the alpha-pass pages of
 * all fifteen .TRG files, the smallest alpha of the pages that lie over such
 * a copy runs from 165 to 233 (TITLE page 2149 the highest), and the next
 * page up has 254 as its smallest (JOBY3 pages 14070 and 14071, ANDY1 page
 * 793, the sky sheets of ANDREA1, ANDREA3 and JOBY2) - a see-through share of
 * one in 255 that no eye finds and that would put 22,752 JOBY3 triangles
 * into a phase of their own for nothing. 243 is halfway across that gap.
 *
 * The largest alpha does not move a page between cutout and clear: the floors
 * of TITLE (pages 2147, 2149, 2154) and RICHCUT2 (page 1103) and RICH2's
 * inlaid ones (3314 to 3319) carry texels at 255 beside the see-through
 * marble, and a texel at 255 blends as solid anyway. It is taken so that a
 * page nothing was measured on - the smallest above the largest, which is how
 * the measurement starts - comes out opaque. */
#define HD_PAGE_OPAQUE		0
#define HD_PAGE_CUTOUT		1
#define HD_PAGE_CLEAR		2

#define HD_PAGE_TEST_REF	128	// the alpha test's 0.5 over 0..255
#define HD_PAGE_CLEAR_TOP	243	// see the note above

inline int HDPageClassOf(int minAlpha, int maxAlpha)
{
	if (minAlpha > maxAlpha)
		return HD_PAGE_OPAQUE;

	if (minAlpha < HD_PAGE_TEST_REF)
		return HD_PAGE_CUTOUT;

	return minAlpha <= HD_PAGE_CLEAR_TOP ? HD_PAGE_CLEAR : HD_PAGE_OPAQUE;
}

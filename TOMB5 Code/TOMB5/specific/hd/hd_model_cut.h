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

/* Which triangles of an HD object model are drawn with the alpha test in the
 * opaque pass: those whose part's material is a cut-out material
 * (HDTrmMaterialCuts) on a page whose texels below the reference are holes
 * (HDPageHolesAreCoverage).
 *
 * This header includes no engine header on purpose, like hd_pageclass.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp includes it and asserts on the
 * functions below without a device, a level or a page. hd_pagecache.cpp
 * measures the two numbers of a page, hd_model.cpp asks the rule for every
 * triangle of a model's opaque run.
 *
 * A .TRM part carries a material, the same numbers a .TRG part carries, and
 * the remaster sorts most of its object cut-outs into the OPAQUE run: the
 * wire mesh in front of the JOBY fans (ANIMATING1_JOBY3_JOBY5.TRM part 0,
 * ANIMATING15_MIP_JOBY3_JOBY5.TRM part 1), the grille of the JOBY trapdoors.
 * Their pages are mostly hole - 71.2 and 77.4 percent of the texels of pages
 * 11163 and 15080 lie below the reference (the cache's sample, see the table
 * below) - and drawn without the test, that hole is a solid dark plate.
 *
 * THE MATERIAL. 6, 7 and 8 are the cut-out materials of the rooms
 * (hd_render.cpp, MaterialAlphaRef). Over every .TRM of the install's 4 and
 * 5 ITEM folders, 6 and 8 are the two of them that occur, and both carry 0.5
 * in param[0] in every part - the reference the model shader clips at
 * (hd_model.hlsl, gPass.x), so the flag needs no number of its own. In the
 * opaque run only material 6 occurs; 7 and 8 are named because they mean the
 * same in a room. Material 0 and material 3 are left alone: their alpha is
 * the gloss mask the object path does not draw, and a ROMAN_GOD_ANDREA_SWORD
 * page (9000) is 64.1 percent below the reference - 715 per mille of that at
 * exactly 0, so the page's own measure below would cut the statue in half;
 * the material is the half of the rule that keeps it whole.
 *
 * THE PAGE. The material alone would cut one model it must not: Lara's
 * HAND_BARE_PISTOL and HAND_GLOVES_PISTOL (4\ITEM) are material 6 over the
 * whole of page 8516, and 9.3 percent of that page lies below the reference -
 * the chequered grip panels, at alpha 3 to 5. That alpha is a mask, the dark
 * matt of the grip against the bright steel, and the test would cut the grips
 * out of both guns. What tells the two apart is how far below the reference
 * the texels lie: a hole is painted at alpha 0 with a soft rim around it, a
 * mask is a dim value that is not 0. Over every texel the cache looks at
 * (every fourth in both directions, hd_pagecache.cpp measure_cutout), the
 * share at exactly 0 among the texels below the reference is:
 *
 *   page   used by                                    below   at 0  share
 *   15080  ANIMATING15_MIP_JOBY3_JOBY5, SAS_MIP_TITLE  12694  11300    890
 *   11163  ANIMATING1, ANIMATING14, CEILING_TRAPDOOR1,
 *          FLOOR_TRAPDOOR1 (JOBY3, JOBY5), ANIMATING2
 *          (JOBY2)                                     11672   9038    774
 *   12118  STEEL_DOOR (RICH2)                            428    418    976
 *   12111  ANIMATING16_ANDY3                            2887   2698    934
 *   22009  ANIMATING9_ANDY1                              232    177    762
 *   6040   DOOR_TYPE3_JOBY3_JOBY5                        365    271    742
 *   12052  ANIMATING14_RICH3, ANIMATING15_RICH3         1505    490    325
 *   8516   HAND_BARE_PISTOL, HAND_GLOVES_PISTOL         1517      7      4
 *
 * (texels of the cache's own sample of the top mip, the share in per mille;
 * ModelProbe -slot prints the share as a percentage beside every page it
 * loads, and the page numbers are the files in the install's 5\TEX and
 * 4\TEX). The cut-out pages lie at 325 and above, the mask at 4.
 * HD_MODEL_CUT_ZERO_PERMILLE is 100: a factor of three below the lowest
 * cut-out page and of twenty-five above the mask.
 *
 * A page with no texel below the reference is not cut either - the test
 * would discard nothing there, and leaving the range as it is keeps its
 * draw call where it was. */
#define HD_TRM_MATERIAL_MASK		6
#define HD_TRM_MATERIAL_WIND2		7
#define HD_TRM_MATERIAL_WIND		8
#define HD_MODEL_CUT_ZERO_PERMILLE	100	// see the table above

/* Is this .TRM material one whose alpha is coverage? Compared whole, as the
 * room renderer compares a .TRG material. */
inline bool HDTrmMaterialCuts(unsigned int material)
{
	return material == HD_TRM_MATERIAL_MASK || material == HD_TRM_MATERIAL_WIND2 ||
		material == HD_TRM_MATERIAL_WIND;
}

/* Are a page's texels below the reference holes rather than a mask? `below`
 * is how many of the texels looked at lie below the reference, `zero` how
 * many of those are at exactly 0. */
inline bool HDPageHolesAreCoverage(int below, int zero)
{
	if (below <= 0 || zero < 0 || zero > below)
		return false;

	return (long long)zero * 1000 >= (long long)below * HD_MODEL_CUT_ZERO_PERMILLE;
}

/* The share of `zero` in `below`, in per mille, for the tools and the log;
 * 0 for a page with nothing below the reference. */
inline int HDPageZeroPermille(int below, int zero)
{
	if (below <= 0 || zero <= 0)
		return 0;

	return (int)(((long long)(zero > below ? below : zero) * 1000) / below);
}

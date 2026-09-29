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

/* The sheen's page table: the surfaces the classic floor cannot name - the
 * walls, crates, ducts and panels a footstep never touches - by the page
 * they wear. hd_gloss_math.h says where the table stands in the order of
 * trust (after the material, the file and the seed).
 *
 * One line per page: the page number, the class, then strength, exponent and
 * F0, where -1 takes the class's default (HDGlossClassDefaults), and a
 * comment naming what the page shows and where. A line of class X is a
 * "-<page>" line: a page the seed names and whose picture is not what its
 * floor says - a relief, a wooden plank, a label atlas - taken out. A page
 * number is a file name in the install's one TEX folder, so a line holds in
 * every level that wears the page. The first version came out of TrgProbe
 * -glossseed over the fifteen levels (the seed's candidates and the pages
 * numbered next to them), and every page was looked at; the file hd_gloss.txt
 * beside the game's executable takes the same lines in text, so a line can
 * be tried there before it is written here.
 *
 * Kept sorted by page number. */

#include "hd_gloss_math.h"

#define M HD_GLOSS_METAL
#define S HD_GLOSS_MARBLE
#define I HD_GLOSS_ICE
#define G HD_GLOSS_GLASS
#define X HD_GLOSS_NONE		// "-<page>": the seed misreads this page

static const HDGlossPage kHDGlossPages[] =
{
	// page class strength power  F0
	{    33, X, -1.0f, -1.0f, -1.0f },	// RICH1: a brick wall and a lamp's glow on a metal floor
	{    53, G, -1.0f, -1.0f, -1.0f },	// RICH2: a black window pane in a white frame
	{   141, M, -1.0f, -1.0f, -1.0f },	// ANDREA1 room 3: the crate's front, the logo panel and the hazard stripes
	{   390, X, -1.0f, -1.0f, -1.0f },	// ANDREA3: a stone relief of a procession
	{   391, X, -1.0f, -1.0f, -1.0f },	// ANDREA3: the same relief
	{   421, M, -1.0f, -1.0f, -1.0f },	// JOBY2: a riveted steel column
	{   436, X, -1.0f, -1.0f, -1.0f },	// JOBY2, JOBY3, JOBY5: a pipe over a wooden pallet in the snow
	{   469, M, -1.0f, -1.0f, -1.0f },	// JOBY2: riveted steel columns, painted
	{   480, I, -1.0f, -1.0f, -1.0f },	// JOBY2: the material 3 snow and ice floor
	{   482, X, -1.0f, -1.0f, -1.0f },	// JOBY3, JOBY5: cracked plaster over concrete
	{   576, M, -1.0f, -1.0f, -1.0f },	// JOBY3 room 79: the duct's diamond plate with its vents
	{   954, M, -1.0f, -1.0f, -1.0f },	// RICH1: a galvanised steel plate with rivets
	{   993, M, -1.0f, -1.0f, -1.0f },	// RICH1: a steel plate with a vent grille
	{  1054, S, -1.0f, -1.0f, -1.0f },	// RICH2 rooms 134 and 135: the washroom's green mosaic
	{  1082, S, -1.0f, -1.0f, -1.0f },	// RICH1: brown marble slabs (the seed calls them metal)
	{  1085, S, -1.0f, -1.0f, -1.0f },	// RICH2 rooms 134 and 135: the green tile band under a marble strip
	{  1086, S, -1.0f, -1.0f, -1.0f },	// RICH2 rooms 134 and 135: green marble tiles
	{  1187, X, -1.0f, -1.0f, -1.0f },	// RICH3: an atlas of black, teal brick and pale pink
	{  3010, X, -1.0f, -1.0f, -1.0f },	// RICH1: a brown wooden panel
	{  3063, G, -1.0f, -1.0f, -1.0f },	// ANDREA1: a green monitor screen
	{  3064, X, -1.0f, -1.0f, -1.0f },	// RICH1: an atlas of labels, cables and a blue flame
	{  3066, M, -1.0f, -1.0f, -1.0f },	// JOBY3 room 79: a vent grille with a warning sign under screens
	{  3121, X, -1.0f, -1.0f, -1.0f },	// ANDREA2, ANDREA3: grass and earth
	{  3162, M, -1.0f, -1.0f, -1.0f },	// ANDREA1: grey steel girders, the twin of 3163
	{  3171, X, -1.0f, -1.0f, -1.0f },	// JOBY2: a plain grey sheet on 762,756 triangles - not a floor's metal
	{  4046, X, -1.0f, -1.0f, -1.0f },	// JOBY3: three steaks
	{  6037, X, -1.0f, -1.0f, -1.0f },	// JOBY2: brown wooden planks
	{  8511, X, -1.0f, -1.0f, -1.0f },	// RICH3: an atlas of coloured labels and parts
	{  9000, M, -1.0f, -1.0f, -1.0f },	// ANDREA2: the material 3 armour and statue metal
	{  9001, M, -1.0f, -1.0f, -1.0f },	// ANDREA2: the same, its second page
	{ 11035, X, -1.0f, -1.0f, -1.0f },	// ANDREA2: an atlas of signs and bricks
	{ 12057, X, -1.0f, -1.0f, -1.0f },	// JOBY2: papers and posters
	{ 12058, X, -1.0f, -1.0f, -1.0f },	// JOBY2: a concrete wall with tyre tracks
	{ 14051, X, -1.0f, -1.0f, -1.0f },	// ANDREA2: an olive atlas with bricks and a gear
	{ 15073, M, -1.0f, -1.0f, -1.0f },	// ANDREA1 room 3: the ceiling lamp and the arrow bracket
	{ 16066, M, -1.0f, -1.0f, -1.0f },	// ANDREA1 room 3: the forklift, its dark parts
	{ 16067, M, -1.0f, -1.0f, -1.0f },	// ANDREA1 room 3: the forklift's yellow body
	{ 16068, M, -1.0f, -1.0f, -1.0f },	// ANDREA1 room 3: the forklift, mast and details
	{ 16100, X, -1.0f, -1.0f, -1.0f },	// RICH1: an atlas with cola cans
};

#undef M
#undef S
#undef I
#undef G
#undef X

inline int HDGlossTableSize()
{
	return (int)(sizeof(kHDGlossPages) / sizeof(kHDGlossPages[0]));
}

// The table's line for a page, or null.
inline const HDGlossPage* HDGlossTableFind(int page)
{
	for (int i = 0; i < HDGlossTableSize(); i++)
		if (kHDGlossPages[i].page == page)
			return &kHDGlossPages[i];

	return 0;
}

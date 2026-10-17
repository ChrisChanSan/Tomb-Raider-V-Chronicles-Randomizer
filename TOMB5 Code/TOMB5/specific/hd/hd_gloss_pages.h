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
 * trust (after the material and the file, before the seed).
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
 * The last column is the mirror column: 1 where the page's flat floors are
 * a planar mirror (hd_render.cpp, "the marble floors"). A line of class A
 * names that column alone and nothing of the sheen. The 37 pages of the
 * column are the polished floors of the Rome levels, RICHCUT2 and the title,
 * looked at by eye on the thirty largest floor pages of each level
 * (RoomProbe -marblecensus, DdsProbe), and RICH2's banded black marble at
 * the corridor's end and in room 128, three squares a page; the pages the
 * sheen bars are among them, the mirror being no sheen. The comment over a
 * line names what the page shows and where.
 *
 * Kept sorted by page number. */

#include "hd_gloss_math.h"

#define M HD_GLOSS_METAL
#define S HD_GLOSS_MARBLE
#define I HD_GLOSS_ICE
#define G HD_GLOSS_GLASS
#define X HD_GLOSS_NONE		// "-<page>": the seed misreads this page
#define A HD_GLOSS_ASIS		// the mirror column alone

static const HDGlossPage kHDGlossPages[] =
{
	// page class strength power F0 mirror
	// RICH1: a brick wall and a lamp's glow on a metal floor
	{    33, X, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1, RICH2: poured concrete with tie holes
	{    50, X, -1.0f, -1.0f, -1.0f, 0 },
	// RICH2: a black window pane in a white frame
	{    53, G, -1.0f, -1.0f, -1.0f, 0 },
	// RICHCUT2: a marble floor
	{    54, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH1, RICH2, JOBY2, TITLE: the locker doors, their upper half
	{    93, M, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1, RICH2, TITLE: the locker doors, their lower half
	{    94, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1 room 3: the crate's front, the logo panel and the hazard stripes
	{   141, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1: the marble halls' floor
	{   162, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA2, ANDREA3: a marble floor
	{   246, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA2, ANDREA3: a marble floor
	{   249, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA2: a marble floor (footstep marble)
	{   273, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA2: a marble floor (footstep marble)
	{   274, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA2: a marble floor (footstep marble)
	{   285, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA3: a marble floor
	{   366, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA3: a stone relief of a procession
	{   390, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA3: the same relief
	{   391, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA3: a marble floor (footstep marble)
	{   408, A, -1.0f, -1.0f, -1.0f, 1 },
	// ANDREA3: a marble floor (footstep marble)
	{   411, A, -1.0f, -1.0f, -1.0f, 1 },
	// JOBY2: a riveted steel column
	{   421, M, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2, JOBY3, JOBY5: a pipe over a wooden pallet in the snow
	{   436, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: riveted steel columns, painted
	{   469, M, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: the same painted steel with a pipe across, the twin of 469
	{   470, M, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: the material 3 snow and ice floor
	{   480, I, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY3, JOBY5: cracked plaster over concrete
	{   482, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY3 room 79: the duct's diamond plate with its vents
	{   576, M, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1: a galvanised steel plate with rivets
	{   954, M, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1, RICH2, RICH3: grey marble slabs
	{   960, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH1: a steel plate with a vent grille
	{   993, M, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1: a marble floor (footstep marble)
	{  1002, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH1, RICH2, RICH3, RICHCUT2: the lobby's green tiles
	{  1010, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's clear black marble
	{  1029, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's clear black marble
	{  1045, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1046, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the corridor's and the lobby's clear black marble
	{  1047, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1048, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's black marble
	{  1049, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's clear black marble
	{  1053, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2 rooms 134 and 135: the washroom's green mosaic
	{  1054, S, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1063, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: a marble floor
	{  1070, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's clear black marble
	{  1079, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2: the lobby's clear black marble
	{  1081, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH1: brown marble slabs (the seed calls them metal)
	{  1082, S, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2 rooms 134 and 135: the green tile band under a marble strip
	{  1085, S, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2 rooms 134 and 135: green marble tiles
	{  1086, S, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2, RICH3: a marble floor
	{  1094, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1104, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1105, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICHCUT2: a marble floor
	{  1110, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH3: an atlas of black, teal brick and pale pink
	{  1187, X, -1.0f, -1.0f, -1.0f, 0 },
	// TITLE: a clear marble floor
	{  2147, A, -1.0f, -1.0f, -1.0f, 1 },
	// TITLE: a clear marble floor
	{  2149, A, -1.0f, -1.0f, -1.0f, 1 },
	// TITLE: a clear marble floor
	{  2154, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH1: a brown wooden panel
	{  3010, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1: a green monitor screen
	{  3063, G, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1: an atlas of labels, cables and a blue flame
	{  3064, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY3 room 79: a vent grille with a warning sign under screens
	{  3066, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA2, ANDREA3: grass and earth
	{  3121, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1: grey steel girders, the twin of 3163
	{  3162, M, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: a plain grey sheet on 762,756 triangles - not a floor's metal
	{  3171, X, -1.0f, -1.0f, -1.0f, 0 },
	// RICH2 rooms 46, 75 and 128: black marble with gold bands
	{  3314, A, -1.0f, -1.0f, -1.0f, 1 },
	// RICH2 rooms 46, 75 and 128: the same, its second page
	{  3315, A, -1.0f, -1.0f, -1.0f, 1 },
	// JOBY3: three steaks
	{  4046, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: brown wooden planks
	{  6037, X, -1.0f, -1.0f, -1.0f, 0 },
	// RICH3: an atlas of coloured labels and parts
	{  8511, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA2: the material 3 armour and statue metal
	{  9000, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA2: the same, its second page
	{  9001, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA2: an atlas of signs and bricks
	{ 11035, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: papers and posters
	{ 12057, X, -1.0f, -1.0f, -1.0f, 0 },
	// JOBY2: a concrete wall with tyre tracks
	{ 12058, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA2: an olive atlas with bricks and a gear
	{ 14051, X, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1 room 3: the ceiling lamp and the arrow bracket
	{ 15073, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1 room 3: the forklift, its dark parts
	{ 16066, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1 room 3: the forklift's yellow body
	{ 16067, M, -1.0f, -1.0f, -1.0f, 0 },
	// ANDREA1 room 3: the forklift, mast and details
	{ 16068, M, -1.0f, -1.0f, -1.0f, 0 },
	// RICH1: an atlas with cola cans
	{ 16100, X, -1.0f, -1.0f, -1.0f, 0 },
};

#undef M
#undef S
#undef I
#undef G
#undef X
#undef A

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

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

/* The arithmetic of the sheen on HD room surfaces: which surfaces shine, how,
 * and how far the sheen may brighten a surface. What it is lit with is the
 * level's light field (hd_gloss_field.h).
 *
 * This header includes no engine header on purpose, like hd_pageclass.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp includes it and asserts on every
 * function below without a device, a level or a page, and TrgProbe resolves a
 * level's pages with the same code the engine runs. hd_render.cpp is the
 * engine's one caller.
 *
 * WHICH SURFACES. The unit is the texture page: a page is one picture - a
 * crate's ribbed side, a steel duct panel, a marble slab - and it is the
 * same picture wherever a level uses it, because a page number is a file
 * name in the install's one TEX folder. Three sources name a page's gloss,
 * and a fourth tunes them; the first that names a page wins:
 *
 *   material  a .TRG part of material 3 or 17 carries its own exponent and
 *             strength (see HDGlossFromMaterial). The art's own datum and
 *             therefore first.
 *   the file  hd_gloss.txt beside the game's executable, if there is one: a
 *             hand-kept list in the table's format, read at every level
 *             load. An entry replaces the table's for its page, and a line
 *             "-<page>" takes the page out of the table and the seed alike -
 *             a surface can be tuned or switched off without a rebuild.
 *             Next after the material, because it is the one source a person
 *             writes on purpose for this installation.
 *   the table the page table compiled into the engine (hd_gloss_pages.h):
 *             what the floor cannot show - walls, ducts, crate fronts - and
 *             the seed's misreadings, a page it names that is not what its
 *             floor says ("-<page>" lines there too). Before the seed,
 *             because every line of it was looked at.
 *   the seed  a page whose up-facing triangles stand on classic floor
 *             squares of the footstep material metal, marble or ice takes
 *             that class, for ALL its triangles - the top of a crate names
 *             its sides as well (see HDGlossSeedClass).
 *
 * Every page RICH2's lobby is made of is barred from the file, the table and
 * the seed (HDGlossPageBarred): its floors are a mirror of their own. */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <map>

/* The classes. A class is a set of three defaults - strength, exponent and
 * the reflectance at normal incidence, F0 - and whether the reflection takes
 * the surface's colour. */
#define HD_GLOSS_NONE		0
#define HD_GLOSS_METAL		1
#define HD_GLOSS_MARBLE		2
#define HD_GLOSS_ICE		3
#define HD_GLOSS_GLASS		4
#define HD_GLOSS_CUSTOM		5
#define HD_GLOSS_CLASSES	6

// Where a page's gloss came from - for the log and the tools.
#define HD_GLOSS_FROM_NONE	0
#define HD_GLOSS_FROM_MAT	1
#define HD_GLOSS_FROM_SEED	2
#define HD_GLOSS_FROM_TABLE	3
#define HD_GLOSS_FROM_FILE	4

/* The seed's two numbers, ported from the rule the floor census used over
 * the fifteen levels (floor_hd_parts_by_fx: an up-facing .TRG triangle
 * belongs to the classic square under its centroid when the centroid lies
 * within 300 units of that square's floor height). HD_GLOSS_SEED_UP is how
 * far off the horizontal a triangle may lean and still count as a floor:
 * |n.y| >= 0.3, about 73 degrees, the census' "floor or slope". */
#define HD_GLOSS_SEED_REACH	300
#define HD_GLOSS_SEED_UP	0.3f

/* A page whose smallest alpha on the top mip it keeps is below this carries
 * a gloss mask in its alpha, and the sheen is multiplied by it; above it the
 * alpha is flat (253 to 255 on 1,345 of the 1,463 opaque-pass pages of the
 * fifteen levels - BC7 noise) and the sheen is uniform. 34 of the 35 pages
 * that carry material 3 or 17 are below it (the top mip of every
 * opaque-pass page of the fifteen levels, read as DdsProbe -alpha reads a
 * page). */
#define HD_GLOSS_MASK_BELOW	250

struct HDGloss
{
	int cls;		// HD_GLOSS_*
	int source;		// HD_GLOSS_FROM_*
	float strength;		// 0 = no gloss, else (0, 1]
	float power;		// the Blinn-Phong exponent, 1..255
	float f0;		// Schlick's F0, 0..1
	float tint;		// 1 = the reflection takes the texel's colour
};

/* One line of the page table or of hd_gloss.txt. A number below zero means
 * "the class's own default" - only a custom page must name all three. */
struct HDGlossPage
{
	int page;
	int cls;
	float strength;
	float power;
	float f0;
};

inline const char* HDGlossClassName(int cls)
{
	switch (cls)
	{
	case HD_GLOSS_METAL:	return "metal";
	case HD_GLOSS_MARBLE:	return "marble";
	case HD_GLOSS_ICE:	return "ice";
	case HD_GLOSS_GLASS:	return "glass";
	case HD_GLOSS_CUSTOM:	return "custom";
	default:		return "none";
	}
}

inline const char* HDGlossSourceName(int source)
{
	switch (source)
	{
	case HD_GLOSS_FROM_MAT:		return "mat";
	case HD_GLOSS_FROM_SEED:	return "seed";
	case HD_GLOSS_FROM_TABLE:	return "table";
	case HD_GLOSS_FROM_FILE:	return "file";
	default:			return "none";
	}
}

/* The defaults of a class.
 *
 * F0, the reflectance at normal incidence, starts from the textbook's
 * ((n - 1) / (n + 1))^2 for a dielectric of refractive index n - 0.04 for
 * glass and polished stone at n = 1.5, 0.02 for ice at n = 1.31 - and about
 * half for steel, whose reflection also takes the metal's own colour.
 *
 * The dielectrics are drawn at F0 0.08, twice the textbook figure, and at
 * strength 0.6. What the sheen reflects is the room's light cube, a blur of
 * the room's mean light, not the lamps and windows a real polished floor
 * mirrors: at a grazing angle it comes up as a veil of that mean light. At
 * strength 1 the veil hides RICH2's washroom mosaic (room 135 from
 * (2800, -24800, 2566), yaw 270, pitch 22, 1280 x 960: 256,720 pixels
 * lifted by more than 32 of 255, the mean from 80.0 to 93.5); at 0.6 the
 * tiles keep their pattern under it (45,960 pixels, mean 88.1). ANDREA2 room
 * 16's marble (from (1300, 200, 2900), yaw 90, pitch 18) goes from a mean of
 * 68.5 to 71.1 at the textbook 0.04 and strength 0.8, to 73.4 at 0.08 and
 * strength 1, and to 71.5 at 0.08 and 0.6, its brightest pixel by 35
 * (RoomProbe, before and after with -noreflect).
 *
 * Strength and exponent are chosen: metal a broad sheen (painted and worn
 * steel, crates, ducts), marble and glass a tighter one, ice between. Marble
 * and ice are weak face on and come up at a grazing angle through Schlick's
 * term; metal shines face on. */
inline bool HDGlossClassDefaults(int cls, HDGloss* out)
{
	out->cls = cls;
	out->source = HD_GLOSS_FROM_NONE;
	out->strength = 0.0f;
	out->power = 0.0f;
	out->f0 = 0.0f;
	out->tint = 0.0f;

	switch (cls)
	{
	case HD_GLOSS_METAL:
		out->strength = 0.8f; out->power = 48.0f; out->f0 = 0.5f; out->tint = 1.0f;
		return true;
	case HD_GLOSS_MARBLE:
		out->strength = 0.6f; out->power = 64.0f; out->f0 = 0.08f;
		return true;
	case HD_GLOSS_ICE:
		out->strength = 0.6f; out->power = 48.0f; out->f0 = 0.08f;
		return true;
	case HD_GLOSS_GLASS:
		out->strength = 0.6f; out->power = 96.0f; out->f0 = 0.08f;
		return true;
	case HD_GLOSS_CUSTOM:
		// A custom page names its own three numbers; the defaults are those
		// of a dielectric.
		out->strength = 0.5f; out->power = 32.0f; out->f0 = 0.04f;
		return true;
	default:
		out->cls = HD_GLOSS_NONE;
		return false;
	}
}

/* The material number without the destructible bits. A piece of a room that
 * Lara can shoot away carries bit 15 and a counter in bits 8 to 14 over its
 * base material - RICH3's glossy window panes are 0x8003, 0x8203, 0x8303 and
 * 0x8403 over material 3 (TrgProbe -mat). The gloss looks at the base. */
inline unsigned int HDGlossBaseMaterial(unsigned int material)
{
	return material & 0xFFu;
}

/* The class of a classic footstep material (FLOOR_INFO.fx): 8 metal and 13
 * old metal, 9 marble, 4 ice. Every other surface names no gloss. fx 13 is
 * in no level of the game (the floor census), it is kept for a level that
 * has it. */
inline int HDGlossClassOfFx(int fx)
{
	switch (fx)
	{
	case 8:
	case 13:	return HD_GLOSS_METAL;
	case 9:		return HD_GLOSS_MARBLE;
	case 4:		return HD_GLOSS_ICE;
	default:	return HD_GLOSS_NONE;
	}
}

/* The gloss a .TRG part names itself.
 *
 * Two materials carry a pair that behaves like a specular one over the
 * distinct tuples of all fifteen levels (TrgProbe -mat):
 *
 *   material  3   param[0] an integer 1..191 - the exponent - and param[2]
 *                 a fraction up to 1.0 - the strength. The masks of its
 *                 pages select window panes, puddles, ice and armour, so the
 *                 class is glass, a dielectric; the page table can still
 *                 name a page's class (armour is metal).
 *   material 17   param[0] 22..72 and param[1] 0.05 or 0.1 - the same shape
 *                 on the other slot. JOBY3 to JOBY5's painted steel: metal.
 *
 * Zero means "no gloss", and a strength outside (0, 1] or an exponent that is
 * not an integer in 1..255 falls through to "names none" rather than being
 * trusted. The class may be overridden by the caller from the table (the
 * strength and the exponent stay the material's). */
inline int HDGlossMaterialClass(unsigned int base)
{
	return base == 17 ? HD_GLOSS_METAL : base == 3 ? HD_GLOSS_GLASS : HD_GLOSS_NONE;
}

inline bool HDGlossFromMaterial(unsigned int material, const float param[4], HDGloss* out)
{
	const unsigned int base = HDGlossBaseMaterial(material);
	const int cls = HDGlossMaterialClass(base);
	float s;

	if (base == 3)
		s = param[2];
	else if (base == 17)
		s = param[1];
	else
		return false;

	if (!(s > 0.0f && s <= 1.0f))
		return false;

	if (!(param[0] >= 1.0f && param[0] <= 255.0f) ||
		(float)(int)param[0] != param[0])
		return false;

	HDGlossClassDefaults(cls, out);
	out->source = HD_GLOSS_FROM_MAT;
	out->strength = s;
	out->power = param[0];
	return true;
}

/* A page entry turned into a gloss: the class's defaults, then whatever the
 * entry names itself. False for an entry whose class names nothing. */
inline bool HDGlossFromPage(const HDGlossPage& page, int source, HDGloss* out)
{
	if (!HDGlossClassDefaults(page.cls, out))
		return false;

	out->source = source;

	if (page.strength >= 0.0f)
		out->strength = page.strength;

	if (page.power >= 0.0f)
		out->power = page.power;

	if (page.f0 >= 0.0f)
		out->f0 = page.f0;

	return out->strength > 0.0f;
}

/* The pages that never shine through the seed, the table or the file:
 * RICH2's lobby. Its marble floors are part-way clear - pages 1029, 1045,
 * 1047, 1049, 1053, 1079 and 1081, alpha 231 to 239 in every texel
 * (hd_pageclass.h) - with the room standing mirrored beneath them, and its
 * marble walls and steps are 1010, 1032 and 1033; the floor already IS a
 * mirror, and a sheen over it would be a second one. The pages are named one
 * by one: the numbers between them are other pictures - 1054 is the
 * washroom's green mosaic, 1039 and 1040 a steel shutter and a tread plate.
 * The engine bars every part-way clear page besides, by its alpha
 * (hd_render.cpp, ResolvePage). The material source is not barred - no page
 * of the lobby carries material 3 or 17 (TrgProbe -mat). */
inline bool HDGlossPageBarred(int page)
{
	switch (page)
	{
	case 1010: case 1029: case 1032: case 1033: case 1045: case 1047:
	case 1049: case 1053: case 1079: case 1081:
		return true;
	default:
		return false;
	}
}

/* One line of the page table's text form - hd_gloss.txt:
 *
 *     <page> <class> [<strength> [<exponent> [<F0>]]]   [# comment]
 *     -<page>                                           [# comment]
 *
 * The class is metal, marble, ice, glass or custom (any case); a custom page
 * names all three numbers. A strength lies in (0, 1], an exponent in 1..255,
 * an F0 in [0, 1]; a number written as "-" or left out takes the class's
 * default. '#' starts a comment anywhere; a blank line or a comment alone is
 * no entry.
 *
 * Returns 1 for an entry (`*remove` says whether it takes the page out), 0
 * for a line with nothing on it and -1 for a line that cannot be read - the
 * caller counts those and goes on with the next. */
inline int HDGlossParseLine(const char* line, HDGlossPage* out, bool* remove)
{
	char buffer[256];
	size_t n = 0;

	*remove = false;
	out->page = -1;
	out->cls = HD_GLOSS_NONE;
	out->strength = -1.0f;
	out->power = -1.0f;
	out->f0 = -1.0f;

	// The line without its comment and its line end.
	while (line[n] && line[n] != '#' && line[n] != '\r' && line[n] != '\n' &&
		n + 1 < sizeof(buffer))
	{
		buffer[n] = line[n];
		n++;
	}

	buffer[n] = 0;

	char* words[8];
	int count = 0;

	for (char* at = buffer; *at && count < 8; )
	{
		while (*at == ' ' || *at == '\t')
			*at++ = 0;

		if (!*at)
			break;

		words[count++] = at;

		while (*at && *at != ' ' && *at != '\t')
			at++;
	}

	if (!count)
		return 0;

	// A page number, digits only, 1..65535 - the range of a page file name.
	const char* first = words[0];
	const bool minus = first[0] == '-';

	if (minus)
		first++;

	if (!*first)
		return -1;

	for (const char* c = first; *c; c++)
		if (!isdigit((unsigned char)*c))
			return -1;

	const long page = strtol(first, 0, 10);

	if (page < 1 || page > 65535)
		return -1;

	out->page = (int)page;

	if (minus)
	{
		*remove = true;
		return count == 1 ? 1 : -1;
	}

	if (count < 2 || count > 5)
		return -1;

	char name[16];
	size_t k = 0;

	for (; words[1][k] && k + 1 < sizeof(name); k++)
		name[k] = (char)tolower((unsigned char)words[1][k]);

	name[k] = 0;

	if (!strcmp(name, "metal"))
		out->cls = HD_GLOSS_METAL;
	else if (!strcmp(name, "marble"))
		out->cls = HD_GLOSS_MARBLE;
	else if (!strcmp(name, "ice"))
		out->cls = HD_GLOSS_ICE;
	else if (!strcmp(name, "glass"))
		out->cls = HD_GLOSS_GLASS;
	else if (!strcmp(name, "custom"))
		out->cls = HD_GLOSS_CUSTOM;
	else
		return -1;

	float* slots[3] = { &out->strength, &out->power, &out->f0 };
	const float lo[3] = { 0.0f, 1.0f, 0.0f };
	const float hi[3] = { 1.0f, 255.0f, 1.0f };

	for (int i = 2; i < count; i++)
	{
		const char* w = words[i];

		if (!strcmp(w, "-"))
			continue;

		char* end = 0;
		const double v = strtod(w, &end);

		if (end == w || *end)
			return -1;

		// The strength may not be 0 - "no gloss" is written "-<page>".
		if (v < lo[i - 2] || v > hi[i - 2] || (i == 2 && v <= 0.0))
			return -1;

		*slots[i - 2] = (float)v;
	}

	// A custom page brings its own three numbers.
	if (out->cls == HD_GLOSS_CUSTOM &&
		(out->strength < 0.0f || out->power < 0.0f || out->f0 < 0.0f))
		return -1;

	return 1;
}

/* hd_gloss.txt as read: the entries by page - a later line for the same page
 * replaces an earlier one, and an entry of class HD_GLOSS_NONE is a "-<page>"
 * line, a page taken out - the lines looked at and the ones that could not
 * be read. */
struct HDGlossFile
{
	std::map<int, HDGlossPage> entries;
	int lines;
	int bad;
	bool present;

	HDGlossFile() : lines(0), bad(0), present(false) {}

	// The file's entry for a page, or null; `removes` says it took the page out.
	const HDGlossPage* find(int page, bool* removes) const
	{
		const std::map<int, HDGlossPage>::const_iterator at = entries.find(page);

		*removes = at != entries.end() && at->second.cls == HD_GLOSS_NONE;
		return at != entries.end() && at->second.cls != HD_GLOSS_NONE ? &at->second : 0;
	}
};

// The whole text of hd_gloss.txt, line by line (CR, LF or both end a line).
inline void HDGlossParseText(const char* text, HDGlossFile* out)
{
	out->entries.clear();
	out->lines = 0;
	out->bad = 0;
	out->present = text != 0;

	// A text editor's "UTF-8 with BOM" puts three bytes before the first line.
	if (text && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
		(unsigned char)text[2] == 0xBF)
		text += 3;

	for (const char* at = text; at && *at; )
	{
		const char* end = at;

		while (*end && *end != '\n' && *end != '\r')
			end++;

		char line[256];
		const size_t n = (size_t)(end - at) < sizeof(line) - 1
			? (size_t)(end - at) : sizeof(line) - 1;

		memcpy(line, at, n);
		line[n] = 0;
		out->lines++;

		HDGlossPage entry = { -1, HD_GLOSS_NONE, -1.0f, -1.0f, -1.0f };
		bool remove = false;
		const int got = (size_t)(end - at) >= sizeof(line) ? -1 :
			HDGlossParseLine(line, &entry, &remove);

		if (got < 0)
			out->bad++;
		else if (got > 0)
		{
			if (remove)
				entry.cls = HD_GLOSS_NONE;

			out->entries[entry.page] = entry;
		}

		// One line end, whichever of the two or both it is.
		if (*end == '\r' && end[1] == '\n')
			end += 2;
		else if (*end)
			end++;

		at = end;
	}
}

/* The order of trust for one page, as described at the top of this file:
 * the material, then the file, then the table, then the seed.
 *
 * `mat` is the part's own gloss or null. `fileEntry` is the file's entry for
 * the page or null, and `fileRemoves` says that the file took the page out.
 * `tableEntry` is the table's line or null; a line of class HD_GLOSS_NONE
 * takes the page out, like the file's "-<page>". `seedClass` is
 * HD_GLOSS_NONE where the seed names nothing. `barred` pages take nothing
 * but the material.
 *
 * A material part keeps its own strength and exponent whatever else names
 * its page; a class the file or the table names for the page replaces the
 * material's class, F0 and tint - so the table can say that ANDREA2's
 * material 3 armour is metal and JOBY2's material 3 snow is ice. */
inline HDGloss HDGlossResolve(const HDGloss* mat, const HDGlossPage* fileEntry,
	bool fileRemoves, const HDGlossPage* tableEntry, int seedClass, bool barred)
{
	HDGloss out;

	HDGlossClassDefaults(HD_GLOSS_NONE, &out);

	const bool tableRemoves = tableEntry && tableEntry->cls == HD_GLOSS_NONE;
	const HDGlossPage* named = barred ? 0 : fileEntry ? fileEntry :
		fileRemoves || tableRemoves ? 0 : tableEntry;

	if (mat)
	{
		out = *mat;

		if (named)
		{
			HDGloss cls;

			if (HDGlossClassDefaults(named->cls, &cls))
			{
				out.cls = cls.cls;
				out.f0 = named->f0 >= 0.0f ? named->f0 : cls.f0;
				out.tint = cls.tint;
			}
		}

		return out;
	}

	if (barred || (!fileEntry && (fileRemoves || tableRemoves)))
		return out;

	if (fileEntry && HDGlossFromPage(*fileEntry, HD_GLOSS_FROM_FILE, &out))
		return out;

	if (tableEntry && HDGlossFromPage(*tableEntry, HD_GLOSS_FROM_TABLE, &out))
		return out;

	if (seedClass != HD_GLOSS_NONE && HDGlossClassDefaults(seedClass, &out))
	{
		out.source = HD_GLOSS_FROM_SEED;
		return out;
	}

	HDGlossClassDefaults(HD_GLOSS_NONE, &out);
	return out;
}

/* Whether a triangle faces up enough to be a floor for the seed: the face
 * normal out of the corners' cross product, at least HD_GLOSS_SEED_UP off the
 * horizontal, and the side taken from the corners' own vertex normals (the
 * winding is not reliable across the art). y counts downwards in this
 * engine, so up is a negative normal y. `nyVertex` is the mean of the three
 * vertex normals' y. */
inline bool HDGlossFacesUp(const float a[3], const float b[3], const float c[3],
	float nyVertex)
{
	const double ux = (double)b[0] - a[0], uy = (double)b[1] - a[1], uz = (double)b[2] - a[2];
	const double vx = (double)c[0] - a[0], vy = (double)c[1] - a[1], vz = (double)c[2] - a[2];
	const double nx = uy * vz - uz * vy;
	const double ny = uz * vx - ux * vz;
	const double nz = ux * vy - uy * vx;
	const double len = sqrt(nx * nx + ny * ny + nz * nz);

	if (len < 1e-6)
		return false;

	return fabs(ny / len) >= HD_GLOSS_SEED_UP && nyVertex < 0.0f;
}

/* The seed's verdict on one page, out of the up-facing triangles the page
 * has over the classic floor: `perFx[16]` counts them per footstep material
 * of the square under each centroid, and `pageTris` is every triangle the
 * page has in the rooms' surface passes. The class of the gloss-naming
 * material with the most votes wins when
 *
 *   - it holds at least half of the page's votes over any floor, so a page
 *     that is mostly stone and crosses one metal square stays matt, and the
 *     crate's top, all of it on its metal square, shines;
 *   - it is at least HD_GLOSS_SEED_MIN triangles, so that one stray triangle
 *     names nothing;
 *   - and it is at least HD_GLOSS_SEED_SHARE per cent of the page's own
 *     triangles. A page the level wears everywhere and that touches a metal
 *     floor here and there - RICH2's 3008, 194 votes on 226,746 triangles,
 *     and 3011, 970 on 315,671; ANDREA1's stucco facades 182 and 213, 18 on
 *     2,479 and 16 on 3,740 - is not a metal page, while the crate's page
 *     140 has 162 votes on 2,490 triangles, 6.5 per cent, and JOBY2's
 *     riveted plates 447 to 453 have 4 to 45 per cent (TrgProbe -glossseed
 *     over the fifteen levels). A metal page the rule turns away - JOBY2's
 *     riveted column 421, 108 votes on 14,406 triangles - is a line of the
 *     page table. */
#define HD_GLOSS_SEED_MIN	2
#define HD_GLOSS_SEED_SHARE	1

inline int HDGlossSeedClass(const int perFx[16], int pageTris)
{
	int total = 0;
	int byClass[HD_GLOSS_CLASSES] = { 0 };

	for (int fx = 0; fx < 16; fx++)
	{
		total += perFx[fx];
		byClass[HDGlossClassOfFx(fx)] += perFx[fx];
	}

	int best = HD_GLOSS_NONE;

	for (int cls = HD_GLOSS_METAL; cls <= HD_GLOSS_ICE; cls++)
		if (byClass[cls] > (best == HD_GLOSS_NONE ? 0 : byClass[best]))
			best = cls;

	if (best == HD_GLOSS_NONE || byClass[best] < HD_GLOSS_SEED_MIN ||
		byClass[best] * 2 < total ||
		(long long)byClass[best] * 100 < (long long)pageTris * HD_GLOSS_SEED_SHARE)
		return HD_GLOSS_NONE;

	return best;
}

/* The classic floor, one record per floor square of the whole level in room
 * order and, inside a room, x-major - square (sx, sz) is record
 * first + sx * zSectors + sz, the order of ROOM_INFO::floor
 * (floor[sz + x_size * sx]) and of the .TRG's light cubes, one cube per
 * square in the same order (the cube's brightness follows the floor light
 * of the .TRG triangles on its square at a correlation of 0.35 to 0.84 per
 * level read that way against 0.07 to 0.51 read z-major; TrgProbe
 * -glosslight over the fifteen levels). A wall square has no floor and an
 * all-black cube - 3,104 of 3,104 in ANDREA1, and the same in every level. */
struct HDSectorFloor
{
	short floorY;		// FLOOR_INFO::floor * 256, world y (y down)
	unsigned char fx;	// FLOOR_INFO::fx, the footstep material
	unsigned char wall;	// 1 = floor -127, a wall square
};

/* One room of that table. x runs over ROOM_INFO::y_size squares and z over
 * x_size - TR5's own naming, see hd_level.cpp's BuildRoomBoxes. */
struct HDRoomGrid
{
	int first;		// the room's first record
	int xSectors;
	int zSectors;
	float originX, originZ;	// r->x and r->z, world units
};

/* The record of the square under a room-local point, or -1 outside the
 * room's grid. */
inline int HDGlossSectorAt(const HDRoomGrid& grid, float x, float z)
{
	const int sx = (int)floorf(x / 1024.0f);
	const int sz = (int)floorf(z / 1024.0f);

	if (sx < 0 || sz < 0 || sx >= grid.xSectors || sz >= grid.zSectors)
		return -1;

	return grid.first + sx * grid.zSectors + sz;
}

/* The seed's vote of one triangle: an up-facing triangle whose centroid
 * stands on a floor square of the room within HD_GLOSS_SEED_REACH of that
 * square's floor height counts for the square's footstep material. */
inline void HDGlossSeedVote(const float a[3], const float b[3], const float c[3],
	float nyVertex, const HDRoomGrid& grid, const HDSectorFloor* sectors,
	int sectorCount, int perFx[16])
{
	if (!HDGlossFacesUp(a, b, c, nyVertex))
		return;

	const float cx = (a[0] + b[0] + c[0]) / 3.0f;
	const float cy = (a[1] + b[1] + c[1]) / 3.0f;
	const float cz = (a[2] + b[2] + c[2]) / 3.0f;
	const int at = HDGlossSectorAt(grid, cx, cz);

	if (at < 0 || at >= sectorCount || sectors[at].wall)
		return;

	if (fabsf(cy - (float)sectors[at].floorY) > (float)HD_GLOSS_SEED_REACH)
		return;

	perFx[sectors[at].fx & 15]++;
}

/* The bound of the sheen, the twins of hd_room.hlsl's HD_GLOSS_BOUND,
 * HD_GLOSS_FLOOR and HD_GLOSS_CAP (the note "The sheen" there says why): the
 * sheen of a pixel is held under B, HD_GLOSS_BOUND times the largest channel
 * of the surface's own lit colour plus HD_GLOSS_FLOOR and never more than
 * HD_GLOSS_CAP - its largest channel t goes to B (1 - e^(-t / B)), the other
 * two in proportion, so that it keeps its hue - and composed over the lit
 * colour as a screen, lit + sheen * (1 - lit), per channel. These two
 * functions are the shader's arithmetic on the processor, for the suite. */
#define HD_GLOSS_BOUND		2.5f
#define HD_GLOSS_FLOOR		(8.0f / 255.0f)
#define HD_GLOSS_CAP		0.75f

inline void HDGlossSheenBound(const float sheen[3], const float own[3], float out[3])
{
	const float bound = fminf(HD_GLOSS_BOUND * fmaxf(own[0], fmaxf(own[1], own[2])) +
		HD_GLOSS_FLOOR, HD_GLOSS_CAP);
	const float k = fmaxf(sheen[0], fmaxf(sheen[1], sheen[2])) / bound;
	const float scale = (1.0f - expf(-k)) / fmaxf(k, 1e-4f);

	for (int c = 0; c < 3; c++)
		out[c] = sheen[c] * scale;
}

inline void HDGlossSheenCompose(const float lit[3], const float sheen[3], float out[3])
{
	for (int c = 0; c < 3; c++)
	{
		const float room = 1.0f - lit[c];

		out[c] = lit[c] + sheen[c] * (room < 0.0f ? 0.0f : room > 1.0f ? 1.0f : room);
	}
}

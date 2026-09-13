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

// A .TRM - the remaster's object models (#123, stages S0 and S3-1).
//
// Stage S0 only counted: how many parts, pages, indices and vertices, and
// whether the file is one of the heads that carry a facial rig before their
// geometry. HDTrmProbe below is still that, unchanged and still cheap.
// Stage S3-1 adds HDTrmLoad, which reads the geometry as well, because a
// model has to be drawn now and not only counted.
//
// THE FORMAT is TRForge's measurement over 2,923 files of all five games
// (MIT, trf_remaster\ItemModels.h - not vendored: see vendor\trforge\
// VERSION.txt for why that one file could not be taken verbatim). The byte
// offsets below were measured again here over the 566 .TRM files of the
// remaster's game 5 folder; every number in the comments is from that pass.
//
//     char[3] "TRM"; u8 version (2 everywhere); u32 parts
//     parts * 44:  u32 material; float param[4]; u32 startIndex;
//                  u32 opaqueCount; u32 opaqueEnd;
//                  u32 alphaCount;  u32 alphaEnd; u32 additiveCount
//     u32 textures; u16 texId[textures]         padded to four bytes
//     u32 0                                     in every file but a head
//     u32 indices; u32 vertices
//     u16 index[indices]                        padded to four bytes
//     vertices * 24:
//         +0   float x, y, z
//         +12  u8 nx, ny, nz      the normal, BIASED BY 127 - (n-127)/128,
//                                 the same bias a .TRG vertex normal carries.
//                                 Read as signed bytes it comes out nonsense
//                                 and quietly, so the bytes are kept raw.
//         +15  u8 layer           which of texId[] this vertex wears,
//                                 COUNTED FROM ONE
//         +16  u8 bone0, bone1, bone2      indices into the CLASSIC mesh tree
//         +19  u8 u                        over 255
//         +20  u8 weight0, weight1, weight2
//         +23  u8 v                        over 255
//
// A SECOND VERTEX LAYOUT, thirty bytes wide, carries the two coordinates as
// FLOATS at the end instead (#153, round M2). The file says which by its
// length and the reader takes either; the measurement that established the
// layout, and what rounding its coordinates onto this engine's byte grid
// costs, are in the .cpp beside TrmVertexBytesWide.
//
// WHAT THE MEASUREMENT OVER 5\ITEM SAYS (566 files, 1,260,496 vertices,
// 1,561,395 triangles), because these are the facts hd_model.cpp builds on:
//
//   - the three weights sum to exactly 255 in ALL 1,260,496 vertices; 174,896
//     of them (13.9 %) carry more than one, so the third weight is real and
//     dropping it would be visible at every joint.
//   - the highest bone index a WEIGHTED vertex names is 28 (GREEN_TEETH),
//     which is what lets the joint palette be 32. The only two files past
//     that are PIERRE_HEAD (40) and LARSON_HEAD (40) - facial-rig heads,
//     whose bone numbers belong to the rig and not to the mesh tree, and
//     which are drawn rigidly on a single node anyway.
//   - the layer is never 0 and never past the texture count: it is 1-based
//     and always valid. The highest is 11.
//   - NOT ONE of the 1,561,395 triangles spans two layers. One page per draw
//     call, exactly as the .TRG measurement found for rooms - no atlas and no
//     rebased texture coordinates.
//   - a model has at most 3 parts (67 of the 566 have more than one), and the
//     three passes of a part are contiguous: opaqueEnd == start + opaque and
//     alphaEnd == opaqueEnd + alpha in every part of every file. 67 parts
//     carry an alpha run, 48 an additive one.
//
// The word that is zero: 58 of the 2,923 models carry a facial rig there
// instead - all of them heads. TRForge finds the geometry behind it by looking
// for the index count the part table already implies, followed by a vertex
// count whose block ends at the last byte of the file; over all 58 that lands
// right 58 times and on nothing else. The same search is done here.

struct HDTrmInfo
{
	int version;
	int parts;
	int textures;
	int indices;
	int vertices;
	bool facialRig;		// the file carried a rig where the zero belongs
	/* 24 or 30 - which of the two vertex layouts the file uses (#153, round
	 * M2). 30 means its two texture coordinates are floats and were rounded
	 * onto the byte grid to be read; see the measurement in the .cpp. Thirty
	 * files of the two remasters are laid out that way and all thirty are
	 * OUTFIT_TRX_*_HD.TRM in the I-III bundle's 1\ITEM. */
	int vertexBytes;
};

// One part of a model: a material and its three index runs.
struct HDTrmPart
{
	unsigned int material;
	float param[4];
	unsigned int start;
	unsigned int opaque, opaqueEnd;
	unsigned int alpha, alphaEnd;
	unsigned int additive;
};

// 24 bytes, laid out as they are on disk - see the table above.
struct HDTrmVertex
{
	float x, y, z;
	unsigned char nx, ny, nz;	// biased by 127
	unsigned char layer;		// 1-based index into HDTrmModel::texture
	unsigned char bone[3];		// classic mesh tree nodes
	unsigned char u;		// over 255
	unsigned char weight[3];	// sum to 255
	unsigned char v;		// over 255
};

// A whole model. Owns its four arrays; HDTrmFree gives them back.
struct HDTrmModel
{
	HDTrmPart* part;
	unsigned short* texture;	// .DDS numbers in <install>\TEX
	unsigned short* index;
	HDTrmVertex* vertex;

	int version;
	int parts;
	int textures;
	int indices;
	int vertices;
	bool facialRig;
	int vertexBytes;	// 24 or 30 on disk - see HDTrmInfo
};

// True when the file exists at all. Cheap - no read.
bool HDTrmExists(const char* path);

// Reads the header of one .TRM. On failure returns false and *why names the
// reason (a short string owned by this module, never null).
bool HDTrmProbe(const char* path, HDTrmInfo* out, const char** why);

// Reads the whole model, geometry included. Same refusals as HDTrmProbe, plus
// the ones a body needs to be drawable at all. Call HDTrmFree on success.
bool HDTrmLoad(const char* path, HDTrmModel* out, const char** why);
void HDTrmFree(HDTrmModel* model);

// The .DDS page a vertex wears, or -1 when its layer names none. The layer is
// counted from ONE, which is the single most common way to paint a model with
// the wrong page.
int HDTrmPageOf(const HDTrmModel* model, const HDTrmVertex* vertex);

/* THE MIRRORED WEIGHTS OF A TWO-SIDED FILE (#153, round M2b).
 *
 * A model of Lara's two hands is drawn once and mirrored, and an exporter that
 * mirrors the GEOMETRY without mirroring the bone names leaves vertices of one
 * hand carrying weight on the other hand's bones. In the bind pose the two
 * hands sit beside each other and nothing shows; the moment an animation moves
 * them apart, the far bone drags those vertices into the gap between the hands
 * and the picture is a sliver of skin hanging in the air. The user's screenshot
 * of the HK USP mod in RICH2 is exactly that.
 *
 * `pair` names the SIDES: pair[i].a is a bone of the side that stands at
 * POSITIVE x in the model's own frame and pair[i].b the same bone of the other.
 * A vertex whose weights name bones from both sides is CROSSED, and the side
 * with the larger weight sum is the side it really belongs to; the far side's
 * weight is moved onto that bone's counterpart - a.b -> a.a and a.a -> a.b -
 * and the weights are added where the counterpart is already named. An exact
 * tie is decided by the sign of the vertex's own x, which is the same rule read
 * off the geometry instead of off the weights - measured over all 94 hand files
 * of the IV-VI install and all seven of the user's three gun mods: of their
 * 6,723 crossed vertices, 6,719 have a majority and on every one of those the
 * majority side and the sign of x agree, not one disagrees, and the other four
 * are the exact ties this rule exists for (all four in the HK USP mod).
 *
 * `barPermille` is how big the far side's share must be before a vertex is
 * folded at all, in thousandths of the vertex's own weight sum; 0 folds every
 * crossed vertex. The number this engine uses and the measurement behind it
 * are in hd_lara.cpp.
 *
 * The weights are renormalised to sum to 255 - the sum every one of the
 * 1,260,496 vertices of the remaster's own game 5 has, and the sum the shader
 * assumes - for every vertex of the model, folded or not. A vertex that
 * already sums to 255 is not touched, so a file with no crossed vertices and
 * no odd sums comes out of this byte for byte as it went in.
 *
 * Any pointer may be null; `counts` is optional. */
struct HDTrmMirrorPair
{
	unsigned char a;
	unsigned char b;
};

struct HDTrmFoldCounts
{
	int crossed;		// vertices whose weights named both sides
	int folded;		// of those, the ones weight was moved on
	/* Crossed with the two sides weighted EXACTLY the same. They are not a
	 * fourth thing that happens to a vertex: a tie above the bar is folded
	 * like any other - `folded` counts it too - and the only difference is
	 * that its far side is picked by the sign of its own x rather than by the
	 * smaller weight. This is the count of the vertices that rule decided,
	 * and it is here because they are the ones a reader of a surprising
	 * picture would want to find. (#153, round M2b: this said "left exactly
	 * as they were", which is what the code does NOT do.) */
	int tied;
	int renormed;		// vertices whose three weights did not sum to 255
	int worstPermille;	// the largest far-side share met, per mille
	int foldedPermille;	// the largest share actually folded
};

void HDTrmFoldMirrored(HDTrmModel* model, const HDTrmMirrorPair* pair, int pairs,
	int barPermille, HDTrmFoldCounts* counts);

/* WHAT THIS READER HAS DONE SINCE THE PROCESS STARTED (#153, round M2, the
 * fix round; both notes corrected in round M2b). Three running totals, and a
 * caller that wants "per level build" takes them twice and subtracts:
 *
 *     opens   files this module opened - every HDTrmExists and every parse
 *     loads   HDTrmLoad CALLS, counted before the file is opened. A call for
 *             a path that is not there counts here and adds nothing to
 *             `opens` or `bytes`, which is what makes the three read
 *             together: loads above opens means somebody asked for files
 *             that are not on this disk
 *     bytes   bytes read off disk. HDTrmProbe reads the WHOLE file and keeps
 *             the header, so a probe costs the same read a load does
 *
 * They exist to answer one question with a number instead of an opinion: does
 * a level build read the whole wardrobe, or only the outfit that is worn. Any
 * of the three pointers may be null.
 *
 * TWO THREADS REALLY DO REACH THEM and the counters are still plain doubles,
 * because the two can never be inside this module at the same time. The load
 * thread's HDTrmExists (hd_scan.cpp, the level scan) and the main thread's
 * HDTrmProbe (hd_wardrobe.cpp, the wardrobe scan) are the two callers, and
 * specific\file.cpp JOINS the load thread before HDLevelBuild runs - so the
 * scan is finished before the wardrobe's first probe. A caller that breaks
 * that order has to make these atomic; the join is the guarantee, not the
 * word "single-threaded". */
void HDTrmCounters(double* opens, double* loads, double* bytes);

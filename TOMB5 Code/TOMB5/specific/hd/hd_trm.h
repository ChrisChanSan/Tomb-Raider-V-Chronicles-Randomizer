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

// A .TRM - the remaster's object models.
//
// HDTrmProbe only counts: how many parts, pages, indices and vertices, and
// whether the file is one of the heads that carry a facial rig before their
// geometry. It is cheap. HDTrmLoad reads the geometry as well, for a model
// that has to be drawn and not only counted.
//
// The layout is TRForge's description (MIT, trf_remaster\ItemModels.h - not
// vendored: see vendor\trforge\VERSION.txt for why that one file could not
// be taken verbatim). It is not taken on trust: the byte offsets below were
// counted again here over the 566 .TRM files of the remaster's game 5
// folder, every number in the comments is from that pass, and the normal
// bias in particular over its triangles (TrgProbe -trmnormals, below).
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
//         +12  u8 nx, ny, nz      the normal, biased by 127 - (n-127)/128,
//                                 the same bias a .TRG vertex normal carries:
//                                 over 4,683,717 triangle corners of 565
//                                 files of 5\ITEM, 38.1 % lie within 10
//                                 degrees of their triangle's plane read so,
//                                 0.6 % read as signed bytes (TrgProbe
//                                 -trmnormals). Read as signed bytes it comes
//                                 out nonsense and quietly, so the bytes are
//                                 kept raw.
//         +15  u8 layer           which of texId[] this vertex wears,
//                                 counted from one
//         +16  u8 bone0, bone1, bone2      indices into the classic mesh tree
//         +19  u8 u                        over 255
//         +20  u8 weight0, weight1, weight2
//         +23  u8 v                        over 255
//
// A second vertex layout, thirty bytes wide, carries the two coordinates as
// floats at the end instead. The file says which by its length and the
// reader takes either; the measurement that established the layout, and
// what rounding its coordinates onto this engine's byte grid costs, are in
// the .cpp beside TrmVertexBytesWide.
//
// What the measurement over 5\ITEM says (566 files, 1,260,496 vertices,
// 1,561,395 triangles), because these are the facts hd_model.cpp builds on:
//
//   - the three weights sum to exactly 255 in all 1,260,496 vertices; 174,896
//     of them (13.9 %) carry more than one, so the third weight is real and
//     dropping it would be visible at every joint.
//   - the highest bone index a weighted vertex names is 28 (GREEN_TEETH),
//     which is what lets the joint palette be 32. The only two files past
//     that are PIERRE_HEAD (40) and LARSON_HEAD (40) - facial-rig heads,
//     whose bone numbers belong to the rig and not to the mesh tree, and
//     which are drawn rigidly on a single node anyway.
//   - the layer is never 0 and never past the texture count: it is 1-based
//     and always valid. The highest is 11.
//   - not one of the 1,561,395 triangles spans two layers. One page per draw
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
	/* 24 or 30 - which of the two vertex layouts the file uses. 30 means its
	 * two texture coordinates are floats and were rounded onto the byte
	 * grid to be read; see the measurement in the .cpp. Thirty files of the
	 * two remasters are laid out that way and all thirty are
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

	/* The facial rig's joints, for a head that carries one: `rigJoints` of
	 * them, three floats each. The rig stores every joint as an inverse bind
	 * matrix, 3x4 and row-major, so the joint itself stands at -R^T t, which
	 * is what is kept. A vertex's bone number is an index into this list.
	 * That reading of the rig is TROne's own measurement on the .TRM files
	 * (Copyright 2026 Todesengel, GPLv3, TROne-main (copy of 2026-10-05), its
	 * vendored ItemModels.h: over the 54 head files of both installs every
	 * facial joint read so lands within 9 units of the vertices it carries),
	 * a fact about the files; here it is held by LipsTest, which finds the
	 * frame-0 joints of ten rigged faces on the rig's joints read so, 0.0002
	 * to 3.46 units apart (hd_trl.cpp, the axes).
	 *
	 * In the rig's own axes, which are not the vertices' - LARA_HEAD's head
	 * joint is (1, -12, 247) here and (1, -247, -12) in the model - so only
	 * the distance between two joints means anything. 0 and null for every
	 * file without a rig. */
	int rigJoints;
	float* rigJoint;

	/* The rest of the rig, for a head that carries one - what a face needs
	 * to be moved by its track files (hd_trl.h).
	 *
	 * `rigBind` is every joint's inverse bind matrix as the file stores it,
	 * twelve floats a joint in the row order above; `rigJoint` is derived
	 * from it.
	 *
	 * Between the rig and the geometry lies a block of joint animation with
	 * a header of its own:
	 *
	 *     u32 N; i32 id[N]; i32 parent[N]; u32 C; i32 column[C]; u32 frames
	 *     frames * C * 48:  3x4 matrices, row-major like the rig's
	 *
	 * The header is 4 * (3 + 2N + C) bytes. The frame count is the low half
	 * of its last word; the high half is not a count (ADMIRAL_HEAD's word
	 * is 0x00010003 and PIERRE_HEAD's 0x00000003, three frames each). Over
	 * the 23 rigged *_HEAD.TRM of the IV-VI install's 4\ITEM and 5\ITEM the
	 * header and frames * C * 48 end exactly where the geometry begins, and
	 * that is the test a block passes to be kept.
	 *
	 * Node n of the header is rig joint n where n < rigJoints, and a node
	 * past the rig is a helper that only carries others. Column c drives
	 * node column[c]; parent[n] is the node n hangs off, -1 for none. A
	 * column's matrix is LOCAL to its parent's. id[] is not read. Frame 0 is
	 * the pose the face was bound in and is what `blockBind` keeps, C
	 * matrices. The frames after it are not held in memory - LARA_HEAD's
	 * whole block is 975,360 bytes of frames - but `blockAt` is where
	 * frame 0 lies in the file, so a reader can stream them (hd_trl.h,
	 * HDTrlOpenBlock): 2264 in LARA_HEAD.TRM, 2648 in LARSON_HEAD.TRM.
	 *
	 * `blockLoop` is the first frame after 0 whose C matrices equal frame
	 * 0's byte for byte, and 0 where no frame does: the block's frames
	 * 0 .. blockLoop - 1 are then one loop. 499 in LARA_HEAD,
	 * LARA_CREEPY_HEAD and LARA_YOUNG_HEAD (508 frames: 0-498 a loop of
	 * the eyes and the brows, 499 and 500 equal to 0, 501-507 seven
	 * single key poses), 300 in LARSON_HEAD (301 frames); the other
	 * rigged heads' blocks hold two to four frames and get 1 or 0.
	 *
	 * `ownBone` / `ownWeight` are every vertex's three bones and three
	 * weights as the file writes them, three bytes a vertex. The model code
	 * renames a rigid face's body joints in `vertex` before it builds
	 * (rig_neck_of, hd_model.cpp); these keep the file's own numbers, which
	 * are what the track files move.
	 *
	 * All of them null and 0 for a file without a rig and for one whose
	 * block does not pass the test. */
	float* rigBind;
	int blockNodes;
	int* blockParent;
	int blockColumns;
	int* blockColumn;
	int blockFrames;
	float* blockBind;
	long blockAt;
	int blockLoop;
	unsigned char* ownBone;
	unsigned char* ownWeight;
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
// counted from one, which is the single most common way to paint a model with
// the wrong page.
int HDTrmPageOf(const HDTrmModel* model, const HDTrmVertex* vertex);

/* The mirrored weights of a two-sided file.
 *
 * A model of Lara's two hands is drawn once and mirrored, and an exporter that
 * mirrors the geometry without mirroring the bone names leaves vertices of one
 * hand carrying weight on the other hand's bones. In the bind pose the two
 * hands sit beside each other and nothing shows; the moment an animation moves
 * them apart, the far bone drags those vertices into the gap between the hands
 * and the picture is a sliver of skin hanging in the air, as seen with the HK
 * USP mod in RICH2.
 *
 * `pair` names the sides: pair[i].a is a bone of the side that stands at
 * positive x in the model's own frame and pair[i].b the same bone of the other.
 * A vertex whose weights name bones from both sides is crossed, and the side
 * with the larger weight sum is the side it really belongs to; the far side's
 * weight is moved onto that bone's counterpart - a.b -> a.a and a.a -> a.b -
 * and the weights are added where the counterpart is already named. An exact
 * tie is decided by the sign of the vertex's own x, which is the same rule read
 * off the geometry instead of off the weights - measured over all 94 hand files
 * of the IV-VI install and all seven files of three gun mods: of their
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
 * 1,260,496 vertices of the remaster's own game 5 has (counted here, see the
 * top of this file), and the sum our model shader reads the three weights
 * against (UBYTE4N, hd_model.hlsl) - for every vertex of the model, folded
 * or not. A vertex that
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
	/* Crossed with the two sides weighted exactly the same. They are not a
	 * fourth thing that happens to a vertex: a tie above the bar is folded
	 * like any other - `folded` counts it too - and the only difference is
	 * that its far side is picked by the sign of its own x rather than by the
	 * smaller weight. This is the count of the vertices that rule decided,
	 * and it is here because they are the ones a reader of a surprising
	 * picture would want to find. */
	int tied;
	int renormed;		// vertices whose three weights did not sum to 255
	int worstPermille;	// the largest far-side share met, per mille
	int foldedPermille;	// the largest share actually folded
};

void HDTrmFoldMirrored(HDTrmModel* model, const HDTrmMirrorPair* pair, int pairs,
	int barPermille, HDTrmFoldCounts* counts);

/* What this reader has done since the process started. Three running
 * totals, and a caller that wants "per level build" takes them twice and
 * subtracts:
 *
 *     opens   files this module opened - every HDTrmExists and every parse
 *     loads   HDTrmLoad calls, counted before the file is opened. A call for
 *             a path that is not there counts here and adds nothing to
 *             `opens` or `bytes`, which is what makes the three read
 *             together: loads above opens means somebody asked for files
 *             that are not on this disk
 *     bytes   bytes read off disk. HDTrmProbe reads the whole file and keeps
 *             the header, so a probe costs the same read a load does
 *
 * They exist to answer one question with a number instead of an opinion: does
 * a level build read the whole wardrobe, or only the outfit that is worn. Any
 * of the three pointers may be null.
 *
 * Two threads really do reach them, and the counters are still plain doubles,
 * because the two can never be inside this module at the same time. The load
 * thread's HDTrmExists (hd_scan.cpp, the level scan) and the main thread's
 * HDTrmProbe (hd_wardrobe.cpp, the wardrobe scan) are the two callers, and
 * specific\file.cpp joins the load thread before HDLevelBuild runs - so the
 * scan is finished before the wardrobe's first probe. A caller that breaks
 * that order has to make these atomic; the join is the guarantee, not the
 * word "single-threaded". */
void HDTrmCounters(double* opens, double* loads, double* bytes);

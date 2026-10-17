/*
 * Modified 2026 by ChrisChanSan.
 *
 * The layout of a face's track file and the deform of a face by the joints
 * such a file moves - a frame's matrix times the inverse of the bind's - were
 * found on the data files by TROne (GPLv3) by Todesengel1931 -
 * https://github.com/todesengel1931. Both were measured again here on the
 * installation's own files, under this engine's names; which column drives
 * which joint is read out of the face's own .TRM (hd_trm.h, HDTrmModel::
 * rigBind), not voted from translations.
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

#include "hd_trm.h"

#include <cstdio>

// The lips of a cutscene face.
//
// The remaster moves a speaking face with one file per spoken track:
// <FACE>_HEAD_<track>.TRL in 5\ITEM, where <track> is the audio track the
// cutscene plays (CUTSEQ.BIN) and <FACE>_HEAD.TRM is the face. LARA_HEAD.TRM
// lies in 4\ITEM and its eighteen track files in 5\ITEM.
//
//     u32 word
//     frames * C * 48:  3x4 matrices, row-major (m0 m1 m2 tx / m4 m5 m6 ty /
//                       m8 m9 m10 tz), one per column, LOCAL to the parent
//                       the face's own header names - see HDTrmModel::rigBind
//
// C is the face's own column count, 40 in 44 of the 45 files of 5\ITEM and 7
// in HANGMAN_HEAD_132.TRL. The word is the frame count in 30 of them; in the
// other 15 - every file of ADMIRAL_HEAD, DEMON_HEAD, PATRICK_BROWN_HEAD,
// PATRICK_WHITE_HEAD and SERGIE_HEAD - only its low half is, and the high half
// is 1 (ADMIRAL_HEAD_113.TRL: 0x00010894, 2196 frames). So the frame count is
// the word when it fills the file and its low half otherwise.
//
// A frame is one frame of the cutscene, thirty a second, and the frame index
// IS the cutscene frame: the face's motion peaks against the engine's own
// speech ranges (game\deltapak.cpp, the *_chat_ranges tables) at a lag of 0 to
// -6 frames in 19 of the 20 speaker pairs they name.
//
// Nothing in here touches a device or a global. The engine's draw
// (hd_model.cpp), ModelProbe and LipsTest call the same functions.

/* ---------------------------------------------------------- the file -- */

struct HDTrlClip
{
	std::FILE* file;	// open while the clip is in use, null otherwise
	int frames;
	int columns;
	unsigned int word;	// the file's first word as it stands
	bool lowWord;		// the frame count is its low half
	long at;		// where frame 0 lies: 4 in a track file
};

/* The frame rule on its own: how many frames of `columns` matrices a file of
 * `bytes` bytes whose first word is `word` holds. False when neither reading
 * of the word fills the file exactly - a file with another column count than
 * the face's. */
bool HDTrlFrames(unsigned int word, unsigned long long bytes, int columns,
	int* frames, bool* lowWord);

/* Opens a track file for a face of `columns` columns and reads its first
 * word; the frames are read one at a time by HDTrlReadFrame. On failure *why
 * names the reason ("columns" when the file holds no whole number of frames of
 * that many columns) and nothing stays open. */
bool HDTrlOpen(const char* path, int columns, HDTrlClip* out, const char** why);

/* Opens the joint block a head's own .TRM carries as a clip: `frames` frames
 * of `columns` matrices from byte `at` on - HDTrmModel::blockAt, blockFrames
 * and blockColumns. The frames are the same 3x4 matrices a track file holds
 * and read the same way; frame 0 is the face's bind (HDTrmModel::blockBind).
 * False, with *why, when the file is shorter than the block. */
bool HDTrlOpenBlock(const char* trmPath, long at, int frames, int columns,
	HDTrlClip* out, const char** why);

// One frame, columns * 12 floats. A frame past either end is clamped.
bool HDTrlReadFrame(HDTrlClip* clip, int frame, float* out);

void HDTrlClose(HDTrlClip* clip);

/* ----------------------------------------------------------- the map -- */

#define HD_LIPS_MAX_COLUMNS	64

/* From the track's axes to the vertices'. A rigged face's vertices stand in
 * (x, -z, y) of its rig; the track files of ten faces stand there too, two
 * stand elsewhere, and ZIP_HEAD fits no order - see g_axis in hd_trl.cpp for
 * the table and the measurement. */
enum HDLipsAxis
{
	HD_LIPS_AXIS_IDENTITY = 0,
	HD_LIPS_AXIS_X_NZ_Y,		// (x, -z, y)
	HD_LIPS_AXIS_X_NY_NZ,		// (x, -y, -z)
	HD_LIPS_AXIS_REFUSED		// no axis order fits the face's vertices
};

/* A face's joints, as its own header names them.
 *
 * Column c drives rig joint joint[c], or is a helper (-1) that only carries
 * the columns hanging off it. Its matrix is composed down the parents,
 * G_c = G_parent(c) * L_c, in `order`; a parent that is no column is the head
 * itself and does not move. The deform of a joint is the composed frame times
 * the inverse of the composed bind, D_c = G_c * inverse(Gb_c), where the bind
 * is frame 0 of the face's own block (HDTrmModel::blockBind), and it turns
 * about the head joint, in the vertices' axes:
 *
 *     S_j = T(head) * A * D_c * A^T * T(-head)
 *
 * Every rig joint no column drives - the head joint and the body joints
 * included - keeps S_j = identity. */
struct HDLipsMap
{
	int columns;
	int rigJoints;
	int headJoint;		// the rig joint the face's own joints hang off
	int driven;		// columns that drive a rig joint
	int axis;		// HDLipsAxis
	double head[3];		// the head joint in the vertices' axes
	int order[HD_LIPS_MAX_COLUMNS];		// parents before children
	int parent[HD_LIPS_MAX_COLUMNS];	// the parent COLUMN, -1 for the head
	int joint[HD_LIPS_MAX_COLUMNS];		// the rig joint driven, -1 for a helper
	double bindInverse[HD_LIPS_MAX_COLUMNS][12];
};

/* Builds the map of a face out of its .TRM (the rig, the block's header and
 * its frame 0 - HDTrmLoad keeps them). `stem` is the file's name without
 * .TRM, which the axis table is keyed on. False, with *why, for a file without
 * a rig or a block and for a face the table refuses; `driven` and
 * `rigJoints` are filled all the same where the header could be read, for the
 * log. */
bool HDLipsMapBuild(const HDTrmModel* trm, const char* stem, HDLipsMap* out,
	const char** why);

/* True when two faces carry the same rig and the same joint block header,
 * bit for bit: the joint count and every inverse bind matrix, the block's
 * nodes and parents, its columns and the joint each drives, and its frame
 * 0. Everything a track file's frame is read against is then the same, so
 * the one face's track files drive the other face's joints exactly as its
 * own; the geometry, its bones and weights and the block's later frames
 * may differ. False for a file without a rig or a block. */
bool HDLipsSameRig(const HDTrmModel* a, const HDTrmModel* b);

// "identity", "(x,-z,y)", "(x,-y,-z)" or "-".
const char* HDLipsAxisName(int axis);

/* A frame's columns composed down the parents: columns * 12 doubles, 3x4
 * row-major, relative to the head. `frame` is columns * 12 floats as a track
 * file or the block holds them. */
void HDLipsCompose(const HDLipsMap* map, const float* frame, double* global);

/* The skins of one frame: rigJoints * 12 doubles, 3x4 row-major, S_j of every
 * rig joint. `amount` blends each towards the identity - 1 is the frame, 0 the
 * bind - for the ease at the end of a clip.
 *
 * `mask` plays only some of the frame's columns, bit c for column c: a column
 * outside it takes the local matrix `bind` holds for it (columns * 12 floats,
 * the block's frame 0) before the columns are composed, and the joint it
 * drives keeps the identity, so the vertices on no masked joint are copied
 * bit for bit (HDLipsDeform). A mask that holds every column, or no `bind`,
 * is the whole frame, exactly as without them. */
void HDLipsSkins(const HDLipsMap* map, const float* frame, double amount,
	double* skins, unsigned long long mask = ~0ull, const float* bind = 0);

/* The turn of one column from the bind at a frame, in degrees: the column's
 * composed matrix times the inverse of its composed bind, its scale taken out
 * (the cube root of the determinant). */
double HDLipsTurn(const HDLipsMap* map, const float* frame, int column);

/* ------------------------------------------------ the loop between lines --
 *
 * The block a head's .TRM carries past its frame 0 (HDTrmModel::blockLoop)
 * is, in four faces, a loop of the eyes. Measured on the IV-VI install:
 *
 *   LARA_HEAD, LARA_CREEPY_HEAD, LARA_YOUNG_HEAD (4\ITEM, the same block):
 *     frames 0-498 move the eyes and the brows alone - blinks peaking at
 *     92, 138, 199, 255, 315 and 323, 422, 469 with the upper lids 29-30
 *     degrees closed, glances of the eyeballs up to 9.7 degrees, the brows
 *     up to 5.9 degrees - while the ten mouth columns, the nostrils and the
 *     cheeks stay at the bind to 0.000 degrees; 499 and 500 are frame 0
 *     again, and 501-507 are seven single key poses, not motion.
 *   LARSON_HEAD (5\ITEM): 300 frames with blinks at 72, 149 and 230, and
 *     two slow presses of the lips (frames 28-117 and 137-247: lips 8.0
 *     degrees, the jaw 1.1) that are no part of a face at rest.
 *
 * So the loop plays through a mask of columns, a table keyed on the face
 * file's name like the axis table: Lara's eye columns 12-15, 22 and 23
 * (joints 19-22 the lids, 29 and 30 the eyeballs) and her brow columns 16-21
 * with the helper columns they hang off, 26-29, 33 and 34 - frames 0-498
 * played in full equal that mask bit for bit, and the mask keeps a file
 * whose loop moves the mouth from moving it; LARSON's eye columns 12-15
 * alone (joints 27-30). Every other face: 0, no loop. */
unsigned long long HDLipsLoopMask(const char* stem);

/* The upper lids of a face with a loop - columns 12 and 13 in all four - for
 * the blinks, and 0 for every other face. */
unsigned long long HDLipsLidMask(const char* stem);

/* A block shorter than this plays no loop: a second at thirty frames. */
#define HD_LIPS_LOOP_MIN	30

/* A blink starts on the first frame of the loop whose upper lids stand more
 * than this far from the bind (the larger of the lid columns' HDLipsTurn),
 * the frame before them not: LARA_HEAD 92, 137, 198, 254, 314, 322, 420 and
 * 468, LARSON_HEAD 71, 148 and 229. Each peak lies on its start or up to two
 * frames after it, at 20.6-30.2 degrees in LARA_HEAD and 27.5 in
 * LARSON_HEAD. */
#define HD_LIPS_BLINK_DEGREES	15.0

/* The blinks of a loop of `loop` frames read out of `clip`, into `start`
 * (the frame) and `lid` (the lids' turn there, degrees), at most `max`.
 * Returns how many; frame 0 is compared against the loop's last frame. */
int HDLipsBlinks(const HDLipsMap* map, HDTrlClip* clip, int loop,
	unsigned long long lids, int* start, float* lid, int max);

/* ------------------------------------------------------ the firing face --
 *
 * The classic game puts LARA_SCREAM's head on Lara while the fire key is held
 * with a gun ready (game\larafire.cpp, LaraGun, LG_READY) and for 16 ticks
 * after each shot a cutscene fires (game\deltapak.cpp, deal_with_pistols).
 * That head is LARA_SKIN's with the same vertices and other paint: bared,
 * clenched teeth, the lips drawn back.
 *
 * Its HD counterpart is a single key pose of Lara's block: frame 501, the
 * clenched teeth with knitted brows (the lower lip 25.8 degrees, the corners
 * 19.5, the lips 4.08 units apart, the jaw at 0). Frame 506 is the other
 * candidate, a shout with the mouth wide open (the jaw 10.89 degrees, the lips
 * 9.36 units apart). Only the faces with a loop have the frame. */
#define HD_LIPS_FIRE_FRAME	501

/* The classic swap is instant; three ticks soften it without lagging behind
 * it: the first tick shows a third of the way. */
#define HD_LIPS_EASE_IN_TICKS	3

/* How far the firing face is shown `ticks` ticks after the swap came on:
 * (ticks + 1) / HD_LIPS_EASE_IN_TICKS up to 1, and 0 before it. */
double HDLipsEaseIn(int ticks);

/* The step of the firing face, 0 .. HD_LIPS_EASE_IN_TICKS, `ticks` ticks on
 * from `step`: up by one a tick while the swap is on, down by one while it is
 * off. step / HD_LIPS_EASE_IN_TICKS is how far the face stands towards the
 * firing frame; the tick the swap comes on is the first step. */
int HDLipsFireStep(int step, bool on, int ticks);

/* The skins `t` of the way from `from` to `to`, rigJoints * 12 doubles each,
 * element by element: from + t * (to - from), `to` itself at t = 1 and `from`
 * itself at t = 0. From the identities it is the ease of HDLipsSkins to the
 * last bit, so every vertex moves on the straight line between the two
 * poses - the firing face comes on out of the loop's frame of the moment and
 * goes back into it. */
void HDLipsSkinsBlend(const double* from, const double* to, double t,
	int rigJoints, double* out);

/* What a face shows at a cutscene frame, for a clip of `frames` frames.
 *
 * Inside the clip the frame itself. A track file is often shorter than its
 * cutscene and most stop moving before their audio ends, but its last frame
 * can stand well off the bind - 9.72 units in LARSON_HEAD_108.TRL, the widest
 * of the 44 files a face takes (LipsTest prints each) - so past the last
 * frame the face eases back over HD_LIPS_EASE_FRAMES frames -
 * `amount` from (N - k) / N down to 1 / N, k frames past the last - and from
 * then on it is the bind, which is the face's own static buffer. A frame
 * before 0 is frame 0. `clipFrame` is the frame to read. */
enum HDLipsSource
{
	HD_LIPS_SOURCE_TRL = 0,
	HD_LIPS_SOURCE_EASE,
	HD_LIPS_SOURCE_BIND
};

#define HD_LIPS_EASE_FRAMES	10

int HDLipsSourceOf(int frame, int frames, int* clipFrame, double* amount);

// "trl", "ease" or "bind".
const char* HDLipsSourceName(int source);

/* The deform. Every vertex through the blend of its own bones' skins,
 *
 *     M = sum over k of (weight[k] / 255) * S[bone[k]]
 *     position' = M * position
 *     normal'   = cofactor(M) * normal, at the normal's own length
 *
 * `position` is 3 floats a vertex in the .TRM's own model space; `normal`,
 * `bone` and `weight` are 3 bytes a vertex as the file writes them, the
 * normal biased by 127. The cofactor and not M itself: the skins scale
 * (determinants 0.958 to 1.229 over LARA_HEAD_98.TRL), and a plain turn
 * leaves a normal up to 25.0 degrees off the cofactor's (the same file, frame
 * 709). A vertex whose weight lies only on joints that do not move is copied
 * as it is. Returns the largest distance a vertex moved. */
double HDLipsDeform(const double* skins, int rigJoints, int vertices,
	const float* position, const unsigned char* normal,
	const unsigned char* bone, const unsigned char* weight,
	float* outPosition, unsigned char* outNormal);

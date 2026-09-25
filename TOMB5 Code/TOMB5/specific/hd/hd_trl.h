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

// "identity", "(x,-z,y)", "(x,-y,-z)" or "-".
const char* HDLipsAxisName(int axis);

/* A frame's columns composed down the parents: columns * 12 doubles, 3x4
 * row-major, relative to the head. `frame` is columns * 12 floats as a track
 * file or the block holds them. */
void HDLipsCompose(const HDLipsMap* map, const float* frame, double* global);

/* The skins of one frame: rigJoints * 12 doubles, 3x4 row-major, S_j of every
 * rig joint. `amount` blends each towards the identity - 1 is the frame, 0 the
 * bind - for the ease at the end of a clip. */
void HDLipsSkins(const HDLipsMap* map, const float* frame, double amount,
	double* skins);

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

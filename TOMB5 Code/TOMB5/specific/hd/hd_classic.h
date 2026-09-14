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

// THE CLASSIC POSE (#155) - a moveable's mesh tree and its first
// frame, read out of a Tomb Raider I, II or III level file.
//
// WHY THIS EXISTS. A mod's inventory model comes out of the Tomb Raider I-III
// remaster and is stored in the classic BIND POSE: node n's vertices carry the
// mesh tree's offsets and nothing else, no rotation at all. That is why the
// user's IMI Uzis stood side by side and PARALLEL in our ring while the same
// file shows two guns CROSSED like an X in the game it came from - the cross
// is not in the file, it is frame 0 of the classic object the file stands for,
// and the remaster's display puts it there. Round M3 measured the parallel
// pair, said so, and left the question to the project; this is the answer.
//
// NOTHING OF A REMASTER BINARY IS IN HERE. The .TRM says which node a vertex
// rides on and the classic level files of the user's own installation say
// where the mesh tree puts that node and how frame 0 turns it. Both are the
// user's data files, read where they lie; the layouts below are the published
// TR1/TR2/TR3 level formats (TRosettaStone).
//
// ENGINE-FREE, like hd_wardrobe and hd_install beside it: plain C on the
// outside, C++17 inside, no engine global and no Log. The caller writes the
// log line out of what it is handed, so that ModelProbe and the engine say
// the same thing about the same file - the rule round M1 laid down when the
// tool's own copy of the page rule had drifted from the engine's.
//
// THE WORD ORDER OF A TR1 FRAME, MEASURED (#155). Readers disagree
// about which of a TR1 frame's two rotation words is the high half. The
// question is settled here on the user's own files and not by choosing a
// reader to believe: a frame carries its own bounding box, so the walk can be
// checked against the file's own answer. Over the eight weapon objects of
// 1\DATA\GYM.PHD the SECOND word as the high half reproduces those bounds to
// 0.5 .. 2.1 units - which is the rounding of a 1024 step angle table - and
// the first word as the high half misses them by 50 .. 107. The same three
// angles then come out of Tomb Raider I, II and III for the same object, whose
// TR2/TR3 packing is not in doubt.

struct HDClassicBone
{
	// 1 = pop (take the saved matrix back), 2 = push (save this one). Both
	// happen BEFORE the translation, which is the classic walk's own order.
	int flags;
	int x, y, z;		// this node's offset from the one it hangs off
};

// A moveable's skeleton is at most this deep here. The classic games go no
// further than Lara's fifteen, and the .TRM bone palette this engine skins
// with is the same 32 (hd_model.cpp, kJointBudget).
#define HD_CLASSIC_NODES 32

struct HDClassicObject
{
	int game;			// 1, 2 or 3
	int id;				// the classic object id
	int meshes;			// 1 .. HD_CLASSIC_NODES
	// bone[n] belongs to mesh n and bone[0] is all zero: mesh 0 IS the root.
	HDClassicBone bone[HD_CLASSIC_NODES];
	// Frame 0's rotation per mesh, x/y/z, in 1024ths of a full turn - the
	// classic unit. phd_RotYXZ order: y first, then x, then z.
	short rot[HD_CLASSIC_NODES][3];
	short bounds[6];		// frame 0's own box: x0 x1 y0 y1 z0 z1
	short offset[3];		// frame 0's root translation
	char level[64];			// the level file it came out of, name only
};

/* WHICH CLASSIC OBJECT AN INVENTORY MODEL NAME STANDS FOR, per game; 0 for a
 * name this table does not know or a game that has not got it.
 *
 * `game` is 1, 2 or 3 - a mod's own origin= line (hd_mods.h, HDModsOrigin).
 *
 * Both spellings of a name are in the table: the one the I-III remaster's own
 * ITEM folder uses and the one THIS engine's slot names make - a mod for TR5's
 * PISTOLS_ITEM ships PISTOLS_OPTION.TRM, where the donor game calls the same
 * model GUN_OPTION. It is one rule and not a list of files: the weapon under
 * its two names. */
int HDClassicObjectId(const char* modelName, int game);

/* THE OBJECT, out of the first level file of <bundleRoot>\<game>\DATA that
 * holds it with at least one mesh, in NAME order.
 *
 * One parse per (game, id) per session, cached - a level file is megabytes and
 * a ring may ask for the same object in every level of a play session.
 *
 * `why` takes a sentence when the answer is false and is never null on return.
 * Every count read out of the file is checked against the bytes that are left,
 * so a truncated or foreign file ends the walk rather than the process. */
bool HDClassicRead(const char* bundleRoot, int game, int id,
	HDClassicObject* out, const char** why);

/* WHAT HAS ALREADY BEEN READ, without a root and without touching the disk.
 * For the caller that writes the log line about a pose somebody else applied:
 * it prints the object the loader really used and can never go and read a
 * different one. False for a pair nobody has asked for yet. */
bool HDClassicCached(int game, int id, HDClassicObject* out);

/* WHERE THE BIND POSE PUTS NODE n - the mesh tree's offsets accumulated along
 * that node's chain, with every rotation zero. This is the point the .TRM's
 * vertices for that node are stored around. False for a node the object has
 * not got. */
bool HDClassicBindPivot(const HDClassicObject* object, int node, float* xyz);

/* THE MATRIX THAT POSES NODE n: it takes the .TRM's own bind pose vertices of
 * that node to where frame 0 puts them. Row major with the translation in row
 * 3 - v' = v * M - which is the convention every matrix in hd_model.cpp and
 * hd_object.cpp is written in. False for a node the object has not got. */
bool HDClassicNodeMatrix(const HDClassicObject* object, int node, float* out16);

// Forget every parsed object. For a tool that changes installation roots
// between runs; the engine never needs it.
void HDClassicForget();

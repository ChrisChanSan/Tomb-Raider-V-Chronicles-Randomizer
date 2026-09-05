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

// THE HEADER OF A .TRM - the remaster's object models (#123, stage S0).
//
// Stage S0 draws nothing, so this reads what a model IS and not what it looks
// like: how many parts, pages, indices and vertices, and whether the file is
// one of the heads that carry a facial rig before their geometry. That is
// enough to say "the model is there and this engine can read it", which is the
// whole of the S0 claim, and it is the walk stage S3 will extend to the
// vertices.
//
// THE FORMAT is TRForge's measurement over 2,923 files of all five games
// (MIT, trf_remaster\ItemModels.h - vendored? no: see vendor\trforge\
// VERSION.txt for why that file could not be taken verbatim):
//
//     char[3] "TRM"; u8 version (2 everywhere); u32 parts
//     parts * 44:  u32 material; float param[4]; u32 startIndex;
//                  u32 opaqueCount; u32 opaqueEnd;
//                  u32 alphaCount;  u32 alphaEnd; u32 additiveCount
//     u32 textures; u16 texId[textures]         padded to four bytes
//     u32 0                                     in every file but a head
//     u32 indices; u32 vertices
//     u16 index[indices]                        padded to four bytes
//     vertices * 24
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
};

// True when the file exists at all. Cheap - no read.
bool HDTrmExists(const char* path);

// Reads the header of one .TRM. On failure returns false and *why names the
// reason (a short string owned by this module, never null).
bool HDTrmProbe(const char* path, HDTrmInfo* out, const char** why);

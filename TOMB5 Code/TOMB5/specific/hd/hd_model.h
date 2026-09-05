/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the object path - one .TRM replaces the WHOLE classic model,
 * the three bone indices are classic mesh tree nodes, the bind pose comes
 * from the mesh tree's accumulated offsets, a <NAME>_HEAD.TRM rides rigidly
 * on the node nearest the face geometry's centre, and the three index runs of
 * a part are the three passes - are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp: build_skinned_model
 * 1807-1913, bind_pose_of_slot 1752-1805, hd_load's model and head loops
 * 1978-2100). The renderer under them is new: TROne is OpenGL 3.3 with vertex
 * arrays, texture arrays and runtime GLSL, this is Direct3D 9 with a vertex
 * declaration, one texture per page and compiled bytecode. See THIRD-PARTY.md.
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

#include "hd_render.h"

struct IDirect3DDevice9;

// THE HD OBJECT MODELS OF ONE LEVEL (#123, stage S3-1).
//
// The remaster does not repaint an object's classic tiles; it throws the mesh
// away and draws a model of its own. This module is the store of those models
// for one level: which slot has one, what it looks like on the card, which of
// the slot's classic nodes it covers - and, in stage S3-2, the draw itself
// where DrawAnimatingItem stands today.
//
// WHAT A NODE IS, AND WHY THE MAPPING IS AN IDENTITY. A .TRM vertex names
// three bones, and those numbers are indices into the CLASSIC mesh tree of
// the slot - not into a rig of the model's own. So TRM node n IS classic mesh
// n, and the "mapping table" a slot needs is not a permutation but a coverage
// list: which of the slot's nmeshes nodes have vertices weighted to them at
// all. HDModelNodes / HDModelNodeCovered answer that, and a node the model
// does not cover is the one stage S3-2 draws the classic way (the per-mesh
// fallback).
//
// The identity is measured, not assumed - ModelProbe -nodes prints the TRM's
// per-node vertex counts beside the classic mesh tree of the same slot for
// any level, and ModelProbe -all does it for every bound slot at once.
//
// TROne takes the other road here and marks every node of a valid model as
// covered whatever the counts say (game_renderer.cpp:1826, `model.covered =
// ~0u`), after user reports of classic patches drawn over an HD body. That is
// a decision about DRAWING and it belongs to stage S3-2; this module measures
// and reports, and leaves the decision to its caller.
//
// Engine-free like the rest of specific\hd: it takes a device, a list of
// slots with their classic node counts, and the bind pose the engine reads
// out of its own mesh tree. Nothing in here includes an engine header, which
// is what lets ModelProbe draw exactly what the game will draw.

// One slot the level loads. `nodes` is objects[slot].nmeshes; `bind` is the
// slot's BIND POSE - the mesh tree's accumulated offsets in classic units,
// three floats a node, in node order - or 0.
//
// The bind pose is handed over here and not set afterwards because a separate
// HD face needs it before it can be built: the node a face rides on is the
// bind-pose node nearest the face's own centre, and that is a measurement, not
// a constant. With `bind` at 0 a model draws in its bind pose and nowhere
// else, which is what the diagnostic tool wants when it is pointed at a .TRM
// with no level behind it.
// `name` overrides the binding: 0 asks hd_bind (the level's .MAP, then our
// hand table, then the slot's own name), anything else is the .TRM stem to
// load. The diagnostic tool draws a model by name with no level behind it
// that way, and stage S7 will need it for a slot the randomizer filled from a
// donor level, where the slot number no longer says what the model is.
struct HDModelSlot
{
	int slot;
	int nodes;
	const float* bind;
	const char* name;
};

// Where a model's file was found.
enum HDModelOrigin
{
	HD_MODEL_ORIGIN_NONE = 0,
	HD_MODEL_ORIGIN_5,
	HD_MODEL_ORIGIN_4,	// the sibling 4\ITEM - SAS and five others live only there
	HD_MODEL_ORIGIN_6
};

struct HDModelStats
{
	int slots;		// handed in
	int lara;		// LARA .. HAIR - stage S4, not touched here
	int dummy;		// the level's .MAP says DUMMY on purpose
	int noName;		// no slot name in this engine at all
	int missing;		// a model name with no file in 5, 4 or 6 -> classic
	int refused;		// the file is there but could not be read -> classic
	int loaded;		// models on the card
	int fromGame5;
	int fromGame4;
	int fromGame6;
	int heads;		// <NAME>_HEAD.TRM built and bound to a node

	int fullSlots;		// every classic node covered by the model
	int partialSlots;	// some nodes uncovered - the per-mesh fallback
	int nodesTotal;		// classic nodes over all loaded slots
	int nodesCovered;
	int nodesFallback;	// = nodesTotal - nodesCovered

	int vertices;
	int indices;
	int ranges;		// draw calls for every model, every pass
	int mixedPageTriangles;	// triangles whose three corners disagree on a page
	int pages;		// distinct .DDS pages the models added to the cache

	double vertexBytes;
	double indexBytes;
	double readMs;		// reading and parsing the .TRM files
	double buildMs;		// sorting, buffers, upload
	HDPageCacheStats pageStats;	// what the append to the page cache did
};

// Per object, handed to HDModelDraw. The bulbs and the fog live in
// HDRenderFrameBegin's HDFrameLighting, because they are per frame and both
// paths share them.
struct HDModelLighting
{
	// The engine's dynamics, ALREADY IN VIEW SPACE - a skinned vertex lands
	// there and nowhere else. Leave the count at 0 and the node colours carry
	// the light on their own, which is what the classic path does
	// (InitDynamicLighting folds the dynamics into the object's light list).
	const HDLightSource* lights;
	int lightCount;
};

// --- the module ------------------------------------------------------------

// Creates the two shaders and the vertex declaration. HDRenderInit must have
// run first: the state bracket that puts the device into the HD path and back
// lives in hd_render, and one HD path has one list of states.
bool HDModelInit(IDirect3DDevice9* device, const char** why);
void HDModelShutdown();
bool HDModelReady();

// Loads every slot's .TRM, builds the buffers and adds the models' pages to
// the page cache. `installRoot` is the remaster's game 5 folder. The level's
// <LEVEL>.MAP must already be loaded through HDMapLoad - HDBindSlot decides
// which file a slot asks for.
//
// ORDER. This comes AFTER HDRenderLevelBegin, because the models' pages are
// appended to the cache the rooms built, and HDModelLevelEnd comes BEFORE
// HDRenderLevelEnd, which destroys that cache. The wrong way round is not a
// crash - a slot number past the end of an emptied cache answers "no texture"
// - but it is an untextured object and a long afternoon.
bool HDModelLevelBegin(IDirect3DDevice9* device, const char* installRoot,
	const HDModelSlot* slots, int count, bool managedPages, int threads,
	HDModelStats* stats, const char** why);
void HDModelLevelEnd();

// --- the frame -------------------------------------------------------------
//
// Between HDRenderFrameBegin and HDRenderFrameEnd, inside that bracket. The
// projection is the same one the rooms use; the objects arrive in view space
// through their node matrices, so there is no view matrix here.
//
// The distance fog and the fog bulbs are NOT uploaded here: they are per
// frame, HDRenderFrameBegin has already put them in c16 and c40, and a
// Direct3D 9 constant register belongs to the device and not to a shader - so
// hd_model.hlsl reads the very registers hd_room.hlsl was given. Only the
// COUNT register is per object, so the bulb count is handed over here and
// written with each object's own light count.
void HDModelFrameBegin(const D3DMATRIX* projection, int bulbCount);

// One object. `nodeMatrix` is nodeCount row-major 4x4 matrices, model space
// to VIEW space - the very matrices the classic path has on its stack when it
// calls phd_PutPolygons for each mesh. `nodeLight` is nodeCount colour
// triples in the classic path's 0..255 space, one per node - what
// CalculateObjectLighting works out for this object. Pass 0 for a flat 128.
//
// False when the slot has no model or the frame is not open; the caller then
// draws it the classic way.
bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, const HDModelLighting* lighting);

// The separate HD face, drawn right after the body on the same matrices. Does
// nothing and returns false when the slot has no <NAME>_HEAD.TRM.
bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, const HDModelLighting* lighting);

int HDModelDrawCalls();		// since the last HDModelFrameBegin

// --- what was built --------------------------------------------------------

bool HDModelHas(int slot);
const char* HDModelName(int slot);		// the .TRM stem, or 0
HDModelOrigin HDModelOriginOf(int slot);
int HDModelNodes(int slot);			// the classic node count handed in
int HDModelNodeVertices(int slot, int node);	// weighted vertices on that node
bool HDModelNodeCovered(int slot, int node);	// false -> S3-2 draws it classic
int HDModelVertices(int slot);
int HDModelIndices(int slot);
int HDModelRanges(int slot);
int HDModelPageCount(int slot);
int HDModelPage(int slot, int at);		// .DDS number
bool HDModelHasHead(int slot);
int HDModelHeadNode(int slot);			// the node the face rides on, or -1
// The model's bounding box in its own bind pose, six floats (min then max).
// The body's alone; a separate HD face reaches past it (its geometry is the
// whole head, where the body has an empty collar), so a caller that draws
// both has to take the union.
bool HDModelBounds(int slot, float* mins, float* maxs);
bool HDModelHeadBounds(int slot, float* mins, float* maxs);

// Everything that could not be bound, in words, for the caller's log. Valid
// until the next HDModelLevelBegin or HDModelLevelEnd.
int HDModelNoteCount();
const char* HDModelNote(int at);

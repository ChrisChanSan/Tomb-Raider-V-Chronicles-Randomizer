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
// all. HDModelNodes / HDModelNodeCovered answer that.
//
// The identity is measured, not assumed - ModelProbe -nodes prints the TRM's
// per-node vertex counts beside the classic mesh tree of the same slot for
// any level, and ModelProbe -all does it for every bound slot at once.
//
// WHAT STAGE S3-2 DID WITH THE ANSWER: nothing is drawn on an uncovered node.
// Not the classic mesh either - TROne tried that and took it out again after
// user reports of classic patches sitting on an HD body, and marks every node
// of a valid model as covered instead (game_renderer.cpp:1826, `model.covered
// = ~0u`). An uncovered node is nearly always a door's invisible root or a
// face that ships as its own _HEAD.TRM, and both of those are meant to be
// empty. This module still only measures and reports; the decision is in
// hd_object.cpp, where the caller is.
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
// `rigidNode` is -1 for a skinned model, which is every ordinary object, and
// a node number for a file whose bone indices are NOT mesh tree nodes: every
// vertex then hangs off that one node with weight 255. Only the wardrobe uses
// it so far - LARA_HEAD.TRM carries a FACIAL rig whose bones run to 31 and
// mean nothing to Lara's 15 node tree, so it rides rigidly on node 14 the way
// a creature's <NAME>_HEAD.TRM rides on the node nearest its face. The
// difference to that path is only who chooses the node: there it is measured,
// here it is the classic head node and there is nothing to measure.
// `fromTr3` says the model is not in this bundle at all: it is one of the four
// Tomb Raider III guests the randomizer imports (#92, #123 stage S7-2), and
// its .TRM and its texture pages come out of the OTHER bundle - Tomb Raider
// I-III Remastered, game folder 3 with 1 and 2 as siblings. Its pages are
// offered to the cache with HD_PAGE_ALT_BASE added, because the remaster
// keeps one texture pool per game and page 12 means two different pictures in
// the two bundles. False for every ordinary slot, and then not one byte of
// the other bundle is touched.
// `parent` is the classic mesh TREE, one entry a node: the node this one hangs
// off, and -1 for the root. 0 means "not handed over", and then a separate HD
// face is bound rigidly to one node the way it was until #123 fix round B.
// With the tree in hand the face's NECK bone follows the head node's PARENT
// instead - see neck_bone_of in hd_model.cpp for the measurement and the
// reason (the user's report of Pierre's neck and chest in his death
// animation).
struct HDModelSlot
{
	int slot = -1;
	int nodes = 0;
	const float* bind = 0;
	const int* parent = 0;
	const char* name = 0;
	int rigidNode = -1;
	bool fromTr3 = false;
};

// Where a model's file was found.
enum HDModelOrigin
{
	HD_MODEL_ORIGIN_NONE = 0,
	HD_MODEL_ORIGIN_5,
	HD_MODEL_ORIGIN_4,	// the sibling 4\ITEM - SAS and five others live only there
	HD_MODEL_ORIGIN_TR3	// the other bundle: 3\ITEM, or its siblings 1 and 2
};

struct HDModelStats
{
	int slots;		// handed in
	int lara;		// LARA .. HAIR - stage S4, not touched here
	int dummy;		// the level's .MAP says DUMMY on purpose
	int noName;		// no slot name in this engine at all
	int missing;		// a model name with no file in 5 or 4 -> classic
	int refused;		// the file is there but could not be read -> classic
	int loaded;		// models on the card
	int fromGame5;
	int fromGame4;
	int fromTr3;		// the other bundle - a Tomb Raider III guest (S7-2)
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

/* THE OBJECT'S LIGHTS (#123, stage S3-2).
 *
 * The classic object path lights PER VERTEX with the vertex's own normal
 * (specific\output.cpp, aTransformLightClipMesh) out of a light list it sets
 * up PER NODE (specific\lighting.cpp, SuperSetupLight, called from
 * phd_PutPolygons through InitObjectLighting). One flat colour a node would
 * therefore not be the classic picture but a mannequin: the front and the
 * back of a torso are one node and the classic path shades them apart.
 *
 * So the directional part is evaluated per vertex here as well, in the vertex
 * shader, and what the engine hands over is the light list itself. Two things
 * make that exact rather than an approximation:
 *
 *   - The engine's per-node vectors are the light seen from the NODE's world
 *     position, turned into that node's model space. A skinned normal arrives
 *     in VIEW space, and the node's rotation is orthonormal, so the dot
 *     product is the same one in either space. Directions therefore need no
 *     per-node copy at all.
 *   - A point or spot light's direction and its falloff are the only things
 *     the classic path recomputes per node. Handing the shader the light's
 *     POSITION instead lets it work both out from the surface point, which is
 *     what the classic formula wanted and could not afford.
 *
 * The one part that really is per node is the ambient - and it is measured to
 * be per OBJECT: the shadow lights that lower it read `light->Range`, which
 * CreateLightList fills from the ITEM's position and not from the node's. It
 * travels in HDModelDraw's `nodeLight` all the same, because Lara's composite
 * (stage S4) has a genuine per-node ambient in LaraNodeAmbient.
 */
#define HD_MAX_OBJ_LIGHTS	12

enum HDObjLightKind
{
	HD_OBJLIGHT_SUN = 0,	// x, y, z is a unit DIRECTION towards the light
	HD_OBJLIGHT_POINT = 1,	// x, y, z is a POSITION
	HD_OBJLIGHT_SPOT = 2	// a position too - the cone test is already done
};

struct HDObjLight
{
	float x, y, z;		// VIEW space, see the kinds above
	float kind;		// one of HDObjLightKind, as a float for the shader
	float r, g, b;		// the classic path's 0..255 scale
	float invRange;		// 1 / Outer, 1 / Cutoff; 0 for a sun
};

// Per object, handed to HDModelDraw. The bulbs and the fog live in
// HDRenderFrameBegin's HDFrameLighting, because they are per frame and both
// paths share them.
struct HDModelLighting
{
	const HDObjLight* lights;
	int lightCount;
	// The sun's own factor: aTransformLightClipMesh multiplies a sun by 0.75
	// in the game and DOUBLES it in the inventory ("fucking shit", says the
	// original comment). One number rather than two code paths.
	float sunScale;
};

// --- the module ------------------------------------------------------------

// Creates the two shaders and the vertex declaration. HDRenderInit must have
// run first: the state bracket that puts the device into the HD path and back
// lives in hd_render, and one HD path has one list of states.
bool HDModelInit(IDirect3DDevice9* device, const char** why);
void HDModelShutdown();
bool HDModelReady();

/* Where the models of the OTHER bundle live: the I-III remaster's game 3
 * folder, or an empty string for "there is none" (#123, stage S7-2). Set
 * before HDModelLevelBegin; a slot with `fromTr3` then looks there and
 * nowhere else, and every other slot never touches it. The page cache needs
 * the same path - HDPageCacheAltRoot - and the caller sets both, because one
 * module setting another module's state is how two answers drift apart. */
void HDModelTr3Root(const char* root);

// Loads every slot's .TRM, builds the buffers and adds the models' pages to
// the page cache. `installRoot` is the remaster's game 5 folder. The level's
// <LEVEL>.MAP must already be loaded through HDMapLoad - HDBindSlot decides
// which file a slot asks for, unless the caller named one in HDModelSlot.
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
// triples in the classic path's 0..255 space, one per node - the ambient
// CalculateObjectLighting works out for this object. Pass 0 for a flat 128.
//
// `nodeMask` is the classic `item->mesh_bits`: bit n clear means node n draws
// nothing. ~0u draws everything, which is what an item with no mesh bits set
// hands over anyway.
//
/* WHICH SIDE OF A TRIANGLE IS DRAWN (#123, stage S8).
 *
 * The HD path draws every model DOUBLE SIDED, which is what TROne does too
 * (game_renderer.cpp:3419, glDisable(GL_CULL_FACE)) and what the classic
 * engine looks like it does - specific/function_table.cpp:105 sets
 * D3DCULL_NONE for the whole session. It only LOOKS like it: TR5 culls back
 * faces in SOFTWARE, per polygon, through the IsVisible function pointer, so
 * the card never sees one. Nobody noticed the difference because no camera
 * ever stands inside an object - except the first person camera, which stands
 * inside Lara. Cut her neck away there and the inside of her chest is what
 * lies behind it (measured: ModelProbe -firstperson, stage S8).
 *
 * So the cut and the culling belong together, and both are per DRAW: only
 * Lara's body in first person asks for either. HD_MODEL_CULL_BACK is the mode
 * that keeps the OUTSIDE of a .TRM - measured by picture, because the winding
 * convention of the files is a fact about the files. Their winding is
 * consistent enough to cull on: over the eight wardrobe bodies, 39,440 to
 * 25,384 triangles each, at most 59 of them have the geometric normal on the
 * other side of the vertex normal (0.06 % to 0.18 %). */
enum HDModelCull
{
	HD_MODEL_CULL_NONE = 0,		// double sided - every model but the one
	HD_MODEL_CULL_CW,		// D3DCULL_CW
	HD_MODEL_CULL_CCW		// D3DCULL_CCW
};

#define HD_MODEL_CULL_BACK	HD_MODEL_CULL_CCW

// `nearCut` is the first person near cut of stage S8, in classic units of VIEW
// DEPTH: every fragment of this model nearer to the eye than that is
// discarded. 0 is off and costs nothing - a rasterised fragment's view depth
// never falls below the near plane. Only Lara's body ever asks for it; see
// HD_LARA_FIRST_PERSON_NEAR_CUT in hd_lara.h for the number and the
// measurement, and hd_model.hlsl's ModelPS for the cut itself.
//
// False when the slot has no model or the frame is not open; the caller then
// draws it the classic way.
bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	float nearCut = 0.0f, int cull = HD_MODEL_CULL_NONE);

// The separate HD face, drawn right after the body on the same matrices. Does
// nothing and returns false when the slot has no <NAME>_HEAD.TRM.
bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	float nearCut = 0.0f, int cull = HD_MODEL_CULL_NONE);

int HDModelDrawCalls();		// since the last HDModelFrameBegin

// --- what was built --------------------------------------------------------

bool HDModelHas(int slot);
const char* HDModelName(int slot);		// the .TRM stem, or 0
HDModelOrigin HDModelOriginOf(int slot);
int HDModelNodes(int slot);			// the classic node count handed in
int HDModelNodeVertices(int slot, int node);	// weighted vertices on that node
bool HDModelNodeCovered(int slot, int node);	// false -> nothing is drawn there

/* DOES THE MODEL FIT THE SKELETON THAT IS IN THE SLOT? (#123, stage S3-2.)
 *
 * A .TRM's bone numbers are the mesh tree numbers of the model it was made
 * for, and the randomizer puts foreign creatures into slots - ANDREA1's SAS
 * carries 25 nodes where SAS.TRM was drawn for 15. Skinning that model on
 * that tree is a torn lump, so the caller has to be able to tell, and these
 * two numbers are what it tells from.
 *
 * HDModelMaxBone is the highest node number any weighted vertex names. Higher
 * than the slot's own node count and the model is certainly not this
 * skeleton's.
 *
 * HDModelBindDrift is the softer half, for a foreign model with the RIGHT
 * node count: for every covered node, the centre of the vertices that ride on
 * that node alone (weight 255) against the classic bind pose of that node,
 * in classic units. A model made for this tree keeps that distance inside its
 * own limb; a foreign one does not. `mean` is over the covered nodes and
 * `worst` is the largest single one. False when there is no bind pose to
 * measure against, in which case the caller has to make do with the count.
 */
int HDModelMaxBone(int slot);
bool HDModelBindDrift(int slot, float* mean, float* worst);

/* THE LIMIT, in classic units, measured and not guessed. It lives here rather
 * than beside the decision in hd_object.cpp so that the diagnostic tool, which
 * is built without the engine, judges a slot by the same number the game does.
 *
 * Read off ModelProbe -all over all fourteen randomized levels and their
 * vanilla twins: the highest drift of a model that belongs where it stands is
 * 239 (ANIMATING3 in JOBY3) and the lowest of one that does not is 308
 * (ANIMATING1 in RICH2). 256 sits in that gap, nearer the accepted end,
 * because the two mistakes do not cost the same - a good model refused is a
 * classic-looking door, a bad one accepted is a body torn across the room. */
#define HD_SKELETON_DRIFT_LIMIT		256.0f
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

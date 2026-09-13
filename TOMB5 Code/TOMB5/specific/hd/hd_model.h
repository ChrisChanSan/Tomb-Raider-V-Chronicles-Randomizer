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
#include "hd_trm.h"	// #123 fix round K (K1): HDModelSlot::measure reads one

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

	/* THE FILE, WHEN THE CALLER ALREADY KNOWS IT (#153, round M2).
	 *
	 * Normally a slot names a MODEL and this module looks for it - the mods,
	 * the game folder, the siblings, the other bundle. The wardrobe cannot be
	 * served that way: an outfit of the Tomb Raider I-III bundle lives in its
	 * 1\ITEM, which is nobody's search path, and a mod's outfit is refused by
	 * the model resolver ON PURPOSE, because a mod's outfit is an extra entry
	 * in the wardrobe and never a replacement of the file it is named after.
	 * The scan found it and knows exactly where it is, so it says so.
	 *
	 * `mod` is whose page numbers the file's are, exactly as a mod's model
	 * has: -1 for an install's file, the mod's index for a mod's. Set it and
	 * `fromTr3` cannot both be meaningful - a mod's namespace wins, which is
	 * the rule round M1 laid down for models the resolver found.
	 *
	 * `name` is still given and is still what the log calls the model. */
	const char* path = 0;
	int mod = -1;

	/* ONE PAGE OF THE FILE AND NOTHING ELSE (#123, fix round D, D4).
	 *
	 * -1 is the whole model, which is what every caller but one asks for. A
	 * .DDS number here keeps only the triangles that wear that page, and only
	 * that page is loaded into the cache - the rest of the file is read,
	 * measured and dropped.
	 *
	 * It exists because a piece of Lara's kit can be PART OF AN OUTFIT MODEL
	 * rather than a file of its own: the thigh holsters. HOLSTER_PISTOL.TRM
	 * holds the two guns and nothing else, and the pouches they sit in are
	 * built into OUTFIT_CLASSIC and OUTFIT_RUSSIA (page 8017) and into the
	 * two TR3 outfits that have them (page 8205). Three outfits have none -
	 * OUTFIT_VCI, OUTFIT_TR3_ANTARC, OUTFIT_TR3_NEVADA - and the guns then
	 * float beside the thigh, which is the user's finding D4. The page cut
	 * lets one of the donors give up its holster without its legs.
	 *
	 * The cut is by PAGE and not by triangle count on purpose: a page is a
	 * fact of the file, the artist's own division of the model into
	 * materials, and the measurement says it is exact here. Over the eight
	 * wardrobe bodies, page 8017 appears on nodes 0, 1, 4 (and 79 triangles
	 * of node 7 in the Russia coat) and nowhere else, and page 8205 on nodes
	 * 0, 1 and 4 alone - the belt and the two thigh rigs, with no part of a
	 * leg in either. */
	int onlyPage = -1;

	/* THE MIRRORED WEIGHTS OF A TWO-SIDED FILE (#153, round M2b).
	 *
	 * Not null makes this module fold the model's bone weights back onto the
	 * side they belong to before anything is built out of them - the rule, the
	 * reason and the measurement are in hd_trm.h and in hd_lara.cpp, which is
	 * the only caller that sets it. `mirrorBar` is the far side's share, in per
	 * mille, at which a vertex is folded at all.
	 *
	 * It changes the vertices the whole build then works from - the coverage,
	 * the bind drift and the buffers alike - which is right: what is folded
	 * here is what the card is given, so every number the log prints is a
	 * number about the model that is drawn. */
	const HDTrmMirrorPair* mirror = 0;
	int mirrors = 0;
	int mirrorBar = 0;

	/* MOD GUNS ON THE INSTALL'S HANDS (#153, round M2b).
	 *
	 * A mod may ship a weapon HAND model - a hand and the gun in it, on two or
	 * three pages of one file - and paint the HAND half with pages of the Tomb
	 * Raider I-III bundle, which its mod.txt reaches with origin=. The user's
	 * IMI Uzis do exactly that: 8002 and 8010 come out of 1\TEX, and Lara's
	 * hands then carry that game's skin tone and its glove seams while the rest
	 * of her is TR5's. He calls it "the Uzis pull on gloves".
	 *
	 * With `twinSlot` set, this module looks at where each of the model's pages
	 * really resolves - HDPageCacheOriginOf, the cache's own chain - and if any
	 * of them lands in the origin bundle it does two things: the mod's
	 * triangles on those pages are DROPPED, and the INSTALL's own file of the
	 * same name is loaded into `twinSlot`, cut to the pages it shares with
	 * `twinSkinOf` - which is the rest hands, whose every page is skin and
	 * nothing else. The caller then draws the two together and sees TR5's hands
	 * holding the mod's gun.
	 *
	 * The decision is made HERE and not by the caller because only this module
	 * has the file open and the pages resolved; a caller would have to read the
	 * .TRM a second time and keep a copy of resolve_page's chain.
	 *
	 * Nothing happens for a model that came out of an install, for a mod with
	 * no origin= line, or for one whose pages all resolve to the mod itself or
	 * to the install - the HK USP and the Spas-12 both wear the install's own
	 * 8001 and are untouched. */
	int twinSlot = -1;
	const char* twinSkinOf = 0;

	/* A LOOK AT THE FILE, WHILE IT IS STILL A FILE (#123, fix round K, K1).
	 *
	 * Called once, right after the .TRM is read and before it becomes buffers
	 * nobody can read back. It answers a question this module has no business
	 * knowing: which texel of which page is Lara's NECK, so that the LID which
	 * closes the first person neck cut can wear her own colour (#123, fix
	 * round S, S1; it was round K's inner skin that first needed it). It
	 * changes nothing and returns nothing - what it measures it keeps
	 * itself. */
	void (*measure)(const HDTrmModel* source) = 0;

	/* A MODEL THAT IS BUILT AND NOT READ (#123, fix round I, I3; withdrawn in
	 * fix round K with the dome it made; back in fix round S for the LID).
	 *
	 * `synth` is a maker: it is handed the .TRM of the slot `synthFrom` -
	 * which has to appear EARLIER in the same array, because the sources are
	 * read in the order the caller gives them - and fills an HDTrmModel of its
	 * own, which this module then treats exactly like a file it read: same
	 * page cache, same skinning, same bind pose, same draw. It owns its four
	 * arrays with new[] and they are given back with HDTrmFree like everything
	 * else.
	 *
	 * There is one maker and it is the reason this exists: the LID that closes
	 * the hole the first person neck cut leaves. The cut is a plane in the
	 * head's own frame, and no file in the remaster's wardrobe is a disc in
	 * that plane, so it is generated - out of the outfit's own neck, at the
	 * radius that neck really has. See HD_LARA_PART_NECK_CAP in hd_lara.h.
	 *
	 * A maker that returns false is a part that does not draw, exactly as a
	 * missing file is: the note says why and the rest of the wardrobe is
	 * unaffected. */
	int synthFrom = -1;
	bool (*synth)(const HDTrmModel* source, HDTrmModel* out,
		const char** why) = 0;
};

// Where a model's file was found.
enum HDModelOrigin
{
	HD_MODEL_ORIGIN_NONE = 0,
	HD_MODEL_ORIGIN_5,
	HD_MODEL_ORIGIN_4,	// the sibling 4\ITEM - SAS and five others live only there
	HD_MODEL_ORIGIN_TR3,	// the other bundle: 3\ITEM, or its siblings 1 and 2
	HD_MODEL_ORIGIN_MOD	// a mod's own ITEM folder (#153, round M1)
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
	int fromMod;		// a mod's own 5\ITEM or 4\ITEM (#153, round M1)
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
	double buildMs;		// everything that is not readMs - the three below
				// and the bookkeeping between them

	/* THE PHASES OF buildMs (#153, round M2, the fix round). Round M2 made
	 * one of them twenty-five seconds long and the level line said only
	 * "models ms=25656", so the next such regression is a number and not a
	 * hunt: findMs is the walk of the ITEM folders that turns a slot
	 * name into a file - the phase every active mod makes longer - keyMs
	 * is resolving the cache key of every page a model names,
	 * pageMs is the append to the page cache - the .DDS reads and the BC7
	 * decode - and bufferMs is pass two, the sort into draw calls and the
	 * vertex and index buffers. */
	double findMs;
	double keyMs;
	double pageMs;
	double bufferMs;

	/* WHAT THE .TRM READER DID FOR THIS LEVEL BUILD (#153, round M2, the fix
	 * round): files opened, models read with their geometry, bytes off disk.
	 * The one number that answers "does a level change load the whole
	 * wardrobe" - see HDTrmCounters. */
	double trmOpens;
	double trmLoads;
	double trmBytes;

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

	/* THE SHEEN (#123, fix round D, D2).
	 *
	 * TR5 has no specular term of any kind: aTransformLightClipMesh adds an
	 * ambient and a diffuse cosine and stops. The remaster's own catsuit and
	 * the TR3 London one read as LATEX because they are lit with one, and
	 * without it every HD outfit here came out matt - the user's finding,
	 * with the remaster's pictures beside ours.
	 *
	 * WHERE THE STRENGTH COMES FROM, and this is the honest part: NOT from
	 * the art. The obvious source would have been the pages' alpha channel,
	 * which fix round A2 found to be a gloss mask on the ROOM pages and which
	 * TROne describes the same way for items. Measured over the wardrobe with
	 * DdsProbe -alpha it is nothing of the sort: 8001, 8008, 8016, 8018,
	 * 8027, 8028 and 8205 all have EVERY texel between 240 and 255 - a flat
	 * opaque channel with BC7 quantisation noise on it and no mask anywhere.
	 * (The one page that is not flat is 8002, Lara's hair, at 15.2 % below
	 * 128 - and that is coverage, not gloss.) So there is no per-texel mask
	 * to read and the strength is a MATERIAL number per part, set from the
	 * remaster's look and written down in hd_lara.cpp's table.
	 *
	 * strength 0 is off and costs the pixel shader one multiply by zero.
	 * power is the Blinn-Phong exponent: small and the highlight is a broad
	 * satin sheen, large and it is a tight wet point.
	 *
	 * Only Lara asks for it today. Every object draws with 0, so nothing
	 * about the creature path changes. */
	float specStrength;
	float specPower;
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

/* WHERE A MODEL'S FILE IS, by the very search a level build does (#153, round
 * M2b) - the mods in order, then game 5, then the sibling game 4, or the I-III
 * bundle for a guest. There is one such search in this engine and this is how a
 * diagnostic tool asks it instead of keeping a copy that goes out of step (the
 * mistake round M1 found in ModelProbe's page rule).
 *
 * `allowMods` false asks the INSTALLS alone, which is what "the install's own
 * file of that name" means where a mod has replaced it. `mod` gets the mod that
 * answered, or -1. Meaningful after HDModelLevelBegin, whose installRoot and
 * whose I-III root it uses; false and an empty string before that.
 *
 * Writes into `out` and returns true when a file was found. */
bool HDModelFindFile(const char* name, bool fromTr3, bool allowMods, char* out,
	int size, int* mod);

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

/* THE WARM-UP (#148, order 1). Offers every model's vertex and index buffer
 * to the device with IDirect3DResource9::PreLoad, so the upload a managed
 * buffer would otherwise pay for inside the first frame it is drawn in
 * happens while the loading bar still stands. Returns how many buffers were
 * offered; safe and a no-op with no level built. */
int HDModelPreload();

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
//
// `passes` is one of the HD_PASS_ masks of hd_render.h and says which of the
// three passes every HDModelDraw of this bracket is to draw (#123, fix round
// E, E6). A frame opens the bracket twice: once for HD_PASS_SOLID, with the
// rooms' own solid passes, and once for HD_PASS_ADDITIVE after the classic
// buckets, so that no later opaque draw can paint over a glow that wrote no
// depth. HD_PASS_ALL is the whole model in one bracket, which is what the
// diagnostic tool asks for.
void HDModelFrameBegin(const D3DMATRIX* projection, int bulbCount,
	unsigned int passes = HD_PASS_ALL);

/* ONE OBJECT DRAWN ABOUT A DIFFERENT SCREEN CENTRE (#123, fix round P, P2).
 *
 * The inventory ring projects every item about the item's own place on the
 * screen: phd_PutPolygonsPickup (specific\output.cpp) moves f_centerx /
 * f_centery to the item's X / Y for the length of one mesh and puts them back
 * afterwards. This is that move, on the frame's own projection matrix, for the
 * next HDModelDraw:
 *
 *     kx = (X - centreX) / persp,   ky = (Y - centreY) / persp
 *
 * and the matrix gets _31 = kx * _11 and _32 = ky * _22, which adds a constant
 * kx * z to clip x before the w division - a constant PIXEL offset, since
 * _11 = persp / centreX. Nothing else in the matrix moves, so the depth is
 * untouched and every item of a ring frame still sorts against every other in
 * the one depth buffer both paths share.
 *
 * WHY HERE AND NOT IN THE ITEM'S MATRIX (P2, and it is the whole of fix round
 * P2). The same offset can be had by shearing the node matrices instead -
 * row 0 += kx * row 2 - and fix round P did exactly that. It is right for the
 * position and wrong for the LIGHT: hd_model.hlsl skins the vertex NORMAL
 * through the same gJoints and renormalises it, so a sheared node matrix tilts
 * and rescales the normal, and the ring's one sun then lights an item by an
 * amount that depends on where the item stands on the screen. Measured on the
 * shader itself (ModelProbe -ringlight, mean brightness over the object's own
 * pixels): 93.61 unsheared against 67.18 in the middle of the inventory row and
 * 108.87 on the ammo row - two rings that stand at once, 45 % apart. In the
 * projection the offset cannot reach a normal at all, because a normal never
 * goes through the projection.
 *
 * Call it before every HDModelDraw that wants it; (0, 0) is the plain frame
 * projection and is what every other object passes, so nothing can inherit a
 * neighbour's offset. Costs one 4-register upload and only when the value
 * really changes. */
void HDModelOffCentre(float kx, float ky);

/* The same arithmetic as a pure function, for the diagnostic tool: `out` is
 * `base` with the two off-centre terms written. ModelProbe -ringcheck measures
 * the engine's own line rather than a copy of it. */
void HDModelOffCentreMatrix(const D3DMATRIX* base, float kx, float ky,
	D3DMATRIX* out);

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
/* THE TWO SKIN_ MODES OF FIX ROUND K STOOD HERE (#123, fix round S, S1). They
 * kept the faces the CULL_ modes throw away and painted them in one colour -
 * the second draw that closed the hole the neck cut left. Fix round S closes
 * it with a LID instead, and measured the second draw out: forty first person
 * pictures are identical to the pixel with it and without it. */
enum HDModelCull
{
	HD_MODEL_CULL_NONE = 0,		// double sided - every model but the one
	HD_MODEL_CULL_CW,		// D3DCULL_CW
	HD_MODEL_CULL_CCW		// D3DCULL_CCW
};

#define HD_MODEL_CULL_BACK	HD_MODEL_CULL_CCW

/* THE FIRST PERSON HEAD CUT (#123, stage S8; a sphere then, a cylinder in fix
 * round J, a plane in fix round S; ONE BONE WEIGHT since fix round U, U1).
 *
 * A FRAGMENT IS DISCARDED WHERE ITS WEIGHT ON NODE `node` REACHES `bar`, and
 * that is the whole of it. The vertex shader adds up the vertex's share of
 * that one node out of the three bones it names, the pixel shader clips on the
 * interpolated value - so a triangle that straddles the boundary is cut
 * exactly where the weight crosses it, and no vertex rule can round the edge
 * off.
 *
 * WHY IT IS NONE OF THE THREE SHAPES BEFORE IT. Stage S8 cut a SPHERE about
 * the eye: it took the neck stump, and a bite the size of its radius out of
 * her chest with it. Rounds J to L cut a CYLINDER about the neck axis in the
 * BIND POSE, and three play tests running came back with the collar still
 * standing, because a neck vertex is skinned to two nodes at once and the
 * patch of skin a bind-pose shape covers MOVES as the head turns. Fix round S
 * cut a PLANE carried by the head node, and the play test came back with Lara
 * "partly rendered away over her whole body": a plane fixed to the head TILTS
 * with the head, and one tilted forty degrees runs through her chest.
 *
 * A WEIGHT IS NOT MEASURED IN SPACE AT ALL. It is written in the file, it is
 * the same number in every pose and at every camera angle, and there is
 * nothing for an animation to move it past. hd_lara.h carries the threshold
 * and the measurement over the wardrobe that picked it.
 *
 * A null pointer, or `on` 0, is off and costs the pixel shader one lerp.
 */
struct HDModelNeckCut
{
	float on;		// 0 = off; nothing else is read then
	float node;	// whose weight decides - Lara's head, as a float for the shader
	float bar;		// discard where that weight reaches this, of 1
};

// False when the slot has no model or the frame is not open; the caller then
// draws it the classic way.
bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE);

// The separate HD face, drawn right after the body on the same matrices. Does
// nothing and returns false when the slot has no <NAME>_HEAD.TRM.
bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE);

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

/* WHAT THE MIRROR FOLD DID TO THIS SLOT'S MODEL (#153, round M2b) - all zero
 * for a slot that asked for none, and for one whose file had nothing to fold.
 * False when the slot holds no model at all. */
bool HDModelFoldCounts(int slot, HDTrmFoldCounts* out);

/* A MATERIAL FOR ONE PAGE OF ONE MODEL (#123, fix round I, I4).
 *
 * The sheen of fix round D is one number per DRAW, and one number is right for
 * every model in the game but three: a HAND_ model is a hand AND the gun in
 * it, on two pages of one file. With one number the skin shines like gunmetal
 * or the gun goes matt, and the user's finding I4 is the second - "the sheen
 * is on the body and the head, the hands go empty".
 *
 * A page is already the unit a draw call is cut to, so naming the material by
 * page costs one constant write on the ranges that have one and nothing at all
 * on every model that has no table. Call it after HDModelLevelBegin, once per
 * level; the table lives with the model and dies with it. */
void HDModelSetPageSheen(int slot, int page, float strength, float power);
int HDModelPageSheenCount(int slot);

/* THE INNER SKIN AND ITS ONE TEXEL ARE GONE (#123, fix round K, K1;
 * WITHDRAWN in fix round S, S1(4); this note corrected in fix round U, U5).
 *
 * What stood here described a function - the SKIN_ cull modes and the flat
 * texel they painted - that fix round S took out along with the second draw
 * it existed for. Forty first person pictures were identical to the pixel
 * with that draw and without it, and it cost a strap its own outside face. The
 * LID closes the neck now (hd_lara.h, HD_LARA_NECK_CAP_SEGMENTS) and the cull
 * modes are the three plain ones above.
 *
 * The comment outlived the code by two rounds, which is the reason it is
 * replaced here rather than deleted: a reader who finds SKIN_ named in an old
 * commit should find out where it went. */
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

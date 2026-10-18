/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the object path - one .TRM replaces the WHOLE classic model,
 * the three bone indices are classic mesh tree nodes, the bind pose comes
 * from the mesh tree's accumulated offsets, a <NAME>_HEAD.TRM rides rigidly
 * on the node nearest the face geometry's centre (TROne splits the neck onto
 * the parent node by default since), and the three index runs of a part are
 * the three passes - are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3, TROne-main (copy of 2026-10-06, commit 75c4c843),
 * host\render\game_renderer.cpp: build_skinned_model, bind_pose_of_slot,
 * hd_load's model and head loops), TROne's own design. The renderer under
 * them is new: TROne is OpenGL 3.3 with vertex
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
#include "hd_trm.h"	// HDModelSlot::measure takes an HDTrmModel

struct IDirect3DDevice9;

// The HD object models of one level.
//
// The install does not repaint an object's classic tiles; it ships a model of
// its own for the mesh (<5>\ITEM). This module stores those models for one
// level: which slot has one, its buffers on the card, which of the slot's
// classic nodes it covers, and the draw that stands in for DrawAnimatingItem.
//
// A .TRM vertex names three bones, and those numbers are indices into the
// CLASSIC mesh tree of the slot, not into a rig of the model's own. So TRM
// node n is classic mesh n, and what a slot needs is not a permutation but a
// coverage list: which of its nmeshes nodes have vertices weighted to them.
// HDModelNodes / HDModelNodeCovered answer that; ModelProbe -nodes / -all
// print the per-node vertex counts beside the classic mesh tree.
//
// Nothing is drawn on an uncovered node, not the classic mesh either: TROne
// marks every node of a valid model as covered (TROne-main, copy of
// 2026-10-05, build_skinned_model: `model.covered = ~0u`), because classic
// patches on an HD body look wrong.
// An uncovered node is nearly always a door's invisible root or a face that
// ships as its own _HEAD.TRM, both meant to be empty. The decision is made
// by the caller in hd_object.cpp.
//
// Engine-free like the rest of specific\hd, so ModelProbe draws exactly what
// the game draws.

// One slot the level loads. `nodes` is objects[slot].nmeshes; `bind` is the
// slot's BIND POSE - the mesh tree's accumulated offsets in classic units,
// three floats a node, in node order - or 0.
//
// The bind pose is handed over here because a separate HD face needs it to be
// built: the node a face rides on is the bind-pose node nearest the face's
// own centre. With `bind` at 0 a model draws in its bind pose, which is what
// the diagnostic tool wants for a .TRM with no level behind it.
// `name` overrides the binding: 0 asks hd_bind (the level's .MAP, then the
// hand table, then the slot's own name), anything else is the .TRM stem to
// load - for the diagnostic tool, and for a slot the randomizer filled from a
// donor level, where the slot number no longer says what the model is.
// `rigidNode` is -1 for a skinned model, and a node number for a file whose
// bone indices are NOT mesh tree nodes: every vertex then hangs off that one
// node with weight 255. LARA_HEAD.TRM carries a facial rig whose bones run to
// 31 and mean nothing to Lara's 15 node tree, so it rides on node 14, the
// classic head node - all of it but the throat its rig weights to her torso,
// which `neckNode` below puts on node 7.
// `fromTr3` says the model is one of the four Tomb Raider III guests the
// randomizer imports, and its .TRM and texture pages come out of the OTHER
// bundle - Tomb Raider I-III Remastered, game folder 3 with 1 and 2 as
// siblings. Its pages are offered to the cache with HD_PAGE_ALT_BASE added,
// because the remaster keeps one texture pool per game and page 12 means two
// different pictures in the two bundles. False for every ordinary slot, and
// then the other bundle is never touched.
// `parent` is the classic mesh TREE, one entry a node: the node this one hangs
// off, and -1 for the root. 0 means "not handed over", and a separate HD face
// is then bound rigidly to one node. With the tree the face's NECK bone
// follows the head node's PARENT instead - see neck_bone_of in hd_model.cpp.
struct HDModelSlot
{
	int slot = -1;
	int nodes = 0;
	const float* bind = 0;
	const int* parent = 0;
	const char* name = 0;
	int rigidNode = -1;
	bool fromTr3 = false;

	/* A rigid face's neck, out of the face's own rig.
	 *
	 * -1 is a face wholly rigid on `rigidNode`. A node number here hangs
	 * every vertex the file weights to a BODY joint of its facial rig on this
	 * node, with the file's own weights, and leaves the rest on `rigidNode`.
	 * Which joints are the body's is measured in the file itself: the ones
	 * that stand farther than kNeckJointDistance (hd_model.cpp) from the
	 * rig's head joint. A file with no rig, or with no joint that far out,
	 * stays wholly rigid. Lara's face asks for the torso - see
	 * HD_LARA_PART_FACE in hd_lara.h. */
	int neckNode = -1;

	/* The file, when the caller already knows it.
	 *
	 * Normally a slot names a MODEL and this module looks for it - the mods,
	 * the game folder, the siblings, the other bundle. The wardrobe cannot be
	 * served that way: an outfit of the Tomb Raider I-III bundle lives in its
	 * 1\ITEM, which is on no search path, and the model resolver refuses a
	 * mod's outfit on purpose, because a mod's outfit is an extra wardrobe
	 * entry and never a replacement of the file it is named after. The
	 * wardrobe scan knows where the file is and passes the path.
	 *
	 * `mod` says whose page numbers the file uses: -1 for an install's file,
	 * the mod's index for a mod's. With both `mod` and `fromTr3` set, the
	 * mod's namespace wins, as for models the resolver found.
	 *
	 * `name` is still given and is what the log calls the model. */
	const char* path = 0;
	int mod = -1;

	/* One page of the file and nothing else.
	 *
	 * -1 is the whole model, which is what every caller but one asks for. A
	 * .DDS number here keeps only the triangles that wear that page, and only
	 * that page is loaded into the cache - the rest of the file is read,
	 * measured and dropped.
	 *
	 * A piece of Lara's kit can be part of an outfit model rather than a file
	 * of its own: the thigh holsters. HOLSTER_PISTOL.TRM holds the two guns
	 * and nothing else, and the pouches they sit in are built into
	 * OUTFIT_CLASSIC and OUTFIT_RUSSIA (page 8017) and into the two TR3
	 * outfits that have them (page 8205). Three outfits have none -
	 * OUTFIT_VCI, OUTFIT_TR3_ANTARC, OUTFIT_TR3_NEVADA - and without a donor
	 * the guns float beside the thigh. The page cut lets a donor give up its
	 * holster without its legs.
	 *
	 * The cut is by PAGE because a page is the artist's own division of the
	 * model into materials, and it is exact here: over the eight wardrobe
	 * bodies, page 8017 appears on nodes 0, 1, 4 (and 79 triangles of node 7
	 * in the Russia coat) and nowhere else, and page 8205 on nodes 0, 1 and 4
	 * alone - the belt and the two thigh rigs, with no part of a leg. */
	int onlyPage = -1;

	/* One node of the file and nothing else.
	 *
	 * A node MASK - bit n for classic node n - and 0 is the whole model,
	 * which is what every caller but one asks for. With a mask only the
	 * triangles whose DOMINANT node is in it are built, and only the pages
	 * those triangles wear are loaded, exactly as `onlyPage` does with a page.
	 *
	 * The page cut's twin, for the same reason: Lara's HEAD is node 14 of
	 * OUTFIT_CLASSIC - hair, skull and the two triangles of neck under it -
	 * and no file in either remaster holds it alone. A body of the Tomb
	 * Raider I-III bundle has no hair of its own, so it borrows that node
	 * (the measurement is in hd_wardrobe.h).
	 *
	 * By node and not by page, although for OUTFIT_CLASSIC the two nearly
	 * agree (9,483 of the head node's 9,485 triangles wear page 8002, and 14
	 * triangles of that page belong to other nodes). Cutting by node puts the
	 * part's ranges on dominant node 14, so the first person mask that takes
	 * the body's head off takes this with it and nothing else, and a donor
	 * whose artist put the hair on two pages still comes out whole. The head
	 * range's vertices also carry scatter weights on nodes 5, 7 and 8; the
	 * draw masks by the range's dominant node, which is 14.
	 *
	 * The dominant node is the one that carries the most weight over the
	 * triangle's three corners - the same rule the ranges are bucketed by, so
	 * this cut keeps exactly the ranges the node mask would have drawn. */
	unsigned int onlyNode = 0;

	/* The mirrored weights of a two-sided file.
	 *
	 * Not null makes this module fold the model's bone weights back onto the
	 * side they belong to before anything is built out of them - the rule, the
	 * reason and the measurement are in hd_trm.h and in hd_lara.cpp, the only
	 * caller that sets it. `mirrorBar` is the far side's share, in per mille,
	 * at which a vertex is folded at all.
	 *
	 * The fold changes the vertices the whole build works from - coverage,
	 * bind drift and buffers alike - so every number the log prints is about
	 * the model that is drawn. */
	const HDTrmMirrorPair* mirror = 0;
	int mirrors = 0;
	int mirrorBar = 0;

	/* Mod guns on the install's hands.
	 *
	 * A mod may ship a weapon HAND model - a hand and the gun in it, on two or
	 * three pages of one file - and paint the HAND half with pages of the Tomb
	 * Raider I-III bundle, which its mod.txt reaches with origin=. A mod's IMI
	 * Uzis do exactly that: 8002 and 8010 come out of 1\TEX, and Lara's hands
	 * then carry that game's skin tone and glove seams while the rest of her
	 * is TR5's.
	 *
	 * With `twinSlot` set, this module looks at where each of the model's pages
	 * really resolves - HDPageCacheOriginOf, the cache's own chain - and if any
	 * of them lands in the origin bundle it does two things: the mod's
	 * triangles on those pages are DROPPED, and the INSTALL's own file of the
	 * same name is loaded into `twinSlot`, cut to the pages it shares with
	 * `twinSkinOf` - the rest hands, whose every page is skin. The caller then
	 * draws the two together: TR5's hands holding the mod's gun.
	 *
	 * The decision is made here because only this module has the file open and
	 * the pages resolved; a caller would have to read the .TRM a second time
	 * and keep a copy of resolve_page's chain.
	 *
	 * Nothing happens for a model that came out of an install, for a mod with
	 * no origin= line, or for one whose pages all resolve to the mod itself or
	 * to the install - the HK USP and the Spas-12 both wear the install's own
	 * 8001 and are untouched. */
	int twinSlot = -1;
	const char* twinSkinOf = 0;

	/* A look at the file while it is still a file.
	 *
	 * Called once, right after the .TRM is read and before it becomes buffers
	 * nobody can read back. It answers a question outside this module: which
	 * texel of which page is Lara's NECK, so that the LID which closes the
	 * first person neck cut can wear her own colour. It changes nothing and
	 * returns nothing - what it measures it keeps itself. */
	void (*measure)(const HDTrmModel* source) = 0;

	/* A model that is built and not read.
	 *
	 * `synth` is a maker: it is handed the .TRM of the slot `synthFrom` -
	 * which has to appear EARLIER in the same array, because the sources are
	 * read in the order the caller gives them - and fills an HDTrmModel of its
	 * own, which this module then treats exactly like a file it read: same
	 * page cache, same skinning, same bind pose, same draw. It owns its four
	 * arrays with new[] and they are given back with HDTrmFree like everything
	 * else.
	 *
	 * There is one maker: the LID that closes
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

	/* The classic pose.
	 *
	 * Set on a mod's <BASE>_OPTION model - the ring's, and nothing else.
	 * A remaster inventory model is stored in the classic BIND POSE, which for
	 * a pair of guns means the two of them side by side and parallel; the
	 * CROSS the donor game shows is frame 0 of the classic object the file
	 * stands for. With this set, the file's vertices are posed with that frame
	 * before anything is measured out of them - the box, the principal axes,
	 * the ring's fit and the buffers alike, exactly as `mirror` above changes
	 * what the whole build then works from.
	 *
	 * It needs three things and says which one it missed otherwise: the model
	 * came from a mod whose mod.txt names an origin, that game's classic
	 * object is in hd_classic.h's table, and the object's mesh count is the
	 * .TRM's own node count. HDModelClassicPoseOf hands the caller the whole
	 * decision for its log line. */
	bool classicPose = false;

	/* Keep the vertex positions.
	 *
	 * A model is read once and becomes a vertex buffer nobody can read back,
	 * which is fine for every measurement this module makes itself - they all
	 * happen while the file is open. The floor rule cannot: how deep a fitted
	 * model reaches depends on the model AND on the item it stands in for,
	 * and the fit is only known once both of them are on the card.
	 *
	 * With this set the drawn vertices' positions are kept beside the model,
	 * in the model's own frame, each with the node it rides on, so that
	 * HDModelFloorLift can push them through that fit later in the same level
	 * build. HDModelReleasePoints frees them - the caller calls it as soon as
	 * it has measured, and no point list lives into the first frame.
	 *
	 * Every model of the pickup family needs it, not only the fitted ones. A
	 * frame that TURNS node 0 - six of ANDREA3's eighteen pickup slots do -
	 * sends the bind-pose box corners where no vertex of the model is, and a
	 * box-based rule would lift a model by the corner's number. */
	bool keepPoints = false;
};

/* What the classic pose did to one slot, for the caller's log line - the
 * engine's and ModelProbe's, out of the one measurement. */
struct HDModelClassicPose
{
	bool applied;
	int game;		// the mod's origin: 1, 2 or 3; 0 when it has none
	int object;		// the classic object id; 0 when the name is unknown
	int meshes;		// the classic object's mesh count
	int nodes;		// the .TRM's own - its highest weighted bone plus one
	char level[64];		// the level file the object was read from
	float before[3];	// the model's box before the pose
	float after[3];		// and after it
	char why[128];		// empty when it was applied
};

// Where a model's file was found.
enum HDModelOrigin
{
	HD_MODEL_ORIGIN_NONE = 0,
	HD_MODEL_ORIGIN_5,
	HD_MODEL_ORIGIN_4,	// the sibling 4\ITEM - SAS and five others live only there
	HD_MODEL_ORIGIN_TR3,	// the other bundle: 3\ITEM, or its siblings 1 and 2
	HD_MODEL_ORIGIN_MOD	// a mod's own ITEM folder
};

struct HDModelStats
{
	int slots;		// handed in
	int lara;		// LARA .. HAIR - drawn by hd_lara, not touched here
	int dummy;		// the level's .MAP says DUMMY on purpose
	int noName;		// no slot name in this engine at all
	int missing;		// a model name with no file in 5 or 4 -> classic
	int refused;		// the file is there but could not be read -> classic
	int loaded;		// models on the card
	int fromGame5;
	int fromGame4;
	int fromTr3;		// the other bundle - a Tomb Raider III guest
	int fromMod;		// a mod's own 5\ITEM or 4\ITEM
	int heads;		// <NAME>_HEAD.TRM built and bound to a node

	int fullSlots;		// every classic node covered by the model
	int partialSlots;	// some nodes uncovered, and those draw nothing
	int nodesTotal;		// classic nodes over all loaded slots
	int nodesCovered;
	int nodesFallback;	// = nodesTotal - nodesCovered

	int vertices;
	int indices;
	int ranges;		// draw calls for every model, every pass
	int cutRanges;		// of those, the opaque ones drawn with the alpha
				// test (hd_model_cut.h)
	int cutModels;		// models that have at least one
	int mixedPageTriangles;	// triangles whose three corners disagree on a page
	int pages;		// distinct .DDS pages the models added to the cache

	double vertexBytes;
	double indexBytes;
	double readMs;		// reading and parsing the .TRM files
	double buildMs;		// everything that is not readMs - the three below
				// and the bookkeeping between them

	/* The phases of buildMs, so that a slow level build points at its
	 * cause: findMs is the walk of the ITEM folders that turns a slot
	 * name into a file - the phase every active mod makes longer - keyMs
	 * is resolving the cache key of every page a model names,
	 * pageMs is the append to the page cache - the .DDS reads and the BC7
	 * decode - and bufferMs is pass two, the sort into draw calls and the
	 * vertex and index buffers. */
	double findMs;
	double keyMs;
	double pageMs;
	double bufferMs;

	/* What the .TRM reader did for this level build: files opened, models
	 * read with their geometry, bytes off disk. Shows whether a level change
	 * loads the whole wardrobe - see HDTrmCounters. */
	double trmOpens;
	double trmLoads;
	double trmBytes;

	HDPageCacheStats pageStats;	// what the append to the page cache did

	/* The FNV-1a 64 of every model built, in build order (HDModelBuildHash):
	 * its vertex and index bytes and its draw calls' starts, counts, page
	 * slots and nodes. 0 while the hash is off. */
	unsigned long long hash;

	/* What the hash took, kept out of buildMs and bufferMs so that the
	 * build's times are the same with the hash on or off. */
	double hashMs;
};

/* The model builds' fingerprint on or off for the builds that follow - the
 * rooms' HDRenderBuildHash for the models. The engine turns it on with the
 * log; the diagnostic tools always. */
void HDModelBuildHash(int on);

/* The time every model hash has taken since the program started, in ms - a
 * running total, so that a caller timing a step which builds models takes
 * the hash out of it by the difference of two readings. */
double HDModelHashMs();

/* The object's lights.
 *
 * The classic object path lights PER VERTEX with the vertex's own normal
 * (specific\output.cpp, aTransformLightClipMesh) out of a light list it sets
 * up PER NODE (specific\lighting.cpp, SuperSetupLight, called from
 * phd_PutPolygons through InitObjectLighting). One flat colour a node would
 * look like a mannequin: the front and the back of a torso are one node and
 * the classic path shades them apart.
 *
 * So the directional part is evaluated per vertex here as well, in the vertex
 * shader, and the engine hands over the light list itself. Two things make
 * that exact rather than an approximation:
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
 * The one part that looks per node is the ambient, and it is in fact per
 * OBJECT: the shadow lights that lower it read `light->Range`, which
 * CreateLightList fills from the ITEM's position and not from the node's. It
 * travels in HDModelDraw's `nodeLight` all the same, because Lara's composite
 * has a genuine per-node ambient in LaraNodeAmbient.
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

	/* The sheen.
	 *
	 * TR5 has no specular term of any kind: aTransformLightClipMesh adds an
	 * ambient and a diffuse cosine and stops. The remaster's catsuit and the
	 * TR3 London one read as LATEX because they are lit with one; without it
	 * every HD outfit comes out matt.
	 *
	 * The strength does not come from the art. The pages' alpha channel is a
	 * gloss mask on the ROOM pages, and TROne describes it the same way for
	 * items, but measured over the wardrobe (DdsProbe -alpha) pages 8001,
	 * 8008, 8016, 8018, 8027, 8028 and 8205 have EVERY texel between 240 and
	 * 255 - a flat opaque channel with BC7 noise and no mask. (The one page
	 * that is not flat is 8002, Lara's hair, at 15.2 % below 128 - and that
	 * is coverage, not gloss.) So the strength is a MATERIAL number per part,
	 * set from the remaster's look, in hd_lara.cpp's table.
	 *
	 * strength 0 is off and costs the pixel shader one multiply by zero.
	 * power is the Blinn-Phong exponent: small and the highlight is a broad
	 * satin sheen, large and it is a tight wet point.
	 *
	 * Only Lara uses it. Every object draws with 0. */
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
 * folder, or an empty string for "there is none". Set
 * before HDModelLevelBegin; a slot with `fromTr3` then looks there and
 * nowhere else, and every other slot never touches it. The page cache needs
 * the same path - HDPageCacheAltRoot - and the caller sets both, because one
 * module setting another module's state is how two answers drift apart. */
void HDModelTr3Root(const char* root);

/* Where a model's file is, by the very search a level build does - the mods
 * in order, then game 5, then the sibling game 4, or the I-III bundle for a
 * guest. There is one such search in this engine, and a diagnostic tool asks
 * it here instead of keeping a copy that goes out of step.
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
// Order: this comes AFTER HDRenderLevelBegin, because the models' pages are
// appended to the cache the rooms built, and HDModelLevelEnd comes BEFORE
// HDRenderLevelEnd, which destroys that cache. The wrong way round is not a
// crash - a slot number past the end of an emptied cache answers "no texture"
// - but it draws untextured objects.
bool HDModelLevelBegin(IDirect3DDevice9* device, const char* installRoot,
	const HDModelSlot* slots, int count, bool managedPages, int threads,
	HDModelStats* stats, const char** why);
void HDModelLevelEnd();

/* The warm-up. Offers every model's vertex and index buffer
 * to the device with IDirect3DResource9::PreLoad, so the upload a managed
 * buffer would otherwise pay for inside the first frame it is drawn in
 * happens while the loading bar still stands. Returns how many buffers were
 * offered; safe and a no-op with no level built. */
int HDModelPreload();
/* The marble floors' mirror draws the models with a clip plane on: after
 * a level's models are built, once for the model shaders' life (made in
 * HDModelInit, released in HDModelShutdown), one triangle through them
 * with the plane on, so the first frame with a model in a mirror does not
 * wait for the driver (hd_model.cpp). Returns its milliseconds, or -1 when
 * nothing was drawn (no model, or the shaders warmed already). */
double HDModelMirrorWarmUp();

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
// three passes every HDModelDraw of this bracket is to draw. A frame opens
// the bracket twice: once for HD_PASS_SOLID, with the
// rooms' own solid passes, and once for HD_PASS_ADDITIVE after the classic
// buckets, so that no later opaque draw can paint over a glow that wrote no
// depth. HD_PASS_ALL is the whole model in one bracket, which is what the
// diagnostic tool asks for.
void HDModelFrameBegin(const D3DMATRIX* projection, int bulbCount,
	unsigned int passes = HD_PASS_ALL);

/* One object drawn about a different screen centre.
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
 * Why here and not in the item's matrix: the same offset can be had by
 * shearing the node matrices - row 0 += kx * row 2. That is right for the
 * position and wrong for the LIGHT: hd_model.hlsl skins the vertex NORMAL
 * through the same gJoints and renormalises it, so a sheared node matrix tilts
 * and rescales the normal, and the ring's one sun then lights an item by an
 * amount that depends on where the item stands on the screen. Measured on the
 * shader (ModelProbe -ringlight, mean brightness over the object's own
 * pixels): 93.61 unsheared against 67.18 in the middle of the inventory row and
 * 108.87 on the ammo row - two rings shown at once, 45 % apart. In the
 * projection the offset cannot reach a normal, because a normal never goes
 * through the projection.
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

/* Three clip planes for the next HDModelDraw, in view space: twelve floats,
 * n.x n.y n.z d three times, and a pixel is kept where dot(n, viewPos) + d
 * >= 0 for all three (hd_model.hlsl, gClip in c5 - c7 of the pixel file). A
 * mirror's reflection is drawn with the planes of the copy behind its glass
 * (game\mirror_math.h, MirrorClipPlanes); null is (0, 0, 0, 1) three times,
 * which keeps every pixel. HDModelFrameBegin uploads the null planes, so a
 * bracket whose draws never call this - the pickup display - keeps every
 * pixel; the upload happens only when the value changes. */
void HDModelClip(const float* planes);

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
/* Which side of a triangle is drawn.
 *
 * The HD path draws every model DOUBLE SIDED, which is what TROne did too in
 * its copy of 2026-09-05; TROne-main (copy of 2026-10-06, commit 75c4c843,
 * item_flush) draws every game's objects one-sided and Lara double-sided,
 * and this rule does not follow it - it rests on our own count below. It is
 * what the classic
 * engine appears to do - specific/function_table.cpp sets D3DCULL_NONE for
 * the whole session. But TR5 culls back faces in SOFTWARE, per polygon,
 * through the IsVisible function pointer, so the card never sees one. The
 * difference shows only when a camera stands inside an object - the first
 * person camera stands inside Lara, and with her neck cut away the inside of
 * her chest lies behind it (ModelProbe -firstperson).
 *
 * So the cut and the culling belong together, and both are per DRAW: only
 * Lara's body in first person asks for either. HD_MODEL_CULL_BACK is the mode
 * that keeps the OUTSIDE of a .TRM, established by picture, because the
 * winding convention is a fact about the files. Their winding is consistent
 * enough to cull on: over the eight wardrobe bodies, 39,440 to 25,384
 * triangles each, at most 59 have the geometric normal on the other side of
 * the vertex normal (0.06 % to 0.18 %). The hole the neck cut leaves is
 * closed by a generated LID (hd_lara.h). */
enum HDModelCull
{
	HD_MODEL_CULL_NONE = 0,		// double sided - every model but the one
	HD_MODEL_CULL_CW,		// D3DCULL_CW
	HD_MODEL_CULL_CCW		// D3DCULL_CCW
};

#define HD_MODEL_CULL_BACK	HD_MODEL_CULL_CCW

/* The first person head cut.
 *
 * A fragment is discarded where its weight on node `node` reaches `bar`. The
 * vertex shader adds up the vertex's share of that one node out of the three
 * bones it names, the pixel shader clips on the interpolated value - so a
 * triangle that straddles the boundary is cut exactly where the weight
 * crosses it, and no vertex rule rounds the edge off.
 *
 * Why a weight and not a shape in space: a SPHERE about the eye takes a bite
 * the size of its radius out of the chest. A CYLINDER about the neck axis in
 * the BIND POSE leaves the collar standing, because a neck vertex is skinned
 * to two nodes at once and the patch of skin a bind-pose shape covers MOVES
 * as the head turns. A PLANE carried by the head node TILTS with the head,
 * and one tilted forty degrees runs through the chest. A weight is written in
 * the file, the same number in every pose and at every camera angle, and no
 * animation can move a vertex past it. hd_lara.h carries the threshold and
 * the measurement over the wardrobe that picked it.
 *
 * A null pointer, or `on` 0, is off and costs the pixel shader one lerp.
 */
struct HDModelNeckCut
{
	float on;		// 0 = off; nothing else is read then
	float node;	// whose weight decides - Lara's head, as a float for the shader
	float bar;		// discard where that weight reaches this, of 1
};

/* The soft motion of the chest, as the four vertex registers c163 to c166 of
 * hd_model.hlsl (gSoft): two regions in the body's bind space and an offset
 * each in view space, laid out as hd_softbody.h's HDSoftConstants fills
 * them. A null pointer is sixteen zeros, which moves nothing; every draw
 * writes the registers, so no draw inherits another's offset. */
struct HDModelSoft
{
	float constants[16];
};

// False when the slot has no model or the frame is not open; the caller then
// draws it the classic way.
//
// `lipsTrack` and `lipsFrame` are a cutscene's audio track and the frame of it
// this draw shows, for a face that speaks - see the lips below; -1 draws the
// model as it was built. `soft` is the chest's secondary motion for this draw,
// or 0 for none.
bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE,
	int lipsTrack = -1, int lipsFrame = -1, const HDModelSoft* soft = 0);

// The separate HD face, drawn right after the body on the same matrices. Does
// nothing and returns false when the slot has no <NAME>_HEAD.TRM.
bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE,
	int lipsTrack = -1, int lipsFrame = -1);

int HDModelDrawCalls();		// since the last HDModelFrameBegin

/* The projected shadow's cast draw: a model into the shadow map
 * (hd_render.h, HDRenderShadowCastBegin), skinned exactly as HDModelDraw
 * skins it - the same joints, the same bind fold, the same soft motion, a
 * speaking face's deformed buffer - but with the cast shaders of
 * hd_shadow.hlsl, which write the map's depth and nothing else.
 *
 * HDModelCastReady says whether the two shaders were made on this device.
 * HDModelCastBegin binds them and the declaration and puts `castClip`
 * (hd_shadow.h, HDShadowCastMatrix; row-major) into c0 - c3 in the frame's
 * projection's place; HDRenderShadowCastEnd puts the frame's registers back.
 *
 * HDModelCastDraw draws the opaque and the alpha-tested pass, never the
 * additive one, with the alpha test of each range as HDModelDraw has it,
 * with no head cut and double sided. `head` draws the slot's separate face
 * instead of its body. False when the model is not there or no cast has
 * begun. HDModelCastEnd closes the cast, before HDRenderShadowCastEnd.
 * HDModelCastCalls counts the draw calls since HDModelCastBegin. */
bool HDModelCastReady();
void HDModelCastBegin(const float* castClip);
void HDModelCastEnd();
bool HDModelCastDraw(int slot, bool head, const float* nodeMatrix, int nodeCount,
	unsigned int nodeMask, int lipsTrack = -1, int lipsFrame = -1,
	const HDModelSoft* soft = 0);
int HDModelCastCalls();

/* The projected shadow's receive draw: a model that the solid pass has just
 * drawn, drawn again inside the models' receive bracket (hd_render.h,
 * HDRenderShadowModelsBegin) with ModelVS and every vertex constant that
 * decides where its pixels lie - the same joints, the same bind fold, the
 * same head cut and soft motion, a speaking face's deformed buffer of the
 * same frame, the same cull - so each pixel lands on the very depth the
 * solid draw wrote, and with hd_shadow_receive.hlsl's ModelReceivePS in
 * ModelPS's place, which repeats ModelPS's three clips (the range's alpha
 * test, the head cut, a mirror's planes as HDModelClip left them) and writes
 * the shadow's factor.
 *
 * HDModelReceiveReady says whether the shader was made on this device.
 * HDModelReceiveBegin, inside an open model bracket (HDModelFrameBegin) with
 * the frame's projection in c0 - c3, binds ModelVS, the receive shader and
 * the declaration; HDModelReceiveEnd closes it, before
 * HDRenderShadowModelsEnd, which puts the shaders back.
 *
 * HDModelReceiveDraw draws the opaque and the alpha-tested pass, never the
 * additive one, with the alpha test of each range and the arguments of the
 * solid draw it repeats: HDModelDraw's for the body (`head` false), and
 * HDModelDrawHead's for the slot's separate face (`head` true, `soft` 0).
 * The two maps' blocks are the caller's (HDRenderShadowModelsBlocks). False
 * when the model is not there or no receive has begun. HDModelReceiveCalls
 * counts the draw calls since HDModelReceiveBegin. */
bool HDModelReceiveReady();
void HDModelReceiveBegin();
void HDModelReceiveEnd();
bool HDModelReceiveDraw(int slot, bool head, const float* nodeMatrix, int nodeCount,
	unsigned int nodeMask, const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE,
	int lipsTrack = -1, int lipsFrame = -1, const HDModelSoft* soft = 0);
int HDModelReceiveCalls();

/* The lips of a speaking face.
 *
 * A cutscene face speaks with one file per audio track, <FACE>_HEAD_<track>
 * .TRL; the file, the map from its columns to the face's joints and the
 * deform are in hd_trl.h. Every separate face (HDModelDrawHead's) and every
 * rigid face whose file carries its joint block (Lara's, a slot of her
 * wardrobe drawn by HDModelDraw) keeps what it needs from level build to
 * level end. `head` says which of the two a slot number means.
 *
 * HDModelLipsOpen opens the face's file for `track` and returns its frame
 * count, or 0 when the face has none or cannot use it; asked again for the
 * same track it answers without looking again. A draw with that track then
 * shows the face at the frame it is handed: a frame change deforms the
 * face's own vertices into a vertex buffer of its own - DYNAMIC, in
 * D3DPOOL_DEFAULT, written with D3DLOCK_DISCARD, made on the first frame
 * that needs it - and draws from it; the same frame again draws the same
 * buffer. Past the clip's last frame the face eases to its bind over
 * HD_LIPS_EASE_FRAMES and then draws its static buffer. A draw never opens a
 * file.
 *
 * HDModelLipsClose closes every clip and releases every such buffer: the cut
 * is over. HDModelLevelEnd does the same, and so HDModelShutdown, which is
 * what the device-lost chain calls before Reset. */
int HDModelLipsOpen(int slot, bool head, int track);
int HDModelLipsFrames(int slot, bool head);	// the open clip's, or 0
void HDModelLipsClose();

/* The loop between lines and the firing face (hd_trl.h): two tracks above
 * every audio track number, played out of the joint block the face's own .TRM
 * carries rather than out of a file of their own. HDModelLipsOpen with either
 * opens that block as the clip - one clip for both, so going from one to the
 * other opens nothing - for a face whose block loops (HDTrmModel::blockLoop at
 * least HD_LIPS_LOOP_MIN) and whose name the loop table knows, and answers 0
 * for every other face; the firing face also wants a block that reaches
 * HD_LIPS_FIRE_FRAME. A track whose file is not there, asked for again while
 * one of them plays, is answered 0 without closing it.
 *
 * The frame a draw hands with them is a key:
 *
 *   HD_LIPS_TRACK_LOOP  HDModelLipsLoopKey(frame, past) - the loop's frame
 *                       through the face's loop mask, and `past` ticks
 *                       after the loop stopped there it eases to the bind
 *                       the way a track eases past its end (HDLipsSourceOf):
 *                       0 is the frame itself, HD_LIPS_EASE_FRAMES on the
 *                       bind.
 *   HD_LIPS_TRACK_FIRE  HDModelLipsFireKey(from, step) - HD_LIPS_FIRE_FRAME
 *                       in full, step / HD_LIPS_EASE_IN_TICKS of the way from
 *                       the loop's frame `from` through the mask, or from the
 *                       bind for -1 (HDLipsSkinsBlend). At the full step the
 *                       frame it came from is left out of the key, so a face
 *                       that keeps firing is deformed once.
 *
 * A draw with either records its frame change the way a track's does
 * (HDModelLipsEventAt), under its own track number. */
#define HD_LIPS_TRACK_LOOP	1000
#define HD_LIPS_TRACK_FIRE	1001
#define HD_LIPS_KEY_STRIDE	65536

int HDModelLipsLoopKey(int frame, int past);
int HDModelLipsFireKey(int from, int step);

/* The loop's length in frames, or 0 for a face that plays none. */
int HDModelLipsLoop(int slot, bool head);

/* The upper lids' turn in degrees where a blink of the loop starts at `frame`
 * (HDLipsBlinks), and -1 on every other frame. */
float HDModelLipsBlink(int slot, bool head, int frame);

/* One line per track file of every face that keeps lips, and one for such a
 * face with none, written at level build for the caller's log:
 *
 *   slot=<n> face=<STEM> file=<STEM>_<track>.TRL frames=<n> cols=<c>
 *   joints=<driven>/<rig> axis=<identity|(x,-z,y)|(x,-y,-z)|-> len=<s>s
 *   reason=<ok|no file|refused|columns>[ from=<4|5|mod|tr3> trl_from=<...>]
 *
 * `from` and `trl_from` stand only where the face file and the track file
 * come from different folders - LARA_HEAD, from=4 trl_from=5. After a face's
 * track lines, one line for its loop:
 *
 *   loop slot=<n> face=<STEM> frames=<block frames> loop=<n> at=<offset>
 *   mask=<hex> blinks=<n> reason=<ok|no block|no loop|no mask|unmapped>
 *
 * `loop` is the block's first frame equal to frame 0 (HDTrmModel::blockLoop),
 * `mask` the loop table's columns (HDLipsLoopMask) and `blinks` how many
 * blinks the loop starts; only `ok` plays. Valid until the next
 * HDModelLevelBegin or HDModelLevelEnd. */
int HDModelLipsLineCount();
const char* HDModelLipsLine(int at);

/* What the draws did with the lips since the caller last cleared them: one
 * entry per frame change of a face with an open clip. `source` is one of
 * hd_trl.h's HDLipsSource and `jaw` the largest distance a vertex of the face
 * moved from its bind, in classic units. At most 64 are kept. */
struct HDModelLipsEvent
{
	int slot;
	bool head;
	int track;
	int frame;
	int source;
	float jaw;
};

int HDModelLipsEventCount();
const HDModelLipsEvent* HDModelLipsEventAt(int at);
void HDModelLipsEventsClear();

// --- what was built --------------------------------------------------------

bool HDModelHas(int slot);
const char* HDModelName(int slot);		// the .TRM stem, or 0
HDModelOrigin HDModelOriginOf(int slot);
int HDModelNodes(int slot);			// the classic node count handed in
int HDModelNodeVertices(int slot, int node);	// weighted vertices on that node
bool HDModelNodeCovered(int slot, int node);	// false -> nothing is drawn there

/* Whether the model fits the skeleton that is in the slot.
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

/* The working set of the drift, per node: the centre of the vertices that
 * ride on that node alone (weight 255), in the model's bind pose, and the
 * return value how many there are - 0 for a node with none, or for a slot
 * with no model. The skeleton fit holds it against the classic mesh's own
 * centre (hd_object_rules.h). */
int HDModelNodeCentre(int slot, int node, float* centre /* 3 */);

/* The drift limit, in classic units. It lives here rather
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
// Of those, the opaque ranges drawn with the alpha test (hd_model_cut.h).
int HDModelCutRanges(int slot);
int HDModelPageCount(int slot);
int HDModelPage(int slot, int at);		// .DDS number

/* What the mirror fold did to this slot's model - all zero
 * for a slot that asked for none, and for one whose file had nothing to fold.
 * False when the slot holds no model at all. */
bool HDModelFoldCounts(int slot, HDTrmFoldCounts* out);

/* A material for one page of one model.
 *
 * The sheen is one number per DRAW, and one number is right for every model
 * in the game but three: a HAND_ model is a hand AND the gun in it, on two
 * pages of one file. With one number either the skin shines like gunmetal or
 * the gun goes matt.
 *
 * A page is already the unit a draw call is cut to, so naming the material by
 * page costs one constant write on the ranges that have one and nothing at all
 * on every model that has no table. Call it after HDModelLevelBegin, once per
 * level; the table lives with the model and dies with it. */
void HDModelSetPageSheen(int slot, int page, float strength, float power);
int HDModelPageSheenCount(int slot);

bool HDModelHasHead(int slot);
int HDModelHeadNode(int slot);			// the node the face rides on, or -1
// The model's bounding box in its own bind pose, six floats (min then max).
// The body's alone; a separate HD face reaches past it (its geometry is the
// whole head, where the body has an empty collar), so a caller that draws
// both has to take the union.
bool HDModelBounds(int slot, float* mins, float* maxs);
bool HDModelHeadBounds(int slot, float* mins, float* maxs);

/* The vertex cloud's principal axes, measured while the model
 * was read. `axes` takes nine floats - three unit ROW vectors in the model's
 * own frame, longest first and right handed - and `extent` the cloud's span
 * along each; either may be null. False for a model with fewer than three
 * drawn vertices.
 *
 * An eigenvector has no sign, and a gun has two ends, so the sign is the
 * model's own geometry: an axis points to the side on which the cloud reaches
 * further from its own centre of mass. The third is the cross product of the
 * other two, so the frame can rotate a model and never mirror it.
 *
 * A box says how much room a model needs; these say which way it RUNS. For a
 * flat pair of guns the two are nothing alike - a box cannot tell that pair
 * from one tall gun standing up, 147 x 253 x 159 being what both measure. */
bool HDModelPrincipalAxes(int model, float* axes, float* extent);

/* The fit that puts one model in another's place, by shape.
 *
 * `out` takes sixteen floats, row-major with the translation in row 3 - the
 * convention every matrix in this module is written in. It centres the source
 * on the target's box, turns the source's k-th principal axis onto the
 * target's k-th (longest first) and scales by the ratio of the two longest
 * extents. Both frames are right handed by construction, so the map is a pure
 * rotation and cannot mirror a model. False when either model has no axes.
 *
 * The inventory ring may be asked to draw a file from a different game, which
 * stands in that game's frame. A fit by bounding BOXES cannot tell a flat pair
 * of guns from one tall gun standing up - a mod's Uzis come out lying flat,
 * one above the other with a gap between them. The vertex cloud's own axes say
 * which way the shape runs - measured on a mod's files, EXTRA_UZI_ITEM is
 * 256 x 236 x 42 and flat along -y, UZI_OPTION is 242 x 240 x 132 and flat
 * along -x - so pairing them lays the mod's pair into the plane the vanilla
 * item lies in, and the ring's own per-item rotation then presents the two
 * alike. Nothing about which game or which model is written down here.
 *
 * It does not CROSS two guns that stand parallel in the file, and that mod's
 * UZI_OPTION is such a file: a rigid turn keeps the angle between two parts.
 * The note at HDModelRingFit in hd_model.cpp gives the reason and says what
 * the IN-PLANE angle rests on, which is the weak half of this. */
bool HDModelRingFit(int model, int target, float* out);

/* How far that fit leaves the room the ring keeps, on the shape's own axes.
 *
 * The scale is the ratio of the two LONGEST extents, so the other two axes
 * land wherever their own proportions put them and a model CAN stand well
 * outside the room kept for the item it replaces - the mod's pair of Uzis is
 * 132 units thick where the vanilla item is 42.
 *
 * The largest of the three ratios, fitted extent over the target's on the same
 * axis: 1.00 is "inside". `worstAxis` is optional and takes 0, 1 or 2 - WHICH
 * OF THE TARGET'S OWN axes, longest first. Zero when either model has no axes.
 *
 * It is a number for the log and refuses nothing - the reason the fit does not
 * fit-inside is at HDModelRingFit - but a ring that looks crowded has a log
 * line pointing at the cause. */
float HDModelRingFitOverflow(int model, int target, int* worstAxis);

/* The frame the level really draws a slot in.
 *
 * A moveable's meshes are placed by its mesh tree and turned by its animation
 * frame - DrawAnimatingItem calls gar_RotYXZsuperpack for node 0 before the
 * first mesh is drawn and once more for every node below it. The bind pose,
 * which is that walk with the rotations left out, is the frame a .TRM's
 * vertices are measured against; it is NOT the frame the item stands in on
 * the floor. Six of ANDREA3's eighteen pickup slots carry a turn on node 0
 * that is not a yaw, and the Uzi item's stands the model on its head.
 *
 * This is the walk WITH them, once per slot at level build: `out` takes twelve
 * floats a node - the engine's own 3x4, M00..M23, so a point is R * p + t -
 * and the return value is how many nodes were filled.
 *
 * `frame` is the object's own first frame: six shorts of bounding box, three
 * of offset, then one rotation a node in gar_RotYXZsuperpack's packing.
 * `tree` is the engine's bone array from the object's bone_index, four longs a
 * node after the first (flags, x, y, z). Either may be null and a null frame
 * gives identity for every node, which is the bind pose again.
 *
 * The frame's OFFSET and the item's own rotation are left out, and the
 * function says why - both of them cancel in the difference the floor rule is
 * about. */
int HDFramePose(const short* frame, const long* tree, int nodes,
	float* out /* nodes * 12 */);

/* The box of a model as it is really drawn - every kept
 * vertex through the fit, the bind fold and the node's own frame, in the
 * item's frame. The same sweep HDModelFloorLift is: the deepest point is this
 * box's largest y, and a tool that has to FRAME a picture of the drawn model
 * needs the other five numbers as well. `fix` and `pose` mean exactly what
 * they mean there, and it needs the same kept vertices.
 *
 * `deepest` is optional and takes the three coordinates of the deepest vertex
 * ITSELF - where the model touches the ground and not only how far down that
 * is. A picture of the rule has to draw the floor plane under that point. */
bool HDModelFramedBox(int model, const float* fix, const float* pose,
	int nodes, float* mins, float* maxs, float* deepest = 0);

/* How far a pickup model sinks, and what lifts it out.
 *
 * A pickup is drawn at its animation frame's own offset, so where the ground
 * meets it is wherever the level's CLASSIC MESH ends - and that varies:
 * ANDREA3's classic PISTOLS_ITEM mesh has its deepest point 59 units below its
 * node and its CROWBAR_ITEM one unit above. An HD model that reaches deeper
 * than that mesh stands in the ground where the level's own picture does not.
 *
 * `classicLowest` is that reference, in the slot's own frame, and it comes
 * from the caller because only the caller has the level: the engine reads
 * mesh_vtxbuf's own bbox, ModelProbe walks the .TRC's mesh. y is DOWN in this
 * engine, so "deepest" is the LARGEST y and a model sinks when its deepest
 * point is larger than `classicLowest`.
 *
 * `fix` is optional. Without it the model is measured AS IT STANDS, node by
 * node through the bind fold the draw does. With it (the ring's fit, what
 * HDModelRingFit wrote) the model is the one-node <BASE>_OPTION file and every
 * kept vertex goes through that matrix first. Either way it is EVERY KEPT
 * VERTEX and never a box: that needs HDModelSlot::keepPoints on the model, and
 * without it the call says so by returning false rather than by quietly
 * measuring the wrong thing. The eight BOX CORNERS through the fit would
 * overstate a crossed pair of guns by about a factor of two.
 *
 * `pose` is HDFramePose's answer for the slot the model is drawn in - twelve
 * floats a node - and `nodes` how many of them there are. It makes the answer
 * the frame the LEVEL draws in rather than the bind pose; null means the
 * frame turns nothing, which is the bind pose.
 *
 * `lowest` takes the model's own deepest point and `lift` how far it has to
 * come up so as not to reach below `classicLowest` - zero for a model that
 * already lies on it or above it, and never negative: a model that floats is
 * the artist's business and not this rule's. Both are optional. A sink inside
 * HALF A UNIT reads as zero as well; the slack and its reason are at the
 * function.
 *
 * One implementation, engine and tool, for the same reason the fit is. */
bool HDModelFloorLift(int model, const float* fix, const float* pose,
	int nodes, float classicLowest, float* lowest, float* lift);

/* What that measurement had in hand. `kept` is how many vertex positions are
 * beside the model - zero for one that never asked to keep them - and
 * `blended` how many of those ride on more than one node, which is the one
 * thing the sweep reads off the heaviest bone alone. Every pickup file
 * measured, the remaster's own and four mods', is rigid per node and reads
 * zero. */
bool HDModelPointsInfo(int model, int* kept, int* blended);

/* Frees the point lists. Drops every kept vertex list of this
 * level; the models themselves are untouched and go on drawing. Call it once
 * the floor rule has measured - a level build keeps the points for the length
 * of the plan and not a frame longer. */
void HDModelReleasePoints();

/* The classic pose's own decision, per model slot. True for
 * a slot that ASKED for one - whether or not it got it; `applied` and `why`
 * say which. Valid until the next HDModelLevelBegin. */
bool HDModelClassicPoseOf(int slot, HDModelClassicPose* out);

// Everything that could not be bound, in words, for the caller's log. Valid
// until the next HDModelLevelBegin or HDModelLevelEnd.
int HDModelNoteCount();
const char* HDModelNote(int at);

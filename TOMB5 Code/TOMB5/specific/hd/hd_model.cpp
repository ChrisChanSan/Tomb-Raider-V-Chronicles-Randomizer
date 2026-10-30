/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the object path are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, TROne-main (copy of 2026-10-06, commit 75c4c843),
 * host\render\game_renderer.cpp) - see hd_model.h for the list, function by
 * function. The renderer under them is new. The .TRM format is TRForge's
 * description (MIT), counted again here over game 5; hd_trm.h has it.
 * See THIRD-PARTY.md.
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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "hd_model.h"
#include "hd_bind.h"
#include "hd_classic.h"		// the donor game's own frame 0
#include "hd_mods.h"		// a mod's model beats the install's
#include "hd_model_cut.h"	// which opaque triangles are cut out
#include "hd_object_rules.h"	// which alpha run triangles are glass
#include "hd_trm.h"
#include "hd_trl.h"		// a face's track files, its map and its deform

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "hd_shader/hd_model_vs.inc"
#include "hd_shader/hd_model_ps.inc"
#include "hd_shader/hd_shadow_cast_vs.inc"
#include "hd_shader/hd_shadow_cast_ps.inc"
#include "hd_shader/hd_shadow_receive_model_ps.inc"

namespace
{
	/* The vertex on the card is the vertex on disk - all 24 bytes of it, in
	 * the same order, with the same meaning. A .TRM vertex needs no repacking,
	 * only a declaration that reads its bytes right. The layout is written out
	 * in hd_trm.h and again in hd_model.hlsl.
	 *
	 * This struct fixes the stride at 24. HDTrmVertex has exactly this shape,
	 * so the fill below is a memcpy in disguise - written out, because
	 * HDTrmVertex's layout belongs to the reader. */
	struct HDModelVertex
	{
		float x, y, z;
		unsigned char nx, ny, nz, layer;
		unsigned char bone0, bone1, bone2, u;
		unsigned char weight0, weight1, weight2, v;
	};

	/* One draw call: a run of indices whose triangles all wear the same page
	 * AND ride on the same node.
	 *
	 * The node is the second key because `item->mesh_bits` hides whole nodes -
	 * the gladiator's shield, a body the game blows apart - and an index
	 * buffer cannot skip a triangle at draw time. Sorted this way the hidden
	 * node's triangles are a contiguous run that is simply not drawn.
	 *
	 * It does not cost draw calls in the normal case: the ranges come off in
	 * (page, node) order, so consecutive ones of the same page whose nodes are
	 * all visible are merged back into one call before the draw. With
	 * mesh_bits at -1, which is nearly always, the count is one call per
	 * page. */
	struct Range
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
		int node = 0;
		// The .DDS number behind pageSlot: a material is named by PAGE and
		// the cache slot is an allocation.
		int page = -1;
		// Drawn with the alpha test in the opaque pass: every triangle of
		// the range wears a cut-out material on a page whose low alpha is
		// holes (hd_model_cut.h).
		bool cut = false;
		// An alpha run range of the glass material (hd_object_rules.h,
		// HD_TRM_MATERIAL_GLASS), set at the build.
		bool glass = false;
		// Drawn as glass: added to what lies behind, with the glows and
		// with no depth write. Set per level by HDModelSetGlassNodes, on
		// glass ranges whose node the classic draws see-through.
		bool blend = false;
		// A glass range every triangle of which has a glass layer of its
		// node facing against it: drawn as glass, one-sided
		// (hd_object_rules.h, HDTrmTrianglesFaceAgainst). Set at the build.
		bool sided = false;
		// What the glass is added times: its part's opacity
		// (HDGlassOpacity), 1 for every other range. Set at the build.
		float opacity = 1.0f;
		// An additive run range that copies a glass triangle of its node
		// and page (HDTrmTriangleCopies). Set at the build.
		bool copy = false;
		// A copy whose node draws glass: left out, the glass draws it.
		// Set per level by HDModelSetGlassNodes.
		bool dropped = false;
	};

	/* One material, named by page.
	 *
	 * The sheen is one number per DRAW, and that is right for every model in
	 * the game but three: a HAND_ model is a hand AND the thing it is
	 * holding, on two pages of one file, and one number makes either the skin
	 * metal or the gun matt - a Blinn-Phong exponent of 34 on a hand-sized
	 * object is a highlight one pixel wide.
	 *
	 * A page is already the unit a draw call is cut to, so a per-page material
	 * costs one constant write on the ranges that have one and nothing at all
	 * on every other model in the game. */
	struct PageSheen
	{
		int page;
		float strength;
		float power;
	};

	/* A face that speaks - see hd_trl.h for the files and the arithmetic.
	 *
	 * Kept beside every separate face and beside a rigid face whose file
	 * carries a rig and its joint block (Lara's), from level build to level
	 * end. The face's own vertices as the file writes them - position,
	 * normal, and the bones and weights the track files move - and the
	 * vertices of its static buffer, whose bones, weights, page layer and
	 * texture coordinates a deformed copy takes over unchanged, so the node
	 * skinning on the card does what it does for the static one.
	 *
	 * The clip is the one track file in play, read a frame at a time: the
	 * seam opens it at the cut's first frame (HDModelLipsOpen) and it closes
	 * when the cut is over or the level ends. A whole clip held in memory is
	 * up to 11.5 MB (DEMON_HEAD_135.TRL, 6001 frames of 40 columns), and
	 * cutscene 44 speaks three such faces at once; a frame is 1920 bytes.
	 *
	 * `vb` is the buffer the deformed vertices go into, D3DPOOL_DEFAULT
	 * because a buffer written thirty times a second is DYNAMIC, and a
	 * DYNAMIC one cannot be managed. Device Reset refuses while a DEFAULT
	 * resource lives, so it is released with the clip, in free_model and so
	 * in HDModelShutdown, and made again on the next speaking frame. */
	struct Lips
	{
		HDLipsMap map;
		bool mapped = false;
		const char* why = "";
		int columns = 0;		// the file's block, even where the map refuses

		std::string stem;		// the face file's name: LARA_HEAD, ADMIRAL_HEAD
		std::string folder;		// where its track files are looked for
		const char* from = "5";		// the face file's own folder, for the log
		const char* folderLabel = "5";	// ... and the track files'
		/* A mod's face only: whose track files it speaks - "own", "install"
		 * (the install's face has the same rig), "differs" or "none" (no
		 * install face of that name); "" for every other face. */
		const char* rig = "";

		std::vector<float> position;		// 3 a vertex, the file's own
		std::vector<unsigned char> normal;	// 3 a vertex, biased by 127
		std::vector<unsigned char> bone;	// 3 a vertex, the rig's numbers
		std::vector<unsigned char> weight;	// 3 a vertex
		std::vector<HDModelVertex> base;	// the static buffer's vertices

		/* The half turn the face table asks for (HeadOfModel::halfTurnX),
		 * put on the deformed vertices exactly as it was put on the file. */
		bool turned = false;
		float turnY = 0.0f;
		float turnZ = 0.0f;

		/* The loop between lines and the firing face (hd_trl.h): the joint
		 * block in the face's own .TRM, where its frame 0 lies and how many
		 * frames it holds, the loop's length (0 for a face that plays none,
		 * and `loopWhy` says why), the columns the loop plays, the block's
		 * frame 0 for the others and where the blinks start. */
		std::string trmPath;
		long blockAt = 0;
		int blockFrames = 0;
		int blockLoop = 0;
		int loop = 0;
		const char* loopWhy = "no block";
		unsigned long long loopMask = 0;
		std::vector<float> bindLocals;
		std::vector<int> blinkStart;
		std::vector<float> blinkLid;

		int track = -1;			// the track the clip was asked for
		int missing = -1;		// a track whose file was not there
		HDTrlClip clip = { 0, 0, 0, 0, false };

		IDirect3DVertexBuffer9* vb = 0;
		int shownFrame = -1;		// the cutscene frame `vb` holds
		int shownSource = HD_LIPS_SOURCE_BIND;

		// Working space, while a clip is open.
		std::vector<float> frame;
		std::vector<double> skins;
		std::vector<float> outPosition;
		std::vector<unsigned char> outNormal;
		std::vector<HDModelVertex> out;
		std::vector<float> fire;		// the firing frame
		std::vector<double> fromSkins;		// the pose the firing face leaves
		std::vector<double> toSkins;		// ... and the one it goes to
	};

	struct Model
	{
		IDirect3DVertexBuffer9* vb = 0;
		IDirect3DIndexBuffer9* ib = 0;
		std::vector<Range> pass[3];
		std::vector<int> pages;			// .DDS numbers, distinct
		// A material per page, empty for every model but the ones a caller
		// has set one for.
		std::vector<PageSheen> pageSheen;

		std::string name;
		HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;

		int vertices = 0;
		int indices = 0;
		// How many vertices the measurements above actually saw - the same as
		// `vertices`, except under the page cut.
		int vertexSeen = 0;
		int nodes = 0;				// classic nmeshes
		// How many alpha run ranges are drawn as glass (Range::blend).
		int blendRanges = 0;
		int nodeVertices[32] = { 0 };
		unsigned int covered = 0;		// bit per node with geometry

		/* The skeleton fit, measured in build() - see HDModelMaxBone and
		 * HDModelBindDrift in hd_model.h for what the two numbers mean. */
		int maxBone = -1;
		float driftMean = 0;
		float driftWorst = 0;
		bool hasDrift = false;
		/* The working set of both: per node, the centre of the vertices
		 * that ride on that node alone, and how many there are - kept for
		 * the skeleton fit (hd_object_rules.h), which holds the centres
		 * against the classic meshes'. */
		float nodeCentre[32][3] = { { 0, 0, 0 } };
		int nodeSingle[32] = { 0 };

		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };

		/* The vertex cloud's own axes - the principal axes of the drawn
		 * vertices, longest first, with the cloud's extent along each. Measured
		 * in the same loop the box is, the one place the vertices are in hand.
		 *
		 * A box is not a shape: a pair of guns lying flat and one gun standing
		 * up have the same kind of box and nothing else in common. These say
		 * which way the SHAPE runs, for the ring's fit. */
		float axis[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		float axisExtent[3] = { 0, 0, 0 };
		bool hasAxes = false;

		float bind[32][3] = { { 0, 0, 0 } };
		bool hasBind = false;
		/* The classic mesh tree, for the head pass. -1 everywhere when the
		 * caller handed none over. */
		int parent[32] = { 0 };
		bool hasTree = false;

		int rigidNode = -1;			// >= 0 for a <NAME>_HEAD.TRM
		/* A rigid face's NECK: the file's OWN bone number that is bound to
		 * `neckNode` instead of `rigidNode`, and -1 when there is none. */
		int neckBone = -1;
		int neckNode = -1;

		// What the mirror fold moved on this file, for the caller's own log
		// line - all zero for a slot that asked for none.
		HDTrmFoldCounts fold = { 0, 0, 0, 0, 0, 0 };

		/* The drawn vertices' positions, for a model that asked to keep them
		 * - three floats each, in the model's own frame, the same vertices the
		 * box above was taken over. Empty for everything else, and emptied by
		 * HDModelReleasePoints once the level build has measured its floor
		 * rule.
		 *
		 * The box cannot answer the fitted question: a rotation sends the box
		 * corners somewhere no vertex of the model is. */
		std::vector<float> points;
		/* And which node each of them rides on. With every node's matrix a
		 * pure translation equal to its own bind offset, (p - bind) * M is p
		 * again whatever the weights are, so a vertex's bind-pose position IS
		 * its drawn position. Once a node's matrix carries a ROTATION - which
		 * the item's own animation frame puts there - the two part company
		 * and the point has to know which matrix to go through.
		 *
		 * The vertex's HEAVIEST bone, one byte each. A vertex that rides on
		 * two nodes at once is really drawn at the blend of both, and
		 * `pointsBlended` counts them so that the log says how many of a
		 * model's points this reads as rigid. Every pickup file measured -
		 * the remaster's own and four mods' - is rigid per node: weight
		 * (255, 0, 0) on every vertex. */
		std::vector<unsigned char> pointNode;
		int pointsBlended = 0;
		bool keepPoints = false;

		// The additive run from the front alone (HDModelSlot).
		bool additiveOneSided = false;

		// Every run lit as the additive run is (HDModelSlot).
		bool lightAsAdditive = false;

		/* A face's lips, or null - see Lips above. Owned by the model and
		 * given back by free_model. */
		Lips* lips = 0;
	};

	const int kJointBudget = 32;

	IDirect3DDevice9* g_device = 0;
	// Whether the card weighs a source by D3DRS_BLENDFACTOR; without it the
	// glass is added whole.
	bool g_blend_factor = false;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;
	// The pair above warmed for the marble mirror (HDModelMirrorWarmUp).
	bool g_mirror_warmed = false;
	/* The projected shadow's cast shaders (hd_shadow.hlsl). Not a condition
	 * of the model path: without them the projected shadow is refused and
	 * the blob stays. */
	IDirect3DVertexShader9* g_cast_vs = 0;
	IDirect3DPixelShader9* g_cast_ps = 0;
	bool g_in_cast = false;
	int g_cast_calls = 0;
	/* The projected shadow's receive shader for the models
	 * (hd_shadow_receive.hlsl, ModelReceivePS). Not a condition of the model
	 * path either: without it no model takes the shadow and the rooms still
	 * do. */
	IDirect3DPixelShader9* g_receive_ps = 0;
	bool g_in_receive = false;
	int g_receive_calls = 0;

	std::map<int, Model> g_models;		// slot -> body
	std::map<int, Model> g_heads;		// slot -> its separate HD face
	std::vector<std::string> g_notes;
	/* The faces' track files, one line each, written at level build - see
	 * HDModelLipsLine. And what the draws did with them since the caller
	 * last read them, for its log; bounded, because a caller that does not
	 * read them must not grow them without end. */
	std::vector<std::string> g_lips_lines;
	std::vector<HDModelLipsEvent> g_lips_events;
	const size_t kLipsEvents = 64;
	/* What the classic pose decided, per slot that asked for one. The
	 * decision is taken while the file is open and the log line is written by
	 * the caller, a level build later - so it is kept, not printed.
	 * Cleared with everything else by HDModelLevelBegin. */
	std::map<int, HDModelClassicPose> g_pose;
	std::string g_install;
	/* The other bundle's game 3 folder, for the four Tomb Raider III guests.
	 * Empty unless the caller set it, and then no slot can reach that bundle
	 * at all. */
	std::string g_install_tr3;
	// A refusal whose wording is not a literal - it has to outlive the call
	// that hands *why out. Never cleared by HDModelLevelEnd.
	std::string g_error;

	/* What the cache keys of this level build cost. Accumulated here rather
	 * than passed down because build() is
	 * called once per model and the number belongs to the level; reset by
	 * HDModelLevelBegin and read out of it into HDModelStats::keyMs. */
	double g_key_ms = 0;

	/* HDModelBuildHash: on or off, and the FNV-1a 64 of the models built so
	 * far in this level build - reset by HDModelLevelBegin, fed by build(). */
	int g_model_hash_on = 0;
	unsigned long long g_model_hash = 14695981039346656037ull;
	// The running total behind HDModelHashMs.
	double g_model_hash_ms = 0;

	void model_hash_add(const void* data, size_t bytes)
	{
		const unsigned char* p = (const unsigned char*)data;

		for (size_t i = 0; i < bytes; i++)
			g_model_hash = (g_model_hash ^ p[i]) * 1099511628211ull;
	}

	bool g_in_frame = false;
	int g_draw_calls = 0;
	int g_frame_bulbs = 0;
	// Which of the three passes this bracket draws - see HDModelFrameBegin
	// and the HD_PASS_ note in hd_render.h.
	unsigned int g_pass_mask = HD_PASS_ALL;

	/* The frame's projection, kept. The inventory ring draws each item about
	 * a different screen centre with a projection of its own per item
	 * (HDModelOffCentre), so the plain one is kept here to build them from
	 * and to go back to. `g_off_centre` says whether c0 currently holds a
	 * moved one, which lets the ordinary case - every object in the world -
	 * upload nothing at all. */
	float g_frame_proj[16] =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	bool g_off_centre = false;

	/* The clip planes c5 - c7 of the pixel file hold (HDModelClip), so that
	 * an unchanged value uploads nothing; and the planes that keep every
	 * pixel. */
	const float kClipKeepAll[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	float g_clip_now[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };

	/* --------------------------------------------------------- helpers -- */

	std::string join(const std::string& a, const char* b)
	{
		std::string out = a;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		out += '\\';
		out += b;
		return out;
	}

	// The folder above the game 5 folder - the bundle that holds 4, 5 and 6;
	// only 4 and 5 are ever searched.
	std::string parent_of(const std::string& path)
	{
		std::string out = path;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		const size_t cut = out.find_last_of("\\/");

		return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
	}

	bool file_there(const std::string& path)
	{
		return HDTrmExists(path.c_str());
	}

	/* Which mods may answer find_model: kModsAny is every
	 * active one in their own order, kModsNone is the installs alone, and a
	 * number is that mod and no other. The third case exists for a separate
	 * HD face - see find_head_model.
	 *
	 * "That mod and no other" is answered by asking for the name and keeping
	 * the answer only when it came from the mod wanted. An EARLIER mod that
	 * ships the same name therefore makes the wanted one invisible, and the
	 * face falls back to the install - which is the safe way round: a face
	 * under the wrong page namespace would be a wrong picture, and a face
	 * from the install is only a missing improvement. */
	const int kModsAny = -1;
	const int kModsNone = -2;

	/* Where a model lives. Game 5 first, then the siblings - and the siblings
	 * are needed: SAS, FIREROPE, BRIDGE_TILT1, RAISING_BLOCK1 and 2
	 * and EXPANDING_PLATFORM are in 4\ITEM and in no TR5 folder, because the
	 * remaster ships one model per NAME across the bundle and TR5 inherited
	 * those slots from TR4. Every substitution is written into the notes, so
	 * the log says which model came out of which game. */
	std::string find_model(const std::string& name, bool fromTr3,
		HDModelOrigin* origin, int* modIndex = 0, int modOnly = kModsAny)
	{
		const std::string file = name + ".TRM";

		if (modIndex)
			*modIndex = -1;

		/* A mod before everything else. The active mods in order, each
		 * 5\ITEM then 4\ITEM; the first one that has the name wins and the
		 * installs are not asked at all. It is a replacement by RESOLUTION
		 * and never on disk - renaming the mod's folder with a leading _ puts
		 * the install's own file back with nothing else to undo.
		 *
		 * Ahead of the fromTr3 branch on purpose: a mod may replace one of
		 * the four Tomb Raider III guests as well as a hand, and a guest
		 * whose model a mod supplies gets that mod's page namespace like any
		 * other. HDModsFindModel refuses OUTFIT_*.TRM by itself - outfits go
		 * through the wardrobe. */
		if (modOnly != kModsNone)
		{
			char path[512];
			int mod = -1;

			/* The wanted mod goes INTO the search rather than being checked
			 * on the way out: the resolver writes "model X <- modN" into the
			 * log once per model, so an answer dropped here would leave a
			 * wrong line and make the correct one for the same name
			 * unsayable. */
			if (*HDModsFindModel(name.c_str(), &mod, path, sizeof(path),
				modOnly == kModsAny ? -1 : modOnly))
			{
				if (origin)
					*origin = HD_MODEL_ORIGIN_MOD;

				if (modIndex)
					*modIndex = mod;

				return std::string(path);
			}
		}

		/* The other bundle, and only it. A Tomb Raider III
		 * guest is looked for in game 3 and its siblings 1 and 2, and NEVER
		 * in the IV-VI folders - a TR5 model that happened to carry the same
		 * name would be the wrong enemy drawn with confidence. The other way
		 * round holds too: an ordinary slot never reaches into game 3. */
		if (fromTr3)
		{
			if (g_install_tr3.empty())
			{
				if (origin)
					*origin = HD_MODEL_ORIGIN_NONE;
				return std::string();
			}

			const std::string bundle3 = parent_of(g_install_tr3);

			std::string path = join(join(g_install_tr3, "ITEM"), file.c_str());

			if (file_there(path))
			{
				if (origin)
					*origin = HD_MODEL_ORIGIN_TR3;
				return path;
			}

			static const char* siblings3[2] = { "1", "2" };

			for (int i = 0; i < 2; i++)
			{
				path = join(join(join(bundle3, siblings3[i]), "ITEM"), file.c_str());

				if (file_there(path))
				{
					if (origin)
						*origin = HD_MODEL_ORIGIN_TR3;
					return path;
				}
			}

			if (origin)
				*origin = HD_MODEL_ORIGIN_NONE;

			return std::string();
		}

		const std::string bundle = parent_of(g_install);

		std::string path = join(join(g_install, "ITEM"), file.c_str());

		if (file_there(path))
		{
			if (origin)
				*origin = HD_MODEL_ORIGIN_5;
			return path;
		}

		/* Then 4, then nothing. The bundle also holds a 6\ITEM, which is
		 * never searched: game 6 is Angel of Darkness, a different engine
		 * with a different Lara and different objects, and a name that
		 * matched there would be a wrong model rather than a missing one.
		 * 5 for what TR5 draws, 4 for the shared base, then classic. */
		path = join(join(join(bundle, "4"), "ITEM"), file.c_str());

		if (file_there(path))
		{
			if (origin)
				*origin = HD_MODEL_ORIGIN_4;
			return path;
		}

		if (origin)
			*origin = HD_MODEL_ORIGIN_NONE;

		return std::string();
	}

	/* Which face file belongs to a body.
	 *
	 * A cutscene body can end at the collar: ANIMATING4_RICHCUT2 stops at
	 * y -234 with an open ring of 16 points at (0, -218, 6), radius 13, and no
	 * vertex above it (ModelProbe). A node the .TRM does not cover is not
	 * drawn classic either, so without its face the actor is headless.
	 *
	 * A face is <MODEL>_HEAD.TRM, and that name rule answers for BLUE_GUARD,
	 * LARSON and PIERRE. It cannot answer for a cutscene actor, because the
	 * remaster names his body after the LEVEL SLOT it fills -
	 * ANIMATING4_RICHCUT2 - while his face keeps the person's name. Game 5
	 * ships twelve *_HEAD.TRM and only three are reachable by name.
	 *
	 * Two more rules, both measured over all 1,335 model names of the 4 and 5
	 * folders:
	 *
	 *   the STRIP rule. A trailing _<QUALIFIER> is the remaster's way of
	 *   saying "the same person, this level's paint": BLUE_GUARD_JOBY,
	 *   BLUE_GUARD_RICH2, BLUE_GUARD_RICHCUT2, PIERRE_SWAP. Cutting the last
	 *   underscore group and asking again answers for seven names, and every
	 *   one of them is right: the three BLUE_GUARD variants above,
	 *   PIERRE_SWAP and VON_CROY_BOOK/BOTH/EMPTY of game 4.
	 *
	 *   It stops at a stem that ends in _MIP. A _MIP model is a cutscene prop
	 *   the remaster fills a spare slot with, and DrawCutSeqActors draws it in
	 *   HD like any actor. Cut past the _MIP, ten names reached a face that is
	 *   not theirs: LARSON_MIP_ANDY1 and PIERRE_MIP_ANDY1 are planks and
	 *   BLUE_GUARD_MIP_ANDY1 a stone block with a chain (cutscene 14),
	 *   BLUE_GUARD_MIP_JOBY4 a pillar (40), LARSON_MIP_RICH1 an ID card (24),
	 *   LARSON_MIP_ANDY3 the hussar, whose own face Larson's covered (36 and
	 *   38), PIERRE_MIP_ANDY3 a horse, LARSON_MIP_JOBY3_JOBY5 a rack of
	 *   torpedoes in JOBY3 that no cutscene draws, and JEAN_YVES_MIP_ANG_RACE
	 *   and TROOPS_MIP_ANG_RACE of game 4. No right answer runs through a
	 *   _MIP.
	 *
	 *   the TABLE below, for the bodies the strip rule cannot reach because
	 *   they carry no part of the person's name at all. Each entry is
	 *   measured, not guessed.
	 *
	 *     ANIMATING4_RICHCUT2  ring (0, -218, 6) r13.1  <-  VON_CROY_SENIOR_HEAD
	 *                          neck (0, -214, 12) r23.1, 7.4 units apart; the
	 *                          next nearest face is 11.4 away, and the two
	 *                          share texture page 9075, which no other pair in
	 *                          the level does.
	 *     ANIMATING6_RICHCUT2  ring (10, -273, -54) r28.0  <-  VON_CROY_ELDER_HEAD
	 *                          neck (9, -274, -52) r32.0, 2.7 units apart
	 *                          against 35.3 for the next nearest face. The
	 *                          seated man: his ring sits 54 units back and 273
	 *                          up, which is where a wheelchair puts a head.
	 *
	 *   Six more are the cutscene speakers, and their evidence is the
	 *   engine's own speech table (game\deltapak.cpp, handle_actor_chatting:
	 *   which slot and node speak in which cutscene) against the face files'
	 *   own lines, <FACE>_HEAD_<track>.TRL, where <track> is the audio track
	 *   the cutscene plays (cutseq.bin). They are keyed on the model, so a
	 *   level whose .MAP puts the same model in another slot comes along -
	 *   JOBY3 and JOBY5 bind ANIMATING6_MIP_JOBY2 to CRANE_GUY_MIP and
	 *   ANIMATING9_MIP_JOBY2 to LION_MIP.
	 *
	 * A ring match ALONE is not proof - ADMIRAL_HEAD's neck lands 1.3 units
	 * from a ring of ANIMATING10_TITLE, and the picture shows that model is
	 * the top-hatted mourner of the title graveyard, who has his own face and
	 * needs none - and 55.8 percent of its neck vertices lie within 10 units
	 * of PATRICK_2's collar, against 42.5 on its own body. So every entry
	 * here is confirmed by a picture as well.
	 *
	 * Left out on purpose: DEMON_HEAD on LARSON_MIP_ANDY3, although the two
	 * share pages 9113 and 9114 - those are the hussar's own face; ZIP_HEAD,
	 * which no cutscene body speaks with; and every game 4 face on a game 5
	 * body. */
	struct HeadOfModel
	{
		const char* model;
		const char* head;
		/* The node the face rides on, where the speech table names one the
		 * nearest-node rule (head_node_of) does not reach; -1 measures it. */
		int node;
		/* True for a face file that stands upside down against the level's
		 * own head mesh of that node: it is turned half a turn about the
		 * node's x axis, through the node's bind, before it is built. */
		bool halfTurnX;
	};

	const HeadOfModel g_head_of_model[] =
	{
		{ "ANIMATING4_RICHCUT2", "VON_CROY_SENIOR_HEAD", -1, false },
		{ "ANIMATING6_RICHCUT2", "VON_CROY_ELDER_HEAD", -1, false },

		/* The priest, SAILOR_MIP in ANDY1: he speaks on node 2 in cutscene
		 * 19 (priest_chat_ranges_andy2), whose track 119 is
		 * PATRICK_BROWN_HEAD_119.TRL. */
		{ "PATRICK_1", "PATRICK_BROWN_HEAD", -1, false },
		/* The priest, SAILOR_MIP in ANDY2 and ANDY3: node 2 in cutscenes 33,
		 * 35, 37 and 44 (priest_chat_ranges_andy6/7/9/11), whose tracks 101,
		 * 97, 134 and 135 are PATRICK_WHITE_HEAD's four .TRL. */
		{ "PATRICK_2", "PATRICK_WHITE_HEAD", -1, false },
		/* The admiral, JOBY2's ANIMATING6_MIP: node 2 in cutscene 25
		 * (admiral_chat_ranges_joby2) and, as CRANE_GUY_MIP, in 12, 13, 15
		 * and 18 - tracks 113, 122, 116, 115 and 114, ADMIRAL_HEAD's five
		 * .TRL. Body and face share pages 9090 and 9091. */
		{ "ANIMATING6_MIP_JOBY2", "ADMIRAL_HEAD", -1, false },
		/* Sergie, JOBY2's ANIMATING9_MIP: node 3 in cutscene 25
		 * (sergie_chat_ranges_joby2) and, as LION_MIP, in 15 and 32 - tracks
		 * 113, 115 and 120, SERGIE_HEAD's three .TRL. They share page 9093,
		 * and node 3 of the body carries no geometry: the face fills it. */
		{ "ANIMATING9_MIP_JOBY2", "SERGIE_HEAD", -1, false },
		/* The hanged man, ANDY1's SNIPER_MIP: node 21 in cutscene 21
		 * (hanged_man_chat_ranges_andy1), whose track 132 is
		 * HANGMAN_HEAD_132.TRL; they share page 9107. The face's centre
		 * stands 40 units from node 2 and 45 from node 21, so the node is the
		 * speech table's.
		 *
		 * And the file stands upside down. Laid over the level's own head
		 * mesh of node 21 - the mesh the speech table swaps - its vertices
		 * lie 37.4 units from that mesh on average as they stand, and 7.6
		 * once turned half a turn about the node's x axis, which is the best
		 * of every turn in 30-degree steps about all three axes. The four
		 * other speakers' faces land best as they stand (6.7 to 20.7). */
		{ "SNIPER_MIP_ANDY1", "HANGMAN_HEAD", 21, true },
		/* The demon knight, ANDY3's SWAT_MIP: node 15 in cutscenes 37 and 44
		 * (knight_chat_ranges_andy9/11), whose tracks 134 and 135 are
		 * DEMON_HEAD's two .TRL. The hussar beside him, LARSON_MIP_ANDY3, in
		 * the same uniform on the same pages 9097..9100, carries that face in
		 * his own body, on DEMON_HEAD's own pages 9113 and 9114. */
		{ "SWAT_MIP_ANDY3", "DEMON_HEAD", -1, false },
		/* Lara in the whole diving suit, actor 1 of JOBY3's cutscene 5 and
		 * of JOBY4's 40 (the hand table, hd_bind.cpp). The classic mesh of
		 * node 14 is her face inside the helmet, and JOBY4's cutscene puts
		 * her speech heads on that node (game\deltapak.cpp,
		 * handle_actor_chatting(LARA_SPEECH_HEAD1, 14, ...)); FULL's own
		 * node 14 is the empty helmet. Her face rides on it as on her own
		 * body: the actor's tree is hers, node 14 at the same bind
		 * (ModelProbe -slot -nodes), and LARA_HEAD_<track>.TRL exists for
		 * both cutscenes' tracks, 110 and 103. */
		{ "EXTRA5_SUBSUIT_FULL", "LARA_HEAD", 14, false },
	};

	bool same_name(const std::string& a, const char* b)
	{
		size_t i = 0;

		for (; i < a.size() && b[i]; i++)
			if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
				return false;

		return i == a.size() && !b[i];
	}

	/* A stem the strip rule stops at: one that ends in _MIP. */
	bool ends_in_mip(const std::string& stem)
	{
		return stem.size() > 4 && same_name(stem.substr(stem.size() - 4), "_MIP");
	}

	/* The face file for a body, and by which of the three rules. `how` is for
	 * the log and is never null on return; it says "name", "table" or "strip".
	 * `entry` is the table's line when the table answered, and null
	 * otherwise. Empty when the model has no face anywhere. */
	/* `bodyMod` is the mod the BODY came out of, or -1. The
	 * face is looked for in the same place and nowhere else: body and face
	 * share one page bias, so a face out of a different source than its body
	 * would ask the cache for its pages under the wrong namespace. Same mod,
	 * or no mod at all.
	 *
	 * The nowhere-else half matters as much as the same-mod half: without it
	 * a mod that ships FOO_HEAD.TRM and no FOO.TRM would put its face on the
	 * install's body under page bias 0, and the face's own pages would be
	 * read out of the install.
	 */
	std::string find_head_model(const std::string& model, bool fromTr3,
		HDModelOrigin* origin, const char** how, std::string* headName,
		const HeadOfModel** entry, int bodyMod = -1)
	{
		const int mods = bodyMod >= 0 ? bodyMod : kModsNone;

		*how = "name";
		*entry = 0;

		/* A face has no face of its own. Lara's wardrobe asks for the part
		 * LARA_HEAD by name, and without this line the strip rule below cuts
		 * "_HEAD" off it, finds LARA_HEAD again and hangs the face on itself -
		 * the same triangles twice, fighting over the depth buffer. */
		if (model.size() > 5 &&
			same_name(model.substr(model.size() - 5), "_HEAD"))
		{
			if (origin)
				*origin = HD_MODEL_ORIGIN_NONE;

			return std::string();
		}

		std::string path = find_model(model + "_HEAD", fromTr3, origin, 0, mods);

		if (!path.empty())
		{
			if (headName)
				*headName = model + "_HEAD";

			return path;
		}

		const int entries = (int)(sizeof(g_head_of_model) / sizeof(g_head_of_model[0]));

		for (int i = 0; i < entries; i++)
		{
			if (!same_name(model, g_head_of_model[i].model))
				continue;

			path = find_model(g_head_of_model[i].head, fromTr3, origin, 0, mods);

			if (!path.empty())
			{
				*how = "table";
				*entry = &g_head_of_model[i];

				if (headName)
					*headName = g_head_of_model[i].head;

				return path;
			}
		}

		std::string stem = model;

		/* Never through a _MIP: see the strip rule above. */
		while (stem.find('_') != std::string::npos && !ends_in_mip(stem))
		{
			stem.erase(stem.find_last_of('_'));

			if (ends_in_mip(stem))
				break;

			path = find_model(stem + "_HEAD", fromTr3, origin, 0, mods);

			if (!path.empty())
			{
				*how = "strip";

				if (headName)
					*headName = stem + "_HEAD";

				return path;
			}
		}

		if (origin)
			*origin = HD_MODEL_ORIGIN_NONE;

		return std::string();
	}

	void note(const char* format, ...)
	{
		char line[512];
		va_list args;

		va_start(args, format);
		std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		g_notes.push_back(line);
	}

	double now_ms()
	{
		LARGE_INTEGER frequency, counter;

		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);

		return frequency.QuadPart
			? 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart
			: 0.0;
	}

	/* ---------------------------------------------- the classic pose --
	 *
	 * A remaster inventory model is stored in the classic BIND POSE - node n's
	 * vertices carry the mesh tree's offsets and no rotation at all - and the
	 * game it came from puts the pose on when it draws. For a mod's IMI Uzis
	 * that is the difference between two guns standing side by side and
	 * PARALLEL, and the two of them CROSSED like an X, as the game the file
	 * came from shows them. hd_classic.h reads the frame; this puts it on the
	 * vertices.
	 *
	 * Before anything is measured out of them: build() takes the box and the
	 * principal axes in one pass over the vertices, and HDModelRingFit fits
	 * the SHAPE those axes describe - so a pose applied afterwards would be a
	 * pose the fit never saw. This runs where HDTrmFoldMirrored runs and for
	 * the same reason: what is changed here is what the card is given, so
	 * every number the log prints is about the model that is drawn.
	 *
	 * It is skinning and not a rigid turn, although every file met so far is
	 * rigid: a vertex names three nodes with three weights, and posing it on
	 * only the first would tear any file whose two parts share a seam. */
	int trm_nodes(const HDTrmModel& trm)
	{
		int highest = -1;

		for (int i = 0; i < trm.vertices; i++)
			for (int b = 0; b < 3; b++)
				if (trm.vertex[i].weight[b] && (int)trm.vertex[i].bone[b] > highest)
					highest = (int)trm.vertex[i].bone[b];

		return highest + 1;
	}

	void trm_box(const HDTrmModel& trm, float* size)
	{
		float mins[3] = { 0, 0, 0 }, maxs[3] = { 0, 0, 0 };

		for (int i = 0; i < trm.vertices; i++)
		{
			const float v[3] =
			{
				trm.vertex[i].x, trm.vertex[i].y, trm.vertex[i].z
			};

			for (int k = 0; k < 3; k++)
			{
				if (!i || v[k] < mins[k]) mins[k] = v[k];
				if (!i || v[k] > maxs[k]) maxs[k] = v[k];
			}
		}

		for (int k = 0; k < 3; k++)
			size[k] = maxs[k] - mins[k];
	}

	// The .TRM's normals are BIASED BYTES - (n - 127) / 128, see hd_trm.h -
	// and a turned model needs turned normals, so they go back the same way.
	unsigned char pack_normal(float v)
	{
		const float scaled = v * 128.0f + 127.0f;

		return (unsigned char)(scaled < 0.0f ? 0 : scaled > 255.0f ? 255
			: (int)(scaled + 0.5f));
	}

	bool pose_classic(HDTrmModel& trm, const HDClassicObject& object)
	{
		float matrix[HD_CLASSIC_NODES][16];

		for (int n = 0; n < object.meshes; n++)
			if (!HDClassicNodeMatrix(&object, n, matrix[n]))
				return false;

		for (int i = 0; i < trm.vertices; i++)
		{
			HDTrmVertex& v = trm.vertex[i];
			const float in[3] = { v.x, v.y, v.z };
			const float normal[3] =
			{
				((float)v.nx - 127.0f) / 128.0f,
				((float)v.ny - 127.0f) / 128.0f,
				((float)v.nz - 127.0f) / 128.0f
			};
			float out[3] = { 0, 0, 0 }, turned[3] = { 0, 0, 0 };
			int share = 0;

			for (int b = 0; b < 3; b++)
			{
				const int node = (int)v.bone[b];
				const int weight = (int)v.weight[b];

				if (!weight || node >= object.meshes)
					continue;

				const float* m = matrix[node];
				const float part = (float)weight / 255.0f;

				share += weight;

				for (int k = 0; k < 3; k++)
				{
					out[k] += part * (in[0] * m[0 * 4 + k] + in[1] * m[1 * 4 + k] +
						in[2] * m[2 * 4 + k] + m[3 * 4 + k]);
					turned[k] += part * (normal[0] * m[0 * 4 + k] +
						normal[1] * m[1 * 4 + k] + normal[2] * m[2 * 4 + k]);
				}
			}

			/* A vertex the pose cannot reach stays where it is. It names only
			 * nodes the classic object has not got, which the mesh-count guard
			 * rules out for the files this runs on - but a vertex quietly
			 * collapsed onto the origin would be a hole in the model that is
			 * hard to trace back to here. */
			if (!share)
				continue;

			v.x = out[0];
			v.y = out[1];
			v.z = out[2];

			const float length = std::sqrt(turned[0] * turned[0] +
				turned[1] * turned[1] + turned[2] * turned[2]);

			if (length > 1e-6f)
			{
				v.nx = pack_normal(turned[0] / length);
				v.ny = pack_normal(turned[1] / length);
				v.nz = pack_normal(turned[2] / length);
			}
		}

		return true;
	}

	/* A shader constant register holds a COLUMN (fxc packs column major by
	 * default and the generated listing confirms it: dp4 o.x, position, c0),
	 * while a D3DMATRIX holds rows. The transpose goes to the device - the
	 * single most common way to get a black screen out of an otherwise
	 * correct Direct3D 9 shader path. */
	void set_matrix_constant(int reg, const float* rowMajor)
	{
		float columns[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF((UINT)reg, columns, 4);
	}

	/* --------------------------------------------------------- building -- */

	/* The clip given back and the buffer with it; the face's own vertices
	 * stay for the next cut. */
	void lips_close(Lips* lips)
	{
		if (!lips)
			return;

		HDTrlClose(&lips->clip);

		if (lips->vb) { lips->vb->Release(); lips->vb = 0; }

		lips->track = -1;
		lips->missing = -1;
		lips->shownFrame = -1;
		lips->shownSource = HD_LIPS_SOURCE_BIND;
		std::vector<float>().swap(lips->frame);
		std::vector<double>().swap(lips->skins);
		std::vector<float>().swap(lips->outPosition);
		std::vector<unsigned char>().swap(lips->outNormal);
		std::vector<HDModelVertex>().swap(lips->out);
		std::vector<float>().swap(lips->fire);
		std::vector<double>().swap(lips->fromSkins);
		std::vector<double>().swap(lips->toSkins);
	}

	void free_model(Model& model)
	{
		if (model.vb) { model.vb->Release(); model.vb = 0; }
		if (model.ib) { model.ib->Release(); model.ib = 0; }

		model.pass[0].clear();
		model.pass[1].clear();
		model.pass[2].clear();
		model.pages.clear();
		model.points.clear();

		if (model.lips)
		{
			lips_close(model.lips);
			delete model.lips;
			model.lips = 0;
		}
	}

	/* The three index runs of one part, in pass order. Measured over all 566
	 * models of game 5: opaqueEnd == start + opaque and alphaEnd == opaqueEnd
	 * + alpha in every part of every file, so the three runs tile the part
	 * back to front without a gap. They are read as they stand all the same,
	 * so a file whose runs did not tile would still draw each run as its
	 * own counts say. */
	void part_runs(const HDTrmPart& part, unsigned int start[3], unsigned int count[3])
	{
		start[0] = part.start;
		count[0] = part.opaque;
		start[1] = part.opaqueEnd;
		count[1] = part.alpha;
		start[2] = part.alphaEnd;
		count[2] = part.additive;
	}

	/* The glass's sides, its copies and its opacity, per triangle
	 * (hd_object_rules.h: HDTrmTrianglesFaceAgainst, HDTrmTriangleCopies,
	 * HDGlassOpacity), on the file's own bind pose: `sided` 1 where a
	 * glass triangle has a glass triangle of its node facing against it,
	 * `copy` 1 where an additive run triangle copies a glass triangle of
	 * its node and page, `opacity` the glass triangle's part's. A model
	 * with no glass material alpha run leaves after the first loop. */
	void glass_sides_and_copies(const HDTrmModel& trm,
		const std::vector<int>& page, const std::vector<int>& node,
		std::vector<unsigned char>& sided, std::vector<unsigned char>& copy,
		std::vector<float>& opacity)
	{
		std::vector<int> glass;		// the glass's triangle numbers
		std::vector<int> added;		// the additive run's

		for (int p = 0; p < trm.parts; p++)
		{
			unsigned int start[3], count[3];

			part_runs(trm.part[p], start, count);

			const bool glassMaterial =
				trm.part[p].material == HD_TRM_MATERIAL_GLASS;
			const float weight =
				HDGlassOpacity(trm.part[p].param[HD_TRM_PARAM_OPACITY]);

			for (int run = HD_TRM_RUN_ALPHA; run <= HD_TRM_RUN_ADDITIVE; run++)
			{
				if (run == HD_TRM_RUN_ALPHA && !glassMaterial)
					continue;

				for (unsigned int i = 0; i + 2 < count[run]; i += 3)
				{
					const unsigned int at = start[run] + i;

					if (at + 2 >= (unsigned int)trm.indices)
						continue;

					const int t = (int)(at / 3);

					if (run == HD_TRM_RUN_ALPHA)
					{
						glass.push_back(t);
						opacity[(size_t)t] = weight;
					}
					else
						added.push_back(t);
				}
			}
		}

		if (glass.empty())
			return;

		// The corners of triangle t, their texture coordinates and normal.
		auto corners = [&trm](int t, float* at, unsigned char* uv)
		{
			for (int k = 0; k < 3; k++)
			{
				const HDTrmVertex& v = trm.vertex[trm.index[t * 3 + k]];

				at[k * 3 + 0] = v.x;
				at[k * 3 + 1] = v.y;
				at[k * 3 + 2] = v.z;
				uv[k * 2 + 0] = v.u;
				uv[k * 2 + 1] = v.v;
			}
		};

		auto normal = [](const float* at, float* n)
		{
			const float e[3] = { at[3] - at[0], at[4] - at[1], at[5] - at[2] };
			const float f[3] = { at[6] - at[0], at[7] - at[1], at[8] - at[2] };

			n[0] = e[1] * f[2] - e[2] * f[1];
			n[1] = e[2] * f[0] - e[0] * f[2];
			n[2] = e[0] * f[1] - e[1] * f[0];
		};

		std::vector<float> place(glass.size() * 9), facing(glass.size() * 3);
		std::vector<unsigned char> uvs(glass.size() * 6);

		for (size_t g = 0; g < glass.size(); g++)
		{
			corners(glass[g], &place[g * 9], &uvs[g * 6]);
			normal(&place[g * 9], &facing[g * 3]);
		}

		for (size_t g = 0; g < glass.size(); g++)
		{
			unsigned char& mark = sided[(size_t)glass[g]];

			for (size_t h = 0; h < glass.size() && !mark; h++)
				if (node[(size_t)glass[g]] == node[(size_t)glass[h]] &&
					HDTrmTrianglesFaceAgainst(&facing[g * 3], &facing[h * 3]))
					mark = 1;
		}

		for (const int a : added)
		{
			float at[9];
			unsigned char uv[6];

			corners(a, at, uv);

			for (size_t g = 0; g < glass.size() && !copy[(size_t)a]; g++)
				if (node[(size_t)a] == node[(size_t)glass[g]] &&
					page[(size_t)a] == page[(size_t)glass[g]] &&
					HDTrmTriangleCopies(&place[g * 9], &uvs[g * 6], at, uv))
					copy[(size_t)a] = 1;
		}
	}

	// How many of a model's opaque ranges the alpha test runs on - for the
	// level's log line.
	int cut_ranges(const Model& model)
	{
		int n = 0;

		for (size_t i = 0; i < model.pass[0].size(); i++)
			n += model.pass[0][i].cut ? 1 : 0;

		return n;
	}

	/* Builds one model's buffers out of a .TRM. `rigidNode` >= 0 makes every
	 * vertex hang off that one node - a facial-rig head, whose own bone
	 * numbers belong to the rig and not to the mesh tree.
	 *
	 * The index list is REBUILT rather than uploaded as it stands: the
	 * triangles of a pass are bucketed by page, so a draw call is one page
	 * and one SetTexture. That is the same road hd_render takes for a room,
	 * and the reason is the same - Direct3D 9 has no texture arrays. */
	/* `pageBias` is what turns this model's raw page numbers into cache keys:
	 * 0 for everything in this bundle, HD_PAGE_ALT_BASE for a model out of
	 * the Tomb Raider I-III one and the mod's own namespace for a model a mod
	 * supplied - the .TRM's numbers are its own game's, and the cache holds
	 * all of them at once.
	 *
	 * It is offered rather than applied: HDPageCacheKeyOf takes the biased
	 * number back down to the plain one where the two would open the same
	 * file, so a mod that ships a model and no art of its own pays for that
	 * art once. */
	/* Which pages of a file are built - a set, kept or dropped.
	 *
	 * One use cuts ONE page out of a bigger model - the holster rig out of the
	 * London catsuit. Another needs the other polarity and more than one page
	 * at a time: a mod's hand keeps its gun and DROPS the pages that resolve
	 * into another bundle, and the install's own file of the same name KEEPS
	 * only the pages that are its skin. One mechanism does both, so the two
	 * cannot disagree about what a page number means.
	 *
	 * The numbers are the FILE'S own, as the .TRM writes them, and never cache
	 * keys - see HDModelSlot::cutPage. */
	struct PageCut
	{
		const int* page;
		int count;
		bool keeps;

		bool wants(int rawPage) const
		{
			if (count <= 0 || !page)
				return true;

			bool found = false;

			for (int i = 0; i < count; i++)
				if (page[i] == rawPage)
				{
					found = true;
					break;
				}

			return found == keeps;
		}
	};

	/* The same thing by node - see HDModelSlot::onlyNode. A mask
	 * of 0 is the whole model and costs one test per triangle. */
	struct NodeCut
	{
		unsigned int nodes;

		bool wants(int node) const
		{
			return !nodes || (nodes & (1u << (node & 31))) != 0;
		}
	};

	/* Which node a triangle belongs to: the node that carries the most weight
	 * over its three corners. A triangle that straddles a joint has to fall on
	 * one side of it, and the heavier side is the side it moves with.
	 *
	 * The rigid case reads the vertex as it was WRITTEN, not as it arrived: a
	 * facial-rig file's neck bone lands on the head node's parent, and a neck
	 * triangle belongs to that node when mesh_bits hides one of the two.
	 *
	 * Used by the triangle loop, the vertex keep pass and the page list,
	 * because a NODE cut has to drop exactly the triangles a node mask would
	 * have hidden. One function, so the cut and the ranges cannot disagree
	 * about which node a triangle is on. */
	int dominant_node_of(const HDTrmModel& trm, int at, int rigidNode,
		int neckBone, int neckNode)
	{
		double weight[kJointBudget] = { 0 };
		int best = 0;
		double heaviest = -1;

		for (int corner = 0; corner < 3; corner++)
		{
			const HDTrmVertex& v = trm.vertex[trm.index[at + corner]];

			for (int b = 0; b < 3; b++)
			{
				const int node = rigidNode >= 0
					? (neckBone >= 0 && (int)v.bone[b] == neckBone
						? neckNode : rigidNode)
					: (int)(v.bone[b] < kJointBudget ? v.bone[b] : kJointBudget - 1);
				const int w = rigidNode >= 0
					? (neckBone >= 0 ? (int)v.weight[b] : (b == 0 ? 255 : 0))
					: (int)v.weight[b];

				weight[node] += (double)w;
			}
		}

		for (int node = 0; node < kJointBudget; node++)
			if (weight[node] > heaviest)
			{
				heaviest = weight[node];
				best = node;
			}

		return best;
	}

	/* The axes of one set of extents, longest first. One function so that the
	 * ring's fit, its overflow figure and the cloud's own axes in build()
	 * cannot disagree about which axis is which - the rule is "k-th longest to
	 * k-th longest". */
	void sort_axes(const float* size, int* order)
	{
		order[0] = 0;
		order[1] = 1;
		order[2] = 2;

		for (int a = 0; a < 2; a++)
			for (int b = a + 1; b < 3; b++)
				if (size[order[b]] > size[order[a]])
				{
					const int keep = order[a];
					order[a] = order[b];
					order[b] = keep;
				}
	}

	/* `base`, where not null, is handed the vertices as they went into the
	 * buffer - a speaking face's deformed copy is made out of them (Lips). */
	bool build(const HDTrmModel& trm, Model& model, int rigidNode,
		int pageBias, int* mixedTriangles, const char** why, const PageCut& cut,
		const NodeCut& nodeCut, std::vector<HDModelVertex>* base = 0)
	{
		const int vertices = trm.vertices;

		std::vector<HDModelVertex> buffer((size_t)vertices);

		/* The page cut and the node cut. With a cut in hand, a vertex no kept
		 * triangle names takes no part in ANYTHING the loop below measures -
		 * not the bounds, not the node coverage, not the bind drift. Otherwise
		 * a holster cut out of an outfit would report that it covers all
		 * fifteen nodes and reaches from her hair to her boots, and the
		 * composite would hand it the whole body's work. The vertex itself
		 * stays in the buffer: the index list is the file's own, and
		 * renumbering it would save 400 KiB at the cost of complexity. */
		const bool whole = (cut.count <= 0 || !cut.page) && !nodeCut.nodes;
		std::vector<bool> keep((size_t)vertices, whole);

		// One allocation: the cut can only take vertices away, so the file's
		// own count is the upper bound.
		if (model.keepPoints)
		{
			model.points.reserve((size_t)vertices * 3);
			model.pointNode.reserve((size_t)vertices);
		}

		if (!whole)
			for (int i = 0; i + 2 < trm.indices; i += 3)
			{
				const int page = HDTrmPageOf(&trm, &trm.vertex[trm.index[i]]);

				if (!cut.wants(page))
					continue;

				if (!nodeCut.wants(dominant_node_of(trm, i, rigidNode,
					model.neckBone, model.neckNode)))
					continue;

				for (int corner = 0; corner < 3; corner++)
					keep[(size_t)trm.index[i + corner]] = true;
			}

		// The bind drift's working set: how many single-weight vertices a
		// node carries and where their centre is.
		int single[kJointBudget] = { 0 };
		double centre[kJointBudget][3] = { { 0, 0, 0 } };
		double square[kJointBudget] = { 0 };

		// The cloud's first and second moments, for the axes above.
		double cloudN = 0;
		double cloudSum[3] = { 0, 0, 0 };
		double cloudSq[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };

		for (int i = 0; i < vertices; i++)
		{
			const HDTrmVertex& in = trm.vertex[i];
			HDModelVertex& out = buffer[(size_t)i];

			out.x = in.x;
			out.y = in.y;
			out.z = in.z;
			out.nx = in.nx;
			out.ny = in.ny;
			out.nz = in.nz;
			out.layer = in.layer;

			if (rigidNode >= 0)
			{
				/* The face's neck goes on the parent node. Everything else
				 * of a facial-rig file hangs off the head node; the one bone
				 * that reaches down into the collar hangs off the node the
				 * head node hangs off - the torso. The weights are kept as
				 * the file has them, so the seam between the two BLENDS
				 * instead of tearing. neck_bone_of says which bone that is
				 * and how it was measured for a creature's face, rig_neck_of
				 * for a face whose slot names its neck node, which is Lara's. */
				const int b0 = model.neckBone >= 0 && (int)in.bone[0] == model.neckBone
					? model.neckNode : rigidNode;
				const int b1 = model.neckBone >= 0 && (int)in.bone[1] == model.neckBone
					? model.neckNode : rigidNode;
				const int b2 = model.neckBone >= 0 && (int)in.bone[2] == model.neckBone
					? model.neckNode : rigidNode;

				out.bone0 = (unsigned char)b0;
				out.bone1 = (unsigned char)b1;
				out.bone2 = (unsigned char)b2;

				if (model.neckBone < 0 || (b0 == b1 && b1 == b2))
				{
					out.weight0 = 255;
					out.weight1 = 0;
					out.weight2 = 0;
				}
				else
				{
					out.weight0 = in.weight[0];
					out.weight1 = in.weight[1];
					out.weight2 = in.weight[2];
				}
			}
			else
			{
				/* The palette is 32 and the highest weighted bone over all
				 * of game 5 is 28, so this clamp never fires on the
				 * remaster's own files. It guards a hand-made .TRM. */
				out.bone0 = (unsigned char)(in.bone[0] < kJointBudget ? in.bone[0] : kJointBudget - 1);
				out.bone1 = (unsigned char)(in.bone[1] < kJointBudget ? in.bone[1] : kJointBudget - 1);
				out.bone2 = (unsigned char)(in.bone[2] < kJointBudget ? in.bone[2] : kJointBudget - 1);
				out.weight0 = in.weight[0];
				out.weight1 = in.weight[1];
				out.weight2 = in.weight[2];
			}

			out.u = in.u;
			out.v = in.v;

			if (!keep[(size_t)i])
				continue;

			{
				const double v[3] = { (double)in.x, (double)in.y, (double)in.z };

				cloudN += 1.0;

				for (int a = 0; a < 3; a++)
				{
					cloudSum[a] += v[a];

					for (int b = 0; b < 3; b++)
						cloudSq[a][b] += v[a] * v[b];
				}
			}

			/* The position itself, for a model that asked. Here and not in a
			 * second walk of the file: this is the one loop that has the kept
			 * vertices in hand, and the floor rule has to see exactly the
			 * vertices the box below is taken over or the two numbers it
			 * compares are not about the same model. */
			if (model.keepPoints)
			{
				model.points.push_back(in.x);
				model.points.push_back(in.y);
				model.points.push_back(in.z);
				/* And the node it rides on, out of the bones this loop has
				 * just written - `out` and not `in`, because a rigid face's
				 * neck rule has already moved a bone of the file onto
				 * another node of the object above. */
				model.pointNode.push_back(out.bone0);

				if (out.weight0 != 255)
					model.pointsBlended++;
			}

			if (model.vertexSeen++ == 0)
			{
				model.mins[0] = model.maxs[0] = in.x;
				model.mins[1] = model.maxs[1] = in.y;
				model.mins[2] = model.maxs[2] = in.z;
			}
			else
			{
				if (in.x < model.mins[0]) model.mins[0] = in.x;
				if (in.y < model.mins[1]) model.mins[1] = in.y;
				if (in.z < model.mins[2]) model.mins[2] = in.z;
				if (in.x > model.maxs[0]) model.maxs[0] = in.x;
				if (in.y > model.maxs[1]) model.maxs[1] = in.y;
				if (in.z > model.maxs[2]) model.maxs[2] = in.z;
			}

			/* The node coverage, which is the whole of the mapping question:
			 * a bone index IS a classic mesh number, so what a slot needs is
			 * not a permutation but the list of nodes the model has geometry
			 * for. A weight of zero does not count - a vertex names three
			 * bones whatever it uses. */
			for (int b = 0; b < 3; b++)
			{
				const int node = rigidNode >= 0
					? (b == 0 ? (int)out.bone0 : b == 1 ? (int)out.bone1 : (int)out.bone2)
					: (int)in.bone[b];
				const int weight = rigidNode >= 0
					? (b == 0 ? (int)out.weight0 : b == 1 ? (int)out.weight1 : (int)out.weight2)
					: (int)in.weight[b];

				if (weight && node < kJointBudget)
				{
					model.nodeVertices[node]++;
					model.covered |= 1u << node;
				}

				/* The highest node the model names, before the clamp above
				 * takes it down to the palette. This is the number that says
				 * "this .TRM was drawn for a different mesh tree", and it has
				 * to be the raw one - a clamped 31 would look like a
				 * legitimate deep node. */
				if (weight && rigidNode < 0 && (int)in.bone[b] > model.maxBone)
					model.maxBone = (int)in.bone[b];
			}

			/* The bind drift. Only the vertices that ride on ONE node count:
			 * a blended vertex sits between two nodes on purpose and would
			 * make every elbow look like a mismatch. */
			if (rigidNode < 0 && in.weight[0] == 255)
			{
				const int node = out.bone0;

				single[node]++;
				centre[node][0] += (double)in.x;
				centre[node][1] += (double)in.y;
				centre[node][2] += (double)in.z;
				square[node] += (double)in.x * in.x + (double)in.y * in.y +
					(double)in.z * in.z;
			}
		}

		/* The cloud's own axes, out of the moments gathered above. The
		 * covariance of the drawn vertices, diagonalised by Jacobi
		 * rotations - three of them are enough for a 3x3 to machine precision and
		 * eight are taken, so a degenerate cloud costs a few microseconds and not
		 * a wrong answer.
		 *
		 * The EXTENT along each axis is the cloud's real span on it and not the
		 * eigenvalue: a fit has to know how long the thing is, and a standard
		 * deviation is not that. It needs the vertices a second time, which is why
		 * this sits inside build() where they are still in hand. */
		if (cloudN >= 3.0)
		{
			double cov[3][3];
			double vec[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

			for (int a = 0; a < 3; a++)
				for (int b = 0; b < 3; b++)
					cov[a][b] = cloudSq[a][b] / cloudN -
						(cloudSum[a] / cloudN) * (cloudSum[b] / cloudN);

			for (int sweep = 0; sweep < 8; sweep++)
				for (int a = 0; a < 2; a++)
					for (int b = a + 1; b < 3; b++)
					{
						if (cov[a][b] == 0.0)
							continue;

						const double theta =
							(cov[b][b] - cov[a][a]) / (2.0 * cov[a][b]);
						const double t = (theta >= 0.0 ? 1.0 : -1.0) /
							(fabs(theta) + sqrt(theta * theta + 1.0));
						const double c = 1.0 / sqrt(t * t + 1.0);
						const double sn = t * c;

						for (int k = 0; k < 3; k++)
						{
							const double ka = cov[k][a], kb = cov[k][b];

							cov[k][a] = c * ka - sn * kb;
							cov[k][b] = sn * ka + c * kb;
						}

						for (int k = 0; k < 3; k++)
						{
							const double ak = cov[a][k], bk = cov[b][k];

							cov[a][k] = c * ak - sn * bk;
							cov[b][k] = sn * ak + c * bk;

							const double av = vec[k][a], bv = vec[k][b];

							vec[k][a] = c * av - sn * bv;
							vec[k][b] = sn * av + c * bv;
						}
					}

			// The cloud's real span on each axis, and then longest first.
			double lo[3], hi[3];
			bool first = true;

			for (int i = 0; i < vertices; i++)
			{
				if (!keep[(size_t)i])
					continue;

				const HDTrmVertex& v = trm.vertex[i];

				for (int a = 0; a < 3; a++)
				{
					const double d = v.x * vec[0][a] + v.y * vec[1][a] +
						v.z * vec[2][a];

					if (first || d < lo[a]) lo[a] = d;
					if (first || d > hi[a]) hi[a] = d;
				}

				first = false;
			}

			if (!first)
			{
				float span[3];
				int order[3];

				for (int a = 0; a < 3; a++)
					span[a] = (float)(hi[a] - lo[a]);

				sort_axes(span, order);

				/* An eigenvector has no sign, and a gun has two ends. Without a
				 * rule for it the same file could come out muzzle-first or
				 * grip-first from one build to the next, and a fit that pairs two
				 * models' axes would pair them back to front half the time.
				 *
				 * The rule is the model's own geometry and nothing else: the axis
				 * points to the side on which the cloud reaches FURTHER from its
				 * own centre of mass. A gun's barrel is the long end, a magazine
				 * box's mouth the short one - and a shape that is symmetric about
				 * that axis has no long end, gets an arbitrary sign, and looks the
				 * same either way, which is why it does not matter there. */
				for (int k = 0; k < 3; k++)
				{
					const int a = order[k];
					const double mean = (cloudSum[0] / cloudN) * vec[0][a] +
						(cloudSum[1] / cloudN) * vec[1][a] +
						(cloudSum[2] / cloudN) * vec[2][a];
					const float sign = (hi[a] - mean) >= (mean - lo[a]) ? 1.0f : -1.0f;

					model.axisExtent[k] = span[a];

					for (int r = 0; r < 3; r++)
						model.axis[k][r] = sign * (float)vec[r][a];
				}

				/* The third axis is the cross product of the other two, rather
				 * than kept from the solver, so the three are right handed BY
				 * CONSTRUCTION: a frame built from them can rotate a model and can
				 * never mirror it. The rule above has already settled the two that
				 * carry the shape. */
				model.axis[2][0] = model.axis[0][1] * model.axis[1][2] -
					model.axis[0][2] * model.axis[1][1];
				model.axis[2][1] = model.axis[0][2] * model.axis[1][0] -
					model.axis[0][0] * model.axis[1][2];
				model.axis[2][2] = model.axis[0][0] * model.axis[1][1] -
					model.axis[0][1] * model.axis[1][0];

				model.hasAxes = true;
			}
		}

		/* The single-weight centres per node, for the skeleton fit: the
		 * same working set the drift below reads, kept with the model. */
		for (int node = 0; node < kJointBudget; node++)
		{
			model.nodeSingle[node] = single[node];

			if (single[node])
				for (int k = 0; k < 3; k++)
					model.nodeCentre[node][k] =
						(float)(centre[node][k] / single[node]);
		}

		/* The bind drift, between pairs of nodes and not against the origin.
		 *
		 * How far a node's own vertices sit from that node in the classic bind
		 * pose says nothing for anything that is not a creature: a door is one
		 * node whose mesh hangs 750 units off its hinge, which would look like
		 * the worst mismatch in a VANILLA level. That number measures where a
		 * modeller put the origin, which is the model's business and not the
		 * skeleton's.
		 *
		 * The skeleton question asks whether the DISTANCES
		 * between the nodes agree. So: for every pair of nodes that both carry
		 * geometry, the distance between their vertex centres against the
		 * distance between their bind positions. Both sides then carry each
		 * mesh's own centring bias and it cancels in the difference; a whole
		 * model that sits off its origin cancels completely, because a
		 * translation changes no distance at all.
		 *
		 * A model with one node has no pair and no answer - which is right:
		 * any one-node .TRM fits any one-node slot, there is nothing for the
		 * skinning to get wrong. */
		if (model.hasBind)
		{
			double centres[kJointBudget][3];
			double spread[kJointBudget];
			int have[kJointBudget];
			int nodes = 0;

			for (int node = 0; node < model.nodes && node < kJointBudget; node++)
			{
				if (single[node] < 16)
					continue;

				have[nodes] = node;
				centres[nodes][0] = centre[node][0] / single[node];
				centres[nodes][1] = centre[node][1] / single[node];
				centres[nodes][2] = centre[node][2] / single[node];

				/* How big the mesh itself is, as the root mean square
				 * distance of its vertices from their own centre. E[r^2] =
				 * E[|v|^2] - |c|^2, so the sum of squares gathered above is
				 * all it takes. */
				const double mean = square[node] / single[node];
				const double length = centres[nodes][0] * centres[nodes][0] +
					centres[nodes][1] * centres[nodes][1] +
					centres[nodes][2] * centres[nodes][2];

				spread[nodes] = std::sqrt(mean - length > 0.0 ? mean - length : 0.0);
				nodes++;
			}

			double sum = 0;
			int pairs = 0;

			for (int a = 0; a < nodes; a++)
				for (int b = a + 1; b < nodes; b++)
				{
					const double mx = centres[a][0] - centres[b][0];
					const double my = centres[a][1] - centres[b][1];
					const double mz = centres[a][2] - centres[b][2];

					const double bx = model.bind[have[a]][0] - model.bind[have[b]][0];
					const double by = model.bind[have[a]][1] - model.bind[have[b]][1];
					const double bz = model.bind[have[a]][2] - model.bind[have[b]][2];

					/* The meshes' own size comes off. A centre is not a
					 * joint: a long bone's vertices average out at the
					 * middle of the bone, so two long meshes disagree with
					 * their joints by half their own length, and a hammer
					 * trap would look like a mismatch in VANILLA. What is left
					 * after the two spreads are taken off is what neither
					 * mesh can explain by its own shape - and that is the
					 * skeleton. */
					double d = std::fabs(
						std::sqrt(mx * mx + my * my + mz * mz) -
						std::sqrt(bx * bx + by * by + bz * bz))
						- spread[a] - spread[b];

					if (d < 0)
						d = 0;

					sum += d;
					pairs++;

					if ((float)d > model.driftWorst)
						model.driftWorst = (float)d;
				}

			if (pairs)
			{
				model.driftMean = (float)(sum / pairs);
				model.hasDrift = true;
			}
		}

		/* The pages, in the order the model names them, and the page of every
		 * triangle. The layer is 1-based; layer 0 or a layer past the texture
		 * list would be a page nobody loaded, and over all 566 models neither
		 * ever happens. */
		std::vector<int> page_of_triangle;
		std::vector<int> node_of_triangle;

		page_of_triangle.reserve((size_t)(trm.indices / 3));
		node_of_triangle.reserve((size_t)(trm.indices / 3));

		/* The cache key of every page this model names, worked out once.
		 *
		 * A vertex names a page by its LAYER, an index into the model's own
		 * short texture list - eight entries at the outside - so there are
		 * exactly `trm.textures` different answers, however many triangles
		 * wear them. Asked per triangle CORNER, HDPageCacheKeyOf opens files
		 * for a model a mod supplied: 30.8 microseconds a call, which takes
		 * ANDREA1's model build from 672 ms to 25,656.
		 *
		 * HDTrmPageOf IS texture[layer-1], so a corner's key is this table at
		 * its layer, value for value. */
		std::vector<int> key_of_layer((size_t)(trm.textures > 0 ? trm.textures : 0));

		{
			const double keys_began = now_ms();

			for (int t = 0; t < trm.textures; t++)
				key_of_layer[(size_t)t] =
					HDPageCacheKeyOf((int)trm.texture[t] + pageBias);

			g_key_ms += now_ms() - keys_began;
		}

		for (int i = 0; i + 2 < trm.indices; i += 3)
		{
			const int layerA = (int)trm.vertex[trm.index[i + 0]].layer;
			const int layerB = (int)trm.vertex[trm.index[i + 1]].layer;
			const int layerC = (int)trm.vertex[trm.index[i + 2]].layer;

			const bool okA = layerA >= 1 && layerA <= trm.textures;

			// The raw page is still wanted for the page CUT below, which is
			// about the FILE's numbering and not the cache's.
			const int rawA = okA ? (int)trm.texture[layerA - 1] : -1;

			const int a = okA ? key_of_layer[(size_t)(layerA - 1)] : -1;
			const int b = layerB >= 1 && layerB <= trm.textures
				? key_of_layer[(size_t)(layerB - 1)] : -1;
			const int c = layerC >= 1 && layerC <= trm.textures
				? key_of_layer[(size_t)(layerC - 1)] : -1;

			if (mixedTriangles && (a != b || b != c))
				(*mixedTriangles)++;

			page_of_triangle.push_back(a);

			/* Which node a triangle belongs to - dominant_node_of above,
			 * worked out for every triangle whether or not anything cuts by
			 * it. Only used to hide a node, so a wrong answer at a seam costs
			 * at most one triangle at the cut edge of a blown-off limb. */
			const int best = dominant_node_of(trm, i, rigidNode, model.neckBone,
				model.neckNode);

			// The page list is the pages of the triangles that SURVIVE, so a
			// node cut pays for its own pages and not for the file's.
			if (a >= 0 && cut.wants(rawA) && nodeCut.wants(best) &&
				std::find(model.pages.begin(), model.pages.end(), a) == model.pages.end())
				model.pages.push_back(a);

			node_of_triangle.push_back(best);
		}

		/* The index list, bucketed by (pass, page). std::stable_sort keeps the
		 * order inside a page, so a model's triangles come off the card in
		 * the order the remaster wrote them - which matters for the additive
		 * pass, where the draw order is the blend order. */
		std::vector<unsigned short> indices;

		indices.reserve((size_t)trm.indices);

		/* One byte per triangle: 1 where the opaque run draws it with the
		 * alpha test (hd_model_cut.h). Filled for the opaque pass only - the
		 * alpha pass tests everything anyway and the additive one nothing -
		 * and before the sort, which keeps the cut triangles together. The
		 * pages are in the cache by now: HDModelLevelBegin appends them
		 * before it builds a single model. */
		std::vector<unsigned char> cut_of_triangle(page_of_triangle.size(), 0);

		/* One byte per triangle: 1 where the alpha run's part wears the
		 * glass material (hd_object_rules.h). Which of them are drawn as
		 * glass is the level's to say (HDModelSetGlassNodes); the build
		 * only keeps them apart from the others in their own ranges. */
		std::vector<unsigned char> glass_of_triangle(page_of_triangle.size(),
			0);

		/* The glass's sides, its copies in the additive run and its
		 * opacity, per triangle (glass_sides_and_copies). A copy and an
		 * opacity keep their triangles in ranges of their own, the sides
		 * are a range's own when all its triangles have them; a model
		 * without glass has all three at 0, 0 and 1, and its ranges come
		 * out as before. */
		std::vector<unsigned char> sided_of_triangle(page_of_triangle.size(),
			0);
		std::vector<unsigned char> copy_of_triangle(page_of_triangle.size(),
			0);
		std::vector<float> opacity_of_triangle(page_of_triangle.size(),
			1.0f);

		glass_sides_and_copies(trm, page_of_triangle, node_of_triangle,
			sided_of_triangle, copy_of_triangle, opacity_of_triangle);

		for (int pass = 0; pass < 3; pass++)
		{
			std::vector<int> triangles;	// triangle numbers of this pass

			for (int p = 0; p < trm.parts; p++)
			{
				unsigned int start[3], count[3];

				part_runs(trm.part[p], start, count);

				const bool cutMaterial = pass == 0 &&
					HDTrmMaterialCuts(trm.part[p].material);
				const bool glassMaterial = pass == HD_TRM_RUN_ALPHA &&
					trm.part[p].material == HD_TRM_MATERIAL_GLASS;

				for (unsigned int i = 0; i + 2 < count[pass]; i += 3)
				{
					const unsigned int at = start[pass] + i;

					if (at + 2 >= (unsigned int)trm.indices)
						continue;

					// The page cut.
					if (!cut.wants(HDTrmPageOf(&trm,
						&trm.vertex[trm.index[at]])))
						continue;

					// And the node cut, out of the answer the loop above has
					// already worked out for this triangle.
					if (!nodeCut.wants(node_of_triangle[(size_t)(at / 3)]))
						continue;

					if (cutMaterial)
					{
						const int key = page_of_triangle[(size_t)(at / 3)];

						cut_of_triangle[(size_t)(at / 3)] = key >= 0 &&
							HDPageCacheSlotHoles(HDPageCacheSlotOf(key)) ? 1 : 0;
					}

					if (glassMaterial)
						glass_of_triangle[(size_t)(at / 3)] = 1;

					triangles.push_back((int)(at / 3));
				}
			}

			if (triangles.empty())
				continue;

			/* (page, node), page first: a page change costs a texture bind
			 * and a node change costs nothing at all once the two runs are
			 * merged again at draw time. The additive pass is sorted the same
			 * way, which changes the order its triangles reach the card in -
			 * and cannot change the picture, because that pass blends ONE to
			 * ONE with the depth write off and addition does not care in
			 * which order it is done.
			 *
			 * The cut comes before both, so that the triangles the alpha test
			 * runs on are one run after the others and the test is switched
			 * once per model. A model with no cut triangle sorts exactly as
			 * without the key. */
			std::stable_sort(triangles.begin(), triangles.end(),
				[&page_of_triangle, &node_of_triangle, &cut_of_triangle](int a, int b)
				{
					if (cut_of_triangle[(size_t)a] != cut_of_triangle[(size_t)b])
						return cut_of_triangle[(size_t)a] < cut_of_triangle[(size_t)b];

					if (page_of_triangle[(size_t)a] != page_of_triangle[(size_t)b])
						return page_of_triangle[(size_t)a] < page_of_triangle[(size_t)b];

					return node_of_triangle[(size_t)a] < node_of_triangle[(size_t)b];
				});

			Range range;
			int page = -2;
			int node = -2;
			int cutNow = -1;
			int glassNow = -1;
			int copyNow = -1;
			float opacityNow = -1.0f;

			/* A glass triangle and another of the same page and node get
			 * a range each. The sort is not keyed on it: over the .TRM
			 * files of both installs no part's alpha run shares a page
			 * and a node with another material's (ModelProbe -alpha), so
			 * the ranges of every model come out as they did before. */
			for (size_t i = 0; i < triangles.size(); i++)
			{
				const int triangle = triangles[i];
				const int here = page_of_triangle[(size_t)triangle];
				const int on = node_of_triangle[(size_t)triangle];
				const int cutHere = cut_of_triangle[(size_t)triangle];
				const int glassHere = glass_of_triangle[(size_t)triangle];
				const int copyHere = pass == HD_TRM_RUN_ADDITIVE
					? copy_of_triangle[(size_t)triangle] : 0;
				const float opacityHere = glassHere
					? opacity_of_triangle[(size_t)triangle] : 1.0f;

				if (here != page || on != node || cutHere != cutNow ||
					glassHere != glassNow || copyHere != copyNow ||
					opacityHere != opacityNow)
				{
					if (range.count)
						model.pass[pass].push_back(range);

					range.start = (unsigned int)indices.size();
					range.count = 0;
					range.pageSlot = here < 0 ? -1 : HDPageCacheSlotOf(here);
					// The .DDS number as well, so a material can be named
					// by page.
					range.page = here;
					range.node = on;
					range.cut = cutHere != 0;
					range.glass = glassHere != 0;
					range.blend = false;
					range.sided = glassHere != 0;
					range.opacity = opacityHere;
					range.copy = copyHere != 0;
					range.dropped = false;
					page = here;
					node = on;
					cutNow = cutHere;
					glassNow = glassHere;
					copyNow = copyHere;
					opacityNow = opacityHere;
				}

				/* A glass range is one-sided when every triangle in it has
				 * a layer facing against it; one without keeps the range
				 * two-sided, so a range is never cut for it. */
				if (!sided_of_triangle[(size_t)triangle])
					range.sided = false;

				indices.push_back(trm.index[(size_t)triangle * 3 + 0]);
				indices.push_back(trm.index[(size_t)triangle * 3 + 1]);
				indices.push_back(trm.index[(size_t)triangle * 3 + 2]);
				range.count += 3;
			}

			if (range.count)
				model.pass[pass].push_back(range);
		}

		if (indices.empty())
		{
			*why = cut.count > 0
				? "has no triangle on the pages that were asked for"
				: "has no triangle in any pass";
			return false;
		}

		model.vertices = vertices;
		model.indices = (int)indices.size();

		const UINT vertex_bytes = (UINT)(buffer.size() * sizeof(HDModelVertex));
		const UINT index_bytes = (UINT)(indices.size() * sizeof(unsigned short));

		/* D3DPOOL_MANAGED, unlike the room buffers. A model is small - the
		 * biggest of the 566 is a quarter of a megabyte - and there are
		 * dozens of them, so the system-memory copy that survives a device
		 * reset costs little and saves a reload of every model. */
		if (FAILED(g_device->CreateVertexBuffer(vertex_bytes, D3DUSAGE_WRITEONLY,
			0, D3DPOOL_MANAGED, &model.vb, 0)))
		{
			*why = "CreateVertexBuffer failed";
			return false;
		}

		if (FAILED(g_device->CreateIndexBuffer(index_bytes, D3DUSAGE_WRITEONLY,
			D3DFMT_INDEX16, D3DPOOL_MANAGED, &model.ib, 0)))
		{
			*why = "CreateIndexBuffer failed";
			return false;
		}

		void* target = 0;

		if (SUCCEEDED(model.vb->Lock(0, vertex_bytes, &target, 0)))
		{
			std::memcpy(target, &buffer[0], vertex_bytes);
			model.vb->Unlock();
		}

		if (SUCCEEDED(model.ib->Lock(0, index_bytes, &target, 0)))
		{
			std::memcpy(target, &indices[0], index_bytes);
			model.ib->Unlock();
		}

		if (g_model_hash_on)
		{
			const double hash_began = now_ms();

			model_hash_add(&buffer[0], vertex_bytes);
			model_hash_add(&indices[0], index_bytes);

			for (int pass = 0; pass < 3; pass++)
				for (const Range& r : model.pass[pass])
				{
					const int fields[4] =
					{
						(int)r.start, (int)r.count, r.pageSlot, r.node
					};

					model_hash_add(fields, sizeof(fields));
				}

			g_model_hash_ms += now_ms() - hash_began;
		}

		if (base)
			base->swap(buffer);

		return true;
	}

	/* Which node a separate HD face rides on. The remaster ships some faces
	 * as their own file - VON_CROY.TRM is a body with an empty collar and its
	 * face is VON_CROY_HEAD.TRM - and this path draws such a face rigidly on
	 * the creature's head node. Which node that is, is measured and not hard
	 * coded: it is the bind-pose node nearest the face geometry's own centre
	 * (TROne's rule, TROne-main (copy of 2026-10-06, commit 75c4c843),
	 * hd_load's head loop - node 21 in a 25 mesh tree, node 14 in a 15 mesh
	 * one). Where the speech table puts the face on another node, the face
	 * table says so (g_head_of_model) and this is not asked. */
	int head_node_of(const HDTrmModel& head, const Model& body)
	{
		if (!body.hasBind || !head.vertices)
			return -1;

		double cx = 0, cy = 0, cz = 0;

		for (int i = 0; i < head.vertices; i++)
		{
			cx += head.vertex[i].x;
			cy += head.vertex[i].y;
			cz += head.vertex[i].z;
		}

		cx /= (double)head.vertices;
		cy /= (double)head.vertices;
		cz /= (double)head.vertices;

		int best = 0;
		double closest = 1e30;

		for (int node = 0; node < body.nodes && node < kJointBudget; node++)
		{
			const double dx = body.bind[node][0] - cx;
			const double dy = body.bind[node][1] - cy;
			const double dz = body.bind[node][2] - cz;
			const double d = dx * dx + dy * dy + dz * dz;

			if (d < closest)
			{
				closest = d;
				best = node;
			}
		}

		return best;
	}

	/* Which bone of a separate HD face is the neck.
	 *
	 * Measured over all twelve <NAME>_HEAD.TRM of game 5, per bone bounding
	 * boxes in the model's own bind pose:
	 *
	 *   - the file is not only a face. PIERRE_HEAD.TRM spans y -376..-177
	 *     while PIERRE.TRM's whole body ends at -286 and the head node sits
	 *     at -342: the file carries head, NECK and a piece of collar,
	 *     reaching 165 units below the head joint.
	 *   - it carries a facial rig, so its bone numbers mean nothing to the
	 *     mesh tree. Riding rigidly on the head node, the neck and the collar
	 *     swing with the skull whenever that node turns - as in Pierre's
	 *     death animation - and tear a hole in the chest.
	 *   - the rig has a shape. Eight of the twelve files use BOTH base bones
	 *     0 and 1, and in every one of the eight one of them is the body of
	 *     the head (2305 to 7817 vertices) and the other a small strip
	 *     (16 to 469). In seven of those eight the small one reaches BELOW
	 *     the big one - PIERRE 85 units, LARSON 88, VON_CROY_ELDER 42,
	 *     BLUE_GUARD 32, ADMIRAL and VON_CROY_SENIOR 16 - and that strip is
	 *     the neck. The eighth is PATRICK, whose 16-vertex bone sits INSIDE
	 *     the head's own range and is no neck at all, which the test below
	 *     catches. The other four files name only one base bone and have no
	 *     neck to split off.
	 *
	 * So: of the two base bones, the minority one is the neck IF its geometry
	 * really reaches below the majority's. Nothing else is touched - the
	 * facial bones stay on the head, where they belong. */
	int neck_bone_of(const HDTrmModel& head)
	{
		int count[2] = { 0, 0 };
		float lowest[2] = { -1e30f, -1e30f };	// y grows DOWNWARDS in TR

		for (int i = 0; i < head.vertices; i++)
		{
			const HDTrmVertex& v = head.vertex[i];
			int best = 0;

			for (int b = 1; b < 3; b++)
				if (v.weight[b] > v.weight[best])
					best = b;

			const int bone = (int)v.bone[best];

			if (bone > 1)
				continue;

			count[bone]++;

			if (v.y > lowest[bone])
				lowest[bone] = v.y;
		}

		if (!count[0] || !count[1])
			return -1;		// one base bone: nothing to split

		const int minor = count[0] < count[1] ? 0 : 1;
		const int major = 1 - minor;

		/* A strip that does not hang below the head is a detail of the head,
		 * not a neck - PATRICK's sixteen vertices. */
		if (lowest[minor] <= lowest[major])
			return -1;

		return minor;
	}

	/* How far a joint of a facial rig stands from the rig's head joint before
	 * it is a joint of the BODY.
	 *
	 * A <NAME>_HEAD.TRM carries a skeleton in its rig and not only a face:
	 * LARA_HEAD's 33 joints include one at (-1, -49, 11) in the model's
	 * axes, which is the bind of Lara's torso node, one at the origin, her
	 * hips, and one at a thigh - and the strip of throat that reaches down
	 * into the collar is weighted to the torso joint. The split is a
	 * distance, which no axis convention of a rig can change. Over the 23
	 * rigged *_HEAD.TRM files of the IV-VI install's games 4 and 5 (the head
	 * joint is the one carrying the most weight, every other weighted joint
	 * measured from it) the farthest joint of the face stands 99.4 units out
	 * (ZIP_HEAD) and the nearest joint of the body 131.8 (LARSON_HEAD); 120
	 * lies in that gap. The rule and the two numbers are TROne's data
	 * measurement over 58 files of both installs, and they hold here.
	 *
	 * LARA_HEAD's two shoulder joints stand 81 units out and count as the
	 * face's. They carry 27 units of weight over 26 vertices, against 12,690
	 * on the torso joint, and stay on the head node. */
	const double kNeckJointDistance = 120.0;

	/* What the neck rule found in one file, for the note. */
	struct RigNeck
	{
		int headJoint;
		int joints;		// body joints that carry weight
		int bone;		// the lowest of them, which all of them become
		double nearest, farthest;	// their distances from the head joint
		int vertices;		// vertices with weight on one of them
		char list[64];		// the joints, for the note
	};

	/* The body joints of a rigid face's rig, folded into one bone number -
	 * see HDModelSlot::neckNode.
	 *
	 * Every vertex bone that names a body joint is renamed to the lowest body
	 * joint, and that number is returned; build() then hangs that one bone on
	 * the neck node with the file's own weight, exactly as it does a
	 * creature's neck bone. A vertex that carries two body joints keeps both
	 * weights on the one node. The facial joints keep their numbers, and all
	 * of them ride on the head node as before. The renaming happens in the
	 * copy read into memory, where nothing reads a bone number of a rigid
	 * face but build().
	 *
	 * -1, with nothing renamed, when the file has no rig, when a vertex names
	 * a bone the rig does not have, or when no weighted joint stands beyond
	 * kNeckJointDistance. */
	int rig_neck_of(HDTrmModel& trm, RigNeck* said)
	{
		std::memset(said, 0, sizeof(*said));
		said->headJoint = -1;
		said->bone = -1;

		if (trm.rigJoints <= 0 || !trm.rigJoint || trm.vertices <= 0)
			return -1;

		std::vector<double> weight((size_t)trm.rigJoints, 0.0);

		for (int i = 0; i < trm.vertices; i++)
			for (int b = 0; b < 3; b++)
			{
				const HDTrmVertex& v = trm.vertex[i];

				if (!v.weight[b])
					continue;

				if ((int)v.bone[b] >= trm.rigJoints)
					return -1;

				weight[v.bone[b]] += (double)v.weight[b];
			}

		int head = 0;

		for (int j = 1; j < trm.rigJoints; j++)
			if (weight[(size_t)j] > weight[(size_t)head])
				head = j;

		said->headJoint = head;

		std::vector<unsigned char> body((size_t)trm.rigJoints, 0);
		const float* h = trm.rigJoint + head * 3;

		for (int j = 0; j < trm.rigJoints; j++)
		{
			if (j == head || weight[(size_t)j] <= 0.0)
				continue;

			const float* p = trm.rigJoint + j * 3;
			const double d = std::sqrt((double)(p[0] - h[0]) * (p[0] - h[0]) +
				(double)(p[1] - h[1]) * (p[1] - h[1]) +
				(double)(p[2] - h[2]) * (p[2] - h[2]));

			if (d <= kNeckJointDistance)
				continue;

			body[(size_t)j] = 1;

			if (!said->joints || d < said->nearest)
				said->nearest = d;

			if (!said->joints || d > said->farthest)
				said->farthest = d;

			if (said->bone < 0)
				said->bone = j;

			const size_t used = std::strlen(said->list);

			std::snprintf(said->list + used, sizeof(said->list) - used, "%s%d",
				said->joints ? "," : "", j);
			said->joints++;
		}

		if (said->bone < 0)
			return -1;

		for (int i = 0; i < trm.vertices; i++)
		{
			HDTrmVertex& v = trm.vertex[i];
			bool touches = false;

			for (int b = 0; b < 3; b++)
				if ((int)v.bone[b] < trm.rigJoints && body[v.bone[b]])
				{
					if (v.weight[b])
						touches = true;

					v.bone[b] = (unsigned char)said->bone;
				}

			if (touches)
				said->vertices++;
		}

		return said->bone;
	}

	/* ------------------------------------------------------------ the lips --
	 *
	 * What a face needs to speak, taken while its file is still a file - see
	 * Lips at the top of this file and hd_trl.h. */

	// The folder a file lies in.
	std::string folder_of(const std::string& path)
	{
		const size_t cut = path.find_last_of("\\/");

		return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
	}

	/* A face's lips, out of its .TRM. `stem` is the face file's own name,
	 * which the track files are named after; `path` is where the file was
	 * read from and `origin` which folder that was.
	 *
	 * Where the track files are looked for. A track file is named after the
	 * audio track of the cutscene that plays it, and track numbers belong to
	 * a game: 5\ITEM holds 18 files of LARA_HEAD numbered after this game's
	 * tracks (96 to 135), and 4\ITEM - where LARA_HEAD.TRM itself lies - 24
	 * numbered after Tomb Raider IV's (29 to 112), three of them (96, 101,
	 * 103) under numbers this game uses for other lines. So an
	 * installation's face looks in game 5's ITEM and nowhere else. A mod's
	 * face, whose rig need not be the install's, looks in its own folder;
	 * where that holds no track file of the face and the install's face of
	 * the same name carries the same rig and block header (HDLipsModRig),
	 * it speaks the install's files from game 5's ITEM - a mod that changes
	 * the face's geometry and keeps its rig. A Tomb Raider III guest's face
	 * looks only in its own folder. */
	void lips_files(const Lips& lips, std::vector<int>& out);

	Lips* make_lips(const HDTrmModel& trm, const std::string& stem,
		const std::string& path, HDModelOrigin origin)
	{
		Lips* lips = new Lips();
		const char* ownLabel = origin == HD_MODEL_ORIGIN_4 ? "4"
			: origin == HD_MODEL_ORIGIN_MOD ? "mod"
			: origin == HD_MODEL_ORIGIN_TR3 ? "tr3" : "5";

		lips->stem = stem;
		lips->from = ownLabel;
		lips->columns = trm.blockColumns;

		if (origin == HD_MODEL_ORIGIN_4 || origin == HD_MODEL_ORIGIN_5)
		{
			lips->folder = join(g_install, "ITEM");
			lips->folderLabel = "5";
		}
		else
		{
			lips->folder = folder_of(path);
			lips->folderLabel = ownLabel;
		}

		/* A mod's face without track files of its own: the install's face
		 * of the same name, read once here, lends its files when the rig
		 * is the same. The face's own vertices, bones and weights still
		 * take the deform, so its changed geometry speaks. */
		if (origin == HD_MODEL_ORIGIN_MOD)
		{
			std::vector<int> own;

			lips_files(*lips, own);

			HDModelOrigin installOrigin = HD_MODEL_ORIGIN_NONE;
			const std::string installPath = own.empty()
				? find_model(stem, false, &installOrigin, 0, kModsNone)
				: std::string();
			const std::string ownFolder = lips->folder;
			const std::string installItem = join(g_install, "ITEM");
			const char* folder = ownFolder.c_str();
			const int rig = HDLipsModRig(&trm, !own.empty(),
				installPath.c_str(), ownFolder.c_str(), installItem.c_str(),
				&folder);

			lips->rig = HDLipsRigName(rig);

			if (rig == HD_LIPS_RIG_INSTALL)
			{
				lips->folder = folder;
				lips->folderLabel = "5";
			}
		}

		lips->mapped = HDLipsMapBuild(&trm, stem.c_str(), &lips->map, &lips->why);

		if (lips->mapped && (!trm.ownBone || !trm.ownWeight || trm.vertices <= 0))
		{
			lips->mapped = false;
			lips->why = "the file's own bones were not kept";
		}

		lips->trmPath = path;
		lips->blockAt = trm.blockAt;
		lips->blockFrames = trm.blockFrames;
		lips->blockLoop = trm.blockLoop;
		lips->loopWhy = !trm.blockBind || trm.blockAt <= 0 || trm.blockFrames <= 0
			? "no block" : "unmapped";

		if (!lips->mapped)
			return lips;

		const size_t n = (size_t)trm.vertices;

		lips->position.resize(n * 3);
		lips->normal.resize(n * 3);
		lips->bone.assign(trm.ownBone, trm.ownBone + n * 3);
		lips->weight.assign(trm.ownWeight, trm.ownWeight + n * 3);

		for (size_t i = 0; i < n; i++)
		{
			lips->position[i * 3 + 0] = trm.vertex[i].x;
			lips->position[i * 3 + 1] = trm.vertex[i].y;
			lips->position[i * 3 + 2] = trm.vertex[i].z;
			lips->normal[i * 3 + 0] = trm.vertex[i].nx;
			lips->normal[i * 3 + 1] = trm.vertex[i].ny;
			lips->normal[i * 3 + 2] = trm.vertex[i].nz;
		}

		/* The loop between lines: a block that comes back to its frame 0
		 * after a second or more, and a face the loop table knows. Its
		 * blinks are read once here, 499 frames of 1920 bytes for Lara's
		 * face, so a draw only looks them up. */
		const unsigned long long mask = HDLipsLoopMask(stem.c_str());

		if (!trm.blockBind || trm.blockAt <= 0 || trm.blockFrames <= 0)
			lips->loopWhy = "no block";
		else if (trm.blockLoop < HD_LIPS_LOOP_MIN)
			lips->loopWhy = "no loop";
		else if (!mask)
			lips->loopWhy = "no mask";
		else
		{
			HDTrlClip clip;
			const char* trouble = "";
			int start[64];
			float lid[64];

			if (HDTrlOpenBlock(path.c_str(), trm.blockAt, trm.blockFrames,
				trm.blockColumns, &clip, &trouble))
			{
				const int blinks = HDLipsBlinks(&lips->map, &clip, trm.blockLoop,
					HDLipsLidMask(stem.c_str()), start, lid, 64);

				HDTrlClose(&clip);
				lips->loop = trm.blockLoop;
				lips->loopMask = mask;
				lips->loopWhy = "ok";
				lips->bindLocals.assign(trm.blockBind,
					trm.blockBind + (size_t)trm.blockColumns * 12);
				lips->blinkStart.assign(start, start + blinks);
				lips->blinkLid.assign(lid, lid + blinks);
			}
			else
				lips->loopWhy = "no block";
		}

		return lips;
	}

	// <STEM>_<track>.TRL in the face's folder.
	std::string lips_file(const Lips& lips, int track)
	{
		char name[160];

		std::snprintf(name, sizeof(name), "%s_%d.TRL", lips.stem.c_str(), track);
		return join(lips.folder, name);
	}

	/* The track files a face has, in track order: every <STEM>_<digits>.TRL
	 * of its folder (HDLipsTrackFiles). */
	void lips_files(const Lips& lips, std::vector<int>& out)
	{
		const int n = HDLipsTrackFiles(lips.folder.c_str(), lips.stem.c_str(),
			0, 0);

		out.assign(n > 0 ? (size_t)n : 0, 0);

		if (n > 0)
			HDLipsTrackFiles(lips.folder.c_str(), lips.stem.c_str(), &out[0],
				n);
	}

	/* The loop's line, one per face that keeps lips - see HDModelLipsLine. */
	void loop_line(int slot, const Lips& lips)
	{
		char line[512];

		std::snprintf(line, sizeof(line), "loop slot=%d face=%s frames=%d loop=%d "
			"at=%ld mask=%llx blinks=%d reason=%s", slot, lips.stem.c_str(),
			lips.blockFrames, lips.blockLoop, lips.blockAt,
			HDLipsLoopMask(lips.stem.c_str()), (int)lips.blinkStart.size(),
			lips.loopWhy);
		g_lips_lines.push_back(line);
	}

	/* The level build's lines, one per track file of a face and one for a
	 * face with none, and the loop's - see HDModelLipsLine. */
	void lips_lines(int slot, const Lips& lips)
	{
		std::vector<int> files;
		char line[512];

		lips_files(lips, files);

		// A mod's face: whose track files it speaks - see Lips::rig.
		char rig[32] = "";

		if (*lips.rig)
			std::snprintf(rig, sizeof(rig), " rig=%s", lips.rig);

		if (files.empty())
		{
			std::snprintf(line, sizeof(line), "slot=%d face=%s file=- frames=0 "
				"cols=%d joints=%d/%d axis=%s len=0.0s reason=no file%s", slot,
				lips.stem.c_str(), lips.columns, lips.map.driven, lips.map.rigJoints,
				HDLipsAxisName(lips.mapped ? lips.map.axis
					: HD_LIPS_AXIS_REFUSED), rig);
			g_lips_lines.push_back(line);
			loop_line(slot, lips);
			return;
		}

		for (size_t i = 0; i < files.size(); i++)
		{
			HDTrlClip clip;
			const char* trouble = "";
			const bool opened = HDTrlOpen(lips_file(lips, files[i]).c_str(),
				lips.columns, &clip, &trouble);
			const char* reason = !lips.mapped ? "refused"
				: opened ? "ok" : !std::strcmp(trouble, "columns") ? "columns"
				: "no file";
			char from[48] = "";

			if (std::strcmp(lips.from, lips.folderLabel) != 0)
				std::snprintf(from, sizeof(from), " from=%s trl_from=%s", lips.from,
					lips.folderLabel);

			std::snprintf(line, sizeof(line), "slot=%d face=%s file=%s_%d.TRL "
				"frames=%d cols=%d joints=%d/%d axis=%s len=%.1fs "
				"reason=%s%s%s", slot,
				lips.stem.c_str(), lips.stem.c_str(), files[i],
				opened ? clip.frames : 0, opened ? clip.columns : 0,
				lips.map.driven, lips.map.rigJoints,
				HDLipsAxisName(lips.mapped ? lips.map.axis : HD_LIPS_AXIS_REFUSED),
				opened ? clip.frames / 30.0 : 0.0, reason, from, rig);
			g_lips_lines.push_back(line);
			HDTrlClose(&clip);
		}

		loop_line(slot, lips);
	}

	/* What a loop or firing key shows (hd_model.h, HD_LIPS_TRACK_LOOP): the
	 * source, and for the loop's ease how far. */
	int pseudo_source(const Lips& lips, int track, int key, double* amount)
	{
		const int a = key / HD_LIPS_KEY_STRIDE;
		const int b = key % HD_LIPS_KEY_STRIDE;

		*amount = 1.0;

		if (track == HD_LIPS_TRACK_FIRE)
			return a <= 0 && b <= 0 ? HD_LIPS_SOURCE_BIND : HD_LIPS_SOURCE_TRL;

		if (a <= 0)
			return HD_LIPS_SOURCE_TRL;

		// `a` ticks past the frame the loop stopped at, as past a clip's end.
		int clipFrame = 0;

		return HDLipsSourceOf(b + a, b + 1, &clipFrame, amount);
	}

	/* The skins of a loop or firing key into lips.skins; false when a frame
	 * does not read. */
	bool pseudo_skins(Lips& lips, int track, int key, double amount)
	{
		const size_t each = (size_t)lips.map.columns * 12;
		const size_t joints = (size_t)lips.map.rigJoints * 12;
		const int a = key / HD_LIPS_KEY_STRIDE;
		const int b = key % HD_LIPS_KEY_STRIDE;

		if (lips.bindLocals.size() != each || lips.loop <= 0)
			return false;

		if (track == HD_LIPS_TRACK_LOOP)
		{
			const int frame = b < lips.loop ? b : lips.loop - 1;

			if (!HDTrlReadFrame(&lips.clip, frame, &lips.frame[0]))
				return false;

			HDLipsSkins(&lips.map, &lips.frame[0], amount, &lips.skins[0],
				lips.loopMask, &lips.bindLocals[0]);
			return true;
		}

		// The firing face: its frame in full, blended from the loop's frame.
		const int from = b - 1;

		lips.fire.resize(each);
		lips.fromSkins.resize(joints);
		lips.toSkins.resize(joints);

		if (!HDTrlReadFrame(&lips.clip, HD_LIPS_FIRE_FRAME, &lips.fire[0]))
			return false;

		HDLipsSkins(&lips.map, &lips.fire[0], 1.0, &lips.toSkins[0]);

		if (from >= 0)
		{
			if (!HDTrlReadFrame(&lips.clip, from < lips.loop ? from : lips.loop - 1,
				&lips.frame[0]))
				return false;

			HDLipsSkins(&lips.map, &lips.frame[0], 1.0, &lips.fromSkins[0],
				lips.loopMask, &lips.bindLocals[0]);
		}
		else
			HDLipsSkins(&lips.map, 0, 1.0, &lips.fromSkins[0]);

		HDLipsSkinsBlend(&lips.fromSkins[0], &lips.toSkins[0],
			(double)a / (double)HD_LIPS_EASE_IN_TICKS, lips.map.rigJoints,
			&lips.skins[0]);
		return true;
	}

	/* The stream a face draws from: its own buffer holding the frame asked
	 * for, or the static one.
	 *
	 * The deform runs when the frame changes and never otherwise - a
	 * cutscene steps thirty frames a second and is drawn without a tween
	 * (game\interpolate.cpp, InterpBeginFrame), so at sixty frames a second
	 * every second draw finds the buffer already holding its frame. A face
	 * whose clip is not open, a frame the clip says is the bind, and every
	 * failure on the way draw the static buffer - which is the face as it
	 * was built.
	 *
	 * The loop and the firing face take the same road with their keys in
	 * place of a cutscene frame. The loop's key changes once a logic tick
	 * (thirty a second, GlobalCounter) and the firing face's stands while it
	 * is full, so the deform runs no more often than a speaking face's. */
	IDirect3DVertexBuffer9* lips_stream(const Model& model, int slot, bool head,
		int track, int frame)
	{
		Lips* lips = model.lips;

		if (!lips || !lips->mapped || track < 0 || lips->track != track ||
			!lips->clip.file || lips->base.size() != lips->position.size() / 3)
			return model.vb;

		if (frame == lips->shownFrame)
			return lips->shownSource == HD_LIPS_SOURCE_BIND || !lips->vb
				? model.vb : lips->vb;

		int clipFrame = 0;
		double amount = 0.0;
		const bool pseudo = track >= HD_LIPS_TRACK_LOOP;
		const int source = pseudo ? pseudo_source(*lips, track, frame, &amount)
			: HDLipsSourceOf(frame, lips->clip.frames, &clipFrame, &amount);
		double jaw = 0.0;

		lips->shownFrame = frame;
		lips->shownSource = HD_LIPS_SOURCE_BIND;

		if (source != HD_LIPS_SOURCE_BIND)
		{
			const size_t n = lips->base.size();
			const UINT bytes = (UINT)(n * sizeof(HDModelVertex));

			if (!lips->vb && FAILED(g_device->CreateVertexBuffer(bytes,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
				&lips->vb, 0)))
				lips->vb = 0;

			lips->frame.resize((size_t)lips->map.columns * 12);
			lips->skins.resize((size_t)lips->map.rigJoints * 12);
			lips->outPosition.resize(n * 3);
			lips->outNormal.resize(n * 3);
			lips->out.resize(n);

			void* target = 0;
			bool skinned = false;

			if (lips->vb && pseudo)
				skinned = pseudo_skins(*lips, track, frame, amount);
			else if (lips->vb && HDTrlReadFrame(&lips->clip, clipFrame, &lips->frame[0]))
			{
				HDLipsSkins(&lips->map, &lips->frame[0], amount, &lips->skins[0]);
				skinned = true;
			}

			if (skinned)
			{
				jaw = HDLipsDeform(&lips->skins[0], lips->map.rigJoints, (int)n,
					&lips->position[0], &lips->normal[0], &lips->bone[0],
					&lips->weight[0], &lips->outPosition[0], &lips->outNormal[0]);

				for (size_t i = 0; i < n; i++)
				{
					HDModelVertex& v = lips->out[i];

					v = lips->base[i];
					v.x = lips->outPosition[i * 3 + 0];
					v.y = lips->outPosition[i * 3 + 1];
					v.z = lips->outPosition[i * 3 + 2];
					v.nx = lips->outNormal[i * 3 + 0];
					v.ny = lips->outNormal[i * 3 + 1];
					v.nz = lips->outNormal[i * 3 + 2];

					/* The face table's half turn, as the build put it on the
					 * file (the face pass, `turned`). */
					if (lips->turned)
					{
						v.y = 2.0f * lips->turnY - v.y;
						v.z = 2.0f * lips->turnZ - v.z;
						v.ny = (unsigned char)(v.ny >= 255 ? 0 : 254 - v.ny);
						v.nz = (unsigned char)(v.nz >= 255 ? 0 : 254 - v.nz);
					}
				}

				if (SUCCEEDED(lips->vb->Lock(0, bytes, &target, D3DLOCK_DISCARD)))
				{
					std::memcpy(target, &lips->out[0], bytes);
					lips->vb->Unlock();
					lips->shownSource = source;
				}
			}
		}

		if (g_lips_events.size() < kLipsEvents)
		{
			HDModelLipsEvent said;

			said.slot = slot;
			said.head = head;
			said.track = track;
			said.frame = frame;
			said.source = lips->shownSource;
			said.jaw = lips->shownSource == HD_LIPS_SOURCE_BIND ? 0.0f : (float)jaw;
			g_lips_events.push_back(said);
		}

		return lips->shownSource == HD_LIPS_SOURCE_BIND ? model.vb : lips->vb;
	}

	/* ---------------------------------------------------------- drawing -- */

	/* The bind fold. A .TRM vertex is in the model's bind pose, so the node's
	 * own bind offset has to come off before the node matrix goes on:
	 *
	 *     (p - bind) * M  =  p * R + t - bind * R  =  p * M'
	 *
	 * with M' the same 3x3 and t' = t - bind * R. TROne subtracts the offset
	 * in the shader with a second constant array of 32 vectors; folding it
	 * here costs three dot products per node per object and saves 32
	 * registers and three subtractions per VERTEX.
	 *
	 * Written straight into the three constant COLUMNS the shader reads. */
	void set_joint(int node, const float* rowMajor, const float* bind)
	{
		float columns[12];

		float tx = rowMajor[12];
		float ty = rowMajor[13];
		float tz = rowMajor[14];

		if (bind)
		{
			tx -= bind[0] * rowMajor[0] + bind[1] * rowMajor[4] + bind[2] * rowMajor[8];
			ty -= bind[0] * rowMajor[1] + bind[1] * rowMajor[5] + bind[2] * rowMajor[9];
			tz -= bind[0] * rowMajor[2] + bind[1] * rowMajor[6] + bind[2] * rowMajor[10];
		}

		for (int col = 0; col < 3; col++)
		{
			columns[col * 4 + 0] = rowMajor[0 * 4 + col];
			columns[col * 4 + 1] = rowMajor[1 * 4 + col];
			columns[col * 4 + 2] = rowMajor[2 * 4 + col];
		}

		columns[0 * 4 + 3] = tx;
		columns[1 * 4 + 3] = ty;
		columns[2 * 4 + 3] = tz;

		g_device->SetVertexShaderConstantF((UINT)(8 + node * 3), columns, 3);
	}

	/* `stream` is the vertex buffer drawn from: the model's own, or a
	 * speaking face's deformed copy of it (lips_stream) - the same stride,
	 * count and order, so the index buffer, the ranges and every constant
	 * below serve both. */
	void draw_model(const Model& model, const float* nodeMatrix, int nodeCount,
		const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
		const HDModelNeckCut* cut, int cull, IDirect3DVertexBuffer9* stream,
		const HDModelSoft* soft)
	{
		const int nodes = nodeCount < kJointBudget ? nodeCount : kJointBudget;

		/* All 32 joints go up, not just the ones the caller has. A .TRM is
		 * the remaster's file and the classic node count is the level's, and
		 * the two disagreeing by one would let a vertex read a joint left
		 * over from the object before - a limb somewhere else in the room,
		 * and only sometimes. The nodes past the caller's count get the last
		 * one it gave, which is TROne's rule for a node it never saw
		 * (TROne-main, copy of 2026-10-06, commit 75c4c843,
		 * fill_unseen_joints): a slightly wrong limb beats a collapsed one.
		 * */
		for (int node = 0; node < kJointBudget; node++)
		{
			const int from = node < nodes ? node : nodes - 1;

			set_joint(node, nodeMatrix + from * 16,
				model.hasBind ? model.bind[from] : 0);
		}

		/* One colour per node, in the classic 0..255 space. A caller with
		 * nothing to say gets a flat 128, which is the classic path's own
		 * "no light list" ambient and draws the model at its plain texture
		 * brightness. The fourth component is not read and stays zero. */
		float light[kJointBudget * 4];

		for (int node = 0; node < kJointBudget; node++)
		{
			const int from = node < nodes ? node : nodes - 1;

			light[node * 4 + 0] = nodeLight ? nodeLight[from * 3 + 0] : 128.0f;
			light[node * 4 + 1] = nodeLight ? nodeLight[from * 3 + 1] : 128.0f;
			light[node * 4 + 2] = nodeLight ? nodeLight[from * 3 + 2] : 128.0f;
			light[node * 4 + 3] = 0.0f;
		}

		g_device->SetVertexShaderConstantF(104, light, (UINT)kJointBudget);

		/* c162, the head cut: which node's weight decides and how much of it
		 * sends a fragment away. Nothing in it depends on the camera or the
		 * pose, so the caller names the two numbers and they are the same in
		 * every frame.
		 *
		 * Written for every draw and not only for the one that cuts, for the
		 * reason c161 is: a constant register keeps what it was last given,
		 * and a stale threshold plus a stale gPass.z would carve the next
		 * creature drawn. Off is a threshold of a million, which no weight
		 * reaches. */
		float neck[4] =
		{
			cut && cut->on > 0.0f ? cut->node : 0.0f,
			cut && cut->on > 0.0f ? cut->bar : 1.0e6f,
			0.0f,
			0.0f
		};

		g_device->SetVertexShaderConstantF(162, neck, 1);

		/* c163 to c166, the chest's secondary motion (hd_model.h,
		 * HDModelSoft). Written for every draw for the reason c162 is, and
		 * zeros - which add nothing to any vertex - for every draw that has
		 * none. */
		static const HDModelSoft kNoSoft = {};

		g_device->SetVertexShaderConstantF(163, (soft ? soft : &kNoSoft)->constants, 4);

		/* The object's lights, in the VERTEX shader (c136) - the classic path
		 * lights per vertex out of the same list, so this is the same place
		 * and not an approximation of it. See HDModelLighting in hd_model.h.
		 *
		 * c160 carries the count and the sun's factor. */
		const int count = lighting && lighting->lights
			? (lighting->lightCount < HD_MAX_OBJ_LIGHTS ? lighting->lightCount : HD_MAX_OBJ_LIGHTS)
			: 0;

		float lights[HD_MAX_OBJ_LIGHTS * 2 * 4] = { 0 };

		for (int i = 0; i < count; i++)
		{
			const HDObjLight& source = lighting->lights[i];

			lights[i * 8 + 0] = source.x;
			lights[i * 8 + 1] = source.y;
			lights[i * 8 + 2] = source.z;
			lights[i * 8 + 3] = source.kind;
			lights[i * 8 + 4] = source.r;
			lights[i * 8 + 5] = source.g;
			lights[i * 8 + 6] = source.b;
			lights[i * 8 + 7] = source.invRange;
		}

		float info[4] =
		{
			(float)count,
			lighting ? lighting->sunScale : 0.75f,
			0.0f,
			0.0f
		};

		g_device->SetVertexShaderConstantF(136, lights, HD_MAX_OBJ_LIGHTS * 2);
		g_device->SetVertexShaderConstantF(160, info, 1);

		/* c161, the sheen. Written for every draw
		 * and not only for the ones that shine: the register keeps the last
		 * value the card was given, and Lara's catsuit would otherwise leave
		 * its gloss on the next creature drawn. A power of 0 would make
		 * pow(x, 0) = 1 everywhere, so the floor is 1. */
		float sheen[4] =
		{
			lighting ? lighting->specStrength : 0.0f,
			lighting && lighting->specPower > 1.0f ? lighting->specPower : 1.0f,
			0.0f,
			0.0f
		};

		g_device->SetVertexShaderConstantF(161, sheen, 1);

		/* What is in the register now, so the per-range writes below can leave
		 * it alone when nothing changes. */
		float sheenNow[2] = { sheen[0], sheen[1] };

		/* c17, the PIXEL side: x is the per-pixel dynamic light loop, which
		 * an object does NOT use - its lights are in the vertex shader above,
		 * where the classic formula puts them - and y is this frame's fog
		 * bulbs, which it does. A room drawn a moment ago left its own count
		 * in this register, so it is written for every object. */
		float counts[4] = { 0.0f, (float)g_frame_bulbs, 0, 0 };

		g_device->SetPixelShaderConstantF(17, counts, 1);

		g_device->SetStreamSource(0, stream ? stream : model.vb, 0,
			sizeof(HDModelVertex));
		g_device->SetIndices(model.ib);

		/* The one model that is not double sided. Set here and put back at
		 * the bottom of this function, so that the state the bracket
		 * installed (D3DCULL_NONE) is what every other draw finds. */
		if (cull != HD_MODEL_CULL_NONE)
			g_device->SetRenderState(D3DRS_CULLMODE,
				cull == HD_MODEL_CULL_CW ? D3DCULL_CW : D3DCULL_CCW);

		/* Four steps over the three passes. The third is the glass of the
		 * alpha run (HDModelSetGlassNodes), drawn with the glows: in the
		 * bracket that draws the additive pass, just before it, after
		 * everything solid has written its depth. A model without glass
		 * skips it at its first test. */
		for (int step = 0; step < 4; step++)
		{
			const int pass = step < 2 ? step : step - 1;
			const bool glassStep = step == 2;

			/* The frame draws its solid work and its glows in two brackets,
			 * and this is where a bracket leaves out what is not its own.
			 * HD_PASS_ALL, which the diagnostic tool asks for, lets all three
			 * through and this line costs a test. */
			if (glassStep)
			{
				if (model.blendRanges <= 0 ||
					!(g_pass_mask & (HD_PASS_ADDITIVE | HD_PASS_BLEND)))
					continue;
			}
			else if (!(g_pass_mask & (1u << pass)))
				continue;

			if (model.pass[pass].empty())
				continue;

			/* gPass: x switches the alpha test on, y the additive weighting,
			 * z the first person HEAD CUT (a flag, 1 or 0; the two numbers
			 * it needs went up in c162 above, and a 0 here
			 * for every model but Lara's body makes the shader's clip discard
			 * nothing), w the overbright add. The PIXEL constant
			 * file only - the vertex shader does not read gPass and fxc has
			 * put its own literals in that register there; the room passes
			 * write the same register of the pixel file with z 0, which
			 * this call overwrites before every pass. */
			float constants[4] =
			{
				pass == 1 ? 1.0f : 0.0f,
				pass == 2 ? 1.0f : 0.0f,
				cut && cut->on > 0.0f ? 1.0f : 0.0f,
				pass == 2 ? 0.0f : 1.0f
			};

			/* The glass as the additive pass draws its texels: no alpha
			 * test, the light clamped and the texel weighed by its alpha,
			 * no overbright add (hd_model.hlsl, gPass). A texel at a
			 * quarter of its alpha adds a quarter of itself. */
			if (glassStep)
			{
				constants[0] = 0.0f;
				constants[1] = 1.0f;
				constants[3] = 0.0f;
			}

			/* A model lit as its additive run is (HDModelSlot::
			 * lightAsAdditive) takes the same two switches in its other runs:
			 * the light under 1.0, no sheen, weighed by the alpha, a glow
			 * under the water and no overbright add. The alpha test of the
			 * alpha run stays its own. */
			if (model.lightAsAdditive && !glassStep)
			{
				constants[1] = 1.0f;
				constants[3] = 0.0f;
			}

			g_device->SetPixelShaderConstantF(4, constants, 1);

			/* The cut-out alpha test in the opaque pass runs on the ranges
			 * the build marked `cut`, and on no others.
			 *
			 * The page's share below the reference cannot decide it:
			 * ROMAN_GOD_ANDREA_SWORD's three body pages measure 64.1, 48.7
			 * and 56.4 percent of texels below alpha 128, and with the test
			 * on, half the statue is discarded. PIERRE and BLUE_GUARD share
			 * a page at 26.2 percent. Their alpha is a gloss mask on material
			 * 0 or 3, and a solid statue sits ABOVE a sheet of wire mesh in
			 * that share. HDPageCacheSlotCutoutPermille is where those
			 * numbers come from.
			 *
			 * What decides it is the part's material and how low the page's
			 * low texels are (hd_model_cut.h): a part of material 6, 7 or 8
			 * on a page whose texels below the reference are mostly at 0 -
			 * the wire mesh of the JOBY fans and trapdoors. The remaster
			 * sorts those parts into the opaque run, and drawn without the
			 * test their holes are a solid dark plate. Everything in the
			 * alpha run is tested at the same 0.5. */
			/* An additive run drawn from the front alone
			 * (HDModelSlot::additiveOneSided) culls the winding turned away
			 * from the eye for this pass, as the glass below does, unless the
			 * draw culls already; the end of the step puts both sides back. */
			const bool oneSided = pass == 2 && model.additiveOneSided &&
				cull == HD_MODEL_CULL_NONE;

			if (pass == 2)
			{
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
			}

			if (oneSided)
				g_device->SetRenderState(D3DRS_CULLMODE,
					HDMatrixMirrors(nodeMatrix) ? D3DCULL_CW : D3DCULL_CCW);

			/* The glass, added as the classic adds its see-through faces
			 * (ONE / ONE, specific/polyinsert.cpp, case 2), with no depth
			 * write: what lies behind it shows through. The blend factors
			 * are put back as the step found them. */
			DWORD glassSrc = D3DBLEND_ONE, glassDest = D3DBLEND_ZERO;

			/* And per range its sides and its weight (hd_object_rules.h):
			 * a range with a glass layer facing against it culls the
			 * winding turned away from the eye - the other winding under a
			 * mirrored matrix - unless the whole draw culls already; such
			 * a range whose part's opacity is under 1 is added times it
			 * (HDGlassWeight), through the blend factor. Both are put back
			 * after the step. The cull and the factor each draw holds now,
			 * -1 for none set: the draw's own cull and a whole add; and
			 * whether the step wrote the factor at all. */
			const DWORD glassOutside = HDMatrixMirrors(nodeMatrix)
				? D3DCULL_CW : D3DCULL_CCW;
			DWORD glassFactor = 0xFFFFFFFF;
			int glassCullNow = -1;
			int glassByteNow = -1;
			bool glassFactorSet = false;

			if (glassStep)
			{
				g_device->GetRenderState(D3DRS_SRCBLEND, &glassSrc);
				g_device->GetRenderState(D3DRS_DESTBLEND, &glassDest);
				g_device->GetRenderState(D3DRS_BLENDFACTOR, &glassFactor);
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
			}

			/* The ranges, merged back together. They come off in (page, node)
			 * order and their index runs are contiguous, so a run of visible
			 * neighbours that share a page is ONE draw call. With mesh_bits
			 * at -1 that is one call per page; a hidden node splits the run
			 * in two and costs one more. */
			for (size_t i = 0; i < model.pass[pass].size(); )
			{
				const Range& first = model.pass[pass][i];

				// The glass in its own step and only there, and a copy of
				// the glass left out where its node draws glass.
				if (!(nodeMask & (1u << (first.node & 31))) ||
					first.blend != glassStep || first.dropped)
				{
					i++;
					continue;
				}

				unsigned int start = first.start;
				unsigned int count = first.count;
				size_t next = i + 1;

				while (next < model.pass[pass].size())
				{
					const Range& more = model.pass[pass][next];

					/* Two ranges join when they wear the same page, when
					 * the second begins where the first ends, when the
					 * mask keeps the second's node and when both are cut
					 * or both are not. */
					if (more.pageSlot != first.pageSlot ||
						more.start != start + count ||
						!(nodeMask & (1u << (more.node & 31))) ||
						more.cut != first.cut || more.blend != first.blend ||
						more.dropped != first.dropped || (first.blend &&
						(more.sided != first.sided ||
						more.opacity != first.opacity)))
						break;

					count += more.count;
					next++;
				}

				g_device->SetTexture(0, first.pageSlot >= 0
					? (IDirect3DBaseTexture9*)HDPageCacheSlotTexture(first.pageSlot)
					: 0);

				/* The opaque pass's alpha test, per range: gPass.x is the
				 * shader's switch for it (hd_model.hlsl clips on gPass.x *
				 * (alpha - 0.5)). Written only when it changes; the cut
				 * ranges come last in the pass, so a model that has any
				 * costs one constant write more, and every other model
				 * none. */
				if (pass == 0 && constants[0] != (first.cut ? 1.0f : 0.0f))
				{
					constants[0] = first.cut ? 1.0f : 0.0f;
					g_device->SetPixelShaderConstantF(4, constants, 1);
				}

				/* The material of this page. Only for a model a caller has
				 * set a table for - and then only when the
				 * numbers really change, so a hand with a gun in it costs two
				 * constant writes and a model without a table costs a test on
				 * an empty vector. */
				if (!model.pageSheen.empty())
				{
					float want[2] = { sheen[0], sheen[1] };

					for (size_t s = 0; s < model.pageSheen.size(); s++)
						if (model.pageSheen[s].page == first.page)
						{
							want[0] = model.pageSheen[s].strength;
							want[1] = model.pageSheen[s].power > 1.0f
								? model.pageSheen[s].power : 1.0f;
							break;
						}

					if (want[0] != sheenNow[0] || want[1] != sheenNow[1])
					{
						const float write[4] = { want[0], want[1], 0.0f, 0.0f };

						g_device->SetVertexShaderConstantF(161, write, 1);
						sheenNow[0] = want[0];
						sheenNow[1] = want[1];
					}
				}

				if (glassStep)
				{
					const int cullHere = first.sided && cull ==
						HD_MODEL_CULL_NONE ? (int)glassOutside : -1;
					const int byteHere = g_blend_factor
						? (int)HDGlassOpacityByte(HDGlassWeight(first.opacity,
							first.sided)) : 255;

					if (cullHere != glassCullNow)
					{
						g_device->SetRenderState(D3DRS_CULLMODE, cullHere >= 0
							? (DWORD)cullHere : D3DCULL_NONE);
						glassCullNow = cullHere;
					}

					if (byteHere != glassByteNow)
					{
						g_device->SetRenderState(D3DRS_SRCBLEND, byteHere < 255
							? D3DBLEND_BLENDFACTOR : D3DBLEND_ONE);

						if (byteHere < 255)
						{
							g_device->SetRenderState(D3DRS_BLENDFACTOR,
								D3DCOLOR_ARGB(byteHere, byteHere, byteHere,
									byteHere));
							glassFactorSet = true;
						}

						glassByteNow = byteHere;
					}
				}

				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
					0, (UINT)model.vertices, start, count / 3);

				g_draw_calls++;
				i = next;
			}

			if (pass == 2 || glassStep)
			{
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			}

			if (oneSided)
				g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

			// The blend factors and the cull as the step found them.
			if (glassStep)
			{
				g_device->SetRenderState(D3DRS_SRCBLEND, glassSrc);
				g_device->SetRenderState(D3DRS_DESTBLEND, glassDest);

				if (glassFactorSet)
					g_device->SetRenderState(D3DRS_BLENDFACTOR, glassFactor);

				if (glassCullNow >= 0)
					g_device->SetRenderState(D3DRS_CULLMODE,
						cull == HD_MODEL_CULL_CW ? D3DCULL_CW
						: cull == HD_MODEL_CULL_CCW ? D3DCULL_CCW
						: D3DCULL_NONE);
			}
		}

		/* And back to double sided. The bracket installed D3DCULL_NONE for
		 * the whole HD path and every other model on this frame's list
		 * expects to find it; a cull mode left standing here would travel
		 * from Lara's body to the next item on the list and take a face off
		 * it - silently, and only in first person. */
		if (cull != HD_MODEL_CULL_NONE)
			g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	}

	const Model* find(const std::map<int, Model>& where, int slot)
	{
		const std::map<int, Model>::const_iterator at = where.find(slot);

		return at == where.end() ? 0 : &at->second;
	}

	/* What to call a slot in a note.
	 *
	 * HDSlotName knows the engine's 471 slots and nothing else, and this
	 * module is handed PSEUDO slots as well - Lara's wardrobe parts
	 * (hd_lara.h) and the alternative models of a mesh swap (hd_object.cpp).
	 * For those the model's own name is the label, because a pseudo slot IS
	 * its model; "?" is left for a slot with neither. */
	const char* slot_label(int slot, const char* model)
	{
		const char* name = HDSlotName(slot);

		if (name)
			return name;

		return model ? model : "?";
	}
}

/* ------------------------------------------------------------- the module -- */

bool HDModelInit(IDirect3DDevice9* device, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	HDModelShutdown();

	if (!device)
	{
		*why = "no device";
		return false;
	}

	if (!HDRenderReady())
	{
		// The state bracket lives there - see hd_model.h.
		*why = "the HD room renderer is not up";
		return false;
	}

	g_device = device;

	{
		D3DCAPS9 caps;

		g_blend_factor = SUCCEEDED(device->GetDeviceCaps(&caps)) &&
			(caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0;
	}

	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0,  0, D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0 },
		{ 0, 12, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0 },
		{ 0, 16, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
		{ 0, 20, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0 },
		D3DDECL_END()
	};

	if (FAILED(device->CreateVertexDeclaration(elements, &g_decl)))
	{
		*why = "CreateVertexDeclaration failed";
		HDModelShutdown();
		return false;
	}

	if (FAILED(device->CreateVertexShader((const DWORD*)kHDModelVS, &g_vs)))
	{
		*why = "CreateVertexShader failed";
		HDModelShutdown();
		return false;
	}

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDModelPS, &g_ps)))
	{
		*why = "CreatePixelShader failed";
		HDModelShutdown();
		return false;
	}

	if (FAILED(device->CreateVertexShader((const DWORD*)kHDShadowCastVS, &g_cast_vs)))
		g_cast_vs = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDShadowCastPS, &g_cast_ps)))
		g_cast_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDShadowReceiveModelPS, &g_receive_ps)))
		g_receive_ps = 0;

	return true;
}

void HDModelShutdown()
{
	HDModelLevelEnd();

	if (g_decl) { g_decl->Release(); g_decl = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_ps) { g_ps->Release(); g_ps = 0; }
	if (g_cast_vs) { g_cast_vs->Release(); g_cast_vs = 0; }
	if (g_cast_ps) { g_cast_ps->Release(); g_cast_ps = 0; }
	if (g_receive_ps) { g_receive_ps->Release(); g_receive_ps = 0; }

	g_mirror_warmed = false;
	g_device = 0;
	g_in_frame = false;
	g_in_cast = false;
	g_in_receive = false;
}

bool HDModelReady()
{
	return g_device && g_vs && g_ps && g_decl;
}

bool HDModelLevelBegin(IDirect3DDevice9* device, const char* installRoot,
	const HDModelSlot* slots, int count, bool managedPages, int threads,
	HDModelStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	HDModelLevelEnd();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!HDModelReady() || !device)
	{
		*why = "the object path is not up";
		return false;
	}

	if (!installRoot || !*installRoot)
	{
		*why = "no install";
		return false;
	}

	g_device = device;
	g_install = installRoot;

	const double started = now_ms();
	double read_ms = 0;
	double page_ms = 0;
	double find_ms = 0;
	// The phases of buildMs, and what the .TRM reader did between these two
	// readings.
	double trm_opens0 = 0, trm_loads0 = 0, trm_bytes0 = 0;

	g_key_ms = 0;
	g_model_hash = 14695981039346656037ull;
	const double hash_ms0 = g_model_hash_ms;
	HDTrmCounters(&trm_opens0, &trm_loads0, &trm_bytes0);

	if (stats)
		stats->slots = count;

	/* Pass one: read every slot's .TRM and build what does not need the
	 * device. The pages are collected on the way, because they all have to be
	 * in the cache before a single range can name a page slot. */
	struct Pending
	{
		int slot;
		HDTrmModel trm;
		HDTrmModel head;
		bool hasHead;
		std::string name;
		HDModelOrigin origin;
		int pageBias;		// 0, HD_PAGE_ALT_BASE for a TR3 guest, or
					// the mod's namespace
		/* Which pages of the file are built, as a set kept or dropped. Empty
		 * is the whole model. The numbers live here rather than in the
		 * caller's slot because the hand-twin rule works some of them out
		 * itself, while the file is open. */
		std::vector<int> cutPage;
		bool cutKeeps;
		// Which NODES of the file are built, as a mask; 0 is all of them.
		// See HDModelSlot::onlyNode.
		unsigned int cutNode;
		/* The index in `slots` this entry borrows its skeleton from, for a
		 * twin this module made up itself; -1 for every entry a caller asked
		 * for. */
		int twinOf;
		HDTrmFoldCounts fold;	// what the mirror fold did
		/* A rigid face's neck, found in the file while it is open - see
		 * HDModelSlot::neckNode. -1 and -1 for every other entry. */
		int neckBone;
		int neckNode;
		// The face table's line for this body's face, or null.
		const HeadOfModel* headEntry;
		/* Where the two files were read from, and the face file's own name
		 * - what a face's track files are looked for by (make_lips). */
		std::string path;
		std::string headPath;
		std::string headStem;
		HDModelOrigin headOrigin;
	};

	std::vector<Pending> pending;
	std::vector<int> pages;

	for (int i = 0; i < count; i++)
	{
		const int slot = slots[i].slot;
		HDBindKind kind = HD_BIND_MODEL;
		bool fromMap = false;
		const char* model = slots[i].name;

		// A name the caller insists on is not asked about again - not even
		// for Lara, whose composite is hd_lara's but whose parts a diagnostic
		// tool may want to look at one at a time.
		if (!model && !slots[i].synth)
		{
			// Counted into find_ms: asking which file a slot means is the
			// same walk of the same folders the search below does, and a
			// mod adds two more places to walk.
			const double bind_began = now_ms();

			model = HDBindSlot(installRoot, slot, &kind, &fromMap, 0);
			find_ms += now_ms() - bind_began;
		}

		/* A model that is built and not read - the lid that closes the neck.
		 * It has no file, no head and no origin; everything after this point
		 * is the same road a read model takes. The source is looked up in what
		 * has already been
		 * read, which is why the maker's slot has to stand after its source in
		 * the caller's array. */
		if (slots[i].synth)
		{
			const HDTrmModel* source = 0;

			for (size_t p = 0; p < pending.size(); p++)
				if (pending[p].slot == slots[i].synthFrom)
				{
					source = &pending[p].trm;
					break;
				}

			if (!source)
			{
				if (stats)
					stats->missing++;
				note("%s (slot %d) is built from slot %d, which is not loaded "
					"- left out", slot_label(slot, model), slot,
					slots[i].synthFrom);
				continue;
			}

			Pending made;
			const char* trouble = "";

			made.slot = slot;
			made.hasHead = false;
			made.name = model ? model : "NECK_CAP";
			made.origin = HD_MODEL_ORIGIN_5;
			made.pageBias = 0;
			made.cutKeeps = true;
			// A made model is whole. Pending is a plain struct with no
			// constructor, so every field of it has to be written here.
			made.cutNode = 0;
			made.twinOf = -1;
			made.neckBone = -1;
			made.neckNode = -1;
			made.headEntry = 0;
			made.headOrigin = HD_MODEL_ORIGIN_NONE;
			std::memset(&made.trm, 0, sizeof(made.trm));
			std::memset(&made.head, 0, sizeof(made.head));

			const double made_started = now_ms();
			const bool built = slots[i].synth(source, &made.trm, &trouble);

			read_ms += now_ms() - made_started;

			if (!built)
			{
				if (stats)
					stats->refused++;
				note("%s (slot %d): %s - left out", slot_label(slot, model),
					slot, trouble);
				continue;
			}

			/* Its pages are the caller's own numbers, in this bundle. A maker
			 * that reuses a page the source already wears costs the cache
			 * nothing, which is what the lid does. */
			for (int t = 0; t < made.trm.textures; t++)
				pages.push_back(made.trm.texture[t]);

			pending.push_back(made);
			continue;
		}

		if (kind == HD_BIND_LARA)
		{
			if (stats)
				stats->lara++;
			continue;
		}

		if (kind == HD_BIND_DUMMY)
		{
			if (stats)
				stats->dummy++;
			continue;
		}

		if (kind != HD_BIND_MODEL || !model)
		{
			if (stats)
				stats->noName++;
			note("slot %d has no name in this engine - classic", slot);
			continue;
		}

		const bool tr3 = slots[i].fromTr3;

		HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;
		int mod = -1;
		std::string path;

		/* A file the caller found itself - the wardrobe, and nothing else. No
		 * search at all; see HDModelSlot::path for why the wardrobe cannot go
		 * through find_model. */
		if (slots[i].path && *slots[i].path)
		{
			path = slots[i].path;
			mod = slots[i].mod;
			origin = mod >= 0 ? HD_MODEL_ORIGIN_MOD
				: tr3 ? HD_MODEL_ORIGIN_TR3 : HD_MODEL_ORIGIN_5;
		}
		else
		{
			// Timed because it is the largest phase of a level build that is
			// not reading or decoding anything: every slot with no .TRM
			// costs a walk of the ITEM folders, and every active mod adds
			// two more places to walk.
			const double find_began = now_ms();

			path = find_model(model, tr3, &origin, &mod);
			find_ms += now_ms() - find_began;
		}

		if (path.empty())
		{
			if (stats)
				stats->missing++;

			if (tr3)
				note("%s (slot %d) asks for %s.TRM out of Tomb Raider I-III "
					"Remastered, which %s - classic",
					slot_label(slot, model), slot, model,
					g_install_tr3.empty() ? "is not installed" : "does not have it");
			else
				note("%s (slot %d) asks for %s.TRM, which is in no ITEM folder - classic",
					slot_label(slot, model), slot, model);
			continue;
		}

		Pending here;

		here.slot = slot;
		here.hasHead = false;
		here.name = model;
		here.origin = origin;
		/* Whose page numbers these are. A mod's model
		 * names its own mod's pages, and only the ones the mod does not
		 * ship fall through to the install - see resolve_page. The mod's
		 * namespace therefore wins over the guest's: a guest a mod
		 * supplied is that mod's model now, and its origin= line is what
		 * sends its pages back to the I-III bundle. */
		here.pageBias = mod >= 0 ? HDPageModBase(mod)
			: tr3 ? HD_PAGE_ALT_BASE : 0;
		here.cutKeeps = true;
		here.cutNode = slots[i].onlyNode;
		here.twinOf = -1;
		here.neckBone = -1;
		here.neckNode = -1;
		here.headEntry = 0;
		here.path = path;
		here.headOrigin = HD_MODEL_ORIGIN_NONE;

		if (slots[i].onlyPage >= 0)
			here.cutPage.push_back(slots[i].onlyPage);
		std::memset(&here.fold, 0, sizeof(here.fold));
		std::memset(&here.trm, 0, sizeof(here.trm));
		std::memset(&here.head, 0, sizeof(here.head));

		const double read_started = now_ms();
		const char* trouble = "";
		const bool read = HDTrmLoad(path.c_str(), &here.trm, &trouble);

		read_ms += now_ms() - read_started;

		if (!read)
		{
			if (stats)
				stats->refused++;
			note("%s (slot %d): %s.TRM %s - classic",
				slot_label(slot, model), slot, model, trouble);
			continue;
		}

		/* The caller's own look at the file, before anything here touches
		 * it. */
		if (slots[i].measure)
			slots[i].measure(&here.trm);

		/* The mirrored weights, folded back - after the measure, on purpose.
		 * The measure reads the outfit's NECK positions and pages, which the
		 * fold never touches, and it sees the file as written. Everything
		 * that builds out of the weights comes after this line. */
		if (slots[i].mirror && slots[i].mirrors > 0)
			HDTrmFoldMirrored(&here.trm, slots[i].mirror, slots[i].mirrors,
				slots[i].mirrorBar, &here.fold);

		/* The donor game's own frame 0. What this is and
		 * what it needs is written out at HDModelSlot::classicPose; this is
		 * where it happens, in the same place and for the same reason the
		 * fold above does - before anything measures the vertices.
		 *
		 * The three things it needs are asked for in the order that makes the
		 * cheapest refusal first: whose file this is, then whether the name is
		 * in the table at all, and only then a walk of the donor game's level
		 * files. */
		if (slots[i].classicPose)
		{
			HDModelClassicPose said;
			const char* trouble = "";

			std::memset(&said, 0, sizeof(said));
			trm_box(here.trm, said.before);
			said.after[0] = said.before[0];
			said.after[1] = said.before[1];
			said.after[2] = said.before[2];
			said.nodes = trm_nodes(here.trm);
			said.game = mod >= 0 ? HDModsOrigin(mod) : 0;
			said.object = said.game ? HDClassicObjectId(model, said.game) : 0;

			HDClassicObject object;

			if (mod < 0)
				std::snprintf(said.why, sizeof(said.why),
					"the model did not come from a mod");
			else if (!said.game)
				std::snprintf(said.why, sizeof(said.why),
					"the mod's mod.txt names no origin= game");
			else if (!said.object)
				std::snprintf(said.why, sizeof(said.why),
					"%s is no inventory model of game %d", model, said.game);
			/* No I-III install is a refusal. `g_install_tr3` is the I-III
			 * bundle's game 3 folder and the reader wants the bundle, which
			 * is its parent - but parent_of("") is ".", which would send the
			 * walk into <cwd>\1\DATA on a machine with only the IV-VI
			 * remaster. */
			else if (g_install_tr3.empty())
				std::snprintf(said.why, sizeof(said.why),
					"there is no Tomb Raider I-III installation to read game "
					"%d's levels from", said.game);
			/* The reason alone, not the object it is about: every caller of
			 * this field names the game and the object itself. The one case
			 * above that says "game %d" is the one where the OBJECT is not
			 * known, so no caller can name it. */
			else if (!HDClassicRead(parent_of(g_install_tr3).c_str(), said.game,
				said.object, &object, &trouble))
				std::snprintf(said.why, sizeof(said.why), "%s", trouble);
			else
			{
				said.meshes = object.meshes;
				std::snprintf(said.level, sizeof(said.level), "%s", object.level);

				/* The mesh count is the proof that the two belong together.
				 * A .TRM's bone numbers ARE the classic mesh numbers, so an
				 * object with a different count is a different object and its
				 * frame would put this model's parts anywhere at all. */
				if (said.nodes != object.meshes)
					std::snprintf(said.why, sizeof(said.why),
						"%d nodes against the object's %d meshes",
						said.nodes, object.meshes);
				else if (!pose_classic(here.trm, object))
					std::snprintf(said.why, sizeof(said.why),
						"the object has no frame this could use");
				else
				{
					said.applied = true;
					trm_box(here.trm, said.after);
				}
			}

			g_pose[slot] = said;
		}

		/* A rigid face's neck, out of its own rig - see HDModelSlot::neckNode
		 * for the rule and rig_neck_of for the arithmetic. Here, while the
		 * file is still a file: the rig is gone once it is a buffer. */
		if (slots[i].rigidNode >= 0 && slots[i].neckNode >= 0)
		{
			RigNeck said;

			here.neckBone = rig_neck_of(here.trm, &said);

			if (here.neckBone >= 0)
			{
				here.neckNode = slots[i].neckNode;
				note("%s (slot %d): %d of %d vertices of the face take the "
					"body's neck (node %d) with the file's own weights - rig "
					"joints %s stand %.1f..%.1f from its head joint %d, the "
					"limit is %.0f", slot_label(slot, model), slot,
					said.vertices, here.trm.vertices, here.neckNode, said.list,
					said.nearest, said.farthest, said.headJoint,
					kNeckJointDistance);
			}
			else
				note("%s (slot %d): the face stays rigid on node %d - %s",
					slot_label(slot, model), slot, slots[i].rigidNode,
					here.trm.rigJoints <= 0 ? "the file carries no rig"
						: said.headJoint < 0 ? "a vertex names a bone its rig "
							"does not have"
						: "no joint of its rig stands beyond the limit");
		}

		/* Mod guns on the install's hands. What this is and
		 * why it is decided here rather than by the caller is written out at
		 * HDModelSlot::twinSlot; this is the arithmetic.
		 *
		 * The question asked of every page the model names is the cache's own -
		 * where would this page really be read from - so there is no second
		 * copy of that chain and no way for the two to drift apart. Only a page
		 * that lands in a game folder of the OTHER bundle counts: a mod's own
		 * page and one that falls through to the install are both the mod
		 * saying what it meant. */
		if (slots[i].twinSlot >= 0 && mod >= 0 && here.trm.textures > 0)
		{
			std::vector<int> foreign;

			for (int t = 0; t < here.trm.textures; t++)
			{
				const int raw = (int)here.trm.texture[t];
				const int from = HDPageCacheOriginOf(installRoot,
					HDPageCacheKeyOf(raw + here.pageBias));

				if (from >= 1 && from <= 3)
					foreign.push_back(raw);
			}

			if (!foreign.empty())
			{
				/* The install's own file of the same name, and the rest
				 * hands' pages that say which of its own are skin. Both are
				 * read from the INSTALLS alone - a mod that replaces the rest
				 * hands is answering a different question. */
				char twinPath[512], skinPath[512];
				HDTrmModel skin;
				std::vector<int> keep;
				const char* skinTrouble = "";
				const bool haveTwin = HDModelFindFile(model, false, false,
					twinPath, sizeof(twinPath), 0);
				/* The rest hands' own read is counted into read_ms, like the
				 * twin's below: it is a whole second .TRM off the disk per
				 * hand part with a mod's gun in it. */
				const double skin_started = now_ms();
				const bool haveSkin = slots[i].twinSkinOf &&
					HDModelFindFile(slots[i].twinSkinOf, false, false, skinPath,
						sizeof(skinPath), 0) &&
					HDTrmLoad(skinPath, &skin, &skinTrouble);

				read_ms += now_ms() - skin_started;

				if (haveSkin)
				{
					for (int t = 0; t < skin.textures; t++)
						keep.push_back((int)skin.texture[t]);

					HDTrmFree(&skin);
				}

				if (haveTwin && !keep.empty())
				{
					Pending twin;

					twin.slot = slots[i].twinSlot;
					twin.hasHead = false;
					twin.name = model;
					twin.origin = HD_MODEL_ORIGIN_5;
					twin.pageBias = 0;
					twin.cutPage = keep;
					twin.cutKeeps = true;
					// A hand twin is a whole model, never a node of one.
					twin.cutNode = 0;
					twin.twinOf = i;
					twin.neckBone = -1;
					twin.neckNode = -1;
					twin.headEntry = 0;
					twin.path = twinPath;
					twin.headOrigin = HD_MODEL_ORIGIN_NONE;
					std::memset(&twin.fold, 0, sizeof(twin.fold));
					std::memset(&twin.trm, 0, sizeof(twin.trm));
					std::memset(&twin.head, 0, sizeof(twin.head));

					const double twin_started = now_ms();
					const char* twinTrouble = "";
					const bool twinRead = HDTrmLoad(twinPath, &twin.trm,
						&twinTrouble);

					read_ms += now_ms() - twin_started;

					if (!twinRead)
						note("MODLOG %s (slot %d): the install's own %s.TRM %s "
							"- the mod's hands are drawn as they are",
							slot_label(slot, model), slot, model, twinTrouble);
					else
					{
						/* The twin's hands must be folded like any other hand
						 * file, and by the same rule: it is the same kind of
						 * file and the caller asked for the fold on this
						 * part. */
						if (slots[i].mirror && slots[i].mirrors > 0)
							HDTrmFoldMirrored(&twin.trm, slots[i].mirror,
								slots[i].mirrors, slots[i].mirrorBar,
								&twin.fold);

						/* If the caller already asked for a cut, the two
						 * become one. Only one cut reaches build(), so writing
						 * this one over the caller's would throw its rule away
						 * without a word.
						 *
						 * They combine exactly, and as a DROP set: what the
						 * caller's keep set leaves out is dropped anyway, so
						 * the union of that and the foreign pages says what
						 * both asked for. The other direction - a keep set
						 * minus the foreign pages - says the same thing until
						 * the two sets cancel, and an EMPTY keep set means
						 * "the whole file" to build() rather than "nothing".
						 * Dropping every page of a file is representable;
						 * keeping none of it is not.
						 *
						 * No current part does both - the one part with an
						 * onlyPage is the holster rig, which is kind 4 and
						 * never gets a twin - so the case is handled and
						 * logged in case it ever happens. */
						if (here.cutKeeps && !here.cutPage.empty())
						{
							std::vector<int> drop = foreign;

							for (int t = 0; t < here.trm.textures; t++)
							{
								const int raw = (int)here.trm.texture[t];
								bool wanted = false;

								for (size_t k = 0; k < here.cutPage.size(); k++)
									if (here.cutPage[k] == raw)
										wanted = true;

								for (size_t d = 0; d < drop.size(); d++)
									if (drop[d] == raw)
										wanted = true;

								if (!wanted)
									drop.push_back(raw);
							}

							note("MODLOG %s (slot %d): this part is already cut "
								"to %d page%s of its own, so the two cuts are "
								"made one - %d page%s of the file dropped, the "
								"%d foreign among them",
								slot_label(slot, model), slot,
								(int)here.cutPage.size(),
								here.cutPage.size() == 1 ? "" : "s",
								(int)drop.size(),
								drop.size() == 1 ? "" : "s",
								(int)foreign.size());

							here.cutPage = drop;
						}
						else
							here.cutPage = foreign;

						here.cutKeeps = false;

						std::string gunList, skinList;
						int guns = 0;

						for (size_t f = 0; f < foreign.size(); f++)
						{
							if (!skinList.empty())
								skinList += ", ";
							skinList += std::to_string(foreign[f]);
						}

						/* What the mod's file really keeps, asked of the cut
						 * that reaches the build and not of `foreign` - the
						 * two are the same list wherever the caller asked for
						 * no cut of its own, and where it did the log has to
						 * name what survives both. */
						for (int t = 0; t < here.trm.textures; t++)
						{
							const int raw = (int)here.trm.texture[t];
							bool dropped = false;

							for (size_t f = 0; f < here.cutPage.size(); f++)
								if (here.cutPage[f] == raw)
									dropped = true;

							if (dropped)
								continue;

							if (!gunList.empty())
								gunList += ", ";
							gunList += std::to_string(raw);
							guns++;
						}

						note("MODLOG %s (slot %d): mod%d's %s paints its HANDS "
							"with page%s %s of the Tomb Raider I-III bundle - "
							"the hands come from the install's own %s (the %d "
							"page%s it shares with %s), the gun from mod%d "
							"(page%s %s)", slot_label(slot, model), slot, mod,
							model, foreign.size() == 1 ? "" : "s",
							skinList.c_str(), model, (int)keep.size(),
							keep.size() == 1 ? "" : "s", slots[i].twinSkinOf,
							mod, guns == 1 ? "" : "s",
							guns ? gunList.c_str() : "none");

						/* Only the pages the cut keeps are paid for, the same
						 * rule the holster rig follows. */
						for (int t = 0; t < twin.trm.textures; t++)
						{
							const int raw = (int)twin.trm.texture[t];

							for (size_t f = 0; f < keep.size(); f++)
								if (keep[f] == raw)
								{
									pages.push_back(HDPageCacheKeyOf(raw));
									break;
								}
						}

						pending.push_back(twin);
					}
				}
				else
					note("MODLOG %s (slot %d): mod%d's %s wears another "
						"bundle's skin, and %s - the mod's hands are drawn as "
						"they are", slot_label(slot, model), slot, mod, model,
						haveTwin ? "the rest hands say nothing about which "
						"pages are skin" : "the install has no file of that "
						"name");
			}
		}

		/* A file the caller found itself says WHERE, in the same shape the
		 * three notes below use, so the wardrobe's parts have a note like
		 * every other model. */
		if (slots[i].path && *slots[i].path)
			note("%s (slot %d): %s.TRM comes from the wardrobe scan - %s",
				slot_label(slot, model), slot, model, path.c_str());
		else if (origin == HD_MODEL_ORIGIN_MOD)
			note("MODLOG %s (slot %d): %s.TRM comes from mod%d %s",
				slot_label(slot, model), slot, model, mod, HDModsName(mod));
		else if (origin == HD_MODEL_ORIGIN_TR3)
			note("%s (slot %d): %s.TRM is a guest out of Tomb Raider I-III "
				"Remastered", slot_label(slot, model), slot, model);
		else if (origin != HD_MODEL_ORIGIN_5)
			note("%s (slot %d): %s.TRM comes from the sibling game %d, not from 5",
				slot_label(slot, model), slot, model,
				origin == HD_MODEL_ORIGIN_4 ? 4 : 6);

		/* The separate HD face, if the remaster ships one for this model -
		 * by name, by the measured table or by the strip rule (see
		 * find_head_model above). */
		HDModelOrigin head_origin = HD_MODEL_ORIGIN_NONE;
		const char* head_how = "name";
		std::string head_name;
		const double head_find_began = now_ms();
		const std::string head_path = find_head_model(model, tr3, &head_origin,
			&head_how, &head_name, &here.headEntry, mod);

		// The face is a second walk of the same folders, so it belongs in
		// the same phase.
		find_ms += now_ms() - head_find_began;

		if (!head_path.empty())
		{
			const char* head_trouble = "";
			const double head_started = now_ms();

			here.hasHead = HDTrmLoad(head_path.c_str(), &here.head, &head_trouble);
			read_ms += now_ms() - head_started;
			here.headPath = head_path;
			here.headStem = head_name;
			here.headOrigin = head_origin;

			if (!here.hasHead)
				note("%s (slot %d): %s.TRM %s - the body draws without it",
					slot_label(slot, model), slot, head_name.c_str(), head_trouble);
			else if (std::strcmp(head_how, "name") != 0)
				note("%s (slot %d): the face is %s.TRM, found by the %s rule",
					slot_label(slot, model), slot, head_name.c_str(), head_how);
		}

		/* Only the page that is kept is paid for: the holster cut out of
		 * OUTFIT_TR3_CATSUIT wants one of that model's ten pages, and loading
		 * the other nine for it would cost more texture than the whole of
		 * Lara. */
		/* Through HDPageCacheKeyOf, exactly as build() does above, because the
		 * list the cache is handed and the keys the ranges carry have to be
		 * the same numbers - a page asked for under one key and drawn under
		 * another is a white tile. Once per texture slot here as well. */
		const double keys_began = now_ms();

		{
			const PageCut paid =
			{
				here.cutPage.empty() ? 0 : &here.cutPage[0],
				(int)here.cutPage.size(), here.cutKeeps
			};

			/* And the node cut pays for its own pages too. A page cut can be
			 * answered out of the texture list alone; a node cut cannot -
			 * which pages a node wears is a fact about its TRIANGLES. So they
			 * are walked once, here, where the .TRM is open and the build has
			 * not run yet. Only a slot that asked for a node cut pays for that
			 * walk: Lara's head out of OUTFIT_CLASSIC is two of that model's
			 * seven pages, 2.7 MiB instead of 9.3. */
			bool wornByNode[256];
			/* A SKINNED model only: dominant_node_of's answer for a facial rig
			 * depends on the neck bone, and that is measured in pass two,
			 * where there is a Model to keep it in. A rigid file with a node
			 * cut is therefore paid for whole - the safe way round, since a
			 * page short is a white tile and a page over is memory. No caller
			 * asks for that combination. */
			const bool byNode = here.cutNode != 0 && slots[i].rigidNode < 0;

			if (byNode)
			{
				const NodeCut nodeCut = { here.cutNode };

				std::memset(wornByNode, 0, sizeof(wornByNode));

				for (int tri = 0; tri + 2 < here.trm.indices; tri += 3)
				{
					const int layer = (int)here.trm.vertex[here.trm.index[tri]].layer;

					if (layer < 1 || layer > here.trm.textures || layer > 256)
						continue;

					if (nodeCut.wants(dominant_node_of(here.trm, tri, -1, -1, -1)))
						wornByNode[layer - 1] = true;
				}
			}

			for (int t = 0; t < here.trm.textures; t++)
				if (paid.wants((int)here.trm.texture[t]) &&
					(!byNode || (t < 256 && wornByNode[t])))
					pages.push_back(
						HDPageCacheKeyOf((int)here.trm.texture[t] + here.pageBias));
		}

		if (here.hasHead)
			for (int t = 0; t < here.head.textures; t++)
				pages.push_back(HDPageCacheKeyOf((int)here.head.texture[t] + here.pageBias));

		g_key_ms += now_ms() - keys_began;

		pending.push_back(here);
	}

	/* The pages, ADDED to what the rooms already loaded: a page a room and a
	 * model both wear keeps one texture and one slot number. */
	const double pages_began = now_ms();

	std::sort(pages.begin(), pages.end());
	pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

	if (!pages.empty())
	{
		HDPageCacheStats page_stats;
		const char* page_why = "";

		if (!HDPageCacheAppend(device, installRoot, pages.empty() ? 0 : &pages[0],
			(int)pages.size(), managedPages, threads, &page_stats, &page_why))
		{
			/* The pages did not load - the card ran out of video memory, or
			 * it cannot make the format at all. Going on
			 * would build models whose draw calls name page slots with no
			 * texture behind them, and a draw call with no texture is
			 * skipped, so the enemies would be INVISIBLE rather than classic.
			 * Refusing the whole model build hands every slot back to the
			 * classic path, which is a picture. */
			if (stats)
				stats->pageStats = page_stats;

			g_error = std::string("the model pages could not be loaded: ") + page_why;
			HDModelLevelEnd();
			*why = g_error.c_str();
			return false;
		}
		else if (stats)
		{
			stats->pageStats = page_stats;
			stats->pages = page_stats.slots;
		}
	}

	page_ms = now_ms() - pages_began;

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		note("%s", HDPageCacheNote(i));

	/* Pass two: the buffers. */
	const double buffers_began = now_ms();
	int mixed = 0;

	for (size_t i = 0; i < pending.size(); i++)
	{
		Pending& here = pending[i];
		Model model;

		model.name = here.name;
		model.origin = here.origin;
		model.nodes = 0;

		for (int k = 0; k < count; k++)
			/* A twin takes its skeleton from the part it stands beside. It is
			 * not a slot the caller asked for - this module made it - so
			 * `slots` has no row of its own for it, and without this it would
			 * draw with no nodes and no bind fold at all: the install's hands
			 * in their own bind pose while the mod's gun follows the
			 * animation. */
			if (slots[k].slot == here.slot ||
				(here.twinOf >= 0 && k == here.twinOf))
			{
				model.nodes = slots[k].nodes;
				model.rigidNode = slots[k].rigidNode;
				// A twin inherits it with the rest of the row - it is drawn
				// beside the model that asked.
				model.keepPoints = slots[k].keepPoints;
				model.additiveOneSided = slots[k].additiveOneSided;
				model.lightAsAdditive = slots[k].lightAsAdditive;

				if (slots[k].bind)
				{
					const int use = model.nodes < kJointBudget ? model.nodes : kJointBudget;

					for (int node = 0; node < use; node++)
					{
						model.bind[node][0] = slots[k].bind[node * 3 + 0];
						model.bind[node][1] = slots[k].bind[node * 3 + 1];
						model.bind[node][2] = slots[k].bind[node * 3 + 2];
					}

					model.hasBind = true;
				}

				/* The classic mesh TREE, for the head pass. */
				if (slots[k].parent)
				{
					const int use = model.nodes < kJointBudget ? model.nodes : kJointBudget;

					for (int node = 0; node < use; node++)
						model.parent[node] = slots[k].parent[node];

					model.hasTree = true;
				}

				break;
			}

		const char* trouble = "";

		model.fold = here.fold;
		model.neckBone = here.neckBone;
		model.neckNode = here.neckNode;

		const PageCut cut =
		{
			here.cutPage.empty() ? 0 : &here.cutPage[0],
			(int)here.cutPage.size(), here.cutKeeps
		};

		const NodeCut nodeCut = { here.cutNode };

		/* A rigid face whose file carries its joint block speaks - Lara's.
		 * Its own bones are the file's (HDTrmModel::ownBone), untouched by
		 * the neck rule above, which renamed them in `vertex`. */
		if (model.rigidNode >= 0 && here.trm.blockBind)
			model.lips = make_lips(here.trm, here.name, here.path, here.origin);

		if (!build(here.trm, model, model.rigidNode, here.pageBias, &mixed,
			&trouble, cut, nodeCut, model.lips ? &model.lips->base : 0))
		{
			if (stats)
				stats->refused++;
			note("%s (slot %d): %s", HDSlotName(here.slot) ? HDSlotName(here.slot) : "?",
				here.slot, trouble);
			free_model(model);
			HDTrmFree(&here.trm);
			if (here.hasHead)
				HDTrmFree(&here.head);
			continue;
		}

		g_models[here.slot] = model;

		if (stats)
		{
			stats->loaded++;
			stats->vertices += model.vertices;
			stats->indices += model.indices;
			stats->ranges += (int)(model.pass[0].size() + model.pass[1].size() +
				model.pass[2].size());
			stats->cutRanges += cut_ranges(model);
			stats->cutModels += cut_ranges(model) > 0 ? 1 : 0;
			stats->vertexBytes += (double)model.vertices * sizeof(HDModelVertex);
			stats->indexBytes += (double)model.indices * 2.0;

			if (here.origin == HD_MODEL_ORIGIN_MOD)
				stats->fromMod++;
			else if (here.origin == HD_MODEL_ORIGIN_4)
				stats->fromGame4++;
			else if (here.origin == HD_MODEL_ORIGIN_TR3)
				stats->fromTr3++;
			else
				stats->fromGame5++;
		}

		HDTrmFree(&here.trm);
	}

	if (stats)
		stats->mixedPageTriangles = mixed;

	/* Pass three: the separate faces, rigidly on ONE node - the bind-pose
	 * node nearest the face geometry's own centre.
	 * Without a bind pose there is nothing to measure against and the face
	 * rides on node 0, which is what the diagnostic tool sees when it is
	 * pointed at a bare .TRM. */
	for (size_t i = 0; i < pending.size(); i++)
	{
		Pending& here = pending[i];

		if (!here.hasHead)
			continue;

		if (!g_models.count(here.slot))
		{
			HDTrmFree(&here.head);
			continue;
		}

		const Model& body = g_models[here.slot];
		Model head;

		head.name = here.name + "_HEAD";
		head.origin = here.origin;
		head.nodes = body.nodes;
		head.hasBind = body.hasBind;
		std::memcpy(head.bind, body.bind, sizeof(head.bind));

		/* The table's node where it names one the body has, the measured one
		 * otherwise. */
		const HeadOfModel* entry = here.headEntry;
		const int node = entry && entry->node >= 0 && entry->node < body.nodes &&
			entry->node < kJointBudget ? entry->node
			: head_node_of(here.head, body);

		head.rigidNode = node < 0 ? 0 : node;

		/* The face's lips, while its vertices are still the file's - the
		 * turn below moves them, and the deformed copy is turned the same
		 * way after the deform. */
		head.lips = make_lips(here.head, here.headStem, here.headPath,
			here.headOrigin);

		/* The half turn about the node's x axis, through the node's bind:
		 * y and z mirror about the bind and the normals turn with them (a
		 * normal byte is biased by 127, so its negation is 254 minus it). A
		 * turn and not a mirror, so every triangle keeps its winding. Before
		 * the neck is looked for, which measures the face as it is drawn. */
		const bool turned = entry && entry->halfTurnX && body.hasBind &&
			head.rigidNode < kJointBudget;

		if (turned)
		{
			const float by = body.bind[head.rigidNode][1];
			const float bz = body.bind[head.rigidNode][2];

			for (int v = 0; v < here.head.vertices; v++)
			{
				HDTrmVertex& at = here.head.vertex[v];

				at.y = 2.0f * by - at.y;
				at.z = 2.0f * bz - at.z;
				at.ny = (unsigned char)(at.ny >= 255 ? 0 : 254 - at.ny);
				at.nz = (unsigned char)(at.nz >= 255 ? 0 : 254 - at.nz);
			}

			head.lips->turned = true;
			head.lips->turnY = by;
			head.lips->turnZ = bz;
		}

		/* The neck. It needs the classic TREE, which only a caller that
		 * walked the mesh tree can hand over - without it the face stays
		 * wholly rigid on the head node. */
		if (body.hasTree)
		{
			const int neck = neck_bone_of(here.head);
			const int up = neck >= 0 && head.rigidNode < kJointBudget
				? body.parent[head.rigidNode] : -1;

			if (neck >= 0 && up >= 0 && up != head.rigidNode && up < head.nodes)
			{
				head.neckBone = neck;
				head.neckNode = up;
			}
		}

		const char* trouble = "";
		int ignore = 0;

		// The whole face: a page cut is the BODY's business, and so is a node
		// cut.
		const PageCut all = { 0, 0, true };
		const NodeCut everyNode = { 0 };

		if (!build(here.head, head, head.rigidNode, here.pageBias, &ignore,
			&trouble, all, everyNode, &head.lips->base))
		{
			note("%s_HEAD (slot %d): %s", here.name.c_str(), here.slot, trouble);
			free_model(head);
			HDTrmFree(&here.head);
			continue;
		}

		g_heads[here.slot] = head;

		if (head.neckBone >= 0)
			note("%s (slot %d): the HD face %s rides on node %d of %d, its "
				"neck (own bone %d) on node %d%s",
				slot_label(here.slot, here.name.c_str()), here.slot,
				head.name.c_str(), head.rigidNode, head.nodes,
				head.neckBone, head.neckNode,
				turned ? ", turned half a turn about x" : "");
		else
			note("%s (slot %d): the HD face %s rides on node %d of %d%s",
				slot_label(here.slot, here.name.c_str()), here.slot,
				head.name.c_str(), head.rigidNode, head.nodes,
				turned ? ", turned half a turn about x" : "");

		if (stats)
		{
			stats->heads++;
			stats->vertices += head.vertices;
			stats->indices += head.indices;
			stats->ranges += (int)(head.pass[0].size() + head.pass[1].size() +
				head.pass[2].size());
			stats->cutRanges += cut_ranges(head);
			stats->cutModels += cut_ranges(head) > 0 ? 1 : 0;
			stats->vertexBytes += (double)head.vertices * sizeof(HDModelVertex);
			stats->indexBytes += (double)head.indices * 2.0;
		}

		HDTrmFree(&here.head);
	}

	/* The faces' track files, one line each - the separate faces first, then
	 * a rigid face of the slots' own (Lara's). Each file is opened for its
	 * first word and closed again; the frames are read while a cut plays. */
	for (std::map<int, Model>::const_iterator at = g_heads.begin();
		at != g_heads.end(); ++at)
		if (at->second.lips)
			lips_lines(at->first, *at->second.lips);

	for (std::map<int, Model>::const_iterator at = g_models.begin();
		at != g_models.end(); ++at)
		if (at->second.lips)
			lips_lines(at->first, *at->second.lips);

	/* The coverage count. A slot whose model has geometry for every classic
	 * node is FULL; one with a gap is PARTIAL. */
	if (stats)
	{
		for (std::map<int, Model>::const_iterator at = g_models.begin();
			at != g_models.end(); ++at)
		{
			const Model& model = at->second;
			int covered = 0;

			for (int node = 0; node < model.nodes && node < kJointBudget; node++)
				if (model.covered & (1u << node))
					covered++;

			stats->nodesTotal += model.nodes;
			stats->nodesCovered += covered;

			if (covered >= model.nodes)
				stats->fullSlots++;
			else
				stats->partialSlots++;
		}

		double trm_opens1 = 0, trm_loads1 = 0, trm_bytes1 = 0;

		HDTrmCounters(&trm_opens1, &trm_loads1, &trm_bytes1);

		stats->nodesFallback = stats->nodesTotal - stats->nodesCovered;
		stats->hash = g_model_hash_on ? g_model_hash : 0;
		stats->hashMs = g_model_hash_ms - hash_ms0;
		stats->readMs = read_ms;
		stats->buildMs = now_ms() - started - read_ms - stats->hashMs;

		/* The phases of buildMs. They do not
		 * add up to it and are not meant to - what is left over is the
		 * bookkeeping between them - but a phase that has run away shows up
		 * here as the number it is. bufferMs is taken last because pass two
		 * is where this block stands. */
		stats->findMs = find_ms;
		stats->keyMs = g_key_ms;
		stats->pageMs = page_ms;
		stats->bufferMs = now_ms() - buffers_began - stats->hashMs;
		stats->trmOpens = trm_opens1 - trm_opens0;
		stats->trmLoads = trm_loads1 - trm_loads0;
		stats->trmBytes = trm_bytes1 - trm_bytes0;
	}

	return true;
}

/* Kept across a level, like the page cache's own alternate root: it names an
 * installation, not a level. */
void HDModelTr3Root(const char* root)
{
	g_install_tr3 = root ? root : "";
}

bool HDModelFindFile(const char* name, bool fromTr3, bool allowMods, char* out,
	int size, int* mod)
{
	if (mod)
		*mod = -1;

	if (out && size > 0)
		*out = 0;

	if (!name || !*name || !out || size <= 0)
		return false;

	HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;
	const std::string path = find_model(name, fromTr3, &origin, mod,
		allowMods ? kModsAny : kModsNone);

	if (path.empty() || (int)path.size() >= size)
		return false;

	std::memcpy(out, path.c_str(), path.size() + 1);
	return true;
}

void HDModelBuildHash(int on)
{
	g_model_hash_on = on ? 1 : 0;
}

double HDModelHashMs()
{
	return g_model_hash_ms;
}

void HDModelLevelEnd()
{
	for (std::map<int, Model>::iterator at = g_models.begin(); at != g_models.end(); ++at)
		free_model(at->second);

	for (std::map<int, Model>::iterator at = g_heads.begin(); at != g_heads.end(); ++at)
		free_model(at->second);

	g_models.clear();
	g_heads.clear();
	g_notes.clear();
	g_pose.clear();
	g_lips_lines.clear();
	g_lips_events.clear();
}

/* The warm-up. A model's vertex and index buffer are D3DPOOL_MANAGED, and a
 * managed resource is a system-memory copy until something DRAWS with it:
 * Direct3D uploads it to the card the first time it is bound, inside the
 * first frame it appears in. Without a warm-up the first seconds of a level
 * pay an upload per model, per page and per room inside frames that are
 * meant to draw.
 *
 * PreLoad is Direct3D's own answer to exactly that and costs nothing when the
 * resource is already resident. Called while the loading bar still stands,
 * where a stall is invisible, instead of in the first second of play.
 *
 * Returns how many buffers were offered, for the caller's log line. */
int HDModelPreload()
{
	int done = 0;

	for (std::map<int, Model>::iterator at = g_models.begin(); at != g_models.end(); ++at)
	{
		if (at->second.vb) { at->second.vb->PreLoad(); done++; }
		if (at->second.ib) { at->second.ib->PreLoad(); done++; }
	}

	for (std::map<int, Model>::iterator at = g_heads.begin(); at != g_heads.end(); ++at)
	{
		if (at->second.vb) { at->second.vb->PreLoad(); done++; }
		if (at->second.ib) { at->second.ib->PreLoad(); done++; }
	}

	return done;
}

/* The models' warm-up for the marble floors' mirror (hd_render.cpp, "the
 * marble floors"). The mirror draws the frame's objects - Lara, the
 * items, all through HDModelDraw's one pair of shaders - once more with a
 * clip plane on, and the driver makes that pair again for the plane the
 * first time a model stands in a mirror, a stall of about a tenth of a
 * second (RoomProbe -wetlara). So once for the pair's life (made in
 * HDModelInit, released in HDModelShutdown), after a level's models are
 * built, one triangle of a model is drawn through it with the plane on,
 * into a target of 16 x 16 of its own, and the queue is drained; nothing
 * of it reaches the frame, and the device's state is put back. Returns
 * the time in milliseconds, or -1 when nothing was drawn. */
double HDModelMirrorWarmUp()
{
	const Model* model = 0;

	for (std::map<int, Model>::const_iterator at = g_models.begin();
		at != g_models.end() && !model; ++at)
		if (at->second.vb && at->second.ib && at->second.vertices >= 3 &&
			at->second.indices >= 3)
			model = &at->second;

	if (!g_device || !g_vs || !g_ps || !g_decl || !model || g_mirror_warmed)
		return -1.0;

	LARGE_INTEGER frequency, t0, t1;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&t0);

	IDirect3DSurface9* target = 0;
	IDirect3DSurface9* depth = 0;
	IDirect3DSurface9* warm = 0;
	IDirect3DSurface9* warmDepth = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return -1.0;

	g_device->GetDepthStencilSurface(&depth);

	if (FAILED(target->GetDesc(&desc)) ||
		FAILED(g_device->CreateRenderTarget(16, 16, desc.Format,
		D3DMULTISAMPLE_NONE, 0, FALSE, &warm, 0)) ||
		FAILED(g_device->CreateDepthStencilSurface(16, 16, D3DFMT_D24S8,
		D3DMULTISAMPLE_NONE, 0, TRUE, &warmDepth, 0)))
	{
		if (warm) warm->Release();
		if (depth) depth->Release();
		target->Release();
		return -1.0;
	}

	// The device's state as the level left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0, clipOn = 0;
	float clip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3DVIEWPORT9 port = {};

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	g_device->GetIndices(&indices);
	g_device->GetViewport(&port);
	g_device->GetClipPlane(0, clip);
	g_device->GetRenderState(D3DRS_CLIPPLANEENABLE, &clipOn);

	const D3DVIEWPORT9 corner = { 0, 0, 16, 16, 0.0f, 1.0f };
	const float keepAll[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	g_device->SetRenderTarget(0, warm);
	g_device->SetDepthStencilSurface(warmDepth);
	g_device->SetViewport(&corner);
	g_device->BeginScene();
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER |
		D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
	g_device->SetClipPlane(0, keepAll);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);
	g_device->SetStreamSource(0, model->vb, 0, sizeof(HDModelVertex));
	g_device->SetIndices(model->ib);
	g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
		(UINT)model->vertices, 0, 1);
	g_device->EndScene();

	// The queue drained, so the driver's work is done here.
	IDirect3DQuery9* done = 0;

	if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &done)) && done)
	{
		done->Issue(D3DISSUE_END);

		while (done->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE)
			;

		done->Release();
	}

	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, clipOn);
	g_device->SetClipPlane(0, clip);
	g_device->SetRenderTarget(0, target);
	g_device->SetDepthStencilSurface(depth);
	g_device->SetViewport(&port);
	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetIndices(indices);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (stream) stream->Release();
	if (indices) indices->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	warm->Release();
	warmDepth->Release();
	target->Release();

	if (depth)
		depth->Release();

	g_mirror_warmed = true;
	QueryPerformanceCounter(&t1);
	return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
		(double)frequency.QuadPart;
}

void HDModelFrameBegin(const D3DMATRIX* projection, int bulbCount,
	unsigned int passes)
{
	if (!HDModelReady())
		return;

	g_in_frame = true;
	g_draw_calls = 0;
	g_pass_mask = passes;
	g_frame_bulbs = bulbCount < HD_MAX_BULBS ? bulbCount : HD_MAX_BULBS;

	if (projection)
	{
		// Kept, because HDModelOffCentre builds this frame's off-centre
		// matrices out of it and has to put it back afterwards.
		std::memcpy(g_frame_proj, projection, sizeof(g_frame_proj));
		set_matrix_constant(0, g_frame_proj);
	}

	g_off_centre = false;

	/* The clip planes start every bracket as the planes that keep every
	 * pixel, uploaded here: a register of the pixel file is the device's,
	 * and the pickup display's bracket draws without calling HDModelClip. */
	std::memcpy(g_clip_now, kClipKeepAll, sizeof(g_clip_now));

	if (g_device)
		g_device->SetPixelShaderConstantF(5, g_clip_now, 3);
}

void HDModelClip(const float* planes)
{
	if (!g_in_frame || !g_device)
		return;

	const float* want = planes ? planes : kClipKeepAll;

	if (!std::memcmp(want, g_clip_now, sizeof(g_clip_now)))
		return;

	std::memcpy(g_clip_now, want, sizeof(g_clip_now));
	g_device->SetPixelShaderConstantF(5, g_clip_now, 3);
}

void HDModelOffCentreMatrix(const D3DMATRIX* base, float kx, float ky,
	D3DMATRIX* out)
{
	if (!base || !out)
		return;

	*out = *base;

	/* _31 and _32, the two entries a plain perspective projection leaves at
	 * zero (hd_render.cpp, HDRenderMakeProjection writes _11, _22, _33, _43
	 * and _34 and nothing else). clip.x picks up kx * _11 * z, which after
	 * the division by w = z is a constant kx * _11 in normalised device x,
	 * and the viewport turns that into kx * _11 * centreX = kx * persp
	 * pixels - which is X - centreX, the offset asked for. The y row carries
	 * the same argument through _22, whose sign already holds the screen's
	 * downward y. */
	out->_31 = kx * base->_11;
	out->_32 = ky * base->_22;
}

void HDModelOffCentre(float kx, float ky)
{
	/* `g_in_frame` says a bracket has been opened at least once, which is
	 * what makes g_frame_proj a matrix rather than the identity it starts
	 * as - it is NOT cleared when a bracket closes, so it is not a test of
	 * "am I inside one". That test is not needed: both callers -
	 * HDObjectFrameDraw for the world and the ring, and hd_object.cpp's
	 * pickup_draw for the display in the corner - sit between their
	 * own HDModelFrameBegin and the (0, 0) they end with, so c0 always
	 * holds the plain frame projection
	 * again by the time the bracket closes and neither the room path nor
	 * the classic path can inherit a moved one. */
	if (!g_in_frame || !g_device)
	{
		g_off_centre = false;
		return;
	}

	const bool want = (kx != 0.0f || ky != 0.0f);

	// The common case by far: an ordinary object after another ordinary
	// object, and not a register touched.
	if (!want && !g_off_centre)
		return;

	D3DMATRIX base;
	D3DMATRIX out;

	std::memcpy(&base, g_frame_proj, sizeof(base));
	HDModelOffCentreMatrix(&base, kx, ky, &out);
	set_matrix_constant(0, (const float*)&out);

	g_off_centre = want;
}

bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut, int cull, int lipsTrack, int lipsFrame,
	const HDModelSoft* soft)
{
	if (!g_in_frame || !nodeMatrix || nodeCount <= 0)
		return false;

	const Model* model = find(g_models, slot);

	if (!model || !model->vb)
		return false;

	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);

	draw_model(*model, nodeMatrix, nodeCount, nodeLight, nodeMask, lighting,
		cut, cull, lips_stream(*model, slot, false, lipsTrack, lipsFrame), soft);
	return true;
}

bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut, int cull, int lipsTrack, int lipsFrame)
{
	if (!g_in_frame || !nodeMatrix || nodeCount <= 0)
		return false;

	std::map<int, Model>::iterator at = g_heads.find(slot);

	if (at == g_heads.end() || !at->second.vb)
		return false;

	/* The face follows its node's bit. A separate <NAME>_HEAD.TRM is built
	 * rigidly on one node, so every one of its ranges carries that node
	 * number and the same mask answers for it - a head shot away by
	 * mesh_bits takes the HD face with it. */
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);

	draw_model(at->second, nodeMatrix, nodeCount, nodeLight, nodeMask, lighting,
		cut, cull, lips_stream(at->second, slot, true, lipsTrack, lipsFrame), 0);
	return true;
}

int HDModelDrawCalls()
{
	return g_draw_calls;
}

/* ---------------------------------------------------- the shadow's cast -- */

bool HDModelCastReady()
{
	return HDModelReady() && g_cast_vs && g_cast_ps;
}

void HDModelCastBegin(const float* castClip)
{
	g_in_cast = false;
	g_cast_calls = 0;

	if (!HDModelCastReady() || !castClip)
		return;

	g_device->SetVertexShader(g_cast_vs);
	g_device->SetPixelShader(g_cast_ps);
	g_device->SetVertexDeclaration(g_decl);
	set_matrix_constant(0, castClip);
	g_in_cast = true;
}

void HDModelCastEnd()
{
	g_in_cast = false;
}

bool HDModelCastDraw(int slot, bool head, const float* nodeMatrix, int nodeCount,
	unsigned int nodeMask, int lipsTrack, int lipsFrame, const HDModelSoft* soft)
{
	if (!g_in_cast || !nodeMatrix || nodeCount <= 0)
		return false;

	const Model* model = find(head ? g_heads : g_models, slot);

	if (!model || !model->vb)
		return false;

	/* The joints and the soft motion as draw_model writes them; the cast
	 * vertex shader reads nothing else of the model's registers. */
	const int nodes = nodeCount < kJointBudget ? nodeCount : kJointBudget;

	for (int node = 0; node < kJointBudget; node++)
	{
		const int from = node < nodes ? node : nodes - 1;

		set_joint(node, nodeMatrix + from * 16, model->hasBind ? model->bind[from] : 0);
	}

	static const HDModelSoft kNoSoft = {};

	g_device->SetVertexShaderConstantF(163, (soft ? soft : &kNoSoft)->constants, 4);

	g_device->SetStreamSource(0, lips_stream(*model, slot, head, lipsTrack, lipsFrame),
		0, sizeof(HDModelVertex));
	g_device->SetIndices(model->ib);

	/* The opaque and the alpha-tested pass: what the picture draws solid.
	 * The additive pass casts nothing - a glow throws no shadow. */
	for (int pass = 0; pass < 2; pass++)
	{
		const std::vector<Range>& ranges = model->pass[pass];

		if (ranges.empty())
			continue;

		float constants[4] = { pass == 1 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

		g_device->SetPixelShaderConstantF(4, constants, 1);

		// The ranges merged as draw_model merges them.
		for (size_t i = 0; i < ranges.size(); )
		{
			const Range& first = ranges[i];

			// The glass is drawn with the glows and is no solid surface.
			if (!(nodeMask & (1u << (first.node & 31))) || first.blend)
			{
				i++;
				continue;
			}

			unsigned int start = first.start;
			unsigned int count = first.count;
			size_t next = i + 1;

			while (next < ranges.size())
			{
				const Range& more = ranges[next];

				if (more.pageSlot != first.pageSlot || more.start != start + count ||
					!(nodeMask & (1u << (more.node & 31))) ||
					more.cut != first.cut || more.blend)
					break;

				count += more.count;
				next++;
			}

			g_device->SetTexture(0, first.pageSlot >= 0
				? (IDirect3DBaseTexture9*)HDPageCacheSlotTexture(first.pageSlot) : 0);

			// The cut-out ranges of the opaque pass keep their alpha test.
			if (pass == 0 && constants[0] != (first.cut ? 1.0f : 0.0f))
			{
				constants[0] = first.cut ? 1.0f : 0.0f;
				g_device->SetPixelShaderConstantF(4, constants, 1);
			}

			g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
				(UINT)model->vertices, start, count / 3);

			g_cast_calls++;
			i = next;
		}
	}

	return true;
}

int HDModelCastCalls()
{
	return g_cast_calls;
}

/* ----------------------------------------------- the shadow's receive -- */

bool HDModelReceiveReady()
{
	return HDModelReady() && g_receive_ps;
}

void HDModelReceiveBegin()
{
	g_in_receive = false;
	g_receive_calls = 0;

	if (!HDModelReceiveReady() || !g_in_frame)
		return;

	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_receive_ps);
	g_device->SetVertexDeclaration(g_decl);
	g_in_receive = true;
}

void HDModelReceiveEnd()
{
	g_in_receive = false;
}

bool HDModelReceiveDraw(int slot, bool head, const float* nodeMatrix, int nodeCount,
	unsigned int nodeMask, const HDModelNeckCut* cut, int cull, int lipsTrack, int lipsFrame,
	const HDModelSoft* soft)
{
	if (!g_in_receive || !nodeMatrix || nodeCount <= 0)
		return false;

	const Model* model = find(head ? g_heads : g_models, slot);

	if (!model || !model->vb)
		return false;

	/* What puts a vertex where the solid draw put it, written as draw_model
	 * writes it: all 32 joints with the same rule for the nodes past the
	 * caller's count, the head cut's two numbers (c162), the soft motion
	 * (c163 - c166), the same stream - a speaking face's deformed buffer of
	 * the same frame - and the same indices. The projection in c0 - c3 is
	 * the one the solid draw left there. ModelVS reads its other registers
	 * for the light and the sheen alone, which the receive shader does not
	 * read. */
	const int nodes = nodeCount < kJointBudget ? nodeCount : kJointBudget;

	for (int node = 0; node < kJointBudget; node++)
	{
		const int from = node < nodes ? node : nodes - 1;

		set_joint(node, nodeMatrix + from * 16, model->hasBind ? model->bind[from] : 0);
	}

	const float neck[4] =
	{
		cut && cut->on > 0.0f ? cut->node : 0.0f,
		cut && cut->on > 0.0f ? cut->bar : 1.0e6f,
		0.0f,
		0.0f
	};

	g_device->SetVertexShaderConstantF(162, neck, 1);

	static const HDModelSoft kNoSoft = {};

	g_device->SetVertexShaderConstantF(163, (soft ? soft : &kNoSoft)->constants, 4);

	g_device->SetStreamSource(0, lips_stream(*model, slot, head, lipsTrack, lipsFrame),
		0, sizeof(HDModelVertex));
	g_device->SetIndices(model->ib);

	// The draw's own cull, as draw_model sets and puts it back.
	if (cull != HD_MODEL_CULL_NONE)
		g_device->SetRenderState(D3DRS_CULLMODE,
			cull == HD_MODEL_CULL_CW ? D3DCULL_CW : D3DCULL_CCW);

	/* The opaque and the alpha-tested pass, never the additive one: what the
	 * solid pass drew and what wrote the depth the test compares with. */
	for (int pass = 0; pass < 2; pass++)
	{
		const std::vector<Range>& ranges = model->pass[pass];

		if (ranges.empty())
			continue;

		// gPass as draw_model writes it: x the alpha test, z the head cut.
		float constants[4] =
		{
			pass == 1 ? 1.0f : 0.0f,
			0.0f,
			cut && cut->on > 0.0f ? 1.0f : 0.0f,
			1.0f
		};

		g_device->SetPixelShaderConstantF(4, constants, 1);

		// The ranges merged as draw_model merges them.
		for (size_t i = 0; i < ranges.size(); )
		{
			const Range& first = ranges[i];

			// The glass is drawn with the glows and is no solid surface.
			if (!(nodeMask & (1u << (first.node & 31))) || first.blend)
			{
				i++;
				continue;
			}

			unsigned int start = first.start;
			unsigned int count = first.count;
			size_t next = i + 1;

			while (next < ranges.size())
			{
				const Range& more = ranges[next];

				if (more.pageSlot != first.pageSlot || more.start != start + count ||
					!(nodeMask & (1u << (more.node & 31))) ||
					more.cut != first.cut || more.blend)
					break;

				count += more.count;
				next++;
			}

			g_device->SetTexture(0, first.pageSlot >= 0
				? (IDirect3DBaseTexture9*)HDPageCacheSlotTexture(first.pageSlot) : 0);

			// The cut-out ranges of the opaque pass keep their alpha test.
			if (pass == 0 && constants[0] != (first.cut ? 1.0f : 0.0f))
			{
				constants[0] = first.cut ? 1.0f : 0.0f;
				g_device->SetPixelShaderConstantF(4, constants, 1);
			}

			g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
				(UINT)model->vertices, start, count / 3);

			g_receive_calls++;
			i = next;
		}
	}

	if (cull != HD_MODEL_CULL_NONE)
		g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	return true;
}

int HDModelReceiveCalls()
{
	return g_receive_calls;
}

/* ------------------------------------------------------------ the lips -- */

namespace
{
	Lips* lips_of(int slot, bool head)
	{
		const Model* model = find(head ? g_heads : g_models, slot);

		return model ? model->lips : 0;
	}
}

int HDModelLipsOpen(int slot, bool head, int track)
{
	Lips* lips = lips_of(slot, head);

	if (!lips || !lips->mapped || track < 0)
		return 0;

	if (lips->track == track)
		return lips->clip.file ? lips->clip.frames : 0;

	/* The loop and the firing face: the block in the face's own .TRM, one
	 * clip for both, so a switch between them keeps the clip and the buffer
	 * and only forgets the frame shown - their keys mean different things.
	 * A face without a loop answers 0 and keeps what it has open. The track
	 * whose file was missing stays remembered across the switch. */
	if (track >= HD_LIPS_TRACK_LOOP)
	{
		if ((track != HD_LIPS_TRACK_LOOP && track != HD_LIPS_TRACK_FIRE) ||
			lips->loop <= 0 || (track == HD_LIPS_TRACK_FIRE &&
			lips->blockFrames <= HD_LIPS_FIRE_FRAME))
			return 0;

		if (lips->track >= HD_LIPS_TRACK_LOOP && lips->clip.file)
		{
			lips->track = track;
			lips->shownFrame = -1;
			return lips->clip.frames;
		}

		const int missing = lips->missing;
		const char* trouble = "";

		lips_close(lips);
		lips->missing = missing;
		lips->track = track;
		HDTrlOpenBlock(lips->trmPath.c_str(), lips->blockAt, lips->blockFrames,
			lips->map.columns, &lips->clip, &trouble);

		return lips->clip.file ? lips->clip.frames : 0;
	}

	/* A track whose file was not there the last time it was asked for, asked
	 * again after the face went to its loop: still not there, and the loop
	 * is not closed for it. */
	if (track == lips->missing)
		return 0;

	lips_close(lips);
	lips->track = track;

	/* A file that does not fit the face (the wrong column count) is as good
	 * as none, and so is a face without one: it draws as it was built. */
	const char* trouble = "";

	HDTrlOpen(lips_file(*lips, track).c_str(), lips->map.columns, &lips->clip,
		&trouble);

	if (!lips->clip.file)
		lips->missing = track;

	return lips->clip.file ? lips->clip.frames : 0;
}

int HDModelLipsLoopKey(int frame, int past)
{
	return past * HD_LIPS_KEY_STRIDE + frame;
}

int HDModelLipsFireKey(int from, int step)
{
	return step * HD_LIPS_KEY_STRIDE + (step >= HD_LIPS_EASE_IN_TICKS || from < 0
		? 0 : from + 1);
}

int HDModelLipsLoop(int slot, bool head)
{
	const Lips* lips = lips_of(slot, head);

	return lips && lips->mapped ? lips->loop : 0;
}

float HDModelLipsBlink(int slot, bool head, int frame)
{
	const Lips* lips = lips_of(slot, head);

	if (!lips)
		return -1.0f;

	for (size_t i = 0; i < lips->blinkStart.size(); i++)
		if (lips->blinkStart[i] == frame)
			return lips->blinkLid[i];

	return -1.0f;
}

int HDModelLipsFrames(int slot, bool head)
{
	const Lips* lips = lips_of(slot, head);

	return lips && lips->clip.file ? lips->clip.frames : 0;
}

void HDModelLipsClose()
{
	for (std::map<int, Model>::iterator at = g_models.begin(); at != g_models.end(); ++at)
		lips_close(at->second.lips);

	for (std::map<int, Model>::iterator at = g_heads.begin(); at != g_heads.end(); ++at)
		lips_close(at->second.lips);
}

int HDModelLipsLineCount()
{
	return (int)g_lips_lines.size();
}

const char* HDModelLipsLine(int at)
{
	return at >= 0 && at < (int)g_lips_lines.size() ? g_lips_lines[at].c_str() : "";
}

int HDModelLipsEventCount()
{
	return (int)g_lips_events.size();
}

const HDModelLipsEvent* HDModelLipsEventAt(int at)
{
	return at >= 0 && at < (int)g_lips_events.size() ? &g_lips_events[at] : 0;
}

void HDModelLipsEventsClear()
{
	g_lips_events.clear();
}


/* ------------------------------------------------------------ what is up -- */

bool HDModelHas(int slot)
{
	return find(g_models, slot) != 0;
}

const char* HDModelName(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->name.c_str() : 0;
}

HDModelOrigin HDModelOriginOf(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->origin : HD_MODEL_ORIGIN_NONE;
}

int HDModelNodes(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->nodes : 0;
}

int HDModelNodeVertices(int slot, int node)
{
	const Model* model = find(g_models, slot);

	if (!model || node < 0 || node >= kJointBudget)
		return 0;

	return model->nodeVertices[node];
}

bool HDModelNodeCovered(int slot, int node)
{
	const Model* model = find(g_models, slot);

	if (!model || node < 0 || node >= kJointBudget)
		return false;

	return (model->covered & (1u << node)) != 0;
}

void HDModelBorrowJoints(const float* bind, int node, const float* at,
	float* out, int nodes)
{
	if (!bind || !at || !out || node < 0 || node >= nodes)
		return;

	for (int j = 0; j < nodes && j < kJointBudget; j++)
	{
		const float dx = bind[j * 3 + 0] - bind[node * 3 + 0];
		const float dy = bind[j * 3 + 1] - bind[node * 3 + 1];
		const float dz = bind[j * 3 + 2] - bind[node * 3 + 2];
		float* m = out + j * 16;

		memcpy(m, at, sizeof(float) * 16);
		m[12] = at[12] + dx * at[0] + dy * at[4] + dz * at[8];
		m[13] = at[13] + dx * at[1] + dy * at[5] + dz * at[9];
		m[14] = at[14] + dx * at[2] + dy * at[6] + dz * at[10];
	}
}

int HDModelMaxBone(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->maxBone : -1;
}

bool HDModelBindDrift(int slot, float* mean, float* worst)
{
	const Model* model = find(g_models, slot);

	if (!model || !model->hasDrift)
		return false;

	if (mean)
		*mean = model->driftMean;

	if (worst)
		*worst = model->driftWorst;

	return true;
}

int HDModelNodeCentre(int slot, int node, float* centre)
{
	const Model* model = find(g_models, slot);

	if (!model || node < 0 || node >= kJointBudget)
		return 0;

	if (centre)
		for (int k = 0; k < 3; k++)
			centre[k] = model->nodeCentre[node][k];

	return model->nodeSingle[node];
}

int HDModelVertices(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->vertices : 0;
}

int HDModelIndices(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->indices : 0;
}

int HDModelRanges(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? (int)(model->pass[0].size() + model->pass[1].size() +
		model->pass[2].size()) : 0;
}

int HDModelCutRanges(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? cut_ranges(*model) : 0;
}

int HDModelSetGlassNodes(int slot, unsigned int nodes)
{
	const std::map<int, Model>::iterator at = g_models.find(slot);

	if (at == g_models.end())
		return 0;

	Model& model = at->second;

	model.blendRanges = 0;

	for (Range& range : model.pass[HD_TRM_RUN_ALPHA])
	{
		range.blend = range.glass && HDTrmTriangleIsGlass(HD_TRM_RUN_ALPHA,
			HD_TRM_MATERIAL_GLASS, range.node, nodes);

		if (range.blend)
			model.blendRanges++;
	}

	/* A copy of the glass in the additive run is left out where its node
	 * draws glass: the glass is that face, added once. */
	for (Range& range : model.pass[HD_TRM_RUN_ADDITIVE])
		range.dropped = range.copy && HDTrmTriangleIsGlass(HD_TRM_RUN_ALPHA,
			HD_TRM_MATERIAL_GLASS, range.node, nodes);

	return model.blendRanges;
}

void HDModelGlassCounts(int slot, HDModelGlassCount* out)
{
	if (!out)
		return;

	memset(out, 0, sizeof(*out));
	out->opacity = 1.0f;
	out->factor = 1.0f;

	const Model* model = find(g_models, slot);

	if (!model)
		return;

	for (const Range& range : model->pass[HD_TRM_RUN_ALPHA])
	{
		if (!range.blend)
			continue;

		out->blended++;
		out->sided += range.sided ? 1 : 0;

		if (range.opacity < out->opacity)
			out->opacity = range.opacity;

		const float weight = HDGlassWeight(range.opacity, range.sided);

		if (g_blend_factor && weight < out->factor)
			out->factor = weight;
	}

	for (const Range& range : model->pass[HD_TRM_RUN_ADDITIVE])
	{
		out->copies += range.copy ? 1 : 0;
		out->dropped += range.dropped ? 1 : 0;
	}
}

int HDModelGlassRanges(int slot, int* blended)
{
	const Model* model = find(g_models, slot);
	int glass = 0;

	if (blended)
		*blended = model ? model->blendRanges : 0;

	if (model)
		for (const Range& range : model->pass[HD_TRM_RUN_ALPHA])
			glass += range.glass ? 1 : 0;

	return glass;
}

int HDModelPageCount(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? (int)model->pages.size() : 0;
}

int HDModelPage(int slot, int at)
{
	const Model* model = find(g_models, slot);

	if (!model || at < 0 || at >= (int)model->pages.size())
		return -1;

	return model->pages[(size_t)at];
}

/* The cloud's own axes, measured in build() - the note at Model::axis says
 * why they are taken there. `axes` takes nine floats, three unit row vectors,
 * longest first and right handed; `extent` takes the cloud's span along each
 * of them. False for a model that has none (fewer than three vertices). */
bool HDModelPrincipalAxes(int slot, float* axes, float* extent)
{
	const Model* model = find(g_models, slot);

	if (!model || !model->hasAxes)
		return false;

	for (int k = 0; k < 3; k++)
	{
		if (extent)
			extent[k] = model->axisExtent[k];

		if (axes)
			for (int r = 0; r < 3; r++)
				axes[k * 3 + r] = model->axis[k][r];
	}

	return true;
}

/* The fit, out of the two models' own SHAPES.
 *
 * A fit by bounding BOXES cannot tell a flat pair of guns from one tall gun
 * standing up - 147 x 253 x 159 with y longest is what both measure - and a
 * mod's Uzi inventory model comes out lying flat, the two guns one above the
 * other, where the same file in the Tomb Raider I-III remaster's own ring
 * stands up facing the camera.
 *
 * The principal axes of the vertex cloud (HDModelPrincipalAxes, measured while
 * the model was read) are the directions the model really runs in: the two
 * largest span the plane the shape lies in and the smallest is the way it is
 * flat. Measured on a mod's files:
 *
 *   install EXTRA_UZI_ITEM    cloud 256 x 236 x  42, flat along y
 *   mod     UZI_OPTION        cloud 242 x 240 x 132, flat along x
 *
 * Sending the source's k-th axis onto the target's k-th lays the mod's pair
 * into the plane the vanilla item lies in, and the ring's own per-item
 * rotation then presents it as it presents the vanilla one: face-on to the
 * camera, turning about the vertical. Nothing about either game is written
 * down - the two files say which way round they are.
 *
 * The fit does not CROSS two parallel guns. In UZI_OPTION's bind pose the two
 * guns stand PARALLEL, side by side and offset (ModelProbe -name UZI_OPTION
 * -rigid -axisview); the vanilla EXTRA_UZI_ITEM is ONE gun. A rigid turn keeps
 * the angle between two parts. The .TRM carries no node transforms at all
 * (hd_trm.h - per-vertex bone indices and weights, nothing else); its 27449
 * vertices sit on two nodes of 13723 and 13726, and the cross comes from the
 * donor game's classic frame, which HDModelSlot::classicPose applies before
 * this fit measures anything.
 *
 * The plane is well conditioned and the angle in it is not. Which axis is the
 * FLATTEST - and so which plane faces the camera - is decided by a wide margin
 * on both files: 132.3 against 239.5 for the mod's pair, 41.6 against 235.7
 * for the vanilla item, a factor of nearly six. The two axes INSIDE that plane
 * are decided by 242.3 against 239.5 on the mod's file, a gap of 1.2 %, and
 * sort_axes orders them by that gap alone. So the ROLL inside the plane is
 * deterministic - the same file gives the same answer every time - but not
 * meaningful: a mod update that moved a few dozen vertices could swap the two
 * and turn the item a quarter turn in the ring. No symmetry test is made; a
 * max-extent sweep in the plane would be the way to pin the angle.
 *
 * Both frames are right handed by construction (see the axes' own note), so
 * the map is a pure rotation and can never mirror a model.
 *
 * The scale is uniform and is the ratio of the two longest extents, so the
 * mod's model is as long in the ring as the model it replaces. The longest
 * axis is always one of the two IN the plane, so the scale is decided by the
 * shape's own reach and never by how thick it is.
 *
 * So the fitted model can still stand out of the target's box
 * on the axes the scale did not decide: the mod's pair of Uzis is 132 units
 * thick where the vanilla item is 42, and that is the mod's own geometry and
 * not a fault of the fit. HDModelRingFitOverflow puts the number on it and
 * the ring's log line prints it. */
bool HDModelRingFit(int model, int target, float* out)
{
	float srcMin[3], srcMax[3], dstMin[3], dstMax[3];
	float srcAxis[9], srcExt[3], dstAxis[9], dstExt[3];

	if (!HDModelBounds(model, srcMin, srcMax) ||
		!HDModelBounds(target, dstMin, dstMax) ||
		!HDModelPrincipalAxes(model, srcAxis, srcExt) ||
		!HDModelPrincipalAxes(target, dstAxis, dstExt) ||
		srcExt[0] <= 0.0f)
		return false;

	/* The turn. Row vectors, as everywhere in this engine's matrices: the
	 * sum of the three outer products u(k) x v(k) sends every u onto its own
	 * v, because the u's are orthonormal. */
	float turn[3][3];

	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
		{
			turn[r][c] = 0.0f;

			for (int k = 0; k < 3; k++)
				turn[r][c] += srcAxis[k * 3 + r] * dstAxis[k * 3 + c];
		}

	const float scale = dstExt[0] / srcExt[0];

	/* Centred on the two boxes and not on the two clouds' centres of mass:
	 * the ring keeps a place for the item it draws and that place is the
	 * vanilla model's box, so the middle of the box is where the middle of
	 * the replacement belongs. */
	float srcMid[3], dstMid[3];

	for (int i = 0; i < 3; i++)
	{
		srcMid[i] = (srcMax[i] + srcMin[i]) * 0.5f;
		dstMid[i] = (dstMax[i] + dstMin[i]) * 0.5f;
	}

	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			out[r * 4 + c] = turn[r][c] * scale;

	for (int c = 0; c < 3; c++)
	{
		out[3 * 4 + c] = dstMid[c];

		for (int k = 0; k < 3; k++)
			out[3 * 4 + c] -= srcMid[k] * turn[k][c] * scale;
	}

	out[0 * 4 + 3] = 0.0f;
	out[1 * 4 + 3] = 0.0f;
	out[2 * 4 + 3] = 0.0f;
	out[3 * 4 + 3] = 1.0f;

	return true;
}

/* How far that fit leaves the target's room, on the shape's own axes.
 *
 * The scale is the ratio of the two longest extents, so the longest axis lands
 * exactly and the other two land wherever their own proportions put them. This
 * is the largest of the three ratios - fitted extent over the target's on the
 * same axis - so 1.00 means the model sits inside the room the ring keeps for
 * that item and the 3.36 the Uzi mod reads means one of its axes is over three
 * times that room.
 *
 * It is a number for the log and not a limit: nothing here refuses a fit, for
 * the reason written at HDModelRingFit. `worstAxis` takes 0, 1 or 2 - WHICH OF
 * THE TARGET'S OWN axes, longest first, is the one overflowed; 0 when either
 * model has no axes. */
float HDModelRingFitOverflow(int model, int target, int* worstAxis)
{
	float srcAxis[9], srcExt[3], dstAxis[9], dstExt[3];

	if (worstAxis)
		*worstAxis = -1;

	if (!HDModelPrincipalAxes(model, srcAxis, srcExt) ||
		!HDModelPrincipalAxes(target, dstAxis, dstExt) ||
		srcExt[0] <= 0.0f || dstExt[0] <= 0.0f)
		return 0.0f;

	const float scale = dstExt[0] / srcExt[0];
	float worst = 0.0f;

	for (int k = 0; k < 3; k++)
	{
		/* A target axis of no extent is a flat model - anything at all stands
		 * out of it - and a ratio there would be an infinity. It is skipped
		 * and the axes that can be measured decide. */
		if (dstExt[k] <= 0.0f)
			continue;

		const float ratio = (srcExt[k] * scale) / dstExt[k];

		if (ratio > worst)
		{
			worst = ratio;

			if (worstAxis)
				*worstAxis = k;
		}
	}

	return worst;
}

/* -------------------------------------- the frame the level draws a slot in --
 *
 * The bind pose is not the pose. A moveable's meshes are placed by its mesh
 * tree AND turned by its animation frame: DrawAnimatingItem translates by the
 * frame's own offset, calls gar_RotYXZsuperpack for node 0 and then walks the
 * tree with a translation and a rotation per node (game\draw.cpp).
 * Six of ANDREA3's eighteen pickup slots carry a turn on node 0 that is not a
 * yaw - the Uzi item's is 180 degrees about z, which stands the model on its
 * head - so a rule that measures two models in the bind pose measures neither
 * of them where the level puts them.
 *
 * This is that walk with nothing but the rotations added, once per slot at
 * level build. `frame` is the object's own first frame (six shorts of box,
 * three of offset, then one rotation a node in gar_RotYXZsuperpack's own
 * packing) and `tree` the engine's bone array from the object's bone_index -
 * four longs a node after the first, flags, x, y, z. Either may be null: a
 * slot with no frame gets identity, which is what the bind pose was.
 *
 * Two things are left out on purpose.
 *
 * The frame's OFFSET, because it is one translation in front of the whole
 * walk: it moves every node of both models by the same vector, so it cancels
 * in the difference the floor rule is about, and leaving it out keeps node 0
 * at the origin where the bind numbers were.
 *
 * The ITEM's own rotation, because a pickup LYING THERE carries a yaw and a
 * yaw does not tilt the up axis: InitialiseItem zeroes x_rot and z_rot
 * (game\items.cpp), the level file gives an item one angle, and the one place
 * that tilts a pickup Lara is standing over puts the three angles back before
 * anything is drawn (PickUpCollision, game\pickup.cpp).
 *
 * Two of the family do tilt, while THROWN. FlareControl turns a flare in
 * flight by 546 about x and 910 about z every frame (game\laraflar.cpp) and
 * FlameTorchControl a falling torch by 910 about z (game\flmtorch.cpp),
 * both outside PickUpCollision's
 * save-and-restore, and both zero the two angles again the moment the thing
 * comes to rest. So the frame this rule measures in is the frame those two
 * are drawn in for as long as they lie on the ground, which is when a floor
 * rule is about them at all, and their sink is small either way -
 * FLARE_ITEM 1.0 units and BURNING_TORCH_ITEM 2.9. A tumbling flare is
 * lifted by that same amount straight up in the world, because that is where
 * the lift is applied (hd_object.cpp, world_up_in_view) and not along the
 * model's own axis: a tilt changes what the number would have been, never
 * which way it goes.
 *
 * The arithmetic is aRotX/aRotY/aRotZ's, line for line, on a local 3x4 in the
 * engine's own M00..M23 order - not the global matrix stack, which is
 * phd_GenerateW2V's to set and is still null on the first level build. The
 * engine's table lookup is sin(i * 2pi / 65536) and this calls sinf on the
 * same angle, so the two agree to a float's last digits; the rule's slack is
 * half a unit. */
namespace
{
	/* The engine's own indices, so that the three rotations below can be read
	 * against aRotX/aRotY/aRotZ (specific\3dmath.cpp) a line at a
	 * time. This module is linked into ModelProbe, which has no engine
	 * headers, so they are named here rather than included. */
	enum
	{
		P00 = 0, P01 = 1, P02 = 2, P03 = 3,
		P10 = 4, P11 = 5, P12 = 6, P13 = 7,
		P20 = 8, P21 = 9, P22 = 10, P23 = 11
	};

	const double kAngleToRad = 6.283185307179586476925286766559 / 65536.0;

	void pose_identity(float* m)
	{
		for (int i = 0; i < 12; i++)
			m[i] = 0.0f;

		m[P00] = m[P11] = m[P22] = 1.0f;
	}

	// phd_TranslateRel: the 3x3 is untouched and the translation moves by the
	// offset THROUGH it.
	void pose_translate(float* m, float x, float y, float z)
	{
		m[P03] += m[P00] * x + m[P01] * y + m[P02] * z;
		m[P13] += m[P10] * x + m[P11] * y + m[P12] * z;
		m[P23] += m[P20] * x + m[P21] * y + m[P22] * z;
	}

	void pose_rot_x(float* m, short angle)
	{
		if (!angle)
			return;

		const float s = (float)sin((double)angle * kAngleToRad);
		const float c = (float)cos((double)angle * kAngleToRad);

		for (int row = 0; row < 3; row++)
		{
			float* r = m + row * 4;
			const float a = c * r[1] + s * r[2];
			const float b = c * r[2] - s * r[1];

			r[1] = a;
			r[2] = b;
		}
	}

	void pose_rot_y(float* m, short angle)
	{
		if (!angle)
			return;

		const float s = (float)sin((double)angle * kAngleToRad);
		const float c = (float)cos((double)angle * kAngleToRad);

		for (int row = 0; row < 3; row++)
		{
			float* r = m + row * 4;
			const float a = c * r[0] - s * r[2];
			const float b = c * r[2] + s * r[0];

			r[0] = a;
			r[2] = b;
		}
	}

	void pose_rot_z(float* m, short angle)
	{
		if (!angle)
			return;

		const float s = (float)sin((double)angle * kAngleToRad);
		const float c = (float)cos((double)angle * kAngleToRad);

		for (int row = 0; row < 3; row++)
		{
			float* r = m + row * 4;
			const float a = c * r[0] + s * r[1];
			const float b = c * r[1] - s * r[0];

			r[0] = a;
			r[1] = b;
		}
	}

	/* gar_RotYXZsuperpack with skip 0 (game\draw.cpp): either two
	 * words holding three ten-bit angles - Y, then X, then Z, each shifted up
	 * by six - or one word with the axis in its top two bits and twelve bits
	 * of angle shifted up by four. The stream pointer is advanced by exactly
	 * what was read, which is why the caller hands it over. */
	void pose_superpack(float* m, const short** stream)
	{
		const unsigned short* prot = (const unsigned short*)*stream;

		switch (*prot >> 14)
		{
		case 0:
		{
			const long packed = ((long)*prot << 16) + (long)*(prot + 1);

			pose_rot_y(m, (short)(((packed >> 10) & 0x3FF) << 6));
			pose_rot_x(m, (short)(((packed >> 20) & 0x3FF) << 6));
			pose_rot_z(m, (short)((packed & 0x3FF) << 6));
			*stream += 2;
			break;
		}

		case 1:
			pose_rot_x(m, (short)((*prot & 0xFFF) << 4));
			*stream += 1;
			break;

		case 2:
			pose_rot_y(m, (short)((*prot & 0xFFF) << 4));
			*stream += 1;
			break;

		default:
			pose_rot_z(m, (short)((*prot & 0xFFF) << 4));
			*stream += 1;
			break;
		}
	}
}

int HDFramePose(const short* frame, const long* tree, int nodes, float* out)
{
	float current[12];
	float stack[kJointBudget][12];
	int top = 0;

	if (nodes > kJointBudget)
		nodes = kJointBudget;

	if (nodes < 1 || !out)
		return 0;

	pose_identity(current);

	const short* rot = frame ? frame + 9 : 0;

	if (rot)
		pose_superpack(current, &rot);

	memcpy(out, current, sizeof(current));

	for (int node = 1; node < nodes; node++)
	{
		const long* bone = tree ? tree + (node - 1) * 4 : 0;
		const long flags = bone ? bone[0] : 0;

		if ((flags & 1) && top > 0)
			memcpy(current, stack[--top], sizeof(current));

		if (flags & 2)
		{
			memcpy(stack[top], current, sizeof(current));

			if (top < kJointBudget - 1)
				++top;
		}

		if (bone)
			pose_translate(current, (float)bone[1], (float)bone[2],
				(float)bone[3]);

		if (rot)
			pose_superpack(current, &rot);

		/* The three extra rotations a bone can ask for (flags 0x1C) are read
		 * out of item->data, which is a CREATURE's head and torso stream. A
		 * pickup has no data and the draw hands the walk `no_rotation`, all
		 * zeroes, so they are identity here - and phd_RotY(0) is identity in
		 * the draw too, which is why nothing has to be skipped. */

		memcpy(out + node * 12, current, sizeof(current));
	}

	return nodes;
}

/* How deep a pickup model reaches, and what lifts it out. The header carries
 * the rule; this is the arithmetic.
 *
 * Every kept vertex and not the eight corners of the box: a box corner of a
 * pair of crossed guns is empty space. Measured on a mod's posed `UZI_OPTION`
 * against the classic mesh it stands in for: 33.9 units by the corners, 11.5
 * by the vertices - a factor of three. A rule that lifted a model by the
 * corner number would hang it in the air.
 *
 * y is DOWN in this engine, so the deepest point is the LARGEST y, and the
 * lift is that minus the classic mesh's own deepest - clamped at zero, so a
 * model that lies on the floor or above it is never touched. */
bool HDModelFramedBox(int model, const float* fix, const float* pose,
	int nodes, float* mins, float* maxs, float* deepest)
{
	const Model* one = find(g_models, model);

	if (!one || !one->vertexSeen)
		return false;

	/* The points or nothing. With every node's matrix a pure translation
	 * equal to its own bind offset, the box of the bind positions would be
	 * the box of the drawn ones; the frame's ROTATION breaks that, and an
	 * answer off the box would be a bound and not the model - quietly, in a
	 * number that looks like the others. */
	if (one->points.empty() || one->points.size() / 3 != one->pointNode.size())
		return false;

	const size_t count = one->points.size() / 3;
	const float* p = &one->points[0];
	float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	float low[3] = { 0, 0, 0 };

	if (nodes > kJointBudget)
		nodes = kJointBudget;

	if (nodes < 1)
		nodes = 1;

	for (size_t i = 0; i < count; i++, p += 3)
	{
		float q[3], out[3];
		int node;

		if (fix)
		{
			/* The fitted case is the ring's <BASE>_OPTION model, which stands
			 * on ONE node and whose fit already puts it in the item's own
			 * frame - so the fold has nothing to take off and the node is 0.
			 * Row-major, row-vector, exactly as HDModelRingFit wrote it and
			 * as the draw multiplies it. */
			for (int c = 0; c < 3; c++)
				q[c] = fix[3 * 4 + c] + p[0] * fix[0 * 4 + c] +
					p[1] * fix[1 * 4 + c] + p[2] * fix[2 * 4 + c];

			node = 0;
		}
		else
		{
			/* The slot's own model goes through the bind fold, which is what
			 * the draw does to every vertex: the node's bind offset comes off
			 * before the node's matrix goes on (set_joint above). */
			node = (int)one->pointNode[i];

			if (node >= nodes)
				node = nodes - 1;

			const float* bind = one->hasBind ? one->bind[node] : 0;

			q[0] = bind ? p[0] - bind[0] : p[0];
			q[1] = bind ? p[1] - bind[1] : p[1];
			q[2] = bind ? p[2] - bind[2] : p[2];
		}

		/* Then through the frame the level draws that node in - HDFramePose's
		 * 3x4, a point as a COLUMN. Without a pose the frame turns nothing
		 * and the answer is the bind position. */
		const float* m = pose ? pose + (size_t)node * 12 : 0;

		if (m)
			for (int r = 0; r < 3; r++)
				out[r] = m[r * 4 + 0] * q[0] + m[r * 4 + 1] * q[1] +
					m[r * 4 + 2] * q[2] + m[r * 4 + 3];
		else
			for (int r = 0; r < 3; r++)
				out[r] = q[r];

		/* And where the deepest point itself is, not only how deep it
		 * reaches: a picture of the rule has to put the floor plane
		 * UNDER that point, and a plane drawn under the box's middle is a
		 * plane the eye cannot check the contact against. */
		if (!i || out[1] > hi[1])
		{
			low[0] = out[0];
			low[1] = out[1];
			low[2] = out[2];
		}

		for (int c = 0; c < 3; c++)
		{
			if (!i || out[c] < lo[c]) lo[c] = out[c];
			if (!i || out[c] > hi[c]) hi[c] = out[c];
		}
	}

	if (mins)
		for (int c = 0; c < 3; c++)
			mins[c] = lo[c];

	if (maxs)
		for (int c = 0; c < 3; c++)
			maxs[c] = hi[c];

	if (deepest)
		for (int c = 0; c < 3; c++)
			deepest[c] = low[c];

	return true;
}

bool HDModelFloorLift(int model, const float* fix, const float* pose,
	int nodes, float classicLowest, float* lowest, float* lift)
{
	float lo[3], hi[3];

	if (lowest)
		*lowest = 0.0f;
	if (lift)
		*lift = 0.0f;

	/* One sweep for both answers: the deepest point IS the drawn box's
	 * largest y, and the tool needs the whole box to frame a picture of the
	 * same thing. */
	if (!HDModelFramedBox(model, fix, pose, nodes, lo, hi))
		return false;

	const float deepest = hi[1];

	/* The slack. Half a unit, which is half a millimetre at this engine's
	 * scale of 1024 units to a metre and well under the rounding of the
	 * classic mesh's own SHORT coordinates. It keeps the rule from writing a
	 * lift for a model that is modelled ON the classic mesh and misses it by
	 * a float's last digit - which is every one of the remaster's own pickup
	 * files - so their matrices stay untouched. */
	const float slack = 0.5f;

	if (lowest)
		*lowest = deepest;
	if (lift)
		*lift = deepest - classicLowest > slack ? deepest - classicLowest : 0.0f;

	return true;
}

/* Frees the point lists of every model of the level at once, because
 * the caller measures every slot of the family in one loop and there is no
 * case where one of them is kept and another dropped. */
void HDModelReleasePoints()
{
	for (std::map<int, Model>::iterator at = g_models.begin();
		at != g_models.end(); ++at)
	{
		std::vector<float> empty;
		std::vector<unsigned char> emptyNodes;

		at->second.points.swap(empty);	// and the capacity with it
		at->second.pointNode.swap(emptyNodes);
		at->second.keepPoints = false;
	}
}

/* What the floor rule had to measure on, for the caller's log. `kept` is how
 * many vertex positions are beside the model and
 * `blended` how many of them ride on more than one node - a pickup file is
 * rigid per node in every file measured, and a number here other than zero
 * says that this one is not and that its deepest point is read off its
 * heaviest bone alone. */
bool HDModelPointsInfo(int model, int* kept, int* blended)
{
	const Model* one = find(g_models, model);

	if (kept)
		*kept = 0;
	if (blended)
		*blended = 0;

	if (!one)
		return false;

	if (kept)
		*kept = (int)(one->points.size() / 3);
	if (blended)
		*blended = one->pointsBlended;

	return !one->points.empty();
}

bool HDModelFoldCounts(int slot, HDTrmFoldCounts* out)
{
	const Model* model = find(g_models, slot);

	if (!model)
		return false;

	if (out)
		*out = model->fold;

	return true;
}

/* The material of one page. See PageSheen at the top
 * of this file for what it is for; the whole of it is a small table on the
 * model that draw_model consults per range. Setting the same page twice
 * replaces it, which is what a caller re-planning a level wants. */
void HDModelSetPageSheen(int slot, int page, float strength, float power)
{
	const std::map<int, Model>::iterator at = g_models.find(slot);

	if (at == g_models.end() || page < 0)
		return;

	for (size_t i = 0; i < at->second.pageSheen.size(); i++)
		if (at->second.pageSheen[i].page == page)
		{
			at->second.pageSheen[i].strength = strength;
			at->second.pageSheen[i].power = power;
			return;
		}

	PageSheen one;

	one.page = page;
	one.strength = strength;
	one.power = power;
	at->second.pageSheen.push_back(one);
}

int HDModelPageSheenCount(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? (int)model->pageSheen.size() : 0;
}

bool HDModelHasHead(int slot)
{
	return find(g_heads, slot) != 0;
}

int HDModelHeadNode(int slot)
{
	const Model* head = find(g_heads, slot);

	return head ? head->rigidNode : -1;
}

namespace
{
	bool bounds_of(const Model* model, float* mins, float* maxs)
	{
		if (!model)
			return false;

		if (mins)
			std::memcpy(mins, model->mins, sizeof(model->mins));
		if (maxs)
			std::memcpy(maxs, model->maxs, sizeof(model->maxs));

		return true;
	}
}

bool HDModelHeadBounds(int slot, float* mins, float* maxs)
{
	return bounds_of(find(g_heads, slot), mins, maxs);
}

bool HDModelBounds(int slot, float* mins, float* maxs)
{
	const Model* model = find(g_models, slot);

	if (!model)
		return false;

	if (mins)
		std::memcpy(mins, model->mins, sizeof(model->mins));
	if (maxs)
		std::memcpy(maxs, model->maxs, sizeof(model->maxs));

	return true;
}

bool HDModelClassicPoseOf(int slot, HDModelClassicPose* out)
{
	const std::map<int, HDModelClassicPose>::const_iterator at = g_pose.find(slot);

	if (at == g_pose.end())
		return false;

	if (out)
		*out = at->second;

	return true;
}

int HDModelNoteCount()
{
	return (int)g_notes.size();
}

const char* HDModelNote(int at)
{
	return (at >= 0 && at < (int)g_notes.size()) ? g_notes[(size_t)at].c_str() : "";
}

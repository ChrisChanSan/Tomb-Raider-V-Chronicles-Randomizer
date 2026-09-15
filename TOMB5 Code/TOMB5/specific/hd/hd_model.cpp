/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the object path are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp) - see hd_model.h for the
 * list, function by function. The renderer under them is new. The .TRM format
 * itself is TRForge's measurement (MIT) and is documented in hd_trm.h.
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
#include "hd_classic.h"		// the donor game's own frame 0 (#155)
#include "hd_mods.h"		// a mod's model beats the install's (#153, M1)
#include "hd_trm.h"

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

namespace
{
	/* THE VERTEX ON THE CARD IS THE VERTEX ON DISK - all 24 bytes of it, in
	 * the same order, with the same meaning. A .TRM vertex needs no repacking
	 * at all; it needs a declaration that reads its bytes right. The layout
	 * is written out in hd_trm.h and again in hd_model.hlsl.
	 *
	 * The only reason this struct exists is the stride and the compiler's
	 * assurance that it is 24. HDTrmVertex has exactly this shape, so the
	 * fill below is a memcpy in disguise - but written out, because
	 * HDTrmVertex belongs to the reader and its layout is the reader's
	 * business, not ours. */
	struct HDModelVertex
	{
		float x, y, z;
		unsigned char nx, ny, nz, layer;
		unsigned char bone0, bone1, bone2, u;
		unsigned char weight0, weight1, weight2, v;
	};

	/* One draw call: a run of indices whose triangles all wear the same page
	 * AND ride on the same node (#123, stage S3-2).
	 *
	 * The node is the second key because `item->mesh_bits` hides whole nodes -
	 * the gladiator's shield, a body the game blows apart - and an index
	 * buffer cannot skip a triangle at draw time. Sorted this way the hidden
	 * node's triangles are a contiguous run that is simply not drawn.
	 *
	 * It does not cost draw calls in the normal case: the ranges come off in
	 * (page, node) order, so consecutive ones of the same page whose nodes are
	 * all visible are merged back into one call before the draw. With
	 * mesh_bits at -1, which is nearly always, the count is exactly the one
	 * stage S3-1 had. */
	struct Range
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
		int node = 0;
		// The .DDS number behind pageSlot (#123, fix round I, I4): a material
		// is named by PAGE and the cache slot is an allocation.
		int page = -1;
	};

	/* ONE MATERIAL, NAMED BY PAGE (#123, fix round I, I4).
	 *
	 * The sheen of fix round D is one number per DRAW, and that is right for
	 * every model in the game but three: a HAND_ model is a hand AND the thing
	 * it is holding, on two pages of one file, and one number makes either the
	 * skin metal or the gun matt. The user's finding I4 is the second of those
	 * - "the sheen is on the body and the head, the hands go empty" - and the
	 * measurement behind it is that a Blinn-Phong exponent of 34 on a hand-
	 * sized object is a highlight one pixel wide.
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

	struct Model
	{
		IDirect3DVertexBuffer9* vb = 0;
		IDirect3DIndexBuffer9* ib = 0;
		std::vector<Range> pass[3];
		std::vector<int> pages;			// .DDS numbers, distinct
		// #123 fix round I (I4): a material per page, empty for every model
		// but the ones a caller has spoken for.
		std::vector<PageSheen> pageSheen;

		std::string name;
		HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;

		int vertices = 0;
		int indices = 0;
		// How many vertices the measurements above actually saw - the same as
		// `vertices`, except under the page cut of fix round D (D4).
		int vertexSeen = 0;
		int nodes = 0;				// classic nmeshes
		int nodeVertices[32] = { 0 };
		unsigned int covered = 0;		// bit per node with geometry

		/* The skeleton fit, measured in build() - see HDModelMaxBone and
		 * HDModelBindDrift in hd_model.h for what the two numbers mean. */
		int maxBone = -1;
		float driftMean = 0;
		float driftWorst = 0;
		bool hasDrift = false;

		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };

		/* THE VERTEX CLOUD'S OWN AXES (#153, round M3) - the principal axes of
		 * the drawn vertices, longest first, with the cloud's extent along each.
		 * Measured in the same loop the box is, because that is the one place the
		 * vertices are in hand; a caller that wanted them later would have to read
		 * the .TRM a second time.
		 *
		 * A BOX IS NOT A SHAPE. A pair of guns lying flat and one gun standing up
		 * have the same kind of box and nothing else in common, and the ring's fit
		 * had only the box to go on - which is how the mod's two Uzis came to lie
		 * flat, one above the other. These say which way the SHAPE runs. */
		float axis[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		float axisExtent[3] = { 0, 0, 0 };
		bool hasAxes = false;

		float bind[32][3] = { { 0, 0, 0 } };
		bool hasBind = false;
		/* The classic mesh tree, for the head pass (#123, fix round B, B4).
		 * -1 everywhere when the caller handed none over. */
		int parent[32] = { 0 };
		bool hasTree = false;

		int rigidNode = -1;			// >= 0 for a <NAME>_HEAD.TRM
		/* A rigid face's NECK: the file's OWN bone number that is bound to
		 * `neckNode` instead of `rigidNode`, and -1 when there is none. */
		int neckBone = -1;
		int neckNode = -1;

		// What the mirror fold moved on this file (#153, round M2b), for the
		// caller's own log line - all zero for a slot that asked for none.
		HDTrmFoldCounts fold = { 0, 0, 0, 0, 0, 0 };

		/* THE DRAWN VERTICES' POSITIONS, for a model that asked to keep them
		 * (#155b) - three floats each, in the model's own frame, and the same
		 * vertices the box above was taken over. Empty for everything else,
		 * and emptied for everything by HDModelReleasePoints the moment the
		 * level build has measured its floor rule.
		 *
		 * The box cannot answer the fitted question: a rotation sends the box
		 * corners somewhere no vertex of the model is. */
		std::vector<float> points;
		/* AND WHICH NODE EACH OF THEM RIDES ON (#155b, fix round 1). The box
		 * needed no such thing: with every node's matrix a pure translation
		 * equal to its own bind offset, (p - bind) * M is p again whatever
		 * the weights are, so a vertex's position in the bind pose IS its
		 * drawn position. The moment a node's matrix carries a ROTATION -
		 * which the item's own animation frame puts there, and which is the
		 * whole of this fix round - the two part company and the point has to
		 * know which matrix to go through.
		 *
		 * The vertex's HEAVIEST bone, one byte each. A vertex that rides on
		 * two nodes at once is really drawn at the blend of both, and
		 * `pointsBlended` counts them so that the log says how many of a
		 * model's points this reads as rigid rather than leaving it to be
		 * assumed. Every pickup file measured - the remaster's own and all
		 * four of the user's mods - is rigid per node: weight (255, 0, 0) on
		 * every vertex. */
		std::vector<unsigned char> pointNode;
		int pointsBlended = 0;
		bool keepPoints = false;
	};

	const int kJointBudget = 32;

	IDirect3DDevice9* g_device = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;

	std::map<int, Model> g_models;		// slot -> body
	std::map<int, Model> g_heads;		// slot -> its separate HD face
	std::vector<std::string> g_notes;
	/* What the classic pose decided, per slot that asked for one (#155). The
	 * decision is taken while the file is open and the log line is written by
	 * the caller, a level build later - so it is kept, not printed.
	 * Cleared with everything else by HDModelLevelBegin. */
	std::map<int, HDModelClassicPose> g_pose;
	std::string g_install;
	/* The other bundle's game 3 folder, for the four Tomb Raider III guests
	 * (#123, stage S7-2). Empty unless the caller set it, and then no slot
	 * can reach that bundle at all. */
	std::string g_install_tr3;
	// A refusal whose wording is not a literal - it has to outlive the call
	// that hands *why out. Never cleared by HDModelLevelEnd.
	std::string g_error;

	/* What the cache keys of this level build cost (#153, round M2, the fix
	 * round). Accumulated here rather than passed down because build() is
	 * called once per model and the number belongs to the level; reset by
	 * HDModelLevelBegin and read out of it into HDModelStats::keyMs. */
	double g_key_ms = 0;

	bool g_in_frame = false;
	int g_draw_calls = 0;
	int g_frame_bulbs = 0;
	// E6: which of the three passes this bracket draws - see
	// HDModelFrameBegin and the HD_PASS_ note in hd_render.h.
	unsigned int g_pass_mask = HD_PASS_ALL;

	/* THE FRAME'S PROJECTION, KEPT (#123, fix round P2). The inventory ring
	 * draws each item about a different screen centre, and the road it takes
	 * is a projection of its own per item (HDModelOffCentre) - so the plain
	 * one has to be here to build them from and to go back to. `g_off_centre`
	 * says whether c0 currently holds a moved one, which is what lets the
	 * ordinary case - every object in the world - upload nothing at all. */
	float g_frame_proj[16] =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	bool g_off_centre = false;

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
	// only 4 and 5 are ever searched (fix round C, C7).
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

	/* WHICH MODS MAY ANSWER find_model (#153, round M1): kModsAny is every
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

	/* WHERE A MODEL LIVES. Game 5 first, then the siblings - and the siblings
	 * are not a courtesy: SAS, FIREROPE, BRIDGE_TILT1, RAISING_BLOCK1 and 2
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

		/* A MOD BEFORE EVERYTHING ELSE (#153, round M1). The active mods in
		 * order, each 5\ITEM then 4\ITEM; the first one that has the name
		 * wins and the installs are not asked at all. It is a replacement by
		 * RESOLUTION and never on disk - renaming the mod's folder with a
		 * leading _ puts the install's own file back with nothing else to
		 * undo.
		 *
		 * Ahead of the fromTr3 branch on purpose: a mod that replaces one of
		 * the four Tomb Raider III guests is the same kind of wish as one
		 * that replaces a hand, and a guest whose model a mod supplies gets
		 * that mod's page namespace like any other. HDModsFindModel refuses
		 * OUTFIT_*.TRM by itself - the wardrobe is round M2. */
		if (modOnly != kModsNone)
		{
			char path[512];
			int mod = -1;

			/* The wanted mod goes INTO the search rather than being checked
			 * on the way out (#153, round M1, fix round). Sifting the answer
			 * here meant the resolver had already written "model X <- mod0"
			 * into the log for a file this call then dropped on the floor,
			 * and its one-line-per-model memory made the correct line for
			 * the same name unsayable afterwards. */
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

		/* THE OTHER BUNDLE, and only it (#123, stage S7-2). A Tomb Raider III
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

		/* AND THEN 4, AND THEN NOTHING (#123, fix round C, user finding C7).
		 * The bundle also holds a 6\ITEM, and until now it was searched last.
		 * It has no business here: game 6 is Angel of Darkness, a different
		 * engine with a different Lara and different objects, and a name that
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

	/* WHICH FACE FILE BELONGS TO A BODY (#123, fix round D, D1).
	 *
	 * THE FINDING. In a cutscene every actor stood there without a head - the
	 * user's picture of RICHCUT2 shows the man in the suit and the man in the
	 * wheelchair, both headless, and the guard beside them wearing a cap and
	 * nothing under it. Measured with ModelProbe: ANIMATING4_RICHCUT2 stops at
	 * y -234 with an open ring of 16 points at (0, -218, 6), radius 13, and no
	 * vertex above it; the body really is a collar. Stage S3-2's rule then
	 * does what it must - a node the .TRM does not cover is not drawn classic
	 * either - and the result is a suit with a tie and no face.
	 *
	 * WHY THE NAME RULE MISSES THEM. A face is <MODEL>_HEAD.TRM, and that
	 * answers for BLUE_GUARD, LARSON and PIERRE. It cannot answer for a
	 * cutscene actor, because the remaster names his body after the LEVEL SLOT
	 * it fills - ANIMATING4_RICHCUT2 - while his face keeps the person's name.
	 * Game 5 ships twelve *_HEAD.TRM and only three of them are reachable by
	 * name.
	 *
	 * TWO RULES ARE ADDED, both measured over all 1,335 model names of the 4
	 * and 5 folders:
	 *
	 *   the STRIP rule. A trailing _<QUALIFIER> is the remaster's way of
	 *   saying "the same person, this level's paint": BLUE_GUARD_JOBY,
	 *   BLUE_GUARD_RICH2, BLUE_GUARD_RICHCUT2, PIERRE_SWAP. Cutting the last
	 *   underscore group and asking again, as long as one is left, answers for
	 *   17 names and every one of them is right or harmless - the five
	 *   BLUE_GUARD variants, PIERRE_SWAP, VON_CROY_BOOK/BOTH/EMPTY of game 4,
	 *   and eight _MIP twins that this engine never draws. Not one name gets a
	 *   face that belongs to somebody else.
	 *
	 *   the TABLE below, for the two the strip rule cannot reach because the
	 *   body carries no part of the person's name at all. Each entry is
	 *   measured, not guessed: the open ring of the body against the neck ring
	 *   of the face, both in the bind pose the .TRM files share.
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
	 * A ring match ALONE is not proof - ADMIRAL_HEAD's neck lands 1.3 units
	 * from a ring of ANIMATING10_TITLE, and the picture shows that model is
	 * the top-hatted mourner of the title graveyard, who has his own face and
	 * needs none. So every entry here is confirmed by a picture as well.
	 *
	 * LEFT UNPLACED, and named so the next pass does not start from nothing:
	 * ADMIRAL, DEMON, HANGMAN, PATRICK_BROWN, PATRICK_WHITE, SERGIE and ZIP
	 * have no body among the models the fifteen .MAP files name that both
	 * fits their neck and lacks a head. PATRICK_1 and PATRICK_2 are headless
	 * and are plainly the two faces' bodies, but the remaster reaches them
	 * only through SAILOR_MIP, and this engine never draws an item's MIP
	 * slot - so they stay classic and the question does not arise. */
	struct HeadOfModel
	{
		const char* model;
		const char* head;
	};

	const HeadOfModel g_head_of_model[] =
	{
		{ "ANIMATING4_RICHCUT2", "VON_CROY_SENIOR_HEAD" },
		{ "ANIMATING6_RICHCUT2", "VON_CROY_ELDER_HEAD" },
	};

	bool same_name(const std::string& a, const char* b)
	{
		size_t i = 0;

		for (; i < a.size() && b[i]; i++)
			if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
				return false;

		return i == a.size() && !b[i];
	}

	/* The face file for a body, and by which of the three rules. `how` is for
	 * the log and is never null on return; it says "name", "table" or "strip".
	 * Empty when the model has no face anywhere. */
	/* `bodyMod` (#153, round M1) is the mod the BODY came out of, or -1. The
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
		int bodyMod = -1)
	{
		const int mods = bodyMod >= 0 ? bodyMod : kModsNone;

		*how = "name";

		/* A FACE HAS NO FACE OF ITS OWN. Lara's wardrobe asks for the part
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

				if (headName)
					*headName = g_head_of_model[i].head;

				return path;
			}
		}

		std::string stem = model;

		while (stem.find('_') != std::string::npos)
		{
			stem.erase(stem.find_last_of('_'));
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

	/* --------------------------------- THE CLASSIC POSE (#155) --
	 *
	 * A remaster inventory model is stored in the classic BIND POSE - node n's
	 * vertices carry the mesh tree's offsets and no rotation at all - and the
	 * game it came from puts the pose on when it draws. For the user's IMI
	 * Uzis that is the difference between two guns standing side by side and
	 * PARALLEL, which is what round M3 measured in our ring, and the two of
	 * them CROSSED like an X, which is what he sees in the game the file came
	 * from and what he asked for. hd_classic.h reads the frame; this puts it
	 * on the vertices.
	 *
	 * BEFORE ANYTHING IS MEASURED OUT OF THEM. build() takes the box and the
	 * principal axes in one pass over the vertices, and HDModelRingFit fits
	 * the SHAPE those axes describe - so a pose applied afterwards would be a
	 * pose the fit never saw. This runs where HDTrmFoldMirrored runs and for
	 * the same reason: what is changed here is what the card is given, so
	 * every number the log prints is a number about the model that is drawn.
	 *
	 * IT IS SKINNING AND NOT A RIGID TURN, although every file this has met so
	 * far is rigid: a vertex names three nodes with three weights, and posing
	 * it on only the first would tear any file whose two parts share a seam. */
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

			/* A VERTEX THE POSE CANNOT REACH STAYS WHERE IT IS. It names only
			 * nodes the classic object has not got, which the mesh-count guard
			 * makes impossible for the files this runs on - but a caller is
			 * not a proof, and a vertex quietly collapsed onto the origin is a
			 * hole in the model nobody would trace back to here. */
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

	void free_model(Model& model)
	{
		if (model.vb) { model.vb->Release(); model.vb = 0; }
		if (model.ib) { model.ib->Release(); model.ib = 0; }

		model.pass[0].clear();
		model.pass[1].clear();
		model.pass[2].clear();
		model.pages.clear();
		model.points.clear();		// #155b
	}

	/* The three index runs of one part, in pass order. Measured over all 566
	 * models of game 5: opaqueEnd == start + opaque and alphaEnd == opaqueEnd
	 * + alpha in every part of every file, so the three runs tile the part
	 * back to front without a gap. They are read as they stand all the same -
	 * the counts are what the remaster's own draw uses. */
	void part_runs(const HDTrmPart& part, unsigned int start[3], unsigned int count[3])
	{
		start[0] = part.start;
		count[0] = part.opaque;
		start[1] = part.opaqueEnd;
		count[1] = part.alpha;
		start[2] = part.alphaEnd;
		count[2] = part.additive;
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
	 * the Tomb Raider I-III one (#123, S7-2) and the mod's own namespace for
	 * a model a mod supplied (#153, round M1) - the .TRM's numbers are its
	 * own game's, and the cache holds all of them at once.
	 *
	 * It is offered rather than applied: HDPageCacheKeyOf takes the biased
	 * number back down to the plain one where the two would open the same
	 * file, so a mod that ships a model and no art of its own pays for that
	 * art once (#153, round M2). */
	/* WHICH PAGES OF A FILE ARE BUILT (#123, fix round D, D4; a SET and either
	 * polarity since #153, round M2b).
	 *
	 * Round D cut ONE page out of a bigger model - the holster rig out of the
	 * London catsuit. Round M2b needs the other polarity and more than one page
	 * at a time: a mod's hand keeps its gun and DROPS the pages that resolve
	 * into another bundle, and the install's own file of the same name KEEPS
	 * only the pages that are its skin. One mechanism does both, because two
	 * would be two chances to disagree about what a page number means.
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

	/* AND THE SAME THING BY NODE (#156) - see HDModelSlot::onlyNode. A mask
	 * of 0 is the whole model and costs one test per triangle. */
	struct NodeCut
	{
		unsigned int nodes;

		bool wants(int node) const
		{
			return !nodes || (nodes & (1u << (node & 31))) != 0;
		}
	};

	/* WHICH NODE A TRIANGLE BELONGS TO: the node that carries the most weight
	 * over its three corners. A triangle that straddles a joint has to fall on
	 * one side of it, and the heavier side is the side it moves with - the
	 * same rule the eye applies when it decides whether a polygon "is" the
	 * upper arm or the forearm.
	 *
	 * The rigid case reads the vertex as it was WRITTEN, not as it arrived:
	 * since fix round B a facial-rig file's neck bone lands on the head node's
	 * parent, and a neck triangle belongs to that node when mesh_bits hides
	 * one of the two.
	 *
	 * It was one block inside build()'s triangle loop until #156, which needs
	 * the same answer in two more places - the vertex keep pass and the page
	 * list - because a NODE cut has to drop exactly the triangles a node mask
	 * would have hidden. One function, so the cut and the ranges cannot come
	 * to disagree about which node a triangle is on. */
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

	/* THE AXES OF ONE SET OF EXTENTS, LONGEST FIRST. One function so that the
	 * ring's fit, its overflow figure and the cloud's own axes in build() cannot
	 * come to disagree about which axis is which - the whole rule is "k-th
	 * longest to k-th longest", and a second copy of it would be a second thing
	 * to keep in step. */
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

	bool build(const HDTrmModel& trm, Model& model, int rigidNode,
		int pageBias, int* mixedTriangles, const char** why, const PageCut& cut,
		const NodeCut& nodeCut)
	{
		const int vertices = trm.vertices;

		std::vector<HDModelVertex> buffer((size_t)vertices);

		/* THE PAGE CUT (#123, fix round D, D4). With a cut in hand, a vertex
		 * no kept triangle names takes no part in ANYTHING the loop below
		 * measures - not the bounds, not the node coverage, not the bind
		 * drift. Otherwise a holster cut out of an outfit would report that
		 * it covers all fifteen nodes and reaches from her hair to her boots,
		 * and the composite would hand it the whole body's work. The vertex
		 * itself stays in the buffer: the index list is the file's own and
		 * renumbering it would buy 400 KiB and a new class of bug. */
		// #156: and the same for a NODE cut, which drops vertices the same way.
		const bool whole = (cut.count <= 0 || !cut.page) && !nodeCut.nodes;
		std::vector<bool> keep((size_t)vertices, whole);

		// #155b: one allocation and not a ladder of them - the cut can only
		// take vertices away, so the file's own count is the upper bound.
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
				/* THE FACE'S NECK GOES ON THE PARENT NODE (#123, fix round
				 * B, B4). Everything else of a facial-rig file hangs off the
				 * head node, which is what "rigid" meant until now; the one
				 * bone that reaches down into the collar hangs off the node
				 * the head node hangs off - the torso. The weights are kept
				 * as the file has them, so the seam between the two BLENDS
				 * instead of tearing. neck_bone_of says which bone that is
				 * and how it was measured. */
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
				/* The palette is 32 and the measurement says the highest
				 * weighted bone over all of game 5 is 28, so this clamp never
				 * fires on the remaster's own files. It is here for a
				 * hand-made .TRM and for the day a sixth game ships. */
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

			/* AND THE POSITION ITSELF, for a model that asked (#155b). Here
			 * and not in a second walk of the file: this is the one loop that
			 * has the kept vertices in hand, and the floor rule has to see
			 * exactly the vertices the box above was taken over or the two
			 * numbers it compares are not about the same model. */
			if (model.keepPoints)
			{
				model.points.push_back(in.x);
				model.points.push_back(in.y);
				model.points.push_back(in.z);
				/* AND THE NODE IT RIDES ON (fix round 1), out of the bones
				 * this loop has just written - `out` and not `in`, because a
				 * rigid face's neck rule has already moved a bone of the file
				 * onto another node of the object above. */
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

			/* THE NODE COVERAGE, which is the whole of the mapping question:
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

				/* THE HIGHEST NODE THE MODEL NAMES, before the clamp above
				 * takes it down to the palette. This is the number that says
				 * "this .TRM was drawn for a different mesh tree", and it has
				 * to be the raw one - a clamped 31 would look like a
				 * legitimate deep node. */
				if (weight && rigidNode < 0 && (int)in.bone[b] > model.maxBone)
					model.maxBone = (int)in.bone[b];
			}

			/* THE BIND DRIFT. Only the vertices that ride on ONE node count:
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

		/* THE CLOUD'S OWN AXES, out of the moments gathered above (#153, round
		 * M3). The covariance of the drawn vertices, diagonalised by Jacobi
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

				/* AN EIGENVECTOR HAS NO SIGN, AND A GUN HAS TWO ENDS. Without a
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

				/* AND THE THIRD IS THE OTHER TWO. Taken as a cross product rather
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

		/* THE BIND DRIFT, BETWEEN PAIRS OF NODES AND NOT AGAINST THE ORIGIN.
		 *
		 * The first version of this measured how far a node's own vertices sit
		 * from that node in the classic bind pose, and it was wrong for
		 * everything that is not a creature: a door is one node whose mesh
		 * hangs 750 units off its hinge, so it looked like the worst mismatch
		 * in the level - in VANILLA, where nothing can be mismatched at all.
		 * What that number really measures is where a modeller put the origin,
		 * which is the model's business and not the skeleton's.
		 *
		 * What the skeleton question actually asks is whether the DISTANCES
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

				/* HOW BIG THE MESH ITSELF IS, as the root mean square
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

					/* THE MESHES' OWN SIZE COMES OFF. A centre is not a
					 * joint: a long bone's vertices average out at the
					 * middle of the bone, so two long meshes disagree with
					 * their joints by half their own length and a hammer
					 * trap looked like a mismatch in VANILLA. What is left
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

		/* THE CACHE KEY OF EVERY PAGE THIS MODEL NAMES, WORKED OUT ONCE
		 * (#153, round M2, the fix round).
		 *
		 * A vertex names a page by its LAYER, which is an index into the
		 * model's own short texture list - eight entries at the outside - so
		 * there are exactly `trm.textures` different answers to give, however
		 * many triangles wear them. Round M2 asked HDPageCacheKeyOf per
		 * triangle CORNER instead, and for a model a mod supplied that
		 * question opens files: measured at 30.8 microseconds a call, which
		 * turned ANDREA1's model build from 672 ms into 25,656.
		 *
		 * The table below is the same question asked per texture slot. It is
		 * not an approximation of the old loop: HDTrmPageOf IS
		 * texture[layer-1], so a corner's key is this table at its layer, and
		 * the two agree value for value. */
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

			/* WHICH NODE A TRIANGLE BELONGS TO - dominant_node_of above, and
			 * it is worked out for every triangle whether or not anything
			 * cuts by it. Only used to hide a node, so a wrong answer at a
			 * seam costs at most one triangle at the cut edge of a blown-off
			 * limb. */
			const int best = dominant_node_of(trm, i, rigidNode, model.neckBone,
				model.neckNode);

			// #156: the page list is the pages of the triangles that SURVIVE,
			// so a node cut pays for its own pages and not for the file's.
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

		for (int pass = 0; pass < 3; pass++)
		{
			std::vector<int> triangles;	// triangle numbers of this pass

			for (int p = 0; p < trm.parts; p++)
			{
				unsigned int start[3], count[3];

				part_runs(trm.part[p], start, count);

				for (unsigned int i = 0; i + 2 < count[pass]; i += 3)
				{
					const unsigned int at = start[pass] + i;

					if (at + 2 >= (unsigned int)trm.indices)
						continue;

					// The page cut (#123, fix round D, D4).
					if (!cut.wants(HDTrmPageOf(&trm,
						&trm.vertex[trm.index[at]])))
						continue;

					// And the node cut (#156), out of the answer the loop
					// above has already worked out for this triangle.
					if (!nodeCut.wants(node_of_triangle[(size_t)(at / 3)]))
						continue;

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
			 * which order it is done. */
			std::stable_sort(triangles.begin(), triangles.end(),
				[&page_of_triangle, &node_of_triangle](int a, int b)
				{
					if (page_of_triangle[(size_t)a] != page_of_triangle[(size_t)b])
						return page_of_triangle[(size_t)a] < page_of_triangle[(size_t)b];

					return node_of_triangle[(size_t)a] < node_of_triangle[(size_t)b];
				});

			Range range;
			int page = -2;
			int node = -2;

			for (size_t i = 0; i < triangles.size(); i++)
			{
				const int triangle = triangles[i];
				const int here = page_of_triangle[(size_t)triangle];
				const int on = node_of_triangle[(size_t)triangle];

				if (here != page || on != node)
				{
					if (range.count)
						model.pass[pass].push_back(range);

					range.start = (unsigned int)indices.size();
					range.count = 0;
					range.pageSlot = here < 0 ? -1 : HDPageCacheSlotOf(here);
					// #123 fix round I (I4): the .DDS number as well, so a
					// material can be named by page.
					range.page = here;
					range.node = on;
					page = here;
					node = on;
				}

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

		return true;
	}

	/* WHICH NODE A SEPARATE HD FACE RIDES ON. The remaster ships some faces
	 * as their own file - VON_CROY.TRM is a body with an empty collar and its
	 * face is VON_CROY_HEAD.TRM - and draws them rigidly on the creature's
	 * head node. Which node that is, is measured and not hard coded: it is
	 * the bind-pose node nearest the face geometry's own centre (TROne,
	 * game_renderer.cpp:2056-2100 - node 21 in a 25 mesh tree, node 14 in a
	 * 15 mesh one). */
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

	/* WHICH BONE OF A SEPARATE HD FACE IS THE NECK (#123, fix round B, B4).
	 *
	 * The user's report: "Pierre's neck and chest are wrong in his death
	 * animation". Measured (scratch tool TrmBones over all twelve
	 * <NAME>_HEAD.TRM of game 5, per bone bounding boxes in the model's own
	 * bind pose):
	 *
	 *   - the file is not a face. PIERRE_HEAD.TRM spans y -376..-177 while
	 *     PIERRE.TRM's whole body ends at -286 and the head node sits at
	 *     -342: the file carries head, NECK and a piece of collar, reaching
	 *     165 units below the joint it was being nailed to.
	 *   - it carries a facial rig, so its bone numbers mean nothing to the
	 *     mesh tree and the whole file rode rigidly on the head node. Turn
	 *     that node - which is what a death animation does - and the neck and
	 *     the collar swing with the skull and tear a hole.
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

	/* ---------------------------------------------------------- drawing -- */

	/* THE BIND FOLD. A .TRM vertex is in the model's bind pose, so the node's
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

	void draw_model(const Model& model, const float* nodeMatrix, int nodeCount,
		const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
		const HDModelNeckCut* cut, int cull)
	{
		const int nodes = nodeCount < kJointBudget ? nodeCount : kJointBudget;

		/* ALL 32 JOINTS GO UP, not just the ones the caller has. A .TRM is
		 * the remaster's file and the classic node count is ours, and the two
		 * disagreeing by one would let a vertex read a joint left over from
		 * the object before - a limb somewhere else in the room, and only
		 * sometimes. The nodes past the caller's count get the last one it
		 * gave, which is TROne's rule for a node it never saw
		 * (fill_unseen_joints, game_renderer.cpp:1389): a slightly wrong limb
		 * beats a collapsed one. */
		for (int node = 0; node < kJointBudget; node++)
		{
			const int from = node < nodes ? node : nodes - 1;

			set_joint(node, nodeMatrix + from * 16,
				model.hasBind ? model.bind[from] : 0);
		}

		/* One colour per node, in the classic 0..255 space. A caller with
		 * nothing to say gets a flat 128, which is the classic path's own
		 * "no light list" ambient and draws the model at its plain texture
		 * brightness.
		 *
		 * THE FOURTH COMPONENT IS NOT READ (#123, fix round S, S1). From fix
		 * round J to fix round L it carried the neck cut's node mask, because
		 * the cut was a shape in the bind pose and a wrist had to be held out
		 * of it. The cut is a plane under her chin now and no arm is ever
		 * above it, so this goes back to the hard zero it was from stage S3 to
		 * fix round I. */
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

		/* c162, THE HEAD CUT (#123, fix round U, U1): which node's weight
		 * decides and how much of it sends a fragment away. Nothing in it is a
		 * fact about the camera or the pose, so unlike fix round S's plane it
		 * does not have to be rebuilt anywhere - the caller names the two
		 * numbers and they are the same in every frame.
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

		/* THE OBJECT'S LIGHTS, in the VERTEX shader (c136) - the classic path
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

		/* c161, THE SHEEN (#123, fix round D, D2). Written for every draw
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
		 * it alone when nothing changes (#123, fix round I, I4). */
		float sheenNow[2] = { sheen[0], sheen[1] };

		/* c17, the PIXEL side: x is the per-pixel dynamic light loop, which
		 * an object does NOT use - its lights are in the vertex shader above,
		 * where the classic formula puts them - and y is this frame's fog
		 * bulbs, which it does. A room drawn a moment ago left its own count
		 * in this register, so it is written for every object. */
		float counts[4] = { 0.0f, (float)g_frame_bulbs, 0, 0 };

		g_device->SetPixelShaderConstantF(17, counts, 1);

		g_device->SetStreamSource(0, model.vb, 0, sizeof(HDModelVertex));
		g_device->SetIndices(model.ib);

		/* THE ONE MODEL THAT IS NOT DOUBLE SIDED. Set here and put back at
		 * the bottom of this function, so that the state the bracket
		 * installed (D3DCULL_NONE) is what every other draw finds. */
		if (cull != HD_MODEL_CULL_NONE)
			g_device->SetRenderState(D3DRS_CULLMODE,
				cull == HD_MODEL_CULL_CW ? D3DCULL_CW : D3DCULL_CCW);

		/* FIX ROUND R'S DEPTH TIE IS GONE WITH THE DRAW THAT NEEDED IT (#123,
		 * fix round S). Round K's inner skin drew the body a second time and,
		 * on a single layer of triangles - a strap, a flap - won the depth tie
		 * against the outward face under D3DCMP_LESSEQUAL, which is what the
		 * play test saw as torn edges. Round R answered it with a D3DCMP_LESS
		 * for that one draw; round S closes the hole with a LID and the second
		 * draw is gone, so the whole HD path is back to one depth rule. */

		for (int pass = 0; pass < 3; pass++)
		{
			/* E6: the frame draws its solid work and its glows in two
			 * brackets, and this is where a bracket leaves out what is not
			 * its own. HD_PASS_ALL, which is what the diagnostic tool and
			 * every caller before fix round E asks for, lets all three
			 * through and this line costs a test. */
			if (!(g_pass_mask & (1u << pass)))
				continue;

			if (model.pass[pass].empty())
				continue;

			/* gPass: x switches the alpha test on, y the additive weighting,
			 * z the first person HEAD CUT (#123, fix round U - a flag, 1 or
			 * 0; the two numbers it needs went up in c162 above, and a 0 here
			 * for every model but Lara's body makes the shader's clip discard
			 * nothing), w the overbright add. The PIXEL constant
			 * file only - the vertex shader does not read gPass and fxc has
			 * put its own literals in that register there, and the room
			 * shader's own gPass.z lives in the VERTEX file, which this call
			 * never touches. */
			float constants[4] =
			{
				pass == 1 ? 1.0f : 0.0f,
				pass == 2 ? 1.0f : 0.0f,
				cut && cut->on > 0.0f ? 1.0f : 0.0f,
				pass == 2 ? 0.0f : 1.0f
			};

			g_device->SetPixelShaderConstantF(4, constants, 1);

			/* NO CUT-OUT ALPHA TEST IN THE OPAQUE PASS - MEASURED, NOT
			 * ASSUMED (#123, fix round B, B2).
			 *
			 * The rooms got exactly that in fix round A (A2, hd_render.cpp's
			 * draw_pass): the remaster puts its vegetation in the opaque
			 * pass, where no alpha test runs, so a leaf sheet drew as a solid
			 * rectangle until the test was switched on for the pages whose
			 * alpha really is a coverage mask. The obvious next step was to
			 * do the same for models. It is wrong, and the picture says so:
			 * ROMAN_GOD_ANDREA_SWORD's three body pages measure 64.1, 48.7
			 * and 56.4 percent of texels below alpha 128, and with the test
			 * on, half the statue is discarded (ModelProbe -name
			 * ROMAN_GOD_ANDREA_SWORD, with and without -nocutout). PIERRE and
			 * BLUE_GUARD share a page at 26.2 percent.
			 *
			 * So an ITEM page's alpha channel is not a coverage mask - it
			 * carries something else, and no threshold separates the two
			 * cases because a solid statue sits ABOVE a leaf sheet. The
			 * models keep the remaster's own rule, which is the one TROne
			 * carries too: the alpha test runs in the ALPHA pass and nowhere
			 * else. hd_pagecache still measures the share, and
			 * HDPageCacheSlotCutoutPermille is where the numbers above come
			 * from. */
			if (pass == 2)
			{
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
			}

			/* THE RANGES, MERGED BACK TOGETHER. They come off in (page, node)
			 * order and their index runs are contiguous, so a run of visible
			 * neighbours that share a page is ONE draw call. With mesh_bits
			 * at -1 that is the same number of calls stage S3-1 made; a
			 * hidden node splits the run in two and costs one more. */
			for (size_t i = 0; i < model.pass[pass].size(); )
			{
				const Range& first = model.pass[pass][i];

				if (!(nodeMask & (1u << (first.node & 31))))
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

					/* A RUN IS CUT BY THE PAGE, THE ORDER AND THE MASK
					 * (#123; the note here named the inner skin, which fix
					 * round S withdrew - corrected in fix round U, U5). Two
					 * ranges join when they wear the same page, when the
					 * second begins where the first ends, and when the mask
					 * keeps the second's node. */
					if (more.pageSlot != first.pageSlot ||
						more.start != start + count ||
						!(nodeMask & (1u << (more.node & 31))))
						break;

					count += more.count;
					next++;
				}

				g_device->SetTexture(0, first.pageSlot >= 0
					? (IDirect3DBaseTexture9*)HDPageCacheSlotTexture(first.pageSlot)
					: 0);

				/* THE MATERIAL OF THIS PAGE (#123, fix round I, I4). Only for
				 * a model a caller has spoken for - and then only when the
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

				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
					0, (UINT)model.vertices, start, count / 3);

				g_draw_calls++;
				i = next;
			}

			if (pass == 2)
			{
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			}
		}

		/* AND BACK TO DOUBLE SIDED. The bracket installed D3DCULL_NONE for
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

	/* WHAT TO CALL A SLOT IN A NOTE (#123, fix round B, B7).
	 *
	 * HDSlotName knows the engine's 471 slots and nothing else, and this
	 * module is handed PSEUDO slots as well - Lara's wardrobe parts
	 * (hd_lara.h) and the alternative models of a mesh swap (hd_object.cpp).
	 * For those it used to print "?", so a note read "? (slot 23041) asks for
	 * LARA_HEAD.TRM", which names the file and hides who wanted it. The
	 * model's own name is the honest label there, because a pseudo slot IS
	 * its model. */
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

	return true;
}

void HDModelShutdown()
{
	HDModelLevelEnd();

	if (g_decl) { g_decl->Release(); g_decl = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_ps) { g_ps->Release(); g_ps = 0; }

	g_device = 0;
	g_in_frame = false;
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
	// #153, round M2, the fix round - the phases of buildMs, and what the
	// .TRM reader did between these two readings.
	double trm_opens0 = 0, trm_loads0 = 0, trm_bytes0 = 0;

	g_key_ms = 0;
	HDTrmCounters(&trm_opens0, &trm_loads0, &trm_bytes0);

	if (stats)
		stats->slots = count;

	/* PASS ONE: read every slot's .TRM and build what does not need the
	 * device. The pages are collected as we go, because they all have to be
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
					// the mod's namespace (#153, round M1)
		/* #123 fix round D (D4), a SET since #153 round M2b: which pages of
		 * the file are built. Empty is the whole model. The numbers live here
		 * rather than in the caller's slot because round M2b works two of
		 * them out itself, while the file is open. */
		std::vector<int> cutPage;
		bool cutKeeps;
		// #156: which NODES of the file are built, as a mask; 0 is all of
		// them. See HDModelSlot::onlyNode.
		unsigned int cutNode;
		/* #153 round M2b: the index in `slots` this entry borrows its skeleton
		 * from, for a twin this module made up itself; -1 for every entry a
		 * caller asked for. */
		int twinOf;
		HDTrmFoldCounts fold;	// #153 round M2b: what the mirror fold did
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
		// for Lara, whose composite is stage S4 but whose parts a diagnostic
		// tool may well want to look at one at a time.
		if (!model && !slots[i].synth)
		{
			// Counted into find_ms: asking which file a slot means is the
			// same walk of the same folders the search below does, and a
			// mod adds two more places to walk (#153, round M2, fix round).
			const double bind_began = now_ms();

			model = HDBindSlot(installRoot, slot, &kind, &fromMap, 0);
			find_ms += now_ms() - bind_began;
		}

		/* A MODEL THAT IS BUILT AND NOT READ (#123, fix round I, I3; back in
		 * fix round S for the lid that closes the neck). It has no file, no
		 * head and no origin; everything after this point is the same road a
		 * read model takes. The source is looked up in what has already been
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
			// #156: a made model is whole. Pending is a plain struct with no
			// constructor, so every field of it has to be written here.
			made.cutNode = 0;
			made.twinOf = -1;
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

		/* A FILE THE CALLER FOUND ITSELF (#153, round M2) - the wardrobe, and
		 * nothing else today. No search at all, because there is nothing left
		 * to search for; see HDModelSlot::path for why the wardrobe cannot go
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
			// not reading or decoding anything (#153, round M2, the fix
			// round): every slot with no .TRM costs a walk of the ITEM
			// folders, and every active mod adds two more places to walk.
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
		/* WHOSE PAGE NUMBERS THESE ARE (#153, round M1). A mod's model
		 * names its own mod's pages, and only the ones the mod does not
		 * ship fall through to the install - see resolve_page. The mod's
		 * namespace therefore wins over the guest's: a guest a mod
		 * supplied is that mod's model now, and its origin= line is what
		 * sends its pages back to the I-III bundle. */
		here.pageBias = mod >= 0 ? HDPageModBase(mod)
			: tr3 ? HD_PAGE_ALT_BASE : 0;
		here.cutKeeps = true;
		here.cutNode = slots[i].onlyNode;	// #156
		here.twinOf = -1;

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

		/* The caller's own look at the file (#123, fix round K, K1), before
		 * anything here touches it. */
		if (slots[i].measure)
			slots[i].measure(&here.trm);

		/* THE MIRRORED WEIGHTS, FOLDED BACK (#153, round M2b) - before the
		 * measure above?  No: after it, and deliberately. The measure looks at
		 * the outfit's NECK and reads positions and pages, which this never
		 * touches; putting the fold first would only mean the one caller with
		 * a measure could no longer say what the file said. Everything that
		 * builds out of the weights comes after this line. */
		if (slots[i].mirror && slots[i].mirrors > 0)
			HDTrmFoldMirrored(&here.trm, slots[i].mirror, slots[i].mirrors,
				slots[i].mirrorBar, &here.fold);

		/* THE DONOR GAME'S OWN FRAME 0 (#155). What this is and
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
			/* AND NO INSTALL IS NO INSTALL (#155b). `g_install_tr3` is the
			 * I-III bundle's game 3 folder and the reader wants the bundle,
			 * which is its parent - but parent_of("") is ".", so a machine
			 * with only the IV-VI remaster sent the walk into <cwd>\1\DATA
			 * and reported whatever it found or did not find there. A path
			 * this module never had is a refusal and says so. */
			else if (g_install_tr3.empty())
				std::snprintf(said.why, sizeof(said.why),
					"there is no Tomb Raider I-III installation to read game "
					"%d's levels from", said.game);
			/* THE REASON ALONE, AND NOT THE OBJECT IT IS ABOUT (#155b). Every
			 * caller of this field names the game and the object itself, and
			 * naming them here as well printed "classic 1 object 102: game 1
			 * object 102: ..." - one fact twice in one line. The one case that
			 * still says "game %d" above is the one where the OBJECT is not
			 * known, so nobody else can name it. */
			else if (!HDClassicRead(parent_of(g_install_tr3).c_str(), said.game,
				said.object, &object, &trouble))
				std::snprintf(said.why, sizeof(said.why), "%s", trouble);
			else
			{
				said.meshes = object.meshes;
				std::snprintf(said.level, sizeof(said.level), "%s", object.level);

				/* THE MESH COUNT IS THE PROOF THAT THE TWO BELONG TOGETHER.
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

		/* MOD GUNS ON THE INSTALL'S HANDS (#153, round M2b). What this is and
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
				/* THE REST HANDS' OWN READ IS IN read_ms TOO (#153, round M3).
				 * The twin's read below has been counted since round M2b and
				 * this one - a whole second .TRM off the disk, per hand part
				 * with a mod's gun in it - was not, so the one line that says
				 * how long a level spent reading models was short by exactly
				 * the work this rule adds. */
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
					// #156: a hand twin is a whole model, never a node of one.
					twin.cutNode = 0;
					twin.twinOf = i;
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

						/* AND IF THE CALLER ALREADY ASKED FOR A CUT, THE TWO
						 * BECOME ONE (#153, round M2b, the fix round). Only
						 * one cut reaches build(), so writing this one over
						 * the caller's would throw its rule away without a
						 * word - and the two features were merged into one
						 * mechanism precisely so that they could not disagree
						 * about what a page number means.
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
						 * Nothing does both today - the one part with an
						 * onlyPage is the holster rig, which is kind 4 and
						 * never gets a twin - so this is the day after
						 * tomorrow's bug, caught while the mechanism is fresh
						 * and logged if it ever happens. */
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
						 * no cut of its own, which is every part today, and
						 * where it did the log has to name what survives both
						 * (#153, round M2b, the fix round). */
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
						 * rule the holster rig has had since fix round D. */
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
		 * three notes below use (#153, round M2). Without it the wardrobe's
		 * body would be the one model in a level whose per-part note says
		 * nothing - it used to say "comes from the sibling game 4", which was
		 * find_model's answer and is no longer asked for. */
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
		 * by name, by the measured table or by the strip rule (#123, fix
		 * round D, D1; see find_head_model above). */
		HDModelOrigin head_origin = HD_MODEL_ORIGIN_NONE;
		const char* head_how = "name";
		std::string head_name;
		const double head_find_began = now_ms();
		const std::string head_path = find_head_model(model, tr3, &head_origin,
			&head_how, &head_name, mod);

		// The face is a second walk of the same folders, so it belongs in
		// the same phase (#153, round M2, the fix round).
		find_ms += now_ms() - head_find_began;

		if (!head_path.empty())
		{
			const char* head_trouble = "";
			const double head_started = now_ms();

			here.hasHead = HDTrmLoad(head_path.c_str(), &here.head, &head_trouble);
			read_ms += now_ms() - head_started;

			if (!here.hasHead)
				note("%s (slot %d): %s.TRM %s - the body draws without it",
					slot_label(slot, model), slot, head_name.c_str(), head_trouble);
			else if (std::strcmp(head_how, "name") != 0)
				note("%s (slot %d): the face is %s.TRM, found by the %s rule",
					slot_label(slot, model), slot, head_name.c_str(), head_how);
		}

		/* Only the page that is kept is paid for (#123, fix round D, D4):
		 * the holster cut out of OUTFIT_TR3_CATSUIT wants one of that
		 * model's ten pages, and loading the other nine for it would cost
		 * more texture than the whole of Lara. */
		/* Through HDPageCacheKeyOf, exactly as build() does above (#153, round
		 * M2), because the list the cache is handed and the keys the ranges
		 * carry have to be the same numbers - a page asked for under one key
		 * and drawn under another is the white tile. Once per texture slot
		 * here as well, which is all this loop ever asked for. */
		const double keys_began = now_ms();

		{
			const PageCut paid =
			{
				here.cutPage.empty() ? 0 : &here.cutPage[0],
				(int)here.cutPage.size(), here.cutKeeps
			};

			/* AND THE NODE CUT PAYS FOR ITS OWN PAGES TOO (#156). A page cut
			 * can be answered out of the texture list alone; a node cut
			 * cannot - which pages a node wears is a fact about its
			 * TRIANGLES. So they are walked once, here, where the .TRM is
			 * open and the build has not run yet. Only a slot that asked for
			 * a node cut pays for that walk, and what it buys is the whole
			 * point of the cut: Lara's head out of OUTFIT_CLASSIC is two of
			 * that model's seven pages, 2.7 MiB instead of 9.3. */
			bool wornByNode[256];
			/* A SKINNED model only: dominant_node_of's answer for a facial rig
			 * depends on the neck bone, and that is measured in pass two,
			 * where there is a Model to keep it in. A rigid file with a node
			 * cut would therefore be paid for whole - which is the safe way
			 * round, since a page short is a white tile and a page over is
			 * memory. Nothing asks for that combination today. */
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
			 * it cannot make the format at all (#123, stage S6). Going on
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

	/* PASS TWO: the buffers. */
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
			/* A TWIN TAKES ITS SKELETON FROM THE PART IT STANDS BESIDE (#153,
			 * round M2b). It is not a slot the caller asked for - this module
			 * made it - so `slots` has no row of its own for it, and without
			 * this it would draw with no nodes and no bind fold at all: the
			 * install's hands in their own bind pose while the mod's gun
			 * follows the animation. */
			if (slots[k].slot == here.slot ||
				(here.twinOf >= 0 && k == here.twinOf))
			{
				model.nodes = slots[k].nodes;
				model.rigidNode = slots[k].rigidNode;
				// #155b: a twin inherits it with the rest of the row, which
				// is right - it is drawn beside the model that asked.
				model.keepPoints = slots[k].keepPoints;

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

				/* The classic mesh TREE, for the head pass (B4). */
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

		const PageCut cut =
		{
			here.cutPage.empty() ? 0 : &here.cutPage[0],
			(int)here.cutPage.size(), here.cutKeeps
		};

		const NodeCut nodeCut = { here.cutNode };	// #156

		if (!build(here.trm, model, model.rigidNode, here.pageBias, &mixed,
			&trouble, cut, nodeCut))
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

	/* PASS THREE: the separate faces, rigidly on ONE node - measured, not
	 * hard coded: the bind-pose node nearest the face geometry's own centre.
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

		const int node = head_node_of(here.head, body);

		head.rigidNode = node < 0 ? 0 : node;

		/* THE NECK (#123, fix round B, B4). It needs the classic TREE, which
		 * only a caller that walked the mesh tree can hand over - without it
		 * the face stays rigid the way it was, which is the old picture and
		 * not a wrong one. */
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

		// The whole face: a page cut is the BODY's business (fix round D, D4),
		// and so is a node cut (#156).
		const PageCut all = { 0, 0, true };
		const NodeCut everyNode = { 0 };

		if (!build(here.head, head, head.rigidNode, here.pageBias, &ignore,
			&trouble, all, everyNode))
		{
			note("%s_HEAD (slot %d): %s", here.name.c_str(), here.slot, trouble);
			free_model(head);
			HDTrmFree(&here.head);
			continue;
		}

		g_heads[here.slot] = head;

		if (head.neckBone >= 0)
			note("%s (slot %d): the HD face %s rides on node %d of %d, its "
				"neck (own bone %d) on node %d",
				slot_label(here.slot, here.name.c_str()), here.slot,
				head.name.c_str(), head.rigidNode, head.nodes,
				head.neckBone, head.neckNode);
		else
			note("%s (slot %d): the HD face %s rides on node %d of %d",
				slot_label(here.slot, here.name.c_str()), here.slot,
				head.name.c_str(), head.rigidNode, head.nodes);

		if (stats)
		{
			stats->heads++;
			stats->vertices += head.vertices;
			stats->indices += head.indices;
			stats->ranges += (int)(head.pass[0].size() + head.pass[1].size() +
				head.pass[2].size());
			stats->vertexBytes += (double)head.vertices * sizeof(HDModelVertex);
			stats->indexBytes += (double)head.indices * 2.0;
		}

		HDTrmFree(&here.head);
	}

	/* The coverage count. A slot whose model has geometry for every classic
	 * node is FULL; one with a gap is PARTIAL, and stage S3-2 draws the gap
	 * the classic way. */
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
		stats->readMs = read_ms;
		stats->buildMs = now_ms() - started - read_ms;

		/* The phases of buildMs (#153, round M2, the fix round). They do not
		 * add up to it and are not meant to - what is left over is the
		 * bookkeeping between them - but a phase that has run away shows up
		 * here as the number it is. bufferMs is taken last because pass two
		 * is where this block stands. */
		stats->findMs = find_ms;
		stats->keyMs = g_key_ms;
		stats->pageMs = page_ms;
		stats->bufferMs = now_ms() - buffers_began;
		stats->trmOpens = trm_opens1 - trm_opens0;
		stats->trmLoads = trm_loads1 - trm_loads0;
		stats->trmBytes = trm_bytes1 - trm_bytes0;
	}

	return true;
}

/* Kept across a level, like the page cache's own alternate root: it names an
 * installation, not a level (#123, stage S7-2). */
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
}

/* THE WARM-UP (#148, order 1). A model's vertex and index buffer are
 * D3DPOOL_MANAGED, and a managed resource is a system-memory copy until
 * something DRAWS with it: Direct3D uploads it to the card the first time it
 * is bound, inside the first frame it appears in. That is one cause of the
 * warm-up the user measured - the first seconds of a level cost an upload per
 * model, per page and per room, and they are paid inside frames that were
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
		// matrices out of it and has to put it back afterwards (P2).
		std::memcpy(g_frame_proj, projection, sizeof(g_frame_proj));
		set_matrix_constant(0, g_frame_proj);
	}

	g_off_centre = false;
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
	 * pickup_draw for the display in the corner (P2) - sit between their
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
	const HDModelNeckCut* cut, int cull)
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
		cut, cull);
	return true;
}

bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, unsigned int nodeMask, const HDModelLighting* lighting,
	const HDModelNeckCut* cut, int cull)
{
	if (!g_in_frame || !nodeMatrix || nodeCount <= 0)
		return false;

	std::map<int, Model>::iterator at = g_heads.find(slot);

	if (at == g_heads.end() || !at->second.vb)
		return false;

	/* THE FACE FOLLOWS ITS NODE'S BIT. A separate <NAME>_HEAD.TRM is built
	 * rigidly on one node, so every one of its ranges carries that node
	 * number and the same mask answers for it - a head shot away by
	 * mesh_bits takes the HD face with it. */
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);

	draw_model(at->second, nodeMatrix, nodeCount, nodeLight, nodeMask, lighting,
		cut, cull);
	return true;
}

int HDModelDrawCalls()
{
	return g_draw_calls;
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

/* THE CLOUD'S OWN AXES (#153, round M3), measured in build() - the note at
 * Model::axis says why they are taken there and not here. `axes` takes nine
 * floats, three unit row vectors, longest first and right handed; `extent`
 * takes the cloud's span along each of them. False for a model that has none
 * (fewer than three vertices).
 *
 * A BOX AND A CLOUD ANSWER DIFFERENT QUESTIONS. A box says how much room a
 * model needs; these say which way it RUNS. For a flat pair of guns the two
 * are nothing alike, which is the whole of the round M2b ring fault. */
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

/* THE CORRECTION, out of the two models' own SHAPES.
 *
 * Round M2b built this out of the two bounding BOXES and the user's picture of
 * the result is why it does not any more: his Uzi mod's inventory model came
 * out lying flat, the two guns one above the other with a gap between them,
 * where the same file in the Tomb Raider I-III remaster's own ring stands up
 * facing the camera. A box cannot tell a flat pair of guns from one tall gun
 * standing up - 147 x 253 x 159 with y longest is what both measure.
 *
 * THE SHAPE SAYS IT AND THE BOX CANNOT. The principal axes of the vertex cloud
 * (HDModelPrincipalAxes, measured while the model was read) are the directions
 * the model really runs in: the two largest span the plane the shape lies in
 * and the smallest is the way it is flat. Measured on the user's own files:
 *
 *   install EXTRA_UZI_ITEM    cloud 256 x 236 x  42, flat along y
 *   mod     UZI_OPTION        cloud 242 x 240 x 132, flat along x
 *
 * - two flat things, each flat along a different axis, which the boxes could
 * not say. Sending the source's k-th axis onto the target's k-th therefore
 * lays the mod's pair into the plane the vanilla item lies in, and the ring's
 * own per-item rotation then presents it as it presents the vanilla one:
 * face-on to the camera, turning about the vertical like every other item in
 * the ring. Nothing about either game is written down - the two files say
 * which way round they are.
 *
 * WHAT IT IS NOT, LOOKED AT AND MEASURED (the fix round of M3). The user asked
 * for the two Uzis CROSSED like an X, which is what the I-III remaster's own
 * display shows him. THE FILE IS NOT A CROSSED PAIR. Face-on to its own plane
 * and along both of that plane's axes - three renders, ModelProbe -name
 * UZI_OPTION -rigid -axisview - the two guns stand PARALLEL, side by side and
 * offset; the vanilla EXTRA_UZI_ITEM beside them is ONE gun, not two. A rigid
 * turn keeps the angle between two parts, so no rotation of this file can
 * cross them, and this fit does not pretend to. Where the remaster's X comes
 * from is not established here: the .TRM carries no node transforms at all
 * (specific\hd\hd_trm.h - per-vertex bone indices and weights, nothing else),
 * its 27449 vertices sit on two nodes of 13723 and 13726, and the ring loads a
 * mod's _OPTION model RIGIDLY on one node because there is no mesh tree of
 * ours behind another game's file. Placing those two nodes would mean reading
 * the donor game's own mesh tree and frame out of the user's I-III data, which
 * is a feature and not a fit - a decision for the project, not for this
 * function to take on its own.
 *
 * THE PLANE IS WELL CONDITIONED AND THE ANGLE IN IT IS NOT (measured; the
 * review after the fix round of M3). Which axis is the FLATTEST - and so which
 * plane faces the camera - is decided by a wide margin on both files: 132.3
 * against 239.5 for the mod's pair, 41.6 against 235.7 for the vanilla item,
 * which is a factor of nearly six. The two axes INSIDE that plane are decided
 * by 242.3 against 239.5 on the mod's file, a gap of 1.2 %, and sort_axes
 * orders them by that gap alone. So the ROLL inside the plane is deterministic
 * - the same file gives the same answer every time - but it is not meaningful:
 * a mod update that moved a few dozen vertices could swap the two and turn the
 * item a quarter turn in the ring. The brief asked for the in-plane angle to
 * put the pair's SYMMETRY axis where the vanilla item's is, and no symmetry
 * test is made here; a max-extent sweep in the plane is the next step if the
 * user dislikes an angle. It is written down rather than guessed at, because a
 * silent quarter turn after a mod update would otherwise look like a new bug.
 *
 * Both frames are right handed by construction (see the axes' own note), so
 * the map is a pure rotation and can never mirror a model - which the box
 * rule had to repair with a negated axis.
 *
 * The scale is uniform and is the ratio of the two longest extents, so the
 * mod's model is as long in the ring as the model it replaces. The longest
 * axis is always one of the two IN the plane, so the scale is decided by the
 * shape's own reach and never by how thick it is - which is the half of the
 * old rule that was right and is kept.
 *
 * THE PRICE IS THAT THE FITTED MODEL CAN STILL STAND OUT OF THE TARGET'S BOX
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

	/* THE TURN. Row vectors, as everywhere in this engine's matrices: the
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

	/* CENTRED ON THE TWO BOXES and not on the two clouds' centres of mass:
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

/* HOW FAR THAT FIT LEAVES THE TARGET'S ROOM (#153, round M2b, the second fix
 * round; on the shape's own axes since round M3).
 *
 * The scale is the ratio of the two longest extents, so the longest axis lands
 * exactly and the other two land wherever their own proportions put them. This
 * is the largest of the three ratios - fitted extent over the target's on the
 * same axis - so 1.00 means the model sits inside the room the ring keeps for
 * that item and the 3.36 the Uzi mod reads means one of its axes is over three
 * times that room.
 *
 * It is a NUMBER FOR THE LOG and not a limit: nothing here refuses a fit, for
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

/* ------------------------------ THE FRAME THE LEVEL DRAWS A SLOT IN (#155b,
 *
 * fix round 1) ---------------------------------------------------------------
 *
 * THE BIND POSE IS NOT THE POSE. A moveable's meshes are placed by its mesh
 * tree AND turned by its animation frame: DrawAnimatingItem translates by the
 * frame's own offset, calls gar_RotYXZsuperpack for node 0 and then walks the
 * tree with a translation and a rotation per node (game\draw.cpp:955-1000).
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
 * TWO THINGS ARE LEFT OUT ON PURPOSE.
 *
 * The frame's OFFSET, because it is one translation in front of the whole
 * walk: it moves every node of both models by the same vector, so it cancels
 * in the difference the floor rule is about, and leaving it out keeps node 0
 * at the origin where the bind numbers were.
 *
 * The ITEM's own rotation, because a pickup LYING THERE carries a yaw and a
 * yaw does not tilt the up axis - measured, not assumed: InitialiseItem
 * zeroes x_rot and z_rot (game\items.cpp:201-202), the level file gives an
 * item one angle, and the one place that tilts a pickup Lara is standing over
 * puts the three angles back before anything is drawn (PickUpCollision,
 * game\pickup.cpp:789-793 and 1124-1126).
 *
 * TWO OF THE FAMILY DO TILT, and the claim above is not exhaustive without
 * them (fix round 2, the reviewer's finding): a THROWN one. FlareControl
 * turns a flare in flight by 546 about x and 910 about z every frame
 * (game\laraflar.cpp:31-37) and FlameTorchControl a falling torch by 910
 * about z (game\flmtorch.cpp:286-287), both outside PickUpCollision's
 * save-and-restore, and both zero the two angles again the moment the thing
 * comes to rest. So the frame this rule measures in is the frame those two
 * are drawn in for as long as they lie on the ground, which is when a floor
 * rule is about them at all, and their sink is small either way -
 * FLARE_ITEM 1.0 units and BURNING_TORCH_ITEM 2.9. A tumbling flare is
 * lifted by that same amount STRAIGHT UP IN THE WORLD, because that is where
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
	 * against aRotX/aRotY/aRotZ (specific\3dmath.cpp:307-440) a line at a
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

	/* gar_RotYXZsuperpack with skip 0 (game\draw.cpp:398-439): either two
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

		/* THE THREE EXTRA ROTATIONS A BONE CAN ASK FOR (flags 0x1C) are read
		 * out of item->data, which is a CREATURE's head and torso stream. A
		 * pickup has no data and the draw hands the walk `no_rotation`, all
		 * zeroes, so they are identity here - and phd_RotY(0) is identity in
		 * the draw too, which is why nothing has to be skipped. */

		memcpy(out + node * 12, current, sizeof(current));
	}

	return nodes;
}

/* HOW DEEP A PICKUP MODEL REACHES, AND WHAT LIFTS IT OUT (#155b). The header
 * carries the rule; this is the arithmetic.
 *
 * Round #155 pushed the eight CORNERS OF THE BOX through the fit and took the
 * deepest of them, which is a bound on the model and not the model: a box
 * corner of a pair of crossed guns is empty space. Measured on the user's own
 * posed `UZI_OPTION` against the classic mesh it stands in for: 33.9 units by
 * the corners, 11.5 by the vertices - a factor of three. A rule that lifted a
 * model by the corner number would hang it in the air, which is the same fault
 * the other way round.
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

	/* THE POINTS OR NOTHING (fix round 1 made this the only way). Until the
	 * frame's own turn came into the measurement, a model drawn as it stands
	 * could be measured off the box: with every node's matrix a pure
	 * translation equal to its own bind offset, a vertex is drawn exactly
	 * where the bind pose has it, so the box of the bind positions IS the box
	 * of the drawn ones. A ROTATION breaks that, and an answer off the box
	 * would then be a bound and not the model - quietly, in a number that
	 * looks like the others. */
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
			/* THE FITTED CASE is the ring's <BASE>_OPTION model, which stands
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
			/* AND THE SLOT'S OWN MODEL THROUGH THE BIND FOLD, which is what
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

		/* AND THROUGH THE FRAME THE LEVEL DRAWS THAT NODE IN - HDFramePose's
		 * 3x4, a point as a COLUMN. Without a pose the frame turns nothing
		 * and the answer is the bind position, which is the number every
		 * bind-pose measurement of this round printed. */
		const float* m = pose ? pose + (size_t)node * 12 : 0;

		if (m)
			for (int r = 0; r < 3; r++)
				out[r] = m[r * 4 + 0] * q[0] + m[r * 4 + 1] * q[1] +
					m[r * 4 + 2] * q[2] + m[r * 4 + 3];
		else
			for (int r = 0; r < 3; r++)
				out[r] = q[r];

		/* AND WHERE THE DEEPEST POINT ITSELF IS (fix round 1), not only how
		 * deep it reaches: a picture of the rule has to put the floor plane
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

	/* ONE SWEEP FOR BOTH ANSWERS (fix round 1): the deepest point IS the
	 * drawn box's largest y, and the tool needs the whole box to frame a
	 * picture of the same thing. Two sweeps of the same points with two
	 * copies of the same fold is one copy too many. */
	if (!HDModelFramedBox(model, fix, pose, nodes, lo, hi))
		return false;

	const float deepest = hi[1];

	/* THE SLACK. Half a unit, which is half a millimetre at this engine's
	 * scale of 1024 units to a metre and well under the rounding of the
	 * classic mesh's own SHORT coordinates. It keeps the rule from writing a
	 * lift for a model that is modelled ON the classic mesh and misses it by
	 * a float's last digit - which is every one of the remaster's own pickup
	 * files, and a matrix touched for nothing is a matrix nobody can rule out
	 * later. */
	const float slack = 0.5f;

	if (lowest)
		*lowest = deepest;
	if (lift)
		*lift = deepest - classicLowest > slack ? deepest - classicLowest : 0.0f;

	return true;
}

/* AND THE POINT LISTS BACK (#155b). Every model of the level at once, because
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

/* WHAT THE FLOOR RULE HAD TO MEASURE ON, for the caller's log (#155b, fix
 * round 1). `kept` is how many vertex positions are beside the model and
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

/* THE MATERIAL OF ONE PAGE (#123, fix round I, I4). See PageSheen at the top
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

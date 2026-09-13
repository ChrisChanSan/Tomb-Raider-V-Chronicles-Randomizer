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
	};

	const int kJointBudget = 32;

	IDirect3DDevice9* g_device = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;

	std::map<int, Model> g_models;		// slot -> body
	std::map<int, Model> g_heads;		// slot -> its separate HD face
	std::vector<std::string> g_notes;
	std::string g_install;
	/* The other bundle's game 3 folder, for the four Tomb Raider III guests
	 * (#123, stage S7-2). Empty unless the caller set it, and then no slot
	 * can reach that bundle at all. */
	std::string g_install_tr3;
	// A refusal whose wording is not a literal - it has to outlive the call
	// that hands *why out. Never cleared by HDModelLevelEnd.
	std::string g_error;

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
	bool build(const HDTrmModel& trm, Model& model, int rigidNode,
		int pageBias, int* mixedTriangles, const char** why, int onlyPage = -1)
	{
		const int vertices = trm.vertices;

		std::vector<HDModelVertex> buffer((size_t)vertices);

		/* THE PAGE CUT (#123, fix round D, D4). With onlyPage set, a vertex
		 * no kept triangle names takes no part in ANYTHING the loop below
		 * measures - not the bounds, not the node coverage, not the bind
		 * drift. Otherwise a holster cut out of an outfit would report that
		 * it covers all fifteen nodes and reaches from her hair to her boots,
		 * and the composite would hand it the whole body's work. The vertex
		 * itself stays in the buffer: the index list is the file's own and
		 * renumbering it would buy 400 KiB and a new class of bug. */
		std::vector<bool> keep((size_t)vertices, onlyPage < 0);

		if (onlyPage >= 0)
			for (int i = 0; i + 2 < trm.indices; i += 3)
			{
				const int page = HDTrmPageOf(&trm, &trm.vertex[trm.index[i]]);

				if (page != onlyPage)
					continue;

				for (int corner = 0; corner < 3; corner++)
					keep[(size_t)trm.index[i + corner]] = true;
			}

		// The bind drift's working set: how many single-weight vertices a
		// node carries and where their centre is.
		int single[kJointBudget] = { 0 };
		double centre[kJointBudget][3] = { { 0, 0, 0 } };
		double square[kJointBudget] = { 0 };

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

		for (int i = 0; i + 2 < trm.indices; i += 3)
		{
			const int rawA = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 0]]);
			const int rawB = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 1]]);
			const int rawC = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 2]]);

			const int a = rawA < 0 ? -1 : HDPageCacheKeyOf(rawA + pageBias);
			const int b = rawB < 0 ? -1 : HDPageCacheKeyOf(rawB + pageBias);
			const int c = rawC < 0 ? -1 : HDPageCacheKeyOf(rawC + pageBias);

			if (mixedTriangles && (a != b || b != c))
				(*mixedTriangles)++;

			page_of_triangle.push_back(a);

			if (a >= 0 && (onlyPage < 0 || rawA == onlyPage) &&
				std::find(model.pages.begin(), model.pages.end(), a) == model.pages.end())
				model.pages.push_back(a);

			/* WHICH NODE A TRIANGLE BELONGS TO: the node that carries the
			 * most weight over its three corners. A triangle that straddles a
			 * joint has to fall on one side of it, and the heavier side is
			 * the side it moves with - the same rule the eye applies when it
			 * decides whether a polygon "is" the upper arm or the forearm.
			 * Only used to hide a node, so a wrong answer at a seam costs at
			 * most one triangle at the cut edge of a blown-off limb. */
			double weight[kJointBudget] = { 0 };
			int best = 0;
			double heaviest = -1;

			for (int corner = 0; corner < 3; corner++)
			{
				const HDTrmVertex& v = trm.vertex[trm.index[i + corner]];

				for (int b2 = 0; b2 < 3; b2++)
				{
					/* The rigid case reads the vertex as it was WRITTEN, not
					 * as it arrived: since fix round B a facial-rig file's
					 * neck bone lands on the head node's parent, and a neck
					 * triangle belongs to that node when mesh_bits hides one
					 * of the two. */
					const int node = rigidNode >= 0
						? (model.neckBone >= 0 && (int)v.bone[b2] == model.neckBone
							? model.neckNode : rigidNode)
						: (int)(v.bone[b2] < kJointBudget ? v.bone[b2] : kJointBudget - 1);
					const int w = rigidNode >= 0
						? (model.neckBone >= 0 ? (int)v.weight[b2] : (b2 == 0 ? 255 : 0))
						: (int)v.weight[b2];

					weight[node] += (double)w;
				}
			}

			for (int node = 0; node < kJointBudget; node++)
				if (weight[node] > heaviest)
				{
					heaviest = weight[node];
					best = node;
				}

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
					if (onlyPage >= 0 &&
						HDTrmPageOf(&trm, &trm.vertex[trm.index[at]]) != onlyPage)
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
			*why = onlyPage >= 0
				? "has no triangle on the page that was asked for"
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
		int onlyPage;		// #123 fix round D (D4): -1 = the whole model
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
			model = HDBindSlot(installRoot, slot, &kind, &fromMap, 0);

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
			made.onlyPage = -1;
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
			path = find_model(model, tr3, &origin, &mod);

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
		here.onlyPage = slots[i].onlyPage;
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
		const std::string head_path = find_head_model(model, tr3, &head_origin,
			&head_how, &head_name, mod);

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
		 * and drawn under another is the white tile. */
		if (here.onlyPage >= 0)
			pages.push_back(HDPageCacheKeyOf(here.onlyPage + here.pageBias));
		else
			for (int t = 0; t < here.trm.textures; t++)
				pages.push_back(HDPageCacheKeyOf((int)here.trm.texture[t] + here.pageBias));

		if (here.hasHead)
			for (int t = 0; t < here.head.textures; t++)
				pages.push_back(HDPageCacheKeyOf((int)here.head.texture[t] + here.pageBias));

		pending.push_back(here);
	}

	/* The pages, ADDED to what the rooms already loaded: a page a room and a
	 * model both wear keeps one texture and one slot number. */
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

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		note("%s", HDPageCacheNote(i));

	/* PASS TWO: the buffers. */
	int mixed = 0;

	for (size_t i = 0; i < pending.size(); i++)
	{
		Pending& here = pending[i];
		Model model;

		model.name = here.name;
		model.origin = here.origin;
		model.nodes = 0;

		for (int k = 0; k < count; k++)
			if (slots[k].slot == here.slot)
			{
				model.nodes = slots[k].nodes;
				model.rigidNode = slots[k].rigidNode;

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

		if (!build(here.trm, model, model.rigidNode, here.pageBias, &mixed,
			&trouble, here.onlyPage))
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

		if (!build(here.head, head, head.rigidNode, here.pageBias, &ignore, &trouble))
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

		stats->nodesFallback = stats->nodesTotal - stats->nodesCovered;
		stats->readMs = read_ms;
		stats->buildMs = now_ms() - started - read_ms;
	}

	return true;
}

/* Kept across a level, like the page cache's own alternate root: it names an
 * installation, not a level (#123, stage S7-2). */
void HDModelTr3Root(const char* root)
{
	g_install_tr3 = root ? root : "";
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

int HDModelNoteCount()
{
	return (int)g_notes.size();
}

const char* HDModelNote(int at)
{
	return (at >= 0 && at < (int)g_notes.size()) ? g_notes[(size_t)at].c_str() : "";
}

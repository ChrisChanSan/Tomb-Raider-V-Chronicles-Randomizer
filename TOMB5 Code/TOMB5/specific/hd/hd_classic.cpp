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
#include "hd_classic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	/* ------------------------------------------ the level file's shape --
	 *
	 * The published TR1/TR2/TR3 level layouts (TRosettaStone), as far as the
	 * moveable table - which is all this module wants. Everything before it
	 * is length prefixed, so the blocks only have to be SKIPPED in the right
	 * order; the room header is the one place where the three games differ in
	 * a way that has to be known rather than read.
	 *
	 *   header   TR1: u32 version, u32 textiles, textiles * 65536, u32 unused
	 *            TR2/TR3: u32 version, 768 B palette, 1024 B palette16,
	 *                     u32 textiles, textiles * 65536, textiles * 131072,
	 *                     u32 unused
	 *   rooms    u16 count, then each: 16 B info; u32 words + u16[]; u16
	 *            portals + 32 B each; u16 z, u16 x, sectors 8 B each; the
	 *            ambient block (below); u16 lights + n B each; u16 statics +
	 *            n B each; i16 alternate room, i16 flags; TR3 alone three
	 *            more bytes (water scheme, reverb, filler)
	 *   then     u32 floor data + u16[]; u32 mesh data + u16[]; u32 mesh
	 *            pointers + u32[]; u32 animations + 32 B each; u32 state
	 *            changes + 6 B; u32 dispatches + 8 B; u32 commands + 2 B;
	 *            u32 mesh tree words + 4 B each; u32 frame words + 2 B each;
	 *            u32 moveables + 18 B each
	 *
	 * A MOVEABLE is u32 object id, u16 meshes, u16 first mesh, u32 mesh tree
	 * (the INDEX of its first i32 word), u32 frame offset (BYTES into the
	 * frame block), u16 animation. */
	struct Variant
	{
		int game;
		int ambient;		// bytes of ambient/light-mode words in a room
		int light;		// bytes per room light
		int staticMesh;		// bytes per room static mesh
		int tail;		// bytes after the room's flags word
	};

	bool variant_of(unsigned int version, Variant* out)
	{
		// TR1 .PHD 0x20; TR2 .TR2 0x2D; TR3 .TR2 0xFF080038 / 0xFF180038 and
		// the 0xFF180034 some builds carry - the high byte is the mark.
		if (version == 0x00000020u)
		{
			const Variant one = { 1, 2, 18, 18, 0 };

			*out = one;
			return true;
		}

		if (version == 0x0000002Du)
		{
			const Variant one = { 2, 6, 24, 20, 0 };

			*out = one;
			return true;
		}

		if ((version >> 24) == 0xFFu)
		{
			const Variant one = { 3, 4, 24, 20, 3 };

			*out = one;
			return true;
		}

		return false;
	}

	/* A READ THAT CANNOT WALK OFF THE FILE. Every count in a level file is a
	 * number this module did not write, so `left()` is checked before each
	 * one is believed and `bad` is sticky: the first impossible number ends
	 * the walk and every read after it is a no-op. The file is STREAMED - a
	 * TR3 level is twelve megabytes and eleven of them are texture pages the
	 * walk only has to seek past. */
	struct Reader
	{
		std::FILE* file = 0;
		long long size = 0;
		long long at = 0;
		bool bad = false;

		bool open(const std::string& path)
		{
			file = std::fopen(path.c_str(), "rb");

			if (!file)
				return false;

			std::fseek(file, 0, SEEK_END);
			size = std::ftell(file);
			std::fseek(file, 0, SEEK_SET);
			at = 0;
			bad = size <= 0;

			/* AND AN EMPTY FILE IS CLOSED HERE (#155b). The caller's rule is
			 * "open failed, nothing to close" - every one of them returns on
			 * false without a close() - so a handle left standing on a
			 * zero-byte file was leaked for the length of the session, once
			 * per such file in the walk. */
			if (bad)
				close();

			return !bad;
		}

		void close()
		{
			if (file)
				std::fclose(file);

			file = 0;
		}

		long long left() const { return size - at; }

		bool room(long long n)
		{
			if (bad || n < 0 || n > left())
			{
				bad = true;
				return false;
			}

			return true;
		}

		bool take(void* into, long long n)
		{
			if (!room(n))
				return false;

			if ((long long)std::fread(into, 1, (size_t)n, file) != n)
			{
				bad = true;
				return false;
			}

			at += n;
			return true;
		}

		void skip(long long n)
		{
			if (!room(n))
				return;

			std::fseek(file, (long)n, SEEK_CUR);
			at += n;
		}

		unsigned short u16()
		{
			unsigned short v = 0;

			take(&v, 2);
			return v;
		}


		unsigned int u32()
		{
			unsigned int v = 0;

			take(&v, 4);
			return v;
		}
	};

	struct Moveable
	{
		unsigned int id;
		unsigned short meshes;
		unsigned short firstMesh;
		unsigned int tree;
		unsigned int frame;
		unsigned short anim;
	};

	/* THE FRAME'S ROTATIONS.
	 *
	 * TR2 and TR3 pack one mesh into one or two words: the top two bits of the
	 * first word are 00 for a three-axis rotation in TWO words and 01 / 10 / 11
	 * for a single axis x / y / z in the low ten bits of that one word. With
	 * the pair read as one 32 bit value, high word first, the three axes are
	 * bits 20..29, 10..19 and 0..9.
	 *
	 * TR1 has no single-axis form: every mesh is two words, and the pair is
	 * read with the SECOND word as the high half. That is the measured half of
	 * the layout - see the note in hd_classic.h for the numbers. */
	void read_rotations(const unsigned char* frame, long long bytes, int game,
		int meshes, short rot[HD_CLASSIC_NODES][3], bool* ok)
	{
		long long p = 18;

		if (game == 1)
			p += 2;		// TR1 alone counts its meshes into the frame

		for (int i = 0; i < meshes; i++)
		{
			if (p + 2 > bytes)
			{
				*ok = false;
				return;
			}

			unsigned short w1 = 0, w2 = 0;

			std::memcpy(&w1, frame + p, 2);

			const int axis = game == 1 ? 0 : (w1 >> 14);

			if (axis == 0)
			{
				if (p + 4 > bytes)
				{
					*ok = false;
					return;
				}

				std::memcpy(&w2, frame + p + 2, 2);

				const unsigned int packed = game == 1
					? ((unsigned int)w2 << 16) | w1
					: ((unsigned int)w1 << 16) | w2;

				rot[i][0] = (short)((packed >> 20) & 0x3FF);
				rot[i][1] = (short)((packed >> 10) & 0x3FF);
				rot[i][2] = (short)(packed & 0x3FF);
				p += 4;
			}
			else
			{
				const short a = (short)(w1 & 0x3FF);

				rot[i][0] = axis == 1 ? a : 0;
				rot[i][1] = axis == 2 ? a : 0;
				rot[i][2] = axis == 3 ? a : 0;
				p += 2;
			}
		}

		*ok = true;
	}

	/* ONE LEVEL FILE, walked to its moveable table and then to the object.
	 *
	 * The mesh tree and the frame block are the only two things read whole -
	 * a few kilobytes each in TR1 and a few hundred in TR3 - because the
	 * object's bones and its frame sit at offsets the moveable table only
	 * names AFTER both blocks have gone past. */
	bool read_level(const std::string& path, int id, HDClassicObject* out)
	{
		Reader r;

		if (!r.open(path))
			return false;

		Variant v;

		if (!variant_of(r.u32(), &v) || r.bad)
		{
			r.close();
			return false;
		}

		if (v.game != 1)
			r.skip(768 + 1024);

		const unsigned int textiles = r.u32();

		r.skip((long long)textiles * 65536);

		if (v.game != 1)
			r.skip((long long)textiles * 131072);

		r.u32();			// unused

		const unsigned short rooms = r.u16();

		for (unsigned int i = 0; i < rooms && !r.bad; i++)
		{
			r.skip(16);
			r.skip((long long)r.u32() * 2);		// the room's geometry
			r.skip((long long)r.u16() * 32);	// portals
			{
				const unsigned int z = r.u16();
				const unsigned int x = r.u16();

				r.skip((long long)z * x * 8);	// sectors
			}
			r.skip(v.ambient);
			r.skip((long long)r.u16() * v.light);
			r.skip((long long)r.u16() * v.staticMesh);
			r.skip(2 + 2 + v.tail);
		}

		r.skip((long long)r.u32() * 2);			// floor data
		r.skip((long long)r.u32() * 2);			// mesh data
		r.skip((long long)r.u32() * 4);			// mesh pointers
		r.skip((long long)r.u32() * 32);		// animations
		r.skip((long long)r.u32() * 6);			// state changes
		r.skip((long long)r.u32() * 8);			// anim dispatches
		r.skip((long long)r.u32() * 2);			// anim commands

		const unsigned int treeWords = r.u32();
		std::vector<int> tree;

		if (r.room((long long)treeWords * 4))
		{
			tree.resize(treeWords);
			r.take(treeWords ? &tree[0] : 0, (long long)treeWords * 4);
		}

		const unsigned int frameWords = r.u32();
		std::vector<unsigned char> frames;

		if (r.room((long long)frameWords * 2))
		{
			frames.resize((size_t)frameWords * 2);
			r.take(frameWords ? &frames[0] : 0, (long long)frameWords * 2);
		}

		const unsigned int count = r.u32();

		if (r.bad || !r.room((long long)count * 18))
		{
			r.close();
			return false;
		}

		Moveable found = { 0, 0, 0, 0, 0, 0 };
		bool have = false;

		for (unsigned int i = 0; i < count && !r.bad; i++)
		{
			Moveable one;

			one.id = r.u32();
			one.meshes = r.u16();
			one.firstMesh = r.u16();
			one.tree = r.u32();
			one.frame = r.u32();
			one.anim = r.u16();

			if (!have && (int)one.id == id && one.meshes > 0)
			{
				found = one;
				have = true;
			}
		}

		r.close();

		if (r.bad || !have || found.meshes > HD_CLASSIC_NODES)
			return false;

		// The bones: four ints each, for every mesh after the first.
		if ((unsigned long long)found.tree + (unsigned long long)(found.meshes - 1) * 4
			> (unsigned long long)tree.size())
			return false;

		if ((unsigned long long)found.frame + 18 > (unsigned long long)frames.size())
			return false;

		std::memset(out, 0, sizeof(*out));
		out->game = v.game;
		out->id = id;
		out->meshes = found.meshes;

		for (int i = 1; i < out->meshes; i++)
		{
			const int* row = &tree[found.tree + (size_t)(i - 1) * 4];

			out->bone[i].flags = row[0];
			out->bone[i].x = row[1];
			out->bone[i].y = row[2];
			out->bone[i].z = row[3];
		}

		const unsigned char* frame = &frames[found.frame];
		const long long bytes = (long long)frames.size() - (long long)found.frame;

		for (int i = 0; i < 6; i++)
			std::memcpy(&out->bounds[i], frame + i * 2, 2);

		for (int i = 0; i < 3; i++)
			std::memcpy(&out->offset[i], frame + 12 + i * 2, 2);

		bool ok = false;

		read_rotations(frame, bytes, v.game, out->meshes, out->rot, &ok);

		if (!ok)
			return false;

		const std::string leaf = fs::u8path(path).filename().u8string();

		std::snprintf(out->level, sizeof(out->level), "%s", leaf.c_str());
		return true;
	}

	/* ------------------------------------------------------- the table --
	 *
	 * The classic object id of each weapon and each ammunition box, per game.
	 *
	 * THE SOURCE, named because a number nobody can check is a number nobody
	 * should trust. Tomb Raider III: the user's copy of the GPL tomb3 sources,
	 * game/objects.h, enum object_types, counted through and verified against
	 * 3\DATA\JUNGLE.TR2. Tomb Raider I and II: the GPL project TRX
	 * (LostArtefacts), data/trx/ship/games/tr1|tr2|tr3/catalog_objects.csv,
	 * branch develop, fetched 2026-09-14 - its TR3 column agrees with tomb3 on
	 * every id here, which is what makes its TR1 and TR2 columns worth having.
	 * Every id below was then looked for in the user's own installation and
	 * found: game 1 in 1\DATA\GYM.PHD, game 2 in 2\DATA\BOAT.TR2, game 3 in
	 * 3\DATA\ANTARC.TR2.
	 *
	 * TWO SPELLINGS OF THE SAME WEAPON. The donor game's ITEM folder calls the
	 * pistols GUN_OPTION and the magnum-shaped gun MAGNUM_OPTION; a mod for
	 * THIS engine names its file after the TR5 slot it replaces, so the same
	 * two models reach us as PISTOLS_OPTION and REVOLVER_OPTION. Both rows
	 * point at the same object. Nothing is guessed here: a name that is in
	 * neither column gets no pose and says so.
	 *
	 * SHOTGUN_AMMO1 and SHOTGUN_AMMO2 are TR5's two shell kinds and the
	 * classic games have one box for both. */
	struct Row
	{
		const char* name;
		int id[3];		// game 1, 2, 3; 0 = that game has not got it
	};

	const Row kTable[] =
	{
		// the donor games' own names
		{ "GUN_OPTION",              {  99, 157, 185 } },
		{ "SHOTGUN_OPTION",          { 100, 158, 186 } },
		{ "MAGNUM_OPTION",           { 101, 159, 187 } },
		{ "UZI_OPTION",              { 102, 160, 188 } },
		{ "MAG_AMMO_OPTION",         { 105, 166, 195 } },
		{ "SG_AMMO_OPTION",          { 104, 165, 194 } },
		{ "UZI_AMMO_OPTION",         { 106, 167, 196 } },
		// and this engine's, where its slot is spelled differently
		{ "PISTOLS_OPTION",          {  99, 157, 185 } },
		{ "PISTOLS_AMMO_OPTION",     { 103, 164, 193 } },
		{ "REVOLVER_OPTION",         { 101, 159, 187 } },
		{ "REVOLVER_AMMO_OPTION",    { 105, 166, 195 } },
		{ "SHOTGUN_AMMO1_OPTION",    { 104, 165, 194 } },
		{ "SHOTGUN_AMMO2_OPTION",    { 104, 165, 194 } }
	};

	/* ------------------------------------------------------- the cache --
	 *
	 * One parse per (game, id) for the life of the process. A refusal is
	 * cached too: a ring that asks in every level of a play session must not
	 * walk twenty level files twenty times over to be told the same no. */
	struct Cached
	{
		bool ok;
		HDClassicObject object;
		std::string why;
	};

	std::map<int, Cached> g_cache;
	std::string g_error;

	std::string join(const std::string& a, const char* b)
	{
		std::string out = a;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		out += '\\';
		out += b;
		return out;
	}

	// Sine and cosine of a classic angle: 1024 steps to the full turn.
	float turn_sin(int a) { return std::sin((float)a * 6.283185307f / 1024.0f); }
	float turn_cos(int a) { return std::cos((float)a * 6.283185307f / 1024.0f); }

	/* THE ROTATION OF ONE MESH, in the classic engines' own convention.
	 *
	 * phd_RotYXZ turns about y, then x, then z, and the classic matrix takes a
	 * point as a COLUMN - so the composed matrix is Ry * Rx * Rz and a vertex
	 * is M * v. It is returned that way here and transposed once at the end of
	 * HDClassicNodeMatrix, where this module's row-vector convention starts.
	 *
	 * MEASURED, NOT ASSUMED. A frame carries the bounding box of the pose it
	 * describes, so the walk has an answer to check itself against. Over
	 * eleven weapon objects of all three games this order, this side and this
	 * sign reproduce those boxes to 2.1 units at worst; the next best of the
	 * ninety-six combinations of order, side, sign and vector convention
	 * misses by 40.8, and the worst by 184.7. */
	void mesh_rotation(const short* rot, float m[3][3])
	{
		const float cy = turn_cos(rot[1]), sy = turn_sin(rot[1]);
		const float cx = turn_cos(rot[0]), sx = turn_sin(rot[0]);
		const float cz = turn_cos(rot[2]), sz = turn_sin(rot[2]);
		const float ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
		const float rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
		const float rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
		float yx[3][3];

		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
			{
				yx[r][c] = 0.0f;

				for (int k = 0; k < 3; k++)
					yx[r][c] += ry[r][k] * rx[k][c];
			}

		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
			{
				m[r][c] = 0.0f;

				for (int k = 0; k < 3; k++)
					m[r][c] += yx[r][k] * rz[k][c];
			}
	}

	/* THE MESH TREE WALK, both poses at once.
	 *
	 * The classic walk starts at mesh 0 with the frame's own offset and mesh
	 * 0's rotation; for every mesh after it the bone says pop (take the saved
	 * matrix back) and/or push (save this one) - both before the translation -
	 * then translates by the bone's offset and rotates by that mesh's frame
	 * angles.
	 *
	 * The BIND POSE the .TRM is stored in is the same walk with every rotation
	 * zero and no frame offset, which makes it a sum of the bone offsets along
	 * the node's chain. So node n's stored vertices sit around `pivot[n]`, and
	 * posing one means (v - pivot[n]) * rotation[n] + place[n]. */
	struct Walk
	{
		float pivot[HD_CLASSIC_NODES][3];	// the bind pose's place
		float rotation[HD_CLASSIC_NODES][3][3];
		float place[HD_CLASSIC_NODES][3];	// frame 0's place
	};

	void walk(const HDClassicObject* o, Walk* w)
	{
		float stackPivot[HD_CLASSIC_NODES][3];
		float stackRot[HD_CLASSIC_NODES][3][3];
		float stackPlace[HD_CLASSIC_NODES][3];
		int depth = 0;
		float pivot[3] = { 0.0f, 0.0f, 0.0f };
		float rotation[3][3];
		float place[3] =
		{
			(float)o->offset[0], (float)o->offset[1], (float)o->offset[2]
		};

		mesh_rotation(o->rot[0], rotation);

		for (int i = 0; i < 3; i++)
		{
			w->pivot[0][i] = pivot[i];
			w->place[0][i] = place[i];

			for (int k = 0; k < 3; k++)
				w->rotation[0][i][k] = rotation[i][k];
		}

		for (int n = 1; n < o->meshes; n++)
		{
			const HDClassicBone& bone = o->bone[n];

			if ((bone.flags & 1) && depth > 0)
			{
				depth--;

				for (int i = 0; i < 3; i++)
				{
					pivot[i] = stackPivot[depth][i];
					place[i] = stackPlace[depth][i];

					for (int k = 0; k < 3; k++)
						rotation[i][k] = stackRot[depth][i][k];
				}
			}

			if ((bone.flags & 2) && depth < HD_CLASSIC_NODES - 1)
			{
				for (int i = 0; i < 3; i++)
				{
					stackPivot[depth][i] = pivot[i];
					stackPlace[depth][i] = place[i];

					for (int k = 0; k < 3; k++)
						stackRot[depth][i][k] = rotation[i][k];
				}

				depth++;
			}

			const float step[3] = { (float)bone.x, (float)bone.y, (float)bone.z };

			for (int i = 0; i < 3; i++)
			{
				pivot[i] += step[i];
				place[i] += rotation[i][0] * step[0] + rotation[i][1] * step[1] +
					rotation[i][2] * step[2];
			}

			float turn[3][3], next[3][3];

			mesh_rotation(o->rot[n], turn);

			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 3; c++)
				{
					next[r][c] = 0.0f;

					for (int k = 0; k < 3; k++)
						next[r][c] += rotation[r][k] * turn[k][c];
				}

			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 3; c++)
					rotation[r][c] = next[r][c];

			for (int i = 0; i < 3; i++)
			{
				w->pivot[n][i] = pivot[i];
				w->place[n][i] = place[i];

				for (int k = 0; k < 3; k++)
					w->rotation[n][i][k] = rotation[i][k];
			}
		}
	}
}

int HDClassicObjectId(const char* modelName, int game)
{
	if (!modelName || !*modelName || game < 1 || game > 3)
		return 0;

	for (const Row& row : kTable)
		if (_stricmp(row.name, modelName) == 0)
			return row.id[game - 1];

	return 0;
}

bool HDClassicRead(const char* bundleRoot, int game, int id,
	HDClassicObject* out, const char** why)
{
	static const char* nothing = "";

	if (why)
		*why = nothing;

	if (!out || game < 1 || game > 3 || id <= 0)
	{
		g_error = "no game or no object id";

		if (why)
			*why = g_error.c_str();

		return false;
	}

	const int key = game * 100000 + id;
	const std::map<int, Cached>::const_iterator seen = g_cache.find(key);

	if (seen != g_cache.end())
	{
		if (seen->second.ok)
			*out = seen->second.object;
		else if (why)
			*why = seen->second.why.c_str();

		return seen->second.ok;
	}

	Cached fresh;

	fresh.ok = false;

	if (!bundleRoot || !*bundleRoot)
		fresh.why = "no Tomb Raider I-III Remastered installation";
	else
	{
		char folder[8];

		std::snprintf(folder, sizeof(folder), "%d", game);

		const std::string data = join(join(bundleRoot, folder), "DATA");
		// TR1 ships .PHD and the other two .TR2 - the extension is the game's.
		const char* ext = game == 1 ? ".PHD" : ".TR2";
		std::error_code ec;
		fs::directory_iterator it(fs::u8path(data), ec), end;
		std::vector<std::string> leaves;

		for (; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec) || ec)
			{
				ec.clear();
				continue;
			}

			const std::string leaf = it->path().filename().u8string();
			const size_t tail = std::strlen(ext);

			if (leaf.size() > tail && _stricmp(leaf.c_str() + leaf.size() - tail, ext) == 0)
				leaves.push_back(leaf);
		}

		std::sort(leaves.begin(), leaves.end());

		/* THE FIRST LEVEL IN NAME ORDER THAT HOLDS IT, and no table of level
		 * names anywhere: a bundle with a level pack in it, or a game whose
		 * first level has no weapons in it, must still answer.
		 *
		 * MEASURED, because "any level will do" is a claim and not a fact
		 * (#155): over the eight weapon and ammunition objects of
		 * game 1 and of game 2 every level file that holds the object holds
		 * exactly the SAME mesh count, bones and frame 0 - 16 levels and 17
		 * levels respectively. Game 3 agrees on 17 of its 20, and the three
		 * that differ are TITLE.TR2 and AREA51.TR2 carrying a stub or a
		 * left-over of the game before; name order lands on ANTARC.TR2, which
		 * agrees with every other real level of that game. */
		for (const std::string& leaf : leaves)
			if (read_level(join(data, leaf.c_str()), id, &fresh.object))
			{
				fresh.ok = true;
				break;
			}

		if (!fresh.ok)
			fresh.why = leaves.empty()
				? "no level file in that game's DATA folder"
				: "in no level file of that game";
	}

	g_cache[key] = fresh;

	if (fresh.ok)
		*out = fresh.object;
	else if (why)
		*why = g_cache[key].why.c_str();

	return fresh.ok;
}

bool HDClassicCached(int game, int id, HDClassicObject* out)
{
	const std::map<int, Cached>::const_iterator seen =
		g_cache.find(game * 100000 + id);

	if (seen == g_cache.end() || !seen->second.ok)
		return false;

	if (out)
		*out = seen->second.object;

	return true;
}

bool HDClassicBindPivot(const HDClassicObject* object, int node, float* xyz)
{
	if (!object || !xyz || node < 0 || node >= object->meshes)
		return false;

	Walk w;

	walk(object, &w);

	for (int i = 0; i < 3; i++)
		xyz[i] = w.pivot[node][i];

	return true;
}

bool HDClassicNodeMatrix(const HDClassicObject* object, int node, float* out16)
{
	if (!object || !out16 || node < 0 || node >= object->meshes)
		return false;

	Walk w;

	walk(object, &w);

	/* THE TRANSPOSE IS THE CONVENTION CHANGE. The walk is done the classic
	 * way, v' = M * v with v a column; every matrix this engine hands about is
	 * v' = v * M with v a row. The rotation's transpose is that same rotation
	 * written the other way round, and the translation moves into row 3. */
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			out16[r * 4 + c] = w.rotation[node][c][r];

		out16[r * 4 + 3] = 0.0f;
	}

	/* The model's vertices are stored around the BIND pivot, so that point
	 * goes to the origin before the turn and the posed place puts it back. */
	for (int c = 0; c < 3; c++)
	{
		out16[3 * 4 + c] = w.place[node][c];

		for (int k = 0; k < 3; k++)
			out16[3 * 4 + c] -= w.pivot[node][k] * out16[k * 4 + c];
	}

	out16[3 * 4 + 3] = 1.0f;
	return true;
}

void HDClassicForget()
{
	g_cache.clear();
}

/*
 * Modified 2026 by ChrisChanSan.
 *
 * The .TRM layout, the meaning of the word that is zero in every model but a
 * head, and the trick that finds a head's geometry behind its facial rig are
 * TRForge's measurements (MIT, trf_remaster\ItemModels.h / .cpp). That file
 * could not be vendored, because it includes TRForge's own level library and
 * would drag it in - see vendor\trforge\VERSION.txt - so the reader below is
 * written against the description instead.
 *
 * Two passages are the exception: the facial-rig rescan further down and the
 * little byte reader beside it (Reader, have / u32 / f32) follow TRForge's
 * own code closely - the same loop, the same bounds, the same order of the
 * arithmetic, with the names and the brace style changed. That is lawful,
 * TRForge being MIT, and its notice travels with this tree in
 * vendor\trforge\LICENSE.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_trm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
	const size_t TrmPartBytes = 44;
	const size_t TrmVertexBytes = 24;

	/* The second vertex layout. Thirty bytes instead of twenty-four, and the
	 * difference is the two texture coordinates: they leave the gaps in the
	 * bone and weight block and stand at the end as floats, which closes the
	 * block up by three bytes each and adds eight.
	 *
	 *     +0   float x, y, z            as in the 24 byte layout
	 *     +12  u8 nx, ny, nz            ... and biased the same way
	 *     +15  u8 layer                 ... and counted from one
	 *     +16  u8 bone0, bone1, bone2
	 *     +19  u8 weight0, weight1, weight2
	 *     +22  float u
	 *     +26  float v
	 *
	 * Measured over all 2,923 .TRM files of both remasters (six ITEM folders:
	 * 882 + 566 + 0 in IV-VI, 379 + 476 + 620 in I-III). Exactly thirty of
	 * them are laid out this way and every one is an OUTFIT_TRX_*_HD.TRM in
	 * the I-III bundle's 1\ITEM; nothing else in either bundle, in any game
	 * folder, at any name. The proof of the layout is arithmetic and then
	 * byte comparison: the index block, aligned, plus vertices * 30 ends at
	 * the last byte of the file in all thirty, and the leading vertices of
	 * each are byte-identical with their 24 byte twin's in position, normal,
	 * layer, bone and weight - only the two coordinates have moved.
	 * OUTFIT_TRX_WETSUIT_1_HD: u of vertex 0 reads 0.33084 where the twin
	 * carries 84/255 = 0.32941.
	 *
	 * The engine's own vertex holds u and v as bytes (hd_model.cpp, the
	 * UBYTE4 lanes of the declaration), so reading one of these files means
	 * rounding its coordinates onto the 1/255 grid. That is a real loss and
	 * it is written down rather than hidden: measured over
	 * OUTFIT_TRX_WETSUIT_1_HD, 14,208 of its 14,262 coordinates lie off that
	 * grid - and off every other grid a texture atlas might use, 1/256 up to
	 * 1/4096, all of them - so the file is genuinely continuous and the
	 * rounding moves nearly every one by up to half a step. Against its own
	 * twin's vertices the two disagree by as much as half a step in both
	 * directions, spread, not by a constant half-texel offset.
	 *
	 * So a file of this layout is read - a mod that ships one and no 24 byte
	 * twin is better drawn with rounded coordinates than not drawn at all -
	 * but the wardrobe wears the 24 byte twin wherever there is one, and says
	 * in the log why (hd_wardrobe.cpp). */
	const size_t TrmVertexBytesWide = 30;

	// A wide layout's texture coordinate on the byte grid the engine's vertex
	// holds. Rounded to nearest and clamped - see the note above.
	unsigned char to_byte(float value)
	{
		const float scaled = value * 255.0f + 0.5f;

		if (!(scaled > 0.0f))
			return 0;

		if (scaled >= 255.0f)
			return 255;

		return (unsigned char)scaled;
	}

	struct Reader
	{
		const unsigned char* data;
		size_t size;
		size_t at;

		bool have(size_t bytes) const { return at + bytes <= size; }
		unsigned int u32()
		{
			unsigned int v = 0;
			std::memcpy(&v, data + at, 4);
			at += 4;
			return v;
		}
		float f32()
		{
			float v = 0;
			std::memcpy(&v, data + at, 4);
			at += 4;
			return v;
		}
		void align4() { at = (at + 3) & ~(size_t)3; }
	};

	/* The reading this module has done - see HDTrmCounters in the header
	 * for what each one answers. Doubles rather than counters of a fixed
	 * width because `bytes` runs into the hundreds of millions over a
	 * session and the three are printed together. */
	double g_opens = 0, g_loads = 0, g_bytes = 0;

	bool read_whole(const char* path, std::vector<unsigned char>& out)
	{
		g_opens++;

		std::FILE* file = std::fopen(path, "rb");
		if (!file)
			return false;

		std::fseek(file, 0, SEEK_END);
		const long length = std::ftell(file);
		if (length < 0) { std::fclose(file); return false; }
		std::fseek(file, 0, SEEK_SET);

		out.resize((size_t)length);
		const size_t got = length ? std::fread(&out[0], 1, (size_t)length, file) : 0;
		std::fclose(file);
		g_bytes += (double)got;
		return got == (size_t)length;
	}

	/* The one reader. HDTrmProbe wants the counts and nothing else - it runs
	 * over every slot of every level in the diagnostic tools and over all of
	 * them again in the engine's -log scan - while HDTrmLoad wants the
	 * geometry as well. Two readers would be two chances to disagree about
	 * where a head's geometry begins, so there is one, and `parts` decides
	 * how much of it is kept. */
	bool parse(const char* path, HDTrmInfo* info, HDTrmModel* model,
		const char** why)
	{
		static const char* nothing = "";
		const char* dummy = nothing;

		if (!why)
			why = &dummy;
		*why = nothing;

		if (info)
			std::memset(info, 0, sizeof(*info));
		if (model)
			std::memset(model, 0, sizeof(*model));

		if (!path || !*path)
		{
			*why = "no path";
			return false;
		}

		std::vector<unsigned char> bytes;
		if (!read_whole(path, bytes))
		{
			*why = "could not be read";
			return false;
		}

		const size_t size = bytes.size();
		if (size < 8 || std::memcmp(&bytes[0], "TRM", 3) != 0)
		{
			*why = "does not begin with TRM";
			return false;
		}

		Reader reader = { &bytes[0], size, 3 };
		const int version = bytes[3];
		reader.at = 4;

		if (version != 2)
		{
			*why = "is not version 2";
			return false;
		}

		const unsigned int parts = reader.u32();
		if (!reader.have((size_t)parts * TrmPartBytes))
		{
			*why = "is not long enough for its parts";
			return false;
		}

		std::vector<HDTrmPart> part_list;
		if (model && parts)
			part_list.resize(parts);

		// The highest index any pass reaches. The passes tile [0, indexCount),
		// so this IS the index count - which is what finds a head's geometry
		// below.
		unsigned int implied_indices = 0;
		for (unsigned int i = 0; i < parts; i++)
		{
			HDTrmPart here;

			here.material = reader.u32();
			for (int k = 0; k < 4; k++)
				here.param[k] = reader.f32();
			here.start = reader.u32();
			here.opaque = reader.u32();
			here.opaqueEnd = reader.u32();
			here.alpha = reader.u32();
			here.alphaEnd = reader.u32();
			here.additive = reader.u32();

			if (here.start + here.opaque > implied_indices)
				implied_indices = here.start + here.opaque;
			if (here.opaqueEnd + here.alpha > implied_indices)
				implied_indices = here.opaqueEnd + here.alpha;
			if (here.alphaEnd + here.additive > implied_indices)
				implied_indices = here.alphaEnd + here.additive;

			if (model && parts)
				part_list[i] = here;
		}

		if (!reader.have(4))
		{
			*why = "ends before its texture list";
			return false;
		}
		const unsigned int textures = reader.u32();
		if (!reader.have((size_t)textures * 2))
		{
			*why = "texture list runs past the end";
			return false;
		}

		std::vector<unsigned short> texture_list;
		if (model && textures)
		{
			texture_list.resize(textures);
			std::memcpy(&texture_list[0], reader.data + reader.at,
				(size_t)textures * 2);
		}

		reader.at += (size_t)textures * 2;
		reader.align4();

		if (!reader.have(4))
		{
			*why = "ends before its geometry";
			return false;
		}

		bool facial_rig = false;
		/* Where the rig's joint matrices begin and how many there are: the
		 * word that is zero everywhere else is the joint count, and the
		 * matrices follow it. */
		const size_t rig_at = reader.at + 4;
		unsigned int rig_joints = 0;
		std::memcpy(&rig_joints, reader.data + reader.at, 4);
		if (reader.u32() != 0)
		{
			// A head. In place of the zero it carries a joint count and that
			// many 3x4 matrices, then a joint order and several hundred
			// kilobytes of facial animation. None of that has to be understood
			// to get past it: look for the index count the part table already
			// implies, followed by a vertex count whose block ends at the last
			// byte of the file.
			bool found = false;
			if (implied_indices >= 3)
			{
				for (size_t at = reader.at - 4; at + 8 <= size; at += 4)
				{
					unsigned int indices = 0, corners = 0;
					std::memcpy(&indices, &bytes[at], 4);
					if (indices != implied_indices)
						continue;
					std::memcpy(&corners, &bytes[at + 4], 4);
					if (corners < 3 || corners > 1000000u)
						continue;

					size_t end = at + 8 + (size_t)indices * 2;
					end = (end + 3) & ~(size_t)3;
					end += (size_t)corners * TrmVertexBytes;
					if (end != size)
						continue;

					reader.at = at;
					facial_rig = true;
					found = true;
					break;
				}
			}
			if (!found)
			{
				*why = "carries a rig this reader could not get past";
				return false;
			}
		}

		const size_t geometry_at = reader.at;

		if (!reader.have(8))
		{
			*why = "ends before its counts";
			return false;
		}
		const unsigned int indices = reader.u32();
		const unsigned int vertices = reader.u32();

		if (!reader.have((size_t)indices * 2))
		{
			*why = "index list runs past the end";
			return false;
		}

		const size_t index_at = reader.at;

		reader.at += (size_t)indices * 2;
		reader.align4();

		/* Which of the two layouts. The file itself says so by its length:
		 * the vertex block runs to the last byte, so what is left over
		 * divided by the vertex count is the stride, and 24 and 30 are the
		 * only two the remasters use. Asked in that order because 24 is what
		 * all but thirty of the 2,923 files carry - and because a file that
		 * fits neither gets the message it always got. */
		const size_t rest = size - reader.at;
		size_t stride = 0;

		if (rest == (size_t)vertices * TrmVertexBytes)
			stride = TrmVertexBytes;
		else if (vertices && rest == (size_t)vertices * TrmVertexBytesWide)
			stride = TrmVertexBytesWide;

		if (!stride)
		{
			if (!reader.have((size_t)vertices * TrmVertexBytes))
				*why = "is not long enough for its vertices";
			else
				*why = "has bytes left over";

			return false;
		}

		const size_t vertex_at = reader.at;

		reader.at += (size_t)vertices * stride;

		if (info)
		{
			info->version = version;
			info->parts = (int)parts;
			info->textures = (int)textures;
			info->indices = (int)indices;
			info->vertices = (int)vertices;
			info->facialRig = facial_rig;
			info->vertexBytes = (int)stride;
		}

		if (!model)
			return true;

		/* The geometry. An index past the vertex count would be a read out of
		 * the vertex buffer on the card, which is a driver reset and not a
		 * wrong picture, so it is refused here rather than clamped. Over all
		 * 566 files of game 5 this never fires; it is the guard for the
		 * randomizer's own future and for a truncated download. */
		for (unsigned int i = 0; i < indices; i++)
		{
			unsigned short corner = 0;
			std::memcpy(&corner, &bytes[index_at + (size_t)i * 2], 2);

			if (corner >= vertices)
			{
				*why = "names a vertex it does not have";
				return false;
			}
		}

		model->version = version;
		model->parts = (int)parts;
		model->textures = (int)textures;
		model->indices = (int)indices;
		model->vertices = (int)vertices;
		model->facialRig = facial_rig;
		model->vertexBytes = (int)stride;

		/* The rig's joints. Each is an inverse bind matrix of twelve floats,
		 * rows (m0 m1 m2 | m3), (m4 m5 m6 | m7), (m8 m9 m10 | m11); the
		 * joint stands at -R^T t. Kept only when the whole list lies between
		 * the count and the geometry the rescan found, and never past 256
		 * joints, the most a byte-wide bone number can name. */
		if (facial_rig && rig_joints && rig_joints <= 256 &&
			rig_at + (size_t)rig_joints * 48 <= geometry_at)
		{
			model->rigJoints = (int)rig_joints;
			model->rigJoint = new float[(size_t)rig_joints * 3];

			for (unsigned int j = 0; j < rig_joints; j++)
			{
				float m[12];

				std::memcpy(m, &bytes[rig_at + (size_t)j * 48], sizeof(m));

				for (int c = 0; c < 3; c++)
					model->rigJoint[j * 3 + c] =
						-(m[0 + c] * m[3] + m[4 + c] * m[7] + m[8 + c] * m[11]);
			}
		}

		if (parts)
		{
			model->part = new HDTrmPart[parts];
			std::memcpy(model->part, &part_list[0], (size_t)parts * sizeof(HDTrmPart));
		}

		if (textures)
		{
			model->texture = new unsigned short[textures];
			std::memcpy(model->texture, &texture_list[0], (size_t)textures * 2);
		}

		if (indices)
		{
			model->index = new unsigned short[indices];
			std::memcpy(model->index, &bytes[index_at], (size_t)indices * 2);
		}

		if (vertices)
		{
			model->vertex = new HDTrmVertex[vertices];

			for (unsigned int i = 0; i < vertices; i++)
			{
				const unsigned char* raw = &bytes[vertex_at + (size_t)i * stride];
				HDTrmVertex& out = model->vertex[i];

				std::memcpy(&out.x, raw + 0, 4);
				std::memcpy(&out.y, raw + 4, 4);
				std::memcpy(&out.z, raw + 8, 4);

				out.nx = raw[12];
				out.ny = raw[13];
				out.nz = raw[14];
				out.layer = raw[15];
				out.bone[0] = raw[16];
				out.bone[1] = raw[17];
				out.bone[2] = raw[18];

				if (stride == TrmVertexBytes)
				{
					out.u = raw[19];
					out.weight[0] = raw[20];
					out.weight[1] = raw[21];
					out.weight[2] = raw[22];
					out.v = raw[23];
				}
				else
				{
					/* The wide layout. The weights close the two gaps the
					 * coordinates left and the coordinates stand at the end
					 * as floats; rounded onto the byte grid here, which is
					 * the loss the note at the top of this file measures.
					 * Clamped, because nothing in the format says a
					 * coordinate stays inside 0..1 and a wrap would paint the
					 * far edge of the atlas onto the model. */
					float u = 0.0f, v = 0.0f;

					out.weight[0] = raw[19];
					out.weight[1] = raw[20];
					out.weight[2] = raw[21];

					std::memcpy(&u, raw + 22, 4);
					std::memcpy(&v, raw + 26, 4);

					out.u = to_byte(u);
					out.v = to_byte(v);
				}
			}
		}

		return true;
	}
}

bool HDTrmExists(const char* path)
{
	if (!path || !*path)
		return false;

	g_opens++;

	std::FILE* file = std::fopen(path, "rb");
	if (!file)
		return false;
	std::fclose(file);
	return true;
}

bool HDTrmProbe(const char* path, HDTrmInfo* out, const char** why)
{
	return parse(path, out, 0, why);
}

bool HDTrmLoad(const char* path, HDTrmModel* out, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;

	if (!out)
	{
		*why = "no model to read into";
		return false;
	}

	HDTrmInfo info;

	g_loads++;

	if (!parse(path, &info, out, why))
	{
		HDTrmFree(out);
		return false;
	}

	/* DUMMY.TRM is a proper file with no parts in it, and the level maps name
	 * it on purpose for a slot that draws nothing. It reads fine and is
	 * refused here, so that a caller never has to ask "is this the empty
	 * one" - an empty model is not a model. */
	if (!out->parts || !out->indices || !out->vertices)
	{
		HDTrmFree(out);
		*why = "has no geometry - DUMMY, or a model that draws nothing";
		return false;
	}

	return true;
}

void HDTrmFree(HDTrmModel* model)
{
	if (!model)
		return;

	delete[] model->part;
	delete[] model->texture;
	delete[] model->index;
	delete[] model->vertex;
	delete[] model->rigJoint;

	std::memset(model, 0, sizeof(*model));
}

void HDTrmCounters(double* opens, double* loads, double* bytes)
{
	if (opens)
		*opens = g_opens;
	if (loads)
		*loads = g_loads;
	if (bytes)
		*bytes = g_bytes;
}

/* The mirrored weights, folded back. What the rule is and why it exists is
 * in hd_trm.h; this is the arithmetic.
 *
 * It works on the three bone and weight bytes alone and never on a position, so
 * nothing here can move a vertex that is not skinned across the two sides - and
 * a model with no crossed vertex and no odd sum is not written to at all. */
void HDTrmFoldMirrored(HDTrmModel* model, const HDTrmMirrorPair* pair, int pairs,
	int barPermille, HDTrmFoldCounts* counts)
{
	HDTrmFoldCounts local;

	std::memset(&local, 0, sizeof(local));

	if (!model || !model->vertex)
	{
		if (counts)
			*counts = local;
		return;
	}

	for (int i = 0; i < model->vertices; i++)
	{
		HDTrmVertex& v = model->vertex[i];
		int sum = (int)v.weight[0] + (int)v.weight[1] + (int)v.weight[2];

		/* Which side this vertex is on, in its own weights. `side` is 0 for
		 * the a-bones and 1 for the b-bones; a bone on neither side belongs
		 * to neither sum and takes no part in the decision. */
		int onSide[2] = { 0, 0 };

		for (int b = 0; b < 3 && pair && pairs > 0; b++)
		{
			if (!v.weight[b])
				continue;

			for (int p = 0; p < pairs; p++)
			{
				if (v.bone[b] == pair[p].a)
					onSide[0] += (int)v.weight[b];
				else if (v.bone[b] == pair[p].b)
					onSide[1] += (int)v.weight[b];
			}
		}

		if (onSide[0] && onSide[1])
		{
			const int lesser = onSide[0] < onSide[1] ? onSide[0] : onSide[1];
			const int share = sum > 0
				? (lesser * 1000 + sum / 2) / sum : 0;

			local.crossed++;

			if (share > local.worstPermille)
				local.worstPermille = share;

			/* An exact tie is broken by the model's own x axis, and that is
			 * not a second rule but the same one read off the geometry: side
			 * a is the +x side, and over every crossed vertex of all 94 hand
			 * files of the install and all seven files of three gun mods -
			 * 6,723 of them - the 6,719 that have a majority agree with the
			 * sign of x 6,719 times and disagree not once. The four that do
			 * not have one are HK USP vertices weighted exactly 128/128, and
			 * without this they would be the one piece of the sliver the
			 * fold left behind. */
			if (onSide[0] == onSide[1])
				local.tied++;

			if (share >= barPermille)
			{
				const int far = onSide[0] != onSide[1]
					? (onSide[0] < onSide[1] ? 0 : 1)
					: (v.x >= 0.0f ? 1 : 0);

				/* The far side's bones become their counterparts. A slot
				 * whose new bone another slot already names is merged into
				 * it and left at weight 0 - the shader adds the three
				 * anyway, so a zero slot draws nothing, and merging keeps
				 * the vertex at three bones without renumbering. */
				for (int b = 0; b < 3; b++)
				{
					if (!v.weight[b])
						continue;

					int onto = -1;

					for (int p = 0; p < pairs; p++)
					{
						if (far == 0 && v.bone[b] == pair[p].a)
							onto = pair[p].b;
						else if (far == 1 && v.bone[b] == pair[p].b)
							onto = pair[p].a;
					}

					if (onto < 0)
						continue;

					v.bone[b] = (unsigned char)onto;

					for (int other = 0; other < 3; other++)
						if (other != b && v.weight[other] &&
							v.bone[other] == v.bone[b])
						{
							const int merged = (int)v.weight[other] +
								(int)v.weight[b];

							v.weight[other] = (unsigned char)
								(merged > 255 ? 255 : merged);
							v.weight[b] = 0;
							break;
						}
				}

				local.folded++;

				if (share > local.foldedPermille)
					local.foldedPermille = share;
			}

			sum = (int)v.weight[0] + (int)v.weight[1] + (int)v.weight[2];
		}

		/* And the three sum to 255. The merge above can lose a unit to its
		 * own clamp, and a file may simply be written that way - the HK USP
		 * mod has 24 vertices summing to 254 or 256. The shader takes the
		 * weights as they stand, so a sum that is not 255 is a vertex drawn
		 * slightly off its own skin. */
		if (sum > 0 && sum != 255)
		{
			int most = 0;
			int total = 0;

			for (int b = 0; b < 3; b++)
			{
				const int scaled = ((int)v.weight[b] * 255 + sum / 2) / sum;

				v.weight[b] = (unsigned char)(scaled > 255 ? 255 : scaled);
				total += (int)v.weight[b];

				if (v.weight[b] > v.weight[most])
					most = b;
			}

			// The rounding's remainder goes on the heaviest bone, where it
			// is the smallest relative change this vertex can be given.
			const int slack = 255 - total + (int)v.weight[most];

			v.weight[most] = (unsigned char)(slack < 0 ? 0
				: slack > 255 ? 255 : slack);
			local.renormed++;
		}
	}

	if (counts)
		*counts = local;
}

int HDTrmPageOf(const HDTrmModel* model, const HDTrmVertex* vertex)
{
	if (!model || !vertex)
		return -1;

	const int layer = (int)vertex->layer;

	if (layer < 1 || layer > model->textures)
		return -1;

	return (int)model->texture[layer - 1];
}

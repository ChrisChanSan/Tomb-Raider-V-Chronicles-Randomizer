/*
 * Modified 2026 by ChrisChanSan.
 *
 * The .TRM layout, the meaning of the word that is zero in every model but a
 * head, and the trick that finds a head's geometry behind its facial rig are
 * TRForge's measurements (MIT, trf_remaster\ItemModels.h / .cpp). The reader
 * below is written against that description rather than copied, because
 * TRForge's own file includes its level library - see vendor\trforge\
 * VERSION.txt.
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

	bool read_whole(const char* path, std::vector<unsigned char>& out)
	{
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
		return got == (size_t)length;
	}

	/* THE ONE READER. HDTrmProbe wants the counts and nothing else - it runs
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
		if (reader.u32() != 0)
		{
			// A HEAD. In place of the zero it carries a joint count and that
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

		if (!reader.have((size_t)vertices * TrmVertexBytes))
		{
			// Some outfits are laid out with 30 byte vertices and are not
			// read; TRForge measured the same and refuses them too.
			*why = "is not long enough for its vertices";
			return false;
		}

		const size_t vertex_at = reader.at;

		reader.at += (size_t)vertices * TrmVertexBytes;

		if (reader.at != size)
		{
			*why = "has bytes left over";
			return false;
		}

		if (info)
		{
			info->version = version;
			info->parts = (int)parts;
			info->textures = (int)textures;
			info->indices = (int)indices;
			info->vertices = (int)vertices;
			info->facialRig = facial_rig;
		}

		if (!model)
			return true;

		/* THE GEOMETRY. An index past the vertex count would be a read out of
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
				const unsigned char* raw = &bytes[vertex_at + (size_t)i * TrmVertexBytes];
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
				out.u = raw[19];
				out.weight[0] = raw[20];
				out.weight[1] = raw[21];
				out.weight[2] = raw[22];
				out.v = raw[23];
			}
		}

		return true;
	}
}

bool HDTrmExists(const char* path)
{
	if (!path || !*path)
		return false;

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

	std::memset(model, 0, sizeof(*model));
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

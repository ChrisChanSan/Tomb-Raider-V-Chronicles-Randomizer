/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
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
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	if (out)
		std::memset(out, 0, sizeof(*out));

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

	// The highest index any pass reaches. The passes tile [0, indexCount), so
	// this IS the index count - which is what finds a head's geometry below.
	unsigned int implied_indices = 0;
	for (unsigned int i = 0; i < parts; i++)
	{
		reader.at += 4 + 16;					// material, param[4]
		const unsigned int start = reader.u32();
		const unsigned int opaque = reader.u32();
		const unsigned int opaque_end = reader.u32();
		const unsigned int alpha = reader.u32();
		const unsigned int alpha_end = reader.u32();
		const unsigned int additive = reader.u32();

		if (start + opaque > implied_indices) implied_indices = start + opaque;
		if (opaque_end + alpha > implied_indices) implied_indices = opaque_end + alpha;
		if (alpha_end + additive > implied_indices) implied_indices = alpha_end + additive;
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
		// A HEAD. In place of the zero it carries a joint count and that many
		// 3x4 matrices, then a joint order and several hundred kilobytes of
		// facial animation. None of that has to be understood to get past it:
		// look for the index count the part table already implies, followed by
		// a vertex count whose block ends at the last byte of the file.
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
	reader.at += (size_t)indices * 2;
	reader.align4();

	if (!reader.have((size_t)vertices * TrmVertexBytes))
	{
		// Some outfits are laid out with 30 byte vertices and are not read;
		// TRForge measured the same and refuses them too.
		*why = "is not long enough for its vertices";
		return false;
	}
	reader.at += (size_t)vertices * TrmVertexBytes;

	if (reader.at != size)
	{
		*why = "has bytes left over";
		return false;
	}

	if (out)
	{
		out->version = version;
		out->parts = (int)parts;
		out->textures = (int)textures;
		out->indices = (int)indices;
		out->vertices = (int)vertices;
		out->facialRig = facial_rig;
	}
	return true;
}

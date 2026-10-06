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
#include "hd_scan.h"
#include "hd_bind.h"
#include "hd_trm.h"
#include "../bc7.h"

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
	struct MissingEntry { std::string slot, model; };

	std::vector<MissingEntry> missing;
	std::vector<int> scan_pages;
	std::string scan_error;

	bool read_whole(const std::string& path, std::vector<unsigned char>& out)
	{
		std::FILE* file = std::fopen(path.c_str(), "rb");
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

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;
		std::string out = a;
		while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
			out.pop_back();
		out += '\\';
		out += b;
		return out;
	}

	/* The page of a triangle.
	 *
	 * A .TRG vertex names its texture as a layer counted from one into the
	 * file's own texture list - reading it from zero gave 0 of 273 faces the
	 * page the .TEX names and reading it from one gave 249 (TRForge's
	 * measurement, Trg.h). Counted again here: every vertex of the fifteen
	 * TR5 files has a layer from 1 to its file's own texture count, none
	 * outside (TrgProbe -uvbytes). Layer 0 means the vertex names no page at
	 * all. */
	int page_of(const trf::remaster::Trg& trg, unsigned int index)
	{
		if (index >= trg.vertices.size())
			return -1;
		const unsigned char layer = trg.vertices[index].layer;
		if (!layer || (size_t)layer > trg.textures.size())
			return -1;
		return (int)trg.textures[layer - 1];
	}
}

const char* HDScanPagePath(const char* installRoot, int page, char* buffer, int size)
{
	if (!buffer || size <= 0)
		return "";

	const std::string folder = join(installRoot ? installRoot : "", "TEX");
	std::string path = join(folder, std::to_string(page) + ".DDS");

	/* The plain name is the only one looked for. The two bundles ship 35
	 * pages a second and third time as <n>_1024.DDS and <n>_2048.DDS, all of
	 * them in the I-III bundle's 1\TEX and none in 2, 3, 4, 5 or 6 (TrgProbe
	 * -ddssizes counts them per folder), and every one of the 35 has its
	 * plain <n>.DDS beside it - so a page this function names exists under
	 * the plain name or under none. */
	std::strncpy(buffer, path.c_str(), (size_t)size - 1);
	buffer[size - 1] = 0;
	return buffer;
}

double HDScanDecodePages(const char* installRoot, const int* pages, int count,
	int threads, double* texels, int* mips, int* failed)
{
	std::atomic<long long> total_texels(0);
	std::atomic<int> total_mips(0);
	std::atomic<int> total_failed(0);
	std::atomic<int> next(0);

	if (texels) *texels = 0;
	if (mips) *mips = 0;
	if (failed) *failed = 0;

	if (!pages || count <= 0)
		return 0.0;

	if (threads < 1)
		threads = 1;
	if (threads > count)
		threads = count;

	const auto work = [&]()
	{
		std::vector<unsigned char> file;
		char path[300];

		for (;;)
		{
			const int at = next.fetch_add(1);
			if (at >= count)
				break;

			HDScanPagePath(installRoot, pages[at], path, sizeof(path));
			if (!read_whole(path, file) || file.empty())
			{
				total_failed.fetch_add(1);
				continue;
			}

			const long levels = BC7DDSMipCount(&file[0], (unsigned long)file.size());
			bool any = false;

			for (long mip = 0; mip < levels; mip++)
			{
				long w = 0, h = 0;
				const char* why = "";
				unsigned long* pixels = BC7DecodeDDSMip(&file[0], (unsigned long)file.size(),
					4096, mip, &w, &h, &why);

				if (!pixels)
					break;		// the chain stops where the blocks stop

				any = true;
				total_texels.fetch_add((long long)w * (long long)h);
				total_mips.fetch_add(1);
				free(pixels);
			}

			if (!any)
				total_failed.fetch_add(1);
		}
	};

	const auto started = std::chrono::steady_clock::now();

	if (threads == 1)
		work();
	else
	{
		std::vector<std::thread> pool;
		pool.reserve((size_t)threads);
		for (int i = 0; i < threads; i++)
			pool.push_back(std::thread(work));
		for (std::thread& t : pool)
			t.join();
	}

	const auto ended = std::chrono::steady_clock::now();

	if (texels) *texels = (double)total_texels.load();
	if (mips) *mips = total_mips.load();
	if (failed) *failed = total_failed.load();

	return std::chrono::duration<double, std::milli>(ended - started).count();
}

bool HDScanLevel(const char* installRoot, const char* stem, int numRooms,
	const unsigned char* slotLoaded, int slotCount,
	bool measureBc7, int threads, HDScanStats* out, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	missing.clear();
	scan_pages.clear();
	scan_error.clear();

	if (out)
		std::memset(out, 0, sizeof(*out));

	if (!installRoot || !*installRoot || !stem || !*stem || !out)
	{
		*why = "no install or no level name";
		return false;
	}

	const std::string data = join(installRoot, "DATA");
	const std::string trg_path = join(data, std::string(stem) + ".TRG");
	const std::string map_path = join(data, std::string(stem) + ".MAP");
	const std::string item = join(installRoot, "ITEM");

	trf::remaster::Trg trg;
	std::string error;
	if (!trf::remaster::read_trg(trg_path, trg, error))
	{
		scan_error = error;
		*why = scan_error.c_str();
		return false;
	}

	out->trgVersion = trg.version;
	out->groups = (int)trg.groups.size();
	out->rooms = numRooms;
	out->skyGroups = out->groups > numRooms ? out->groups - numRooms : 0;
	out->pages = (int)trg.textures.size();
	out->vertices = (int)trg.vertices.size();
	out->indices = (int)trg.indices.size();
	out->lightCubes = (int)trg.light_cubes.size();
	out->fogBoxes = (int)trg.fog_boxes.size();
	out->boxVolumes = (int)trg.box_volumes.size();

	for (const trf::remaster::TrgGroup& group : trg.groups)
		out->pointLights += (int)group.volumes.size();

	/* The ranges. One draw call per (group, pass, page): the index stream is
	 * not sorted by page, so the triangles are bucketed by that key and the
	 * number of buckets that hold anything is the number of draws the level
	 * will issue. pageChanges is what the same walk costs without the sort -
	 * the number the sort is there to bring down.
	 *
	 * Only groups below numRooms are counted: group i is room i, and anything
	 * beyond the room count is the sky dome, which TR5 draws procedurally and
	 * the HD path leaves alone. */
	std::vector<unsigned long long> keys;
	keys.reserve(trg.indices.size() / 3);

	const int room_groups = out->groups < numRooms ? out->groups : numRooms;

	for (int g = 0; g < room_groups; g++)
	{
		const trf::remaster::TrgGroup& group = trg.groups[(size_t)g];
		size_t group_indices = 0;

		for (const trf::remaster::TrgPart& part : group.parts)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart::Pass& p = part.pass[pass];
				group_indices += p.count;

				int last_page = -2;
				for (unsigned int at = 0; at + 2 < p.count; at += 3)
				{
					const unsigned int i0 = p.start + at;
					const int a = page_of(trg, trg.indices[i0]);
					const int b = page_of(trg, trg.indices[i0 + 1]);
					const int c = page_of(trg, trg.indices[i0 + 2]);

					if (a != b || b != c)
						out->mixedPageTriangles++;

					/* Only a change is remembered. Two triangles in a row on
					 * the same page carry the same key, so collapsing them
					 * here leaves the set of distinct keys - and therefore
					 * the range count - exactly as it was, at a thirtieth of
					 * the memory: 32k keys instead of 1.8M for ANDREA1. */
					if (a != last_page)
					{
						out->pageChanges++;
						last_page = a;

						keys.push_back(((unsigned long long)(unsigned)g << 40) |
							((unsigned long long)(unsigned)pass << 32) |
							(unsigned long long)(unsigned)(a + 1));
					}
				}
			}

		if (!group_indices)
			out->emptyGroups++;
	}

	if (!keys.empty())
	{
		std::vector<unsigned long long> sorted = keys;
		std::sort(sorted.begin(), sorted.end());
		out->ranges = 1;
		for (size_t i = 1; i < sorted.size(); i++)
			if (sorted[i] != sorted[i - 1])
				out->ranges++;
	}

	scan_pages.reserve(trg.textures.size());
	for (unsigned short page : trg.textures)
		scan_pages.push_back((int)page);

	/* The models. The level's own .MAP first, then the hand table for the
	 * slots beyond the original 460, then the slot's own name - see
	 * hd_bind.h. Existence only: nothing of a model is read here beyond its
	 * header, and nothing at all is built. */
	HDMapLoad(map_path.c_str());
	out->mapEntries = HDMapCount();

	if (slotLoaded && slotCount > 0)
	{
		for (int slot = 0; slot < slotCount; slot++)
		{
			if (!slotLoaded[slot])
				continue;

			out->slotsLoaded++;

			HDBindKind kind = HD_BIND_NONE;
			bool from_map = false;
			const char* model = HDBindSlot(installRoot, slot, &kind, &from_map, 0);

			if (kind == HD_BIND_LARA)
			{
				out->trmLara++;
				continue;
			}
			if (kind == HD_BIND_DUMMY)
			{
				out->trmDummy++;
				continue;
			}
			if (kind != HD_BIND_MODEL || !model)
				continue;

			const std::string path = join(item, std::string(model) + ".TRM");

			if (HDTrmExists(path.c_str()))
			{
				out->trmBound++;

				const std::string head = join(item, std::string(model) + "_HEAD.TRM");
				if (HDTrmExists(head.c_str()))
					out->headsBound++;
			}
			else
			{
				out->trmMissing++;

				MissingEntry entry;
				entry.slot = HDSlotName(slot) ? HDSlotName(slot) : "?";
				entry.model = model;
				missing.push_back(entry);
			}
		}
	}

	HDMapFree();

	if (measureBc7 && !scan_pages.empty())
	{
		out->bc7Ms = HDScanDecodePages(installRoot, &scan_pages[0], (int)scan_pages.size(),
			threads, &out->bc7Texels, &out->bc7Mips, 0);
		out->bc7Pages = (int)scan_pages.size();
	}

	return true;
}

int HDScanMissingCount()
{
	return (int)missing.size();
}

const char* HDScanMissingSlot(int at)
{
	if (at < 0 || at >= (int)missing.size())
		return "";
	return missing[(size_t)at].slot.c_str();
}

const char* HDScanMissingModel(int at)
{
	if (at < 0 || at >= (int)missing.size())
		return "";
	return missing[(size_t)at].model.c_str();
}

int HDScanPageCount()
{
	return (int)scan_pages.size();
}

int HDScanPage(int at)
{
	if (at < 0 || at >= (int)scan_pages.size())
		return -1;
	return scan_pages[(size_t)at];
}

/* The .TEX table of one level - see hd_scan.h for the format and for what
 * it is good for.
 *
 * Refusing rather than half believing: a table whose page indices point past
 * its own page list is not this level's table, and a file too short to hold
 * its own header is not one either. Both come back as 0, which the caller
 * reads as "no HD frames for this level" and draws the classic tiles.
 *
 * The tile entries are converted here, so the caller never sees the
 * indirection: what it gets is the .DDS number itself, or 0xFFFF. */
int HDScanLevelTextures(const char* installRoot, const char* stem,
	unsigned short* out, int maxTiles)
{
	if (!out || maxTiles <= 0 || !stem || !*stem)
		return 0;

	const std::string path = join(join(installRoot ? installRoot : "", "DATA"),
		std::string(stem) + ".TEX");
	std::vector<unsigned char> bytes;

	if (!read_whole(path, bytes) || bytes.size() < 2)
		return 0;

	const unsigned int pages = (unsigned int)bytes[0] |
		((unsigned int)bytes[1] << 8);
	const size_t head = 2 + (size_t)pages * 2;

	if (!pages || bytes.size() < head)
		return 0;

	const size_t tiles = (bytes.size() - head) / 2;
	int written = 0;

	for (size_t i = 0; i < tiles && written < maxTiles; i++)
	{
		const size_t at = head + i * 2;
		const unsigned int slot = (unsigned int)bytes[at] |
			((unsigned int)bytes[at + 1] << 8);

		if (slot >= pages)
		{
			// 0xFFFF is the file's own "no HD art"; anything else in that
			// range would be a table we have misread, so it is refused.
			if (slot != 0xFFFFu)
				return 0;

			out[written++] = 0xFFFFu;
			continue;
		}

		out[written++] = (unsigned short)((unsigned int)bytes[2 + slot * 2] |
			((unsigned int)bytes[3 + slot * 2] << 8));
	}

	return written;
}

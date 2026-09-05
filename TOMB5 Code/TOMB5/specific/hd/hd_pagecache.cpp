/*
 * Modified 2026 by ChrisChanSan.
 *
 * The search order for a missing page and the "no page is ever black, it is
 * WHITE.DDS" rule are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3, host\render\hd_pages.cpp, upload2 / fill_missing_tiles). The upload
 * itself is new: TROne hands the driver BC7 blocks in one texture array,
 * Direct3D 9 has neither. See THIRD-PARTY.md.
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

#include "hd_pagecache.h"
#include "hd_scan.h"
#include "../bc7.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	/* ONE DECODED PAGE, mip chain and all. The pixels are malloc'd by
	 * BC7DecodeDDSMip and freed here; nothing of a page survives its
	 * upload. */
	struct DecodedMip
	{
		unsigned long* pixels = 0;
		long w = 0, h = 0;
	};

	struct DecodedPage
	{
		std::vector<DecodedMip> mips;
		int page = 0;
		int slot = 0;
		int origin = 0;			// 5, 4, 6, or 0 for WHITE.DDS
		bool white = false;
		bool ok = false;

		void free_all()
		{
			for (DecodedMip& mip : mips)
				if (mip.pixels)
					std::free(mip.pixels);
			mips.clear();
		}
	};

	IDirect3DDevice9* g_device = 0;
	std::vector<IDirect3DTexture9*> g_textures;
	std::vector<int> g_page_of_slot;
	std::unordered_map<int, int> g_slot_of_page;
	std::vector<std::string> g_notes;

	/* The one reused staging texture of the DEFAULT path. It is recreated
	 * only when a page has a shape the last one did not - which over every
	 * TR5 level means exactly once, because every remaster page is 512x512
	 * with six mips. */
	IDirect3DTexture9* g_staging = 0;
	long g_staging_w = 0, g_staging_h = 0, g_staging_levels = 0;

	double now_ms()
	{
		return std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
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

	// "<bundle>\5" -> "<bundle>". Empty when there is no separator at all.
	std::string parent_of(const std::string& path)
	{
		std::string out = path;
		while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
			out.pop_back();

		const size_t cut = out.find_last_of("\\/");
		if (cut == std::string::npos)
			return std::string();
		return out.substr(0, cut);
	}

	// The length of a file, or 0 when it is not there. Doubles as the
	// existence test, and the length is worth having: four times it is a
	// good estimate of the pixels a BC7 page decodes to (BC7 is one byte a
	// texel, A8R8G8B8 is four), and the batching below needs that estimate
	// before it decodes anything.
	long file_size(const std::string& path)
	{
		std::FILE* file = std::fopen(path.c_str(), "rb");

		if (!file)
			return 0;

		std::fseek(file, 0, SEEK_END);
		const long length = std::ftell(file);
		std::fclose(file);
		return length > 0 ? length : 0;
	}

	bool file_there(const std::string& path)
	{
		return file_size(path) > 0;
	}

	bool read_whole(const std::string& path, std::vector<unsigned char>& out)
	{
		std::FILE* file = std::fopen(path.c_str(), "rb");
		if (!file)
			return false;

		std::fseek(file, 0, SEEK_END);
		const long length = std::ftell(file);
		if (length <= 0) { std::fclose(file); return false; }
		std::fseek(file, 0, SEEK_SET);

		out.resize((size_t)length);
		const size_t got = std::fread(&out[0], 1, (size_t)length, file);
		std::fclose(file);
		return got == (size_t)length;
	}

	/* WHERE A PAGE LIVES. The level's own game folder first, then the two
	 * siblings, then the remaster's own white tile.
	 *
	 * The siblings are not a nicety: RICH3 names three pages that ship only
	 * in the bundle's 4\TEX (measured in stage S0), because the remaster
	 * keeps one texture pool per game and the level's art was moved between
	 * them. Without the fallback those three pages are black holes in the
	 * middle of the level. Six item models are in the same position, which is
	 * stage S3's problem and the same rule.
	 *
	 * WHITE.DDS is slot 0 of the remaster's own page table - a plain white
	 * tile. A missing page painted white shows the room's baked light and
	 * looks like untextured geometry; painted black it looks like a hole and
	 * gets reported as a rendering bug. That choice is TROne's and it is the
	 * right one. */
	std::string resolve_page(const std::string& root5, const std::string& bundle,
		int page, int* origin, bool* white)
	{
		char buffer[300];

		*origin = 5;
		*white = false;

		std::string path = HDScanPagePath(root5.c_str(), page, buffer, sizeof(buffer));
		if (file_there(path))
			return path;

		if (!bundle.empty())
		{
			const int siblings[2] = { 4, 6 };

			for (int i = 0; i < 2; i++)
			{
				const std::string other = join(bundle, std::to_string(siblings[i]));
				path = HDScanPagePath(other.c_str(), page, buffer, sizeof(buffer));

				if (file_there(path))
				{
					*origin = siblings[i];
					return path;
				}
			}
		}

		*white = true;
		*origin = 0;

		path = join(join(root5, "TEX"), "WHITE.DDS");
		if (file_there(path))
			return path;

		if (!bundle.empty())
		{
			const int siblings[2] = { 4, 6 };

			for (int i = 0; i < 2; i++)
			{
				const std::string other = join(bundle, std::to_string(siblings[i]));
				path = join(join(other, "TEX"), "WHITE.DDS");
				if (file_there(path))
					return path;
			}
		}

		return std::string();
	}

	void decode_one(const std::string& path, DecodedPage& out)
	{
		std::vector<unsigned char> file;

		if (!read_whole(path, file) || file.empty())
			return;

		const long levels = BC7DDSMipCount(&file[0], (unsigned long)file.size());

		for (long mip = 0; mip < levels; mip++)
		{
			DecodedMip level;
			const char* why = "";
			level.pixels = BC7DecodeDDSMip(&file[0], (unsigned long)file.size(),
				4096, mip, &level.w, &level.h, &why);

			if (!level.pixels)
				break;		// the chain ends where whole 4x4 blocks end

			out.mips.push_back(level);
		}

		out.ok = !out.mips.empty();
	}

	bool ensure_staging(long w, long h, long levels)
	{
		if (g_staging && g_staging_w == w && g_staging_h == h && g_staging_levels == levels)
			return true;

		if (g_staging)
		{
			g_staging->Release();
			g_staging = 0;
		}

		if (FAILED(g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
			D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_staging, 0)))
		{
			g_staging = 0;
			return false;
		}

		g_staging_w = w;
		g_staging_h = h;
		g_staging_levels = levels;
		return true;
	}

	void copy_into(IDirect3DTexture9* texture, const DecodedPage& page, DWORD lockFlags)
	{
		for (size_t mip = 0; mip < page.mips.size(); mip++)
		{
			D3DLOCKED_RECT rect;

			if (FAILED(texture->LockRect((UINT)mip, &rect, 0, lockFlags)))
				continue;

			const DecodedMip& level = page.mips[mip];
			const unsigned char* src = (const unsigned char*)level.pixels;
			unsigned char* dst = (unsigned char*)rect.pBits;

			for (long y = 0; y < level.h; y++)
			{
				std::memcpy(dst, src, (size_t)level.w * 4);
				src += (size_t)level.w * 4;
				dst += rect.Pitch;
			}

			texture->UnlockRect((UINT)mip);
		}
	}
}

bool HDPageCacheCreate(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	HDPageCacheDestroy();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!device || !installRoot || !*installRoot)
	{
		*why = "no device or no install";
		return false;
	}

	g_device = device;

	const std::string root5 = installRoot;
	const std::string bundle = parent_of(root5);

	if (threads < 1)
		threads = 1;

	// The distinct pages, in the order they were named. A .TRG names each of
	// its pages once, but a caller may hand in the item pages as well, and
	// then the same number turns up twice.
	std::vector<int> unique;
	unique.reserve((size_t)(count > 0 ? count : 0));

	for (int i = 0; i < count; i++)
	{
		if (g_slot_of_page.count(pages[i]))
			continue;

		g_slot_of_page[pages[i]] = (int)unique.size();
		unique.push_back(pages[i]);
	}

	g_textures.assign(unique.size(), (IDirect3DTexture9*)0);
	g_page_of_slot = unique;

	if (stats)
	{
		stats->pages = count;
		stats->slots = (int)unique.size();
		stats->managed = managed ? 1 : 0;
		stats->threads = threads;
	}

	if (unique.empty())
		return true;

	/* The paths first, on this thread: resolving a page is three or four
	 * fopen calls and the notes have to come out in page order, not in
	 * whatever order the threads happen to finish in. */
	std::vector<std::string> paths(unique.size());
	std::vector<int> origins(unique.size(), 5);
	std::vector<char> whites(unique.size(), 0);
	std::vector<long> sizes(unique.size(), 0);

	const double resolve_started = now_ms();

	for (size_t i = 0; i < unique.size(); i++)
	{
		int origin = 5;
		bool white = false;
		paths[i] = resolve_page(root5, bundle, unique[i], &origin, &white);
		origins[i] = origin;
		whites[i] = white ? 1 : 0;
		sizes[i] = paths[i].empty() ? 0 : file_size(paths[i]);

		char line[256];

		if (paths[i].empty())
		{
			std::snprintf(line, sizeof(line),
				"page %d has no .DDS and no WHITE.DDS - that page stays untextured",
				unique[i]);
			g_notes.push_back(line);
		}
		else if (white)
		{
			std::snprintf(line, sizeof(line),
				"page %d is not in any TEX folder - WHITE.DDS instead", unique[i]);
			g_notes.push_back(line);
		}
		else if (origin != 5)
		{
			std::snprintf(line, sizeof(line),
				"page %d comes from the sibling game %d, not from 5", unique[i], origin);
			g_notes.push_back(line);
		}
	}

	/* THE BATCH. Decoding everything first and uploading afterwards would
	 * hold 376 MiB of pixels at once for TITLE's 220 pages - a large part of
	 * what a 32 bit process can address, and all of it transient. So the
	 * pages go through in batches, and the address space never holds more
	 * than one batch.
	 *
	 * The batch is measured in BYTES, not in pages, and that is not a
	 * refinement. The remaster's pages are not one size: 2391 of them are
	 * 512x512 and decode to 1.4 MiB, but ELEVEN are 4096x4096 and decode to
	 * 85 MiB each. A batch of "threads * 2 + 2" pages that happened to catch
	 * several of those would ask for one and a half gigabytes in a process
	 * that has three, and it would do it rarely enough to look like a random
	 * crash. Four times the file's length is the estimate - BC7 is one byte
	 * per texel where A8R8G8B8 is four, and the mip chain scales both the
	 * same way - and a batch always holds at least one page, however big. */
	const size_t batch_pages = (size_t)threads * 2 + 2;
	const double batch_budget = 96.0 * 1048576.0;

	std::vector<DecodedPage> batch;
	std::atomic<int> next(0);

	double decode_ms = 0, upload_ms = 0;
	const double started = now_ms();

	for (size_t base = 0; base < unique.size(); )
	{
		size_t here = 0;
		double bytes = 0;

		while (base + here < unique.size() && here < batch_pages)
		{
			const double next_bytes = 4.0 * (double)sizes[base + here];

			if (here && bytes + next_bytes > batch_budget)
				break;

			bytes += next_bytes;
			here++;
		}

		batch.clear();
		batch.resize(here);

		for (size_t i = 0; i < here; i++)
		{
			batch[i].page = unique[base + i];
			batch[i].slot = (int)(base + i);
			batch[i].origin = origins[base + i];
			batch[i].white = whites[base + i] != 0;
		}

		next.store(0);

		const double decode_started = now_ms();

		const auto work = [&]()
		{
			for (;;)
			{
				const int at = next.fetch_add(1);
				if (at >= (int)here)
					break;

				if (!paths[base + (size_t)at].empty())
					decode_one(paths[base + (size_t)at], batch[(size_t)at]);
			}
		};

		if (threads == 1)
			work();
		else
		{
			std::vector<std::thread> pool;
			const int use = threads > (int)here ? (int)here : threads;
			pool.reserve((size_t)use);

			for (int i = 0; i < use; i++)
				pool.push_back(std::thread(work));
			for (std::thread& t : pool)
				t.join();
		}

		decode_ms += now_ms() - decode_started;

		const double upload_started = now_ms();

		for (size_t i = 0; i < here; i++)
		{
			DecodedPage& page = batch[i];

			if (!page.ok)
			{
				if (stats)
					stats->failed++;

				page.free_all();
				continue;
			}

			const long w = page.mips[0].w;
			const long h = page.mips[0].h;
			const long levels = (long)page.mips.size();

			IDirect3DTexture9* texture = 0;

			if (managed)
			{
				if (SUCCEEDED(g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
					D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, 0)))
					copy_into(texture, page, 0);
				else
					texture = 0;
			}
			else if (ensure_staging(w, h, levels))
			{
				/* D3DPOOL_DEFAULT cannot be locked, so the pixels go into
				 * the one staging texture and from there straight across
				 * the bus. The staging texture is the ONLY system memory
				 * this path ever holds - 1.4 MiB, not 293. */
				copy_into(g_staging, page, 0);

				if (SUCCEEDED(g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
					D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, 0)))
				{
					if (FAILED(g_device->UpdateTexture(g_staging, texture)))
					{
						texture->Release();
						texture = 0;
					}
				}
				else
					texture = 0;
			}

			if (texture)
			{
				g_textures[page.slot] = texture;

				if (stats)
				{
					stats->mipLevels += (int)levels;

					if (w > stats->widest)
						stats->widest = (int)w;

					if (w > 1024 || h > 1024)
						stats->bigPages++;

					for (const DecodedMip& mip : page.mips)
						stats->bytes += (double)mip.w * (double)mip.h * 4.0;

					if (page.white)
						stats->fromWhite++;
					else if (page.origin == 4)
						stats->fromGame4++;
					else if (page.origin == 6)
						stats->fromGame6++;
					else
						stats->fromGame5++;
				}
			}
			else if (stats)
				stats->failed++;

			page.free_all();
		}

		upload_ms += now_ms() - upload_started;

		/* A staging texture for one of the eleven 4096x4096 pages is 85 MiB
		 * of system memory, and keeping it "for reuse" would mean carrying
		 * that for the rest of the load because one page in the middle
		 * happened to be big. Anything past a 1024x1024 page goes back at
		 * the end of its batch; the 512x512 case - which is 2391 of 2406
		 * pages - keeps the one staging texture for the whole level, which
		 * is what it is for. */
		if (g_staging && (g_staging_w > 1024 || g_staging_h > 1024))
		{
			g_staging->Release();
			g_staging = 0;
			g_staging_w = g_staging_h = g_staging_levels = 0;
		}

		base += here;
	}

	if (g_staging)
	{
		g_staging->Release();
		g_staging = 0;
		g_staging_w = g_staging_h = g_staging_levels = 0;
	}

	if (stats)
	{
		// readMs is the path search - the three or four fopen calls per page
		// that decide which TEX folder a page comes out of. The reading of
		// the bytes themselves sits inside decodeMs, where the threads are.
		stats->readMs = started - resolve_started;
		stats->decodeMs = decode_ms;
		stats->uploadMs = upload_ms;
		stats->totalMs = now_ms() - resolve_started;
	}

	return true;
}

void HDPageCacheDestroy()
{
	for (IDirect3DTexture9* texture : g_textures)
		if (texture)
			texture->Release();

	g_textures.clear();
	g_page_of_slot.clear();
	g_slot_of_page.clear();
	g_notes.clear();

	if (g_staging)
	{
		g_staging->Release();
		g_staging = 0;
	}

	g_staging_w = g_staging_h = g_staging_levels = 0;
	g_device = 0;
}

IDirect3DTexture9* HDPageCacheTexture(int page)
{
	const std::unordered_map<int, int>::const_iterator at = g_slot_of_page.find(page);

	if (at == g_slot_of_page.end())
		return 0;

	return g_textures[(size_t)at->second];
}

int HDPageCacheSlots()
{
	return (int)g_textures.size();
}

int HDPageCacheSlotOf(int page)
{
	const std::unordered_map<int, int>::const_iterator at = g_slot_of_page.find(page);
	return at == g_slot_of_page.end() ? -1 : at->second;
}

IDirect3DTexture9* HDPageCacheSlotTexture(int slot)
{
	if (slot < 0 || slot >= (int)g_textures.size())
		return 0;

	return g_textures[(size_t)slot];
}

int HDPageCacheNoteCount()
{
	return (int)g_notes.size();
}

const char* HDPageCacheNote(int at)
{
	if (at < 0 || at >= (int)g_notes.size())
		return "";

	return g_notes[(size_t)at].c_str();
}

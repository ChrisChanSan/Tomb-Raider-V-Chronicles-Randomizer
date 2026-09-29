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
#include "hd_dxt5.h"
#include "hd_mods.h"		// where a mod's own page lives
#include "hd_pageclass.h"	// clear, cut-out or opaque
#include "hd_scan.h"
#include "../bc7.h"

#include <algorithm>
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
	/* One decoded page, mip chain and all. The pixels are malloc'd by
	 * BC7DecodeDDSMip and freed here; nothing of a page survives its
	 * upload. In the DXT5 case the compression happens on the same worker
	 * thread that decoded the level, and the pixels are let go the moment
	 * the blocks exist - so the batch below holds a quarter of what it
	 * would otherwise carry into the upload. */
	struct DecodedMip
	{
		unsigned long* pixels = 0;
		unsigned char* blocks = 0;
		long w = 0, h = 0;
	};

	struct DecodedPage
	{
		std::vector<DecodedMip> mips;
		int page = 0;
		int slot = 0;
		int origin = 0;			// 5, 4, or 0 for WHITE.DDS
		int firstMip = 0;		// the level this page starts at
		double compressMs = 0;		// this page's share of the DXT5 work
		bool white = false;
		bool ok = false;
		/* Is this a cut-out page? Set while the top mip is still in
		 * A8R8G8B8, which is the only moment it can be: with -format dxt5
		 * the pixels are thrown away three lines later, and the device
		 * surface is write-only. See the long note beside
		 * measure_cutout. */
		bool cutout = false;
		int cutoutLow = 0;		// texels below the threshold
		int cutoutTexels = 0;		// texels looked at
		/* The smallest and the largest alpha over every texel of the same
		 * level, for HDPageClassOf (hd_pageclass.h). Smallest above largest
		 * is "nothing measured", which the class reads as opaque. */
		int alphaMin = 255;
		int alphaMax = 0;

		void free_all()
		{
			for (DecodedMip& mip : mips)
			{
				if (mip.pixels)
					std::free(mip.pixels);
				if (mip.blocks)
					std::free(mip.blocks);
			}
			mips.clear();
		}
	};

	/* What one page will cost, worked out from its 148 byte header before a
	 * single block is decoded. `firstMip` is the level the page will start
	 * at: 0 normally, 1 or more when the device cannot hold the full size or
	 * when the budget said no. */
	struct PagePlan
	{
		std::string path;
		int origin = 5;
		bool white = false;
		long w = 0, h = 0, mips = 0;
		int firstMip = 0;
		int clamped = 0;		// levels the device's size limit took
		bool synth = false;		// no file at all - a white tile made here
		double bytes = 0;		// cost of firstMip..end in the chosen format
		double full = 0;		// the same at firstMip = 0
		double decodeBytes = 0;		// A8R8G8B8 transient, whatever the format
	};

	IDirect3DDevice9* g_device = 0;
	std::vector<IDirect3DTexture9*> g_textures;
	// One byte per slot: 1 = this page has real cut-out holes in its alpha
	// channel, so a draw call on it wants the alpha test even in the opaque
	// pass. Grown and cleared exactly with g_textures.
	std::vector<unsigned char> g_cutout;
	/* The measured share itself, in per mille. The bool above is that number
	 * against HD_CUTOUT_PERMILLE; the number itself is what a diagnostic
	 * tool needs to say why a page counts as a cut-out sheet, and it is how
	 * an item page's alpha was shown not to be a cut-out mask at all. */
	std::vector<short> g_cutout_share;
	/* One byte per slot: HDPageClassOf's answer (hd_pageclass.h) - 2 for a
	 * page that is part-way clear in every texel, the RICH2 marble floors'
	 * kind. Grown and cleared exactly with g_textures, like g_cutout. */
	std::vector<unsigned char> g_class;
	/* One byte per slot: the smallest alpha of the top mip the page keeps,
	 * the number HDPageClassOf reads - and the room renderer's test for a
	 * gloss mask (hd_gloss_math.h, HD_GLOSS_MASK_BELOW). 255 for a slot
	 * nothing was measured on. Grown and cleared exactly with g_textures. */
	std::vector<unsigned char> g_alpha_min;
	/* Where each slot's file was found, in resolve_page's own encoding.
	 * Kept per slot rather than only in the notes because the notes report
	 * the odd cases and a tool has to be able to ask about every page -
	 * which is what proves that a mod replaced the two it brought and none
	 * of the others. */
	std::vector<short> g_origin;
	std::vector<int> g_page_of_slot;
	std::unordered_map<int, int> g_slot_of_page;
	std::vector<std::string> g_notes;

	/* The one reused staging texture of the DEFAULT path. It is recreated
	 * only when a page has a shape the last one did not - which over every
	 * TR5 level means exactly once, because every remaster page is 512x512
	 * with six mips. */
	IDirect3DTexture9* g_staging = 0;
	long g_staging_w = 0, g_staging_h = 0, g_staging_levels = 0;
	int g_staging_format = HD_PAGE_FORMAT_RGBA;

	/* The policy and the running total. Both outlive a single call: a level
	 * fills this cache twice - once for its rooms and once for its models
	 * and Lara's wardrobe - and one budget has to cover the two of them.
	 * `g_bytes` is what is on the device now, `g_format_used` what the
	 * device let us have after CheckDeviceFormat had its say. */
	HDPageCachePolicy g_policy = { HD_PAGE_FORMAT_RGBA, 0.0, 0 };
	double g_bytes = 0;
	int g_format_used = HD_PAGE_FORMAT_RGBA;

	/* The other bundle's game folder - see HD_PAGE_ALT_BASE. Outlives
	 * Destroy for the same reason the policy does: it is a property of the
	 * installation, not of a level. */
	std::string g_alt_root;

	/* What HDPageCacheKeyOf answered last time. Its answer costs up to five
	 * file system probes - two into the mod's own TEX folder and three into
	 * its origin bundle - and it sits on the hottest path this module has:
	 * once per corner of every triangle of every model a mod supplied.
	 * Measured on a real Mods folder: 30.8
	 * microseconds a call over 25,058 calls.
	 *
	 * The answer depends on the mods scan, which happens once per session,
	 * and on g_alt_root, which is set once per level - so it is a pure
	 * function of the key between two of those events, and this is the
	 * memory of it. Cleared where either can change: HDPageCacheAltRoot and
	 * HDPageCacheDestroy. hd_model.cpp asks once per texture slot instead of
	 * once per corner, which keeps the question cheap for every other
	 * caller too. */
	std::unordered_map<int, int> g_key_of;

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

	// The length of a file, or 0 when it is not there.
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

	/* The shape of a page without decoding it. 148 bytes off the front of
	 * the file say how wide it is and how many levels it carries, and that
	 * is the whole basis of the budget: a length-based estimate is not a
	 * thing to promise a card with two gigabytes. */
	bool page_shape(const std::string& path, long* w, long* h, long* mips)
	{
		unsigned char header[148];
		std::FILE* file = std::fopen(path.c_str(), "rb");

		*w = *h = *mips = 0;

		if (!file)
			return false;

		const size_t got = std::fread(header, 1, sizeof(header), file);
		std::fclose(file);

		if (got != sizeof(header))
			return false;

		return BC7DDSShape(header, (unsigned long)sizeof(header), w, h, mips);
	}

	/* How many levels of a page can actually be decoded, starting at
	 * `first`. The rule is the decoder's own and not a guess: BC7DecodeDDSMip
	 * refuses a level that is not a whole number of 4x4 blocks, so the chain
	 * stops where that stops - which for a remaster page is 16x16, its
	 * sixth level. */
	long usable_levels(long w, long h, long mips, int first)
	{
		long levels = 0;

		for (long m = first; m < mips; m++)
		{
			const long mw = w >> m;
			const long mh = h >> m;

			if (mw < 4 || mh < 4 || (mw & 3) || (mh & 3))
				break;

			levels++;
		}

		return levels;
	}

	// What those levels cost on the device in the chosen format.
	double level_bytes(long w, long h, long mips, int first, int format)
	{
		double total = 0;

		for (long m = first; m < mips; m++)
		{
			const long mw = w >> m;
			const long mh = h >> m;

			if (mw < 4 || mh < 4 || (mw & 3) || (mh & 3))
				break;

			total += format == HD_PAGE_FORMAT_DXT5
				? (double)DXT5Size(mw, mh)
				: (double)mw * (double)mh * 4.0;
		}

		return total;
	}

	/* Whether the device can make a texture of that format at all. Asked of
	 * the device rather than assumed, because the whole HD path rests on it:
	 * without A8R8G8B8 there is no page and no picture, and without DXT5 the
	 * switch has to go quietly back to A8R8G8B8 instead of handing the driver
	 * a format it will refuse page by page. */
	bool device_can_make(IDirect3DDevice9* device, D3DFORMAT format)
	{
		IDirect3D9* d3d = 0;
		D3DDEVICE_CREATION_PARAMETERS made;
		D3DDISPLAYMODE mode;

		if (!device || FAILED(device->GetDirect3D(&d3d)) || !d3d)
			return false;

		if (FAILED(device->GetCreationParameters(&made)) ||
			FAILED(device->GetDisplayMode(0, &mode)))
		{
			d3d->Release();
			return false;
		}

		const HRESULT hr = d3d->CheckDeviceFormat(made.AdapterOrdinal,
			made.DeviceType, mode.Format, 0, D3DRTYPE_TEXTURE, format);

		d3d->Release();
		return SUCCEEDED(hr);
	}

	bool out_of_memory(HRESULT hr)
	{
		return hr == D3DERR_OUTOFVIDEOMEMORY || hr == E_OUTOFMEMORY;
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

	/* Where a page lives. The level's own game folder first, then the two
	 * siblings, then the remaster's own white tile.
	 *
	 * The siblings are not a nicety: RICH3 names three pages that ship only
	 * in the bundle's 4\TEX, because the remaster keeps one texture pool per
	 * game and the level's art was moved between them. Without the fallback
	 * those three pages are black holes in the middle of the level. Six item
	 * models are in the same position and follow the same rule.
	 *
	 * WHITE.DDS is slot 0 of the remaster's own page table - a plain white
	 * tile. A missing page painted white shows the room's baked light and
	 * looks like untextured geometry; painted black it looks like a hole and
	 * gets reported as a rendering bug. That choice is TROne's and it is the
	 * right one. */
	/* A page number as a note should show it: plain, "TR3 12" for one out of
	 * the other bundle, and "mod3:6111" for one out of a mod - the namespace
	 * and the number the mod's own file is called, so a reader can tell a
	 * mod's 6111 from the install's at a glance. A note that printed
	 * 1342183535 would be true and unreadable. */
	std::string page_text(int page)
	{
		const int mod = HDPageModIndex(page);

		if (mod >= 0)
			return "mod" + std::to_string(mod) + ":" +
				std::to_string(HDPageModNumber(page));

		if (page >= HD_PAGE_ALT_BASE)
			return "TR3 " + std::to_string(page - HD_PAGE_ALT_BASE);

		return std::to_string(page);
	}

	/* "<TEX folder of game `game` in the other bundle>\<page>.DDS", with
	 * `game` 0 meaning the alternate root itself. Written into `buffer`,
	 * which is a caller's 300 characters exactly as HDScanPagePath wants
	 * them. Empty when there is no alternate root. */
	void path_of_alt(int page, int game, char* buffer, int size)
	{
		buffer[0] = 0;

		if (g_alt_root.empty())
			return;

		const std::string root = game
			? join(parent_of(g_alt_root), std::to_string(game))
			: g_alt_root;

		HDScanPagePath(root.c_str(), page, buffer, size);
	}

	/* Where a page came from, as resolve_page reports it: 0 is the white
	 * tile, 1..3 a game folder of the I-III bundle, 4 and 5 the IV-VI games,
	 * and this plus the mod's index is "out of that mod's own TEX folder".
	 * Far enough above 5 that no game number can be mistaken for it and the
	 * notes below can say which mod without a second field. */
#define HD_PAGE_ORIGIN_MOD	100

	std::string resolve_page(const std::string& root5, const std::string& bundle,
		int page, int* origin, bool* white)
	{
		char buffer[300];

		*white = false;

		/* A mod's own page first of all. The key says which mod asked, so
		 * the chain is that mod's 5\TEX, its 4\TEX, then - only if the mod's
		 * mod.txt named one - the game folder of the Tomb Raider I-III
		 * bundle its models were drawn for and that folder's two siblings,
		 * and then the ordinary IV-VI chain below with the page number as it
		 * stands.
		 *
		 * The fall-through to the ordinary chain is what lets a mod ship
		 * models and no art at all: every page it does not bring is the
		 * install's own, resolved exactly as it always was. And the other
		 * way round is the rule this rests on - a page a vanilla model asks
		 * for never carries a mod key, so it never enters this branch and
		 * cannot be replaced by a mod. */
		const int mod = HDPageModIndex(page);

		if (mod >= 0)
		{
			const int real = HDPageModNumber(page);

			*origin = HD_PAGE_ORIGIN_MOD + mod;

			if (*HDModsFindPage(mod, real, buffer, sizeof(buffer)))
				return buffer;

			/* The mod's origin bundle. Its own game folder first, then the
			 * other two in number order - the same courtesy the guests get,
			 * and needed for the same reason: the I-III remaster keeps one
			 * texture per page number across the bundle and moved art between
			 * the three games. Lara's skin pages of that bundle are what a
			 * weapon mod's models reach for (an Uzi mod, for one). */
			const int from = HDModsOrigin(mod);

			if (from >= 1 && from <= 3 && !g_alt_root.empty())
			{
				int order[3] = { from, 0, 0 };
				int at = 1;

				for (int game = 1; game <= 3; game++)
					if (game != from)
						order[at++] = game;

				for (int i = 0; i < 3; i++)
				{
					path_of_alt(real, order[i], buffer, sizeof(buffer));

					if (file_there(std::string(buffer)))
					{
						*origin = order[i];
						return buffer;
					}
				}
			}

			// Neither the mod nor its origin bundle has it: on to the
			// install, with the plain number, exactly as a vanilla page.
			page = real;
			*origin = 5;
		}

		/* The other bundle first, because a page above the base is not a
		 * page of this game at all. Game 3 of the I-III remaster, then its
		 * siblings 1 and 2 - the same courtesy the IV-VI games do each other
		 * below, and needed for the same reason: the remaster ships one
		 * texture per page number across a bundle, and the guests' art was
		 * moved between the three games. */
		if (page >= HD_PAGE_ALT_BASE)
		{
			const int real = page - HD_PAGE_ALT_BASE;

			*origin = 3;

			if (!g_alt_root.empty())
			{
				path_of_alt(real, 0, buffer, sizeof(buffer));

				std::string path = buffer;

				if (file_there(path))
					return path;

				const int siblings[2] = { 1, 2 };

				for (int i = 0; i < 2; i++)
				{
					path_of_alt(real, siblings[i], buffer, sizeof(buffer));
					path = buffer;

					if (file_there(path))
					{
						*origin = siblings[i];
						return path;
					}
				}
			}

			/* Falls through to the white tile below, which is looked for in
			 * the level's OWN bundle - it is four white texels either way and
			 * the one that is certainly installed is the better bet. */
			*white = true;
			*origin = 0;
		}
		else
		{
			*origin = 5;

			std::string path = HDScanPagePath(root5.c_str(), page, buffer, sizeof(buffer));
			if (file_there(path))
				return path;

			/* 5, then 4, then nothing - game 6 is Angel of Darkness and is
			 * not searched. */
			if (!bundle.empty())
			{
				const std::string other = join(bundle, std::string("4"));
				path = HDScanPagePath(other.c_str(), page, buffer, sizeof(buffer));

				if (file_there(path))
				{
					*origin = 4;
					return path;
				}
			}
		}

		std::string path;

		*white = true;
		*origin = 0;

		path = join(join(root5, "TEX"), "WHITE.DDS");
		if (file_there(path))
			return path;

		if (!bundle.empty())
		{
			const std::string other = join(bundle, std::string("4"));
			path = join(join(other, "TEX"), "WHITE.DDS");
			if (file_there(path))
				return path;
		}

		return std::string();
	}

	/* `first` is the level the page starts at: 0 is the whole picture, 1
	 * halves it in both directions and quarters what it costs on the card.
	 * The texture coordinates never notice - they are 0..1 of whatever the
	 * texture turns out to be - so a dropped level is a blurrier wall and
	 * nothing else.
	 *
	 * `format` decides whether the pixels stay as they are or are compressed
	 * here, on this worker thread, and let go straight away. */
	/* A white tile made here, out of nothing.
	 *
	 * TROne's rule is that a page which cannot be found is white and never
	 * black, followed here by reaching for the remaster's own TEX\WHITE.DDS.
	 * But that file exists in 4\TEX only - not in 5\TEX and not in 6\TEX -
	 * so an installation without the fourth game (or one where the file has
	 * been deleted) had no white tile at all, and the page ended up with no
	 * texture. A draw call with no texture is skipped, so the surface did
	 * not go white - it went away, and a wall you can see through reads as a
	 * rendering fault rather than as a missing file. Four white texels cost
	 * 64 bytes and remove the whole class of failure. WHITE.DDS is still
	 * preferred when it is there: it is the remaster's own tile, and using
	 * it keeps the two paths agreeing. */
	void make_white(DecodedPage& out, int format)
	{
		DecodedMip level;

		level.w = 4;
		level.h = 4;

		if (format == HD_PAGE_FORMAT_DXT5)
		{
			unsigned long white[16];

			for (int i = 0; i < 16; i++)
				white[i] = 0xFFFFFFFFul;

			level.blocks = (unsigned char*)std::malloc(DXT5Size(4, 4));

			if (!level.blocks)
				return;

			DXT5Encode(white, 4, 4, level.blocks);
		}
		else
		{
			level.pixels = (unsigned long*)std::malloc(16 * sizeof(unsigned long));

			if (!level.pixels)
				return;

			for (int i = 0; i < 16; i++)
				level.pixels[i] = 0xFFFFFFFFul;
		}

		out.mips.push_back(level);
		out.ok = true;
	}

/* ------------------------------------------------- the cut-out pages -----
 *
 * Without this, the kelp of JOBY4 and the vegetation of ANDY1 draw as solid
 * green rectangles with hard edges. They are material 8 in the .TRG, and
 * material 8 sits in the opaque pass - 23,040 triangles of it in JOBY4,
 * 260,756 in ANDY1 - while the alpha test lives in the alpha pass and
 * nowhere else. That rule is right in general (TROne, a TR1 snow floor: an
 * opaque tile's alpha channel carries a gloss mask, and clipping on it
 * blanked the floor), but it is right about the pages the opaque pass
 * usually gets, not about these.
 *
 * Measured over the remaster's own files, counting texels of the top mip
 * whose alpha is below 128:
 *
 *   JOBY4  material 8   pages 3198, 3257     52 % / 95 % of the blocks
 *   JOBY4  material 17  page 3212            51 %
 *   ANDY1  material 3   pages 3268..3272     62..87 %
 *   ANDY1  material 8   pages 3264, 3165     52..59 %
 *   ANDY1  material 0   pages 3263, 694-698   0 %   (and every other one)
 *   JOBY4  material 16  page 3255 (light rays) 0 %
 *
 * So the .TRG's opaque pass carries two kinds of page in TR5: the ordinary
 * ones, whose alpha channel is a flat 255 and on which an alpha test changes
 * nothing, and a handful of cut-out sheets of leaves. That is what makes the
 * per-page answer safe: a page with no low-alpha texel cannot lose a pixel to
 * a test it always passes, so the test is switched on exactly where it has
 * something to do and the gloss-mask trap is never entered.
 *
 * The threshold is 0.5, and it is not a taste: material 6 and material 8 both
 * carry 0.5 in param[0] in every level of the game, which is the remaster's
 * own alpha reference for the material.
 *
 * The cutoff is 2 % and not one texel: a page that is a gloss mask has a few
 * dark corners; a page that is a cut-out sheet is half hole. The measured gap
 * is between 0 % and 51 %, so anything in between decides the same way, and
 * 2 % is picked because it is the smallest number that no rounding can
 * reach. */
#define HD_CUTOUT_ALPHA		128	// the shader clips at 0.5
#define HD_CUTOUT_PERMILLE	20	// 2 % of the page below it

	void measure_cutout(DecodedPage& out, const DecodedMip& level)
	{
		if (!level.pixels || level.w <= 0 || level.h <= 0)
			return;

		/* Every fourth texel in both directions - 16,384 of a 512x512 page,
		 * 65,536 of a 4096-wide one. A cut-out sheet's holes are whole
		 * leaves wide, so a sixteenth of the page counts them as well as all
		 * of it does, and it keeps this off the level load time (measured:
		 * under a millisecond over a whole level's pages). */
		const long step = 4;
		int low = 0, seen = 0;

		for (long y = 0; y < level.h; y += step)
		{
			const unsigned long* row = level.pixels + (size_t)y * (size_t)level.w;

			for (long x = 0; x < level.w; x += step)
			{
				seen++;

				if ((row[x] >> 24) < HD_CUTOUT_ALPHA)
					low++;
			}
		}

		out.cutoutLow = low;
		out.cutoutTexels = seen;
		out.cutout = seen > 0 && low * 1000 >= seen * HD_CUTOUT_PERMILLE;

		/* The smallest and the largest alpha, over every texel and not every
		 * fourth: "clear in every texel" is a claim about all of them, and one
		 * opaque inlay or one hole the step skips would flip the class. It
		 * runs on the decoding thread beside a BC7 decode of the same page,
		 * which costs far more than one compare per texel. */
		int amin = 255, amax = 0;
		const size_t texels = (size_t)level.w * (size_t)level.h;

		for (size_t i = 0; i < texels; i++)
		{
			const int a = (int)(level.pixels[i] >> 24);

			if (a < amin)
				amin = a;

			if (a > amax)
				amax = a;
		}

		out.alphaMin = amin;
		out.alphaMax = amax;
	}

	void decode_one(const std::string& path, int first, int format,
		DecodedPage& out)
	{
		std::vector<unsigned char> file;

		if (!read_whole(path, file) || file.empty())
			return;

		const long levels = BC7DDSMipCount(&file[0], (unsigned long)file.size());

		out.firstMip = first;

		for (long mip = first; mip < levels; mip++)
		{
			DecodedMip level;
			const char* why = "";
			level.pixels = BC7DecodeDDSMip(&file[0], (unsigned long)file.size(),
				4096, mip, &level.w, &level.h, &why);

			if (!level.pixels)
				break;		// the chain ends where whole 4x4 blocks end

			// The cutout share is measured on the first level this page
			// keeps, and while it is still A8R8G8B8. The DXT5 branch below
			// frees these pixels.
			if (out.mips.empty())
				measure_cutout(out, level);

			if (format == HD_PAGE_FORMAT_DXT5)
			{
				const double started = now_ms();
				const unsigned long size = DXT5Size(level.w, level.h);

				level.blocks = (unsigned char*)std::malloc(size ? size : 1);

				if (level.blocks)
					DXT5Encode(level.pixels, level.w, level.h, level.blocks);

				std::free(level.pixels);
				level.pixels = 0;

				/* Counted into the page, not into a shared total: this runs
				 * on a worker thread and two of them adding to one double is
				 * a race that costs a wrong number for nothing. The batch
				 * sums them after the join. */
				out.compressMs += now_ms() - started;

				if (!level.blocks)
					break;
			}

			out.mips.push_back(level);
		}

		out.ok = !out.mips.empty();
	}

	bool ensure_staging(long w, long h, long levels, int format)
	{
		if (g_staging && g_staging_w == w && g_staging_h == h &&
			g_staging_levels == levels && g_staging_format == format)
			return true;

		if (g_staging)
		{
			g_staging->Release();
			g_staging = 0;
		}

		if (FAILED(g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
			format == HD_PAGE_FORMAT_DXT5 ? D3DFMT_DXT5 : D3DFMT_A8R8G8B8,
			D3DPOOL_SYSTEMMEM, &g_staging, 0)))
		{
			g_staging = 0;
			return false;
		}

		g_staging_w = w;
		g_staging_h = h;
		g_staging_levels = levels;
		g_staging_format = format;
		return true;
	}

	void copy_into(IDirect3DTexture9* texture, const DecodedPage& page,
		int format, DWORD lockFlags)
	{
		for (size_t mip = 0; mip < page.mips.size(); mip++)
		{
			D3DLOCKED_RECT rect;

			if (FAILED(texture->LockRect((UINT)mip, &rect, 0, lockFlags)))
				continue;

			const DecodedMip& level = page.mips[mip];
			unsigned char* dst = (unsigned char*)rect.pBits;

			if (format == HD_PAGE_FORMAT_DXT5)
			{
				/* A compressed surface is locked in rows of blocks: the pitch
				 * is the bytes of one row of 4x4 blocks, and there are h/4 of
				 * them. Copying it as h rows would write four times too much
				 * and off the end of the surface. */
				const unsigned char* src = level.blocks;
				const size_t row = (size_t)((level.w + 3) / 4) * 16;

				for (long by = 0; by < level.h; by += 4)
				{
					std::memcpy(dst, src, row);
					src += row;
					dst += rect.Pitch;
				}
			}
			else
			{
				const unsigned char* src = (const unsigned char*)level.pixels;

				for (long y = 0; y < level.h; y++)
				{
					std::memcpy(dst, src, (size_t)level.w * 4);
					src += (size_t)level.w * 4;
					dst += rect.Pitch;
				}
			}

			texture->UnlockRect((UINT)mip);
		}
	}
}

/* The one builder. Create empties the cache first and Append does not; every
 * other line is the same, and a second copy of the batching, the decoding and
 * the two upload paths would be a second place for the 4096x4096 budget to be
 * got wrong. `append` is the whole difference, and it shows up in exactly
 * three places below: the slot numbers start at what is already there, the
 * texture array grows instead of being replaced, and the notes are kept. */
static bool build_cache(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads, bool append,
	HDPageCacheStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!device || !installRoot || !*installRoot)
	{
		*why = "no device or no install";
		return false;
	}

	g_device = device;

	/* What the device will actually make, asked once per call rather than
	 * assumed. A8R8G8B8 as a texture is the floor the whole
	 * HD path stands on and a device without it has no business trying;
	 * DXT5 is a switch, so a device without it goes quietly back to the
	 * lossless default with a line in the log rather than failing 220 times
	 * in a row with an HRESULT nobody reads. */
	if (!device_can_make(device, D3DFMT_A8R8G8B8))
	{
		*why = "the device cannot make A8R8G8B8 textures";
		return false;
	}

	g_format_used = g_policy.format;

	if (g_format_used == HD_PAGE_FORMAT_DXT5 && !device_can_make(device, D3DFMT_DXT5))
	{
		g_notes.push_back("the device has no DXT5 - the pages stay A8R8G8B8");
		g_format_used = HD_PAGE_FORMAT_RGBA;
	}

	const std::string root5 = installRoot;
	const std::string bundle = parent_of(root5);

	if (threads < 1)
		threads = 1;

	// The distinct pages, in the order they were named. A .TRG names each of
	// its pages once, but a caller may hand in the item pages as well, and
	// then the same number turns up twice.
	std::vector<int> unique;
	unique.reserve((size_t)(count > 0 ? count : 0));

	/* Where this call's first new slot lands. Zero for Create (the cache was
	 * just emptied), the count so far for Append - which is what lets a page
	 * a room and a model both wear keep ONE texture and ONE slot number. */
	const size_t slot_base = append ? g_page_of_slot.size() : 0;

	for (int i = 0; i < count; i++)
	{
		if (g_slot_of_page.count(pages[i]))
			continue;

		g_slot_of_page[pages[i]] = (int)(slot_base + unique.size());
		unique.push_back(pages[i]);
	}

	g_textures.resize(slot_base + unique.size(), (IDirect3DTexture9*)0);
	// resize, not assign: HDPageCacheAppend keeps the slots that are
	// already there, and their answer with them.
	g_cutout.resize(g_textures.size(), 0);
	g_origin.resize(g_textures.size(), (short)-1);
	g_cutout_share.resize(g_textures.size(), 0);
	g_class.resize(g_textures.size(), (unsigned char)HD_PAGE_OPAQUE);
	g_alpha_min.resize(g_textures.size(), (unsigned char)255);
	g_page_of_slot.insert(g_page_of_slot.end(), unique.begin(), unique.end());

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
	 * whatever order the threads happen to finish in. The 148 byte header
	 * comes along at the same time, because everything the budget below
	 * decides rests on it. */
	std::vector<PagePlan> plan(unique.size());

	const double resolve_started = now_ms();

	for (size_t i = 0; i < unique.size(); i++)
	{
		PagePlan& page = plan[i];
		int origin = 5;
		bool white = false;

		page.path = resolve_page(root5, bundle, unique[i], &origin, &white);
		page.origin = origin;
		page.white = white;

		char line[256];

		if (page.path.empty())
		{
			std::snprintf(line, sizeof(line),
				"page %s has no .DDS and no WHITE.DDS - four white texels "
				"instead, so the surface shows its baked light", page_text(unique[i]).c_str());
			g_notes.push_back(line);
			page.synth = true;
			page.white = true;
			page.origin = 0;
			continue;
		}

		if (white)
		{
			std::snprintf(line, sizeof(line),
				"page %s is not in any TEX folder - WHITE.DDS instead", page_text(unique[i]).c_str());
			g_notes.push_back(line);
		}
		else if (origin >= HD_PAGE_ORIGIN_MOD)
		{
			std::snprintf(line, sizeof(line),
				"MODLOG page %s comes from mod%d's own TEX folder",
				page_text(unique[i]).c_str(), origin - HD_PAGE_ORIGIN_MOD);
			g_notes.push_back(line);
		}
		else if (HDPageModIndex(unique[i]) >= 0)
		{
			// A mod's page that the mod itself does not ship: it came out of
			// the bundle its mod.txt named with origin=, or out of the
			// install. Said apart from the line above so that a reader can
			// see which of the two answered.
			std::snprintf(line, sizeof(line),
				"MODLOG page %s comes from %s", page_text(unique[i]).c_str(),
				origin >= 1 && origin <= 3
					? (origin == 1 ? "the origin bundle, game 1"
						: origin == 2 ? "the origin bundle, game 2"
						: "the origin bundle, game 3")
					: origin == 4 ? "the install's game 4"
					: "the install's game 5");
			g_notes.push_back(line);
		}
		else if (origin != 5)
		{
			std::snprintf(line, sizeof(line),
				"page %s comes from the sibling game %d, not from 5", page_text(unique[i]).c_str(), origin);
			g_notes.push_back(line);
		}

		if (!page_shape(page.path, &page.w, &page.h, &page.mips))
		{
			// Not a BC7 DDS, or too short to say. The decoder would refuse it
			// as well, so it gets the same white tile a missing file gets.
			std::snprintf(line, sizeof(line),
				"page %s does not carry a readable BC7 header - four white "
				"texels instead", page_text(unique[i]).c_str());
			g_notes.push_back(line);
			page.synth = true;
			page.white = true;
			continue;
		}

		/* The device's own limit first, before the budget gets a say. Eleven
		 * remaster pages are 4096x4096, and a card whose MaxTextureWidth is
		 * 2048 cannot hold one of them at all - the CreateTexture would fail
		 * and the page would be a hole. Starting it one level down is not a
		 * compromise, it is the only picture that card can show. */
		if (g_policy.maxSide > 0)
			while ((page.w >> page.firstMip) > g_policy.maxSide ||
				(page.h >> page.firstMip) > g_policy.maxSide)
			{
				if (usable_levels(page.w, page.h, page.mips, page.firstMip + 1) <= 0)
					break;		// nothing left to give - the decode refuses it

				page.firstMip++;
				page.clamped++;
			}

		if (page.clamped)
		{
			std::snprintf(line, sizeof(line),
				"page %s is %ldx%ld and the device holds %d - it starts %d level(s) down",
				page_text(unique[i]).c_str(), page.w, page.h, g_policy.maxSide, page.clamped);
			g_notes.push_back(line);
		}

		page.full = level_bytes(page.w, page.h, page.mips, page.firstMip, g_format_used);
		page.bytes = page.full;
		page.decodeBytes = level_bytes(page.w, page.h, page.mips, page.firstMip,
			HD_PAGE_FORMAT_RGBA);
	}

	/* --- the budget ----------------------------------------------------
	 *
	 * The rule: never leave a page out - a missing page is a white hole in
	 * the middle of a wall and reads as a rendering fault - but give up the
	 * largest level of the largest pages until what is left fits. A page
	 * that starts at level 1 is half as wide and a quarter as dear, and it
	 * costs nothing anywhere else: the texture coordinates are 0..1 of
	 * whatever the texture is, so the wall is simply softer.
	 *
	 * Biggest first, and repeatedly: one pass takes the top level off every
	 * page that has one to give, in order of what they cost, and if that is
	 * still not enough the next pass takes the next one. The loop ends when
	 * it fits or when no page can give anything more - it cannot spin.
	 *
	 * The budget covers the whole cache, not this call: `have` is what the
	 * rooms already put there when the models arrive. */
	double need = 0;

	for (const PagePlan& page : plan)
		need += page.bytes;

	const double need_full = need;
	const double have = g_bytes;
	const double budget = g_policy.budgetBytes;
	int dropped_pages = 0;
	int dropped_levels = 0;

	if (budget > 0 && have + need > budget)
	{
		std::vector<size_t> order;
		order.reserve(plan.size());

		for (size_t i = 0; i < plan.size(); i++)
			if (plan[i].bytes > 0)
				order.push_back(i);

		bool gave = true;

		while (have + need > budget && gave)
		{
			gave = false;

			/* Stable, and that is not a nicety: 2391 of the remaster's 2406
			 * pages are the same size, so almost every comparison here is a
			 * tie, and an unstable sort would break those ties differently
			 * from one run to the next. The same level with the same budget
			 * would then load a different set of pages at full size each
			 * time, and two screenshots of the same wall would disagree for
			 * no reason anybody could find. Stable means ties keep the
			 * level's own page order. */
			std::stable_sort(order.begin(), order.end(),
				[&plan](size_t a, size_t b)
				{ return plan[a].bytes > plan[b].bytes; });

			for (size_t at : order)
			{
				PagePlan& page = plan[at];

				if (usable_levels(page.w, page.h, page.mips, page.firstMip + 1) <= 0)
					continue;	// its last level - that one stays

				const double before = page.bytes;

				if (!page.firstMip)
					dropped_pages++;

				page.firstMip++;
				dropped_levels++;
				page.bytes = level_bytes(page.w, page.h, page.mips, page.firstMip,
					g_format_used);
				need -= before - page.bytes;
				gave = true;

				if (have + need <= budget)
					break;
			}
		}

		char line[256];

		std::snprintf(line, sizeof(line),
			"budget %.0f MiB: %.0f MiB already on the card and %.0f MiB asked "
			"for - %d page(s) gave up %d level(s), %.0f MiB left to load",
			budget / 1048576.0, have / 1048576.0, need_full / 1048576.0,
			dropped_pages, dropped_levels, need / 1048576.0);
		g_notes.push_back(line);

		if (have + need > budget)
		{
			std::snprintf(line, sizeof(line),
				"budget %.0f MiB is smaller than the smallest the pages go "
				"(%.0f MiB) - loading them anyway rather than leaving holes",
				budget / 1048576.0, (have + need) / 1048576.0);
			g_notes.push_back(line);
		}
	}

	/* The batch. Decoding everything first and uploading afterwards would
	 * hold 376 MiB of pixels at once for TITLE's 220 pages - a large part of
	 * what a 32 bit process can address, and all of it transient. So the
	 * pages go through in batches, and the address space never holds more
	 * than one batch.
	 *
	 * The batch is measured in bytes, not in pages, and that is not a
	 * refinement. The remaster's pages are not one size: 2391 of them are
	 * 512x512 and decode to 1.4 MiB, but eleven are 4096x4096 and decode to
	 * 85 MiB each. A batch of "threads * 2 + 2" pages that happened to catch
	 * several of those would ask for one and a half gigabytes in a process
	 * that has three, and it would do it rarely enough to look like a random
	 * crash. The estimate is the page's own header, read above - and a batch
	 * always holds at least one page, however big.
	 *
	 * It is the A8R8G8B8 size that is budgeted even when the pages end up as
	 * DXT5: the decode goes through the full pixels either way, and it is
	 * the transient peak that has to fit, not the result. */
	const size_t batch_pages = (size_t)threads * 2 + 2;
	const double batch_budget = 96.0 * 1048576.0;

	std::vector<DecodedPage> batch;
	std::atomic<int> next(0);

	double decode_ms = 0, upload_ms = 0, compress_ms = 0;
	double loaded_bytes = 0;
	const double started = now_ms();
	bool oom = false;

	for (size_t base = 0; base < unique.size() && !oom; )
	{
		size_t here = 0;
		double bytes = 0;

		while (base + here < unique.size() && here < batch_pages)
		{
			const double next_bytes = plan[base + here].decodeBytes;

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
			batch[i].slot = (int)(slot_base + base + i);
			batch[i].origin = plan[base + i].origin;
			batch[i].white = plan[base + i].white;
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

				const PagePlan& page = plan[base + (size_t)at];

				if (page.synth || page.path.empty())
					make_white(batch[(size_t)at], g_format_used);
				else
					decode_one(page.path, page.firstMip, g_format_used,
						batch[(size_t)at]);
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

		for (size_t i = 0; i < here; i++)
			compress_ms += batch[i].compressMs;

		const double upload_started = now_ms();
		const D3DFORMAT surface = g_format_used == HD_PAGE_FORMAT_DXT5
			? D3DFMT_DXT5 : D3DFMT_A8R8G8B8;

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
			HRESULT hr = S_OK;

			if (managed)
			{
				hr = g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
					surface, D3DPOOL_MANAGED, &texture, 0);

				if (SUCCEEDED(hr))
					copy_into(texture, page, g_format_used, 0);
				else
					texture = 0;
			}
			else if (ensure_staging(w, h, levels, g_format_used))
			{
				/* D3DPOOL_DEFAULT cannot be locked, so the pixels go into
				 * the one staging texture and from there straight across
				 * the bus. The staging texture is the ONLY system memory
				 * this path ever holds - 1.4 MiB, not 293. */
				copy_into(g_staging, page, g_format_used, 0);

				hr = g_device->CreateTexture((UINT)w, (UINT)h, (UINT)levels, 0,
					surface, D3DPOOL_DEFAULT, &texture, 0);

				if (SUCCEEDED(hr))
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

				if ((size_t)page.slot < g_origin.size())
					g_origin[(size_t)page.slot] = (short)(page.white ? 0 : page.origin);

				if ((size_t)page.slot < g_cutout.size())
				{
					g_cutout[(size_t)page.slot] = page.cutout ? 1 : 0;
					g_cutout_share[(size_t)page.slot] = page.cutoutTexels > 0
						? (short)((page.cutoutLow * 1000) / page.cutoutTexels)
						: (short)0;
				}

				if ((size_t)page.slot < g_class.size())
					g_class[(size_t)page.slot] = (unsigned char)HDPageClassOf(
						page.alphaMin, page.alphaMax);

				// Nothing measured (smallest above largest) reads as flat.
				if ((size_t)page.slot < g_alpha_min.size())
					g_alpha_min[(size_t)page.slot] = page.alphaMin <= page.alphaMax
						? (unsigned char)page.alphaMin : (unsigned char)255;

				for (const DecodedMip& mip : page.mips)
					loaded_bytes += g_format_used == HD_PAGE_FORMAT_DXT5
						? (double)DXT5Size(mip.w, mip.h)
						: (double)mip.w * (double)mip.h * 4.0;

				if (stats)
				{
					/* The two reasons a page can start below its own top
					 * level are counted apart, because they mean different
					 * things to a reader of the log: the budget is a choice
					 * this build made and can be taken back with a bigger
					 * number, the device's size limit is not. */
					const int gave = page.firstMip - plan[base + i].clamped;

					if (gave > 0)
					{
						stats->droppedMip0++;
						stats->droppedLevels += gave;
					}

					if (plan[base + i].clamped)
						stats->clampedByCaps++;

					stats->mipLevels += (int)levels;

					if (w > stats->widest)
						stats->widest = (int)w;

					if (w > 1024 || h > 1024)
						stats->bigPages++;

					if (page.white)
						stats->fromWhite++;
					else if (page.origin >= HD_PAGE_ORIGIN_MOD)
						stats->fromMod++;	// a mod's own TEX
					else if (page.origin == 4)
						stats->fromGame4++;
					else if (page.origin >= 1 && page.origin <= 3)
						stats->fromTr3++;	// the other bundle
					else
						stats->fromGame5++;
				}
			}
			else
			{
				if (stats)
					stats->failed++;

				/* Out of video memory in the middle of a level. Every other
				 * refusal is one page's business - a broken file, a page
				 * that is not BC7 - and the level goes on with a hole where
				 * that page was. This one is not: the card is full, every
				 * page after this would fail the same way, and half a level
				 * in HD is worse than none. Stop here, say why, and let the
				 * caller put the whole level back on classic. */
				if (out_of_memory(hr))
				{
					char line[200];

					/* page_text, not %d: printing the cache key directly
					 * would read "page 1342183535" for a mod's page - exactly
					 * the unreadable line page_text exists to prevent, on the
					 * one path where a player most needs to know which mod
					 * filled his card. */
					std::snprintf(line, sizeof(line),
						"page %s: the card ran out of video memory after "
						"%.0f MiB - the level goes back to classic",
						page_text(page.page).c_str(), loaded_bytes / 1048576.0);
					g_notes.push_back(line);

					if (stats)
						stats->outOfMemory = 1;

					oom = true;

					// Nothing of this batch is uploaded any more, so nothing
					// of it may stay malloc'd either.
					for (size_t k = i; k < here; k++)
						batch[k].free_all();

					break;
				}
			}

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
		// readMs is the path search plus the 148 byte header of every page -
		// the three or four fopen calls that decide which TEX folder a page
		// comes out of and what it will cost. The reading of the bytes
		// themselves sits inside decodeMs, where the threads are.
		stats->readMs = started - resolve_started;
		stats->decodeMs = decode_ms;
		stats->compressMs = compress_ms;
		stats->uploadMs = upload_ms;
		stats->totalMs = now_ms() - resolve_started;

		stats->bytes = loaded_bytes;
		stats->needBytes = need_full;
		stats->budgetBytes = budget;
		stats->haveBytes = have;
		stats->formatUsed = g_format_used;
		stats->cutoutPages = HDPageCacheCutoutPages();	// cut-out pages
	}

	g_bytes += loaded_bytes;

	if (oom)
	{
		*why = "the card ran out of video memory while loading the pages";
		return false;
	}

	return true;
}

bool HDPageCacheCreate(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why)
{
	HDPageCacheDestroy();

	return build_cache(device, installRoot, pages, count, managed, threads,
		false, stats, why);
}

bool HDPageCacheAppend(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why)
{
	return build_cache(device, installRoot, pages, count, managed, threads,
		true, stats, why);
}

void HDPageCacheDestroy()
{
	for (IDirect3DTexture9* texture : g_textures)
		if (texture)
			texture->Release();

	g_textures.clear();
	g_cutout.clear();
	g_cutout_share.clear();
	g_class.clear();
	g_alpha_min.clear();
	g_origin.clear();
	g_page_of_slot.clear();
	g_slot_of_page.clear();
	g_notes.clear();
	// The remembered answers of HDPageCacheKeyOf. Nothing behind them
	// changes at a level end, so this is housekeeping and not correctness -
	// the AltRoot call below is the one that has to clear them.
	g_key_of.clear();

	if (g_staging)
	{
		g_staging->Release();
		g_staging = 0;
	}

	g_staging_w = g_staging_h = g_staging_levels = 0;
	g_staging_format = HD_PAGE_FORMAT_RGBA;
	g_bytes = 0;
	g_device = 0;
}

/* The policy is not reset by Destroy: it is the setting for the run, and a
 * level change must not quietly put a card that was given a budget back on
 * to no budget at all. HDPageCacheConfigure(0) is the way back. */
void HDPageCacheConfigure(const HDPageCachePolicy* policy)
{
	if (policy)
	{
		g_policy = *policy;

		if (g_policy.format != HD_PAGE_FORMAT_DXT5)
			g_policy.format = HD_PAGE_FORMAT_RGBA;

		if (g_policy.budgetBytes < 0)
			g_policy.budgetBytes = 0;

		if (g_policy.maxSide < 0)
			g_policy.maxSide = 0;
	}
	else
	{
		g_policy.format = HD_PAGE_FORMAT_RGBA;
		g_policy.budgetBytes = 0;
		g_policy.maxSide = 0;
	}
}

void HDPageCachePolicyNow(HDPageCachePolicy* out)
{
	if (out)
		*out = g_policy;
}

/* Not reset by Destroy either, and for the policy's reason: it names an
 * INSTALLATION, and where the Tomb Raider I-III Remaster lies does not change
 * because a level ended. */
void HDPageCacheAltRoot(const char* root)
{
	g_alt_root = root ? root : "";
	/* HDPageCacheKeyOf asks the other bundle whether it has a page, so every
	 * answer it remembered was given under the old root. Forget them here
	 * rather than in Destroy, because this is the call that can change
	 * one. */
	g_key_of.clear();
}

double HDPageCacheBytes()
{
	return g_bytes;
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

int HDPageCachePageOfSlot(int slot)
{
	if (slot < 0 || slot >= (int)g_page_of_slot.size())
		return -1;

	return g_page_of_slot[(size_t)slot];
}

bool HDPageCacheSlotCutout(int slot)
{
	if (slot < 0 || slot >= (int)g_cutout.size())
		return false;

	return g_cutout[(size_t)slot] != 0;
}

/* The same two questions resolve_page's mod branch asks, and in the same
 * order - see hd_pagecache.h for why answering them here is a merge and not a
 * guess. */
int HDPageCacheKeyOf(int key)
{
	const int mod = HDPageModIndex(key);

	if (mod < 0)
		return key;

	const std::unordered_map<int, int>::const_iterator seen = g_key_of.find(key);

	if (seen != g_key_of.end())
		return seen->second;

	const int real = HDPageModNumber(key);
	char buffer[300];
	// Neither the mod nor its bundle has it: the install's page, under the
	// install's own key, exactly as any vanilla model would ask for it. That
	// is the answer unless one of the two questions below says otherwise.
	int answer = real;

	if (*HDModsFindPage(mod, real, buffer, sizeof(buffer)))
		answer = key;
	else
	{
		const int from = HDModsOrigin(mod);

		if (from >= 1 && from <= 3 && !g_alt_root.empty())
		{
			int order[3] = { from, 0, 0 };
			int at = 1;

			for (int game = 1; game <= 3; game++)
				if (game != from)
					order[at++] = game;

			for (int i = 0; i < 3; i++)
			{
				path_of_alt(real, order[i], buffer, sizeof(buffer));

				if (file_there(std::string(buffer)))
				{
					answer = key;
					break;
				}
			}
		}
	}

	g_key_of[key] = answer;
	return answer;
}

int HDPageCacheSlotOrigin(int slot)
{
	if (slot < 0 || slot >= (int)g_origin.size())
		return -1;

	return g_origin[(size_t)slot];
}

int HDPageCacheOriginOf(const char* root5, int key)
{
	int origin = 5;
	bool white = false;

	if (!root5 || !*root5)
		return -1;

	const std::string root = root5;

	resolve_page(root, parent_of(root), key, &origin, &white);

	return white ? 0 : origin;
}

const char* HDPageCacheOriginText(int origin)
{
	static char text[48];

	if (origin >= HD_PAGE_ORIGIN_MOD)
	{
		std::snprintf(text, sizeof(text), "mod%d", origin - HD_PAGE_ORIGIN_MOD);
		return text;
	}

	switch (origin)
	{
	case 0: return "WHITE";
	case 1: return "TR1";
	case 2: return "TR2";
	case 3: return "TR3";
	case 4: return "game 4";
	case 5: return "game 5";
	}

	return "-";
}

int HDPageCacheSlotCutoutPermille(int slot)
{
	if (slot < 0 || slot >= (int)g_cutout_share.size())
		return 0;

	return g_cutout_share[(size_t)slot];
}

int HDPageCacheCutoutPages()
{
	int n = 0;

	for (size_t i = 0; i < g_cutout.size(); i++)
		n += g_cutout[i] ? 1 : 0;

	return n;
}

bool HDPageCacheSlotClear(int slot)
{
	if (slot < 0 || slot >= (int)g_class.size())
		return false;

	return g_class[(size_t)slot] == HD_PAGE_CLEAR;
}

int HDPageCacheSlotAlphaMin(int slot)
{
	if (slot < 0 || slot >= (int)g_alpha_min.size())
		return 255;

	return (int)g_alpha_min[(size_t)slot];
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

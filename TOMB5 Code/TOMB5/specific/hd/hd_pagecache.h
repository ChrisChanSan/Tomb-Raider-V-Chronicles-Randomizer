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
#pragma once

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

// The HD texture pages of one level.
//
// One Direct3D texture per .DDS page the level names - no atlas and no array.
// That is a measurement, not a compromise: over 1,590,768 triangles of RICH1
// and ANDREA1 not one triangle spans two pages, and after the sort by
// (room, pass, page) a level needs about 7.6 draw calls per room. Sharing
// pages in an atlas would buy nothing and cost bilinear bleeding and rebased
// texture coordinates.
//
// The pages are BC7, which Direct3D 9 does not know, so every one of them is
// decoded on the processor (specific\bc7.cpp) and uploaded as A8R8G8B8 - a
// deliberate choice: lossless, whatever it costs in video memory. The whole mip
// chain comes along; without it a wall at a distance sparkles.
//
// Engine-free like the rest of specific\hd: it takes a device and a list of
// page numbers, and it says what it did through the numbers and notes below
// instead of writing a log line itself.

/* The policy: three answers to "the card is smaller than the art", set once
 * per level out of the registry and in force until the next call. They are
 * not parameters of Create/Append because they are not properties of one
 * call: a level's rooms and its models go through this cache one after the
 * other and must obey one budget and one format, or the second call would
 * happily spend what the first one saved. */
#define HD_PAGE_FORMAT_RGBA	0	// A8R8G8B8, lossless - the default
#define HD_PAGE_FORMAT_DXT5	1	// BC3 on the processor - a quarter of it

/* A page out of the other bundle.
 *
 * A page number is a file name, <n>.DDS in a TEX folder, and the remaster
 * keeps ONE texture pool per game: page 12 of Tomb Raider III and page 12 of
 * Tomb Raider V are two different pictures with the same number. The four
 * Tomb Raider III guests the randomizer imports bring their own pages, so the
 * cache would hand them the level's page 12 and paint a T-Rex in a Rome wall.
 *
 * So a page from the other bundle is offered with this base added to it. The
 * cache keeps the number as it is - the key is opaque to it - and takes the
 * base off again at the one place a number becomes a file name. The value is
 * far past any page a remaster ships (the largest measured is in the
 * thousands) and far below the end of an int, so nothing else has to change.
 *
 * The alternate root is set once per level with HDPageCacheAltRoot(); without
 * one, a page above the base cannot be found and gets the white tile, which
 * is the same answer as any other missing page. */
#define HD_PAGE_ALT_BASE	0x40000000

/* A page out of a mod.
 *
 * The same problem one step further on. A mod ships its own <n>.DDS files and
 * numbers them as it likes: an Uzi mod may ship 6111 and 6112, and the
 * install has a 6111 and a 6112 of its own that other models wear. One key
 * per number would mean the first of the two to be loaded wins the slot and
 * the other model silently gets the wrong picture - which is exactly the
 * corruption of the vanilla look that must not be risked.
 *
 * So every mod gets a namespace of its own, one stride wide, and a page is
 * offered to the cache as
 *
 *     key = HD_PAGE_MOD_BASE + modIndex * HD_PAGE_MOD_STRIDE + page
 *
 * The stride is 65536, which is the whole page number range the format can
 * express as a file name anybody would write (the largest measured in the
 * installs is 23001). The cache keeps the key opaque, exactly as it does for
 * the alternate bundle, and takes it apart again at the one place a number
 * becomes a file name.
 *
 * The three namespaces must not meet: plain pages are 0..65535, the alternate
 * bundle is HD_PAGE_ALT_BASE + 0..65535, and the mods start above that and
 * end below the end of an int. The static_assert below is the proof, not a
 * hope: change any one of the four numbers and a build that cannot hold them
 * apart fails to compile instead of drawing the wrong texture. */
#define HD_PAGE_MOD_BASE	0x50000000
#define HD_PAGE_MOD_STRIDE	0x00010000	// 65536 - one page number range
#define HD_PAGE_MOD_MAX		64		// namespaces, i.e. mods

static_assert(HD_PAGE_ALT_BASE + HD_PAGE_MOD_STRIDE <= HD_PAGE_MOD_BASE,
	"the alternate bundle's namespace runs into the mods'");
static_assert(HD_PAGE_MOD_BASE > HD_PAGE_MOD_STRIDE,
	"a plain page number could be read as a mod's");
static_assert(HD_PAGE_MOD_MAX > 0 &&
	HD_PAGE_MOD_MAX <= (0x7FFFFFFF - HD_PAGE_MOD_BASE) / HD_PAGE_MOD_STRIDE,
	"that many mod namespaces do not fit below the end of an int");

// The first key of mod `index`, which is what a model's page bias is.
inline int HDPageModBase(int index)
{
	return HD_PAGE_MOD_BASE + index * HD_PAGE_MOD_STRIDE;
}

// Which mod a key belongs to, or -1 for a plain or alternate-bundle page.
inline int HDPageModIndex(int key)
{
	return key >= HD_PAGE_MOD_BASE
		? (key - HD_PAGE_MOD_BASE) / HD_PAGE_MOD_STRIDE : -1;
}

// The page number inside that namespace. Meaningless unless the key is a
// mod's - ask HDPageModIndex first.
inline int HDPageModNumber(int key)
{
	return (key - HD_PAGE_MOD_BASE) % HD_PAGE_MOD_STRIDE;
}

struct HDPageCachePolicy
{
	int format;		// HD_PAGE_FORMAT_*
	double budgetBytes;	// the cap on everything this cache holds; 0 = none
	int maxSide;		// the device's largest texture; 0 = no limit
};

// Sets the policy. A null pointer puts back the defaults (RGBA, no budget, no
// size limit), which is also what the cache starts with, so an engine that
// never calls this gets the unbudgeted, lossless behaviour.
void HDPageCacheConfigure(const HDPageCachePolicy* policy);
void HDPageCachePolicyNow(HDPageCachePolicy* out);

/* Where a page number at or above HD_PAGE_ALT_BASE is looked for: a GAME
 * folder of the other bundle, "<Tomb Raider I-III Remastered>\3", with its
 * siblings 1 and 2 tried after it, where the TR5 bundle tries 4 after 5. A
 * null or empty path (the default) means there is no other bundle, and every
 * such page falls straight through to the white tile. Set once per level,
 * before the models are appended; it survives HDPageCacheDestroy so that a
 * caller does not have to set it twice. */
void HDPageCacheAltRoot(const char* root);

// The texture bytes the cache holds right now - what a second call has to
// budget against.
double HDPageCacheBytes();

struct HDPageCacheStats
{
	int pages;		// page numbers handed in
	int slots;		// distinct pages actually kept (duplicates folded)
	int fromGame5;		// found in <install>\TEX
	int fromGame4;		// found in the sibling 4\TEX (three RICH3 pages)
	int fromTr3;		// found in the other bundle, 3\TEX or its siblings
				// (the Tomb Raider III guests)
	int fromMod;		// found in a mod's own TEX folder
	int fromWhite;		// no file anywhere - WHITE.DDS, the remaster's slot 0
	int failed;		// not even WHITE.DDS - that slot has no texture
	int mipLevels;		// mip levels uploaded over all pages
	int widest;		// widest page seen
	int bigPages;		// pages past 1024 on a side - 85 MiB each at 4096
	double bytes;		// texture bytes this call put on the device
	double readMs;		// wall clock: reading the files
	double decodeMs;	// wall clock: BC7 -> A8R8G8B8, over all threads
	double compressMs;	// wall clock: A8R8G8B8 -> DXT5, 0 in the RGBA case
	double uploadMs;	// wall clock: lock, copy, UpdateTexture
	double totalMs;
	int managed;		// 1 = D3DPOOL_MANAGED, 0 = D3DPOOL_DEFAULT
	int threads;

	/* --- the budget ---------------------------------------------------- */
	double needBytes;	// what this call's pages cost at full size
	double budgetBytes;	// the cap in force, 0 = none
	double haveBytes;	// what the cache already held when this call began
	int formatUsed;		// HD_PAGE_FORMAT_*, after the device had its say
	int droppedMip0;	// pages loaded without their largest level
	int droppedLevels;	// levels given up over all of them
	int clampedByCaps;	// pages shrunk because the device cannot hold them
	int outOfMemory;	// 1 = the device ran out of video memory mid-level

	/* --- cutout pages ---------------------------------------------------- */
	int cutoutPages;	// pages whose alpha really cuts (see the .cpp)
};

// Builds the cache. `installRoot` is the remaster's game 5 folder (the one
// HDInstallRoot() returns), `pages` the .DDS numbers of the level.
//
// managed = false puts the textures in D3DPOOL_DEFAULT and pushes every mip
// through ONE reused system-memory staging texture, so the address space of
// the process never holds more than a single page at a time; managed = true
// lets Direct3D keep its own system-memory copy of everything, which survives
// a device reset but doubles the footprint.
//
// threads is 0 or 1 for one core, otherwise that many decoding threads. The
// decode runs in batches so that only a batch's worth of decoded pages is in
// memory at once, whatever the level's page count.
//
// Refuses and reports through *why if the device cannot make the textures.
// Call HDPageCacheDestroy() before creating another one.
bool HDPageCacheCreate(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why);

// The same, but keeping what is already there. The models
// of a level name their own pages and a good many of them are pages the rooms
// already wear - a level's item pages and room pages overlap - so the models
// are ADDED to the cache rather than given a cache of their own: one texture
// per page for the whole level, one slot number, one place that frees them.
// Pages already in the cache are skipped silently; `stats` counts only what
// this call brought in. Creates the cache when there is none yet, which is
// what the diagnostic tool does when it draws a model with no level behind it.
bool HDPageCacheAppend(IDirect3DDevice9* device, const char* installRoot,
	const int* pages, int count, bool managed, int threads,
	HDPageCacheStats* stats, const char** why);

void HDPageCacheDestroy();

// The texture of a page number, or null when the level never named it.
IDirect3DTexture9* HDPageCacheTexture(int page);

// The same by slot, 0..HDPageCacheSlots()-1, which is the order the pages
// were handed in with the duplicates folded out. hd_render keeps slot numbers
// in its ranges so that a draw call is one array lookup, not a search.
int HDPageCacheSlots();
int HDPageCacheSlotOf(int page);			// -1 when unknown
int HDPageCachePageOfSlot(int slot);			// -1 when out of range
IDirect3DTexture9* HDPageCacheSlotTexture(int slot);
/* Is the page in that slot a cut-out sheet? True when at least 2 % of its
 * top mip's texels have an alpha below 128 - a sheet of leaves, a grating, a
 * net. False for every ordinary page, whose alpha channel is a flat 255 or a
 * gloss mask, and for a slot that has no texture.
 *
 * The room renderer switches the alpha test on for the opaque pass of a draw
 * call that lands on such a page, and only there: the remaster puts its
 * vegetation (material 8, and materials 3, 6 and 17 with it) in the opaque
 * pass, and without the test the kelp of JOBY4 and the trees of ANDY1 draw as
 * solid green rectangles. Measured per page rather than per material so that
 * a page with nothing to cut cannot lose a pixel - see hd_pagecache.cpp. */
bool HDPageCacheSlotCutout(int slot);
// The measured hole share of that page, in per mille of the texels looked
// at. The bool above is this against HD_CUTOUT_PERMILLE; the number is for
// the diagnostic tools.
int HDPageCacheSlotCutoutPermille(int slot);
// How many of the cache's slots answered true - for the level's log line.
int HDPageCacheCutoutPages();
/* Are the texels of the page in that slot that lie below the reference holes
 * rather than a mask - HDPageHolesAreCoverage (hd_model_cut.h) on the same
 * sample? The object path turns the alpha test on for a cut-out material only
 * on such a page. False for a page with nothing below the reference and for a
 * slot with no texture. */
bool HDPageCacheSlotHoles(int slot);
// The share of those texels at alpha 0, in per mille - for the tools.
int HDPageCacheSlotZeroPermille(int slot);
/* Is the page in that slot part-way clear in every texel of the top mip it
 * keeps - HD_PAGE_CLEAR out of HDPageClassOf (hd_pageclass.h)? False for a
 * slot that has no texture. The room renderer draws the alpha-pass triangles
 * on such a page blended instead of alpha-tested: the RICH2 marble floors,
 * through which the copy of the room beneath them shows. */
bool HDPageCacheSlotClear(int slot);
/* The smallest alpha of that page's top mip, 0..255 - what HDPageClassOf
 * reads. The room renderer takes a page below HD_GLOSS_MASK_BELOW
 * (hd_gloss_math.h) to carry a gloss mask in its alpha. 255 for a slot that
 * has no texture or was not measured. */
int HDPageCacheSlotAlphaMin(int slot);

/* Where that slot's .DDS was found: 0 for the white tile, 1..3 for a game
 * folder of the Tomb Raider I-III bundle, 4 or 5 for the IV-VI games, 100 +
 * the mod's index for a mod's own TEX folder, and -1 for a slot that has no
 * texture. HDPageCacheOriginText turns one into a word for a log or a tool;
 * the pointer it returns is a static buffer, so print it before asking
 * again. */
int HDPageCacheSlotOrigin(int slot);
const char* HDPageCacheOriginText(int origin);

/* The same answer before the page is loaded - the very chain the append
 * walks, asked about one key and loading nothing at all. `root5` is the
 * game 5 folder, as HDPageCacheAppend takes it.
 *
 * It exists for a decision that has to be made while a model is still being
 * read: whether a mod's hand model wears another bundle's skin, which is what
 * puts the install's own hands under the mod's gun (hd_model.cpp, the twin).
 * Asking resolve_page rather than keeping a copy of its chain is the whole
 * point - a second copy of that order is how a page rule elsewhere could go
 * out of step with the cache without anyone noticing. */
int HDPageCacheOriginOf(const char* root5, int key);

/* The key a page really belongs under.
 *
 * Every mod has a namespace of its own, so that a mod's 6111 and the
 * install's 6111 cannot be the same cache slot (see HD_PAGE_MOD_BASE above).
 * The cost that leaves: a page a mod's model asks for but the mod does not
 * ship falls through to the install, and the install's file is then decoded
 * a second time under the mod's key. Measured on ANDREA1's wardrobe with
 * four gun and outfit mods installed: 25 slots and 33.3 MiB against 21 and
 * 28.0 without them, and one of the four extra slots was the install's 8001
 * held twice.
 *
 * This folds exactly that case and no other: a mod key whose mod has no such
 * .DDS and whose origin bundle has none either becomes the plain number. The
 * merge is safe by construction: in that case resolve_page itself does
 * `page = real` and runs the ordinary chain, so the two keys open the same
 * file, byte for byte, and everything the cache measures on it (the cutout
 * share, the format, the size) is measured on the same bytes. A mod key
 * whose page the mod or its bundle does have is left alone.
 *
 * A plain or alternate-bundle key comes back unchanged. Needs the mods
 * scanned and the alternate root set, which is what every caller has done
 * before it loads a model.
 *
 * The answer is remembered per key, because working it out costs up to five
 * file system probes and a caller on a hot path would otherwise pay them
 * again for every triangle. What it rests on - the mods scan and the
 * alternate root - is fixed for a level, and the memory is dropped in
 * HDPageCacheAltRoot and HDPageCacheDestroy. A caller that re-scans the mods
 * (HDModsForget) mid-session has to call HDPageCacheAltRoot again before it
 * asks; nothing in the engine does either. */
int HDPageCacheKeyOf(int key);

// Everything that was not found where it should have been, in words, for the
// caller's log. Valid until the next Create or Destroy.
int HDPageCacheNoteCount();
const char* HDPageCacheNote(int at);

/* The pool of the HD level load: the page decode, the room build's vertex
 * buffer, its index fill per group, its mirror search and the build's
 * fingerprint run their independent items on it. In the header, whole, so
 * that the suite tests it as the engine runs it (ModernTest, load_pool).
 *
 * HDPoolThreads turns a wish into a thread count: 1 or more is taken as it
 * is, 0 or less is the machine's own (std::thread::hardware_concurrency,
 * 1 when the library cannot tell), and the answer is never above 16, which
 * keeps the page decode's transient batches (2 pages per thread) inside the
 * 32-bit address space.
 *
 * HDPoolRun calls work(i) once for every i in [0, count) on up to `threads`
 * threads, the calling thread one of them, and returns when every item is
 * done; no thread outlives the call. Items are handed out in index order,
 * but run in any order and at the same time, so an item may write only
 * what is its own (its slot of an output array) and read only what nothing
 * writes during the call. The first exception an item throws is thrown
 * again on the calling thread after the join, and no item starts after it.
 * threads 1 runs everything on the calling thread in index order. */
inline int HDPoolThreads(int asked)
{
	int threads = asked;

	if (threads <= 0)
		threads = (int)std::thread::hardware_concurrency();

	if (threads < 1)
		threads = 1;

	if (threads > 16)
		threads = 16;

	return threads;
}

template <class Work> void HDPoolRun(int count, int threads, const Work& work)
{
	if (count <= 0)
		return;

	if (threads > count)
		threads = count;

	if (threads <= 1)
	{
		for (int at = 0; at < count; at++)
			work(at);

		return;
	}

	std::atomic<int> next(0);
	std::atomic<bool> failed(false);
	std::exception_ptr first;
	std::mutex guard;

	// Every thread takes the next item until none is left or one has thrown.
	const auto run = [&]()
	{
		for (;;)
		{
			const int at = next.fetch_add(1);

			if (at >= count || failed.load())
				break;

			try
			{
				work(at);
			}
			catch (...)
			{
				std::lock_guard<std::mutex> lock(guard);

				if (!first)
					first = std::current_exception();

				failed.store(true);
			}
		}
	};

	std::vector<std::thread> helpers;
	helpers.reserve((size_t)threads - 1);

	for (int i = 1; i < threads; i++)
		helpers.push_back(std::thread(run));

	run();

	for (std::thread& t : helpers)
		t.join();

	if (first)
		std::rethrow_exception(first);
}

/* FNV-1a 64 over bytes, and over a value's bytes - the build's fingerprint
 * (HDRenderBuildHash). Every value fed in is a field or a struct without
 * padding, so two equal builds give equal bytes; list() feeds the count
 * first, as 4 bytes, then the elements. */
struct HDFnv64
{
	unsigned long long h = 14695981039346656037ull;

	void add(const void* data, size_t bytes)
	{
		const unsigned char* p = (const unsigned char*)data;

		for (size_t i = 0; i < bytes; i++)
			h = (h ^ p[i]) * 1099511628211ull;
	}

	template <class T> void put(const T& value)
	{
		add(&value, sizeof(value));
	}

	template <class T> void list(const std::vector<T>& values)
	{
		put((unsigned int)values.size());

		if (!values.empty())
			add(&values[0], values.size() * sizeof(T));
	}
};

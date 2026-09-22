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
#include "hd_mods.h"
#include "hd_pagecache.h"	// HD_PAGE_MOD_MAX - the real cap on the count

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// The header says what this is and why. What follows is only how.

/* One mod, one page namespace. The two caps are written down in two headers
 * because the two halves are independent of each other - this file knows
 * nothing about textures and hd_pagecache.h knows nothing about folders - and
 * a build in which they disagree would either lose mods silently or hand the
 * cache a key out of the next mod's range. */
static_assert(HD_MODS_MAX == HD_PAGE_MOD_MAX,
	"the mod cap and the page namespace cap have to be the same number");

namespace fs = std::filesystem;

namespace
{
	struct Wav
	{
		int number;
		int mod;
		std::string path;
	};

	/* One OUTFIT_*.TRM a mod ships. Kept by the scan rather
	 * than looked for again, because the wardrobe wants the LIST and the
	 * resolver answers only "where is the file called X" - and a mod's outfit
	 * is an ADDITIONAL entry in the wardrobe, never a replacement of the
	 * install's file of the same name, so there is no name to ask about. */
	struct Outfit
	{
		std::string stem;	// "OUTFIT_CLASSIC", upper case
		std::string path;	// absolute
		int game;		// 5 or 4 - which of the mod's folders it sat in
	};

	struct Mod
	{
		std::string path;	// absolute, the mod folder itself
		std::string name;	// relative to Mods, with backslashes
		std::string sort;	// `name` lower-cased - the order key
		int origin = 0;		// mod.txt origin=tr1|tr2|tr3, 0 = none
		std::string bank;	// 5\DATA\MAIN.SFX, empty when there is none
		HDModShips ships = { 0, 0, 0, 0, 0 };
		std::vector<Outfit> outfits;
	};

	bool scanned = false;
	std::string folder_wanted;	// what HDModsFolder was given
	std::string folder_used;	// what the scan actually looked at

	std::vector<Mod> mods;
	std::vector<Wav> wavs;

	/* The loose lists: absolute paths in the same order the mods are
	 * in. A .txt that lies BESIDE the mod folders and not inside one; what is
	 * in it is the wardrobe's business and none of this file's. */
	std::vector<std::string> lists;

	std::vector<std::string> notes;
	size_t notes_flushed = 0;
	void (*note_sink)(const char*) = 0;

	/* Every model this session has already reported, as "<NAME>/<mod>". The
	 * resolver is asked once per level and the answer never changes, so the
	 * log gets one line per replaced model for the whole run rather than one
	 * per level.
	 *
	 * KEYED BY THE PAIR, not by the name alone:
	 * the same name can be answered by two different mods in two different
	 * calls - once for whoever asks first and once for a caller that wants
	 * one particular mod's copy - and a key of the name alone would let the
	 * first line stand for the second one as well. */
	std::vector<std::string> reported;

	void note(const char* format, ...)
	{
		char line[512];
		va_list args;

		va_start(args, format);
		std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		notes.push_back(line);

		if (note_sink)
			while (notes_flushed < notes.size())
				note_sink(notes[notes_flushed++].c_str());
	}

	std::string lower(const std::string& in)
	{
		std::string out = in;

		for (size_t i = 0; i < out.size(); i++)
			if (out[i] >= 'A' && out[i] <= 'Z')
				out[i] = (char)(out[i] - 'A' + 'a');

		return out;
	}

	std::string upper(const std::string& in)
	{
		std::string out = in;

		for (size_t i = 0; i < out.size(); i++)
			if (out[i] >= 'a' && out[i] <= 'z')
				out[i] = (char)(out[i] - 'a' + 'A');

		return out;
	}

	std::string join(const std::string& a, const std::string& b)
	{
		std::string out = a;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		if (!out.empty())
			out += '\\';

		out += b;
		return out;
	}

	bool file_there(const std::string& path)
	{
		std::error_code ec;
		return fs::is_regular_file(fs::u8path(path), ec);
	}

	bool folder_there(const std::string& path)
	{
		std::error_code ec;
		return fs::is_directory(fs::u8path(path), ec);
	}

	// A name beginning with "_" is switched off, at every depth. That is the
	// whole mechanism for "give me vanilla back without deleting anything".
	bool disabled(const std::string& name)
	{
		return !name.empty() && name[0] == '_';
	}

	std::string trim(const std::string& in)
	{
		size_t a = 0, b = in.size();

		while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r'))
			a++;

		while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r'))
			b--;

		return in.substr(a, b - a);
	}

	// Counts the files with that extension directly in `folder`, and nothing
	// deeper: a mod's ITEM and TEX folders are flat in the remaster's layout,
	// and the PNGs some modders keep in a subfolder beside their .DDS are not
	// art this engine can read.
	int count_files(const std::string& folder, const char* ext, Mod* mod, int game)
	{
		std::error_code ec;
		fs::directory_iterator it(fs::u8path(folder), ec), end;
		int count = 0;

		if (ec)
			return 0;

		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;

			if (!it->is_regular_file(ec) || ec)
				continue;

			const std::string leaf = it->path().filename().string();
			const std::string low = lower(leaf);

			if (low.size() <= std::strlen(ext) ||
				low.compare(low.size() - std::strlen(ext), std::strlen(ext), ext) != 0)
				continue;

			count++;

			/* The outfits by name, not only by count. The
			 * 5 folder is walked before the 4 one, so a mod that ships the
			 * same outfit in both is listed once, out of 5 - the order the
			 * model resolver uses for everything else. */
			if (mod && low.compare(0, 7, "outfit_") == 0)
			{
				Outfit one;

				one.stem = upper(leaf.substr(0, leaf.size() - std::strlen(ext)));
				one.path = it->path().string();
				one.game = game;

				bool twice = false;

				for (size_t k = 0; k < mod->outfits.size(); k++)
					if (mod->outfits[k].stem == one.stem)
						twice = true;

				if (!twice)
				{
					mod->outfits.push_back(one);
					mod->ships.outfits++;
				}
			}
		}

		return count;
	}

	/* mod.txt: one key=value per line, "#" starts a comment. The only key
	 * this build knows is origin. An unknown key is a note and never a
	 * fault - a mod written for a later build has to stay loadable by this
	 * one. */
	void read_mod_txt(Mod& mod, int index)
	{
		const std::string path = join(mod.path, "mod.txt");

		if (!file_there(path))
			return;

		std::FILE* file = std::fopen(path.c_str(), "rb");

		if (!file)
		{
			note("MODLOG mod%d %s: mod.txt could not be opened - ignored",
				index, mod.name.c_str());
			return;
		}

		char raw[512];

		while (std::fgets(raw, sizeof(raw), file))
		{
			std::string line = raw;

			const size_t hash = line.find('#');

			if (hash != std::string::npos)
				line.erase(hash);

			while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r'))
				line.erase(line.size() - 1);

			line = trim(line);

			if (line.empty())
				continue;

			const size_t eq = line.find('=');

			if (eq == std::string::npos)
			{
				note("MODLOG mod%d %s: mod.txt line \"%s\" has no = - ignored",
					index, mod.name.c_str(), line.c_str());
				continue;
			}

			const std::string key = lower(trim(line.substr(0, eq)));
			const std::string value = lower(trim(line.substr(eq + 1)));

			if (key == "origin")
			{
				if (value == "tr1")
					mod.origin = 1;
				else if (value == "tr2")
					mod.origin = 2;
				else if (value == "tr3")
					mod.origin = 3;
				else
					note("MODLOG mod%d %s: mod.txt origin=%s is not tr1, tr2 "
						"or tr3 - ignored", index, mod.name.c_str(), value.c_str());
			}
			else
				note("MODLOG mod%d %s: mod.txt key \"%s\" is not one this "
					"build knows - ignored", index, mod.name.c_str(), key.c_str());
		}

		std::fclose(file);
	}

	// <number>.wav with EXACTLY four digits, case-insensitive extension.
	// Four digits and not "any number" because the name has to be unmistakable:
	// a modder's "12.wav" is far more likely to be a track than bank sample 12.
	bool wav_number(const std::string& leaf, int* number)
	{
		if (leaf.size() != 8)
			return false;

		for (int i = 0; i < 4; i++)
			if (leaf[i] < '0' || leaf[i] > '9')
				return false;

		if (lower(leaf.substr(4)) != ".wav")
			return false;

		*number = (leaf[0] - '0') * 1000 + (leaf[1] - '0') * 100 +
			(leaf[2] - '0') * 10 + (leaf[3] - '0');
		return true;
	}

	// The loose samples of one mod, at any depth. The depth cap is a guard
	// against a folder that walks into itself through a junction, not a
	// statement about how deep a mod may be.
	void collect_wavs(Mod& mod, int index)
	{
		std::error_code ec;
		fs::recursive_directory_iterator it(fs::u8path(mod.path),
			fs::directory_options::skip_permission_denied, ec), end;

		if (ec)
			return;

		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;

			const std::string leaf = it->path().filename().string();

			if (it->is_directory(ec) && !ec)
			{
				if (disabled(leaf) || it.depth() >= 8)
					it.disable_recursion_pending();

				continue;
			}

			int number = 0;

			if (!it->is_regular_file(ec) || ec || !wav_number(leaf, &number))
				continue;

			bool taken = false;

			for (size_t i = 0; i < wavs.size(); i++)
				if (wavs[i].number == number)
				{
					taken = true;
					break;
				}

			if (taken)
			{
				note("MODLOG mod%d %s: %s is already covered by an earlier "
					"mod - ignored", index, mod.name.c_str(), leaf.c_str());
				continue;
			}

			Wav one;

			one.number = number;
			one.mod = index;
			one.path = it->path().string();
			wavs.push_back(one);
			mod.ships.wavs++;
		}
	}

	// Is this folder a mod? The remaster's own layout says so: a game folder
	// under it, or a mod.txt that says something about it.
	bool is_mod(const std::string& path)
	{
		return folder_there(join(path, "5")) || folder_there(join(path, "4")) ||
			file_there(join(path, "mod.txt"));
	}

	void gather(const std::string& root, const std::string& rel, int depth,
		std::vector<Mod>& found)
	{
		std::error_code ec;
		fs::directory_iterator it(fs::u8path(rel.empty() ? root : join(root, rel)), ec), end;

		if (ec)
			return;

		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;

			const std::string leaf = it->path().filename().string();

			/* A loose .txt is a list, not a mod. It lies beside the mod
			 * folders - Mods\outfit.txt, Mods\Outfits\outfit_1-5.txt - and the
			 * wardrobe reads it for the outfits that have no mod folder of
			 * their own and therefore no outfit.txt to be named in.
			 *
			 * Collected here because this is the one walk that knows which
			 * folder is a mod: it never steps INTO one, so a mod's own mod.txt
			 * or outfit.txt can never be picked up as a list. The leading "_"
			 * switches a list off exactly as it switches a mod off. */
			if (it->is_regular_file(ec) && !ec)
			{
				const std::string low = lower(leaf);

				if (low.size() > 4 && low.compare(low.size() - 4, 4, ".txt") == 0 &&
					!disabled(leaf))
					lists.push_back(it->path().string());

				continue;
			}

			if (!it->is_directory(ec) || ec)
				continue;

			const std::string here = rel.empty() ? leaf : join(rel, leaf);

			if (disabled(leaf))
			{
				note("MODLOG disabled %s - the leading _ switches it off", here.c_str());
				continue;
			}

			if (is_mod(it->path().string()))
			{
				Mod one;

				one.path = it->path().string();
				one.name = here;
				one.sort = lower(here);
				found.push_back(one);
				continue;
			}

			// Not a mod itself: a category folder, and the mods are one level
			// down. Only one level - a deeper tree is a modder's own filing
			// and nothing this engine has to understand.
			if (depth == 0)
				gather(root, here, 1, found);
		}
	}

	void scan()
	{
		scanned = true;

		folder_used = folder_wanted.empty() ? std::string("Mods") : folder_wanted;

		if (!folder_there(folder_used))
		{
			/* mods=0 is written out rather than left to be inferred: without
			 * it, a reader counting mods= across a fleet of logs finds no
			 * line at all for a machine with no Mods folder - the one case
			 * that most needs to be told apart from a missing log. Same
			 * shape in both branches. */
			note("MODLOG folder=%s present=no mods=0 lists=0 - nothing is overlaid",
				folder_used.c_str());
			return;
		}

		std::vector<Mod> found;

		lists.clear();
		gather(folder_used, std::string(), 0, found);

		std::sort(found.begin(), found.end(),
			[](const Mod& a, const Mod& b) { return a.sort < b.sort; });

		// The file system's own order is not a promise and a list that
		// overrides another has to do it in a written-down order.
		std::sort(lists.begin(), lists.end(),
			[](const std::string& a, const std::string& b)
			{ return lower(a) < lower(b); });

		if ((int)found.size() > HD_PAGE_MOD_MAX)
		{
			note("MODLOG folder=%s has %d mods and this build carries %d - "
				"the rest are ignored", folder_used.c_str(),
				(int)found.size(), HD_PAGE_MOD_MAX);
			found.resize((size_t)HD_PAGE_MOD_MAX);
		}

		/* `lists=` is the loose .txt count: a file the wardrobe may read
		 * and this file may not, so the one line that says the folder was
		 * walked says whether it was seen at all. */
		note("MODLOG folder=%s present=yes mods=%d lists=%d", folder_used.c_str(),
			(int)found.size(), (int)lists.size());

		for (size_t i = 0; i < lists.size(); i++)
			note("MODLOG list%d %s - beside the mod folders, so the wardrobe "
				"reads it", (int)i, lists[i].c_str());

		mods = found;

		for (size_t i = 0; i < mods.size(); i++)
		{
			Mod& mod = mods[i];
			const int index = (int)i;

			read_mod_txt(mod, index);

			for (int game = 5; game >= 4; game--)
			{
				const std::string gf = join(mod.path, game == 5 ? "5" : "4");

				mod.ships.models += count_files(join(gf, "ITEM"), ".trm", &mod, game);
				mod.ships.pages += count_files(join(gf, "TEX"), ".dds", 0, game);
			}

			const std::string bank = join(join(join(mod.path, "5"), "DATA"), "MAIN.SFX");

			if (file_there(bank))
			{
				mod.bank = bank;
				mod.ships.bank = 1;
			}

			collect_wavs(mod, index);

			const char* origin = mod.origin == 1 ? "tr1"
				: mod.origin == 2 ? "tr2"
				: mod.origin == 3 ? "tr3" : "none";

			note("MODLOG mod%d path=%s origin=%s models=%d pages=%d bank=%s wavs=%d",
				index, mod.name.c_str(), origin, mod.ships.models,
				mod.ships.pages, mod.ships.bank ? "yes" : "no", mod.ships.wavs);

			/* The wardrobe takes them from here. They are
			 * still refused by HDModsFindModel - a mod's outfit is an extra
			 * entry in the wardrobe and never a replacement of the install's
			 * file of the same name - so the line says where they went. */
			for (size_t k = 0; k < mod.outfits.size(); k++)
				note("MODLOG mod%d %s ships outfit %s (game %d) - a wardrobe "
					"entry of its own, not a replacement",
					index, mod.name.c_str(), mod.outfits[k].stem.c_str(),
					mod.outfits[k].game);
		}
	}

	void need_scan()
	{
		if (!scanned)
			scan();
	}
}

void HDModsFolder(const char* path)
{
	folder_wanted = path ? path : "";
}

void HDModsScan()
{
	need_scan();
}

int HDModsCount()
{
	need_scan();
	return (int)mods.size();
}

const char* HDModsPath(int index)
{
	need_scan();
	return index >= 0 && index < (int)mods.size() ? mods[(size_t)index].path.c_str() : "";
}

const char* HDModsName(int index)
{
	need_scan();
	return index >= 0 && index < (int)mods.size() ? mods[(size_t)index].name.c_str() : "";
}

int HDModsOrigin(int index)
{
	need_scan();
	return index >= 0 && index < (int)mods.size() ? mods[(size_t)index].origin : 0;
}

/* The outfits a mod ships. Not a resolver question - the
 * wardrobe wants the list and there is no name to ask about, because a mod's
 * outfit stands beside the install's file of the same name rather than in
 * place of it. */
int HDModsOutfitCount(int index)
{
	need_scan();
	return index >= 0 && index < (int)mods.size()
		? (int)mods[(size_t)index].outfits.size() : 0;
}

namespace
{
	const Outfit* outfit_at(int index, int at)
	{
		need_scan();

		if (index < 0 || index >= (int)mods.size())
			return 0;

		const std::vector<Outfit>& list = mods[(size_t)index].outfits;

		return at >= 0 && at < (int)list.size() ? &list[(size_t)at] : 0;
	}
}

const char* HDModsOutfitName(int index, int at)
{
	const Outfit* one = outfit_at(index, at);

	return one ? one->stem.c_str() : "";
}

const char* HDModsOutfitPath(int index, int at)
{
	const Outfit* one = outfit_at(index, at);

	return one ? one->path.c_str() : "";
}

int HDModsOutfitGame(int index, int at)
{
	const Outfit* one = outfit_at(index, at);

	return one ? one->game : 0;
}

void HDModsShipsOf(int index, HDModShips* out)
{
	need_scan();

	if (!out)
		return;

	if (index < 0 || index >= (int)mods.size())
	{
		std::memset(out, 0, sizeof(*out));
		return;
	}

	*out = mods[(size_t)index].ships;
}

const char* HDModsFindModel(const char* name, int* modIndex, char* buffer, int size,
	int wantMod)
{
	if (modIndex)
		*modIndex = -1;

	if (!buffer || size <= 0)
		return "";

	buffer[0] = 0;

	if (!name || !*name)
		return buffer;

	need_scan();

	if (mods.empty())
		return buffer;

	const std::string upper_name = upper(name);

	/* THE ONE NAME THIS RESOLVER REFUSES. A mod's outfit is an ENTRY OF ITS
	 * OWN in the wardrobe scan
	 * (hd_wardrobe.h), which the player picks from; it is never a replacement
	 * of the install's file of the same name, so a level that asks for
	 * OUTFIT_CLASSIC by name goes on getting the install's OUTFIT_CLASSIC. The
	 * scan finds a mod's outfits through HDModsOutfitCount and its two
	 * neighbours, which is a different door and deliberately so.
	 *
	 * Refused here rather than at the scan so that the refusal is the same
	 * wherever the name comes from - the wardrobe, a slot, a tool. */
	if (upper_name.compare(0, 7, "OUTFIT_") == 0)
		return buffer;

	for (size_t i = 0; i < mods.size(); i++)
	{
		/* ONE PARTICULAR MOD, WHEN THE CALLER WANTS ONE. A face is looked up
		 * with the body's mod named, because a head from one mod on a body
		 * from another is two namespaces on one model. The filter belongs
		 * here, in the search, and not in a check the caller applies to the
		 * answer afterwards: applying it after the note below is written
		 * would let the log claim a replacement that was never drawn, once
		 * and permanently. */
		if (wantMod >= 0 && (int)i != wantMod)
			continue;

		for (int game = 5; game >= 4; game--)
		{
			const std::string path = join(join(join(mods[i].path,
				game == 5 ? "5" : "4"), "ITEM"), upper_name + ".TRM");

			if (!file_there(path))
				continue;

			if (modIndex)
				*modIndex = (int)i;

			std::snprintf(buffer, (size_t)size, "%s", path.c_str());

			// One line per replaced model for the whole session, not one per
			// level: the answer cannot change while the game runs.
			const std::string key = upper_name + "/" + std::to_string(i);
			bool said = false;

			for (size_t r = 0; r < reported.size(); r++)
				if (reported[r] == key)
				{
					said = true;
					break;
				}

			if (!said)
			{
				reported.push_back(key);
				note("MODLOG model %s <- mod%d %s (game %d)", upper_name.c_str(),
					(int)i, mods[i].name.c_str(), game);
			}

			return buffer;
		}
	}

	return buffer;
}

const char* HDModsFindPage(int modIndex, int page, char* buffer, int size)
{
	if (!buffer || size <= 0)
		return "";

	buffer[0] = 0;

	need_scan();

	if (modIndex < 0 || modIndex >= (int)mods.size())
		return buffer;

	char leaf[64];

	std::snprintf(leaf, sizeof(leaf), "%d.DDS", page);

	for (int game = 5; game >= 4; game--)
	{
		const std::string path = join(join(join(mods[(size_t)modIndex].path,
			game == 5 ? "5" : "4"), "TEX"), leaf);

		if (!file_there(path))
			continue;

		std::snprintf(buffer, (size_t)size, "%s", path.c_str());
		return buffer;
	}

	return buffer;
}

const char* HDModsBank(int index)
{
	need_scan();
	return index >= 0 && index < (int)mods.size() ? mods[(size_t)index].bank.c_str() : "";
}

const char* HDModsWav(int number, int* modIndex, char* buffer, int size)
{
	if (modIndex)
		*modIndex = -1;

	if (!buffer || size <= 0)
		return "";

	buffer[0] = 0;

	need_scan();

	for (size_t i = 0; i < wavs.size(); i++)
		if (wavs[i].number == number)
		{
			if (modIndex)
				*modIndex = wavs[i].mod;

			std::snprintf(buffer, (size_t)size, "%s", wavs[i].path.c_str());
			return buffer;
		}

	return buffer;
}

void HDModsLogSink(void (*sink)(const char* line))
{
	note_sink = sink;

	if (!note_sink)
		return;

	while (notes_flushed < notes.size())
		note_sink(notes[notes_flushed++].c_str());
}

int HDModsNoteCount()
{
	need_scan();
	return (int)notes.size();
}

const char* HDModsNote(int at)
{
	need_scan();
	return at >= 0 && at < (int)notes.size() ? notes[(size_t)at].c_str() : "";
}

int HDModsListCount()
{
	need_scan();
	return (int)lists.size();
}

const char* HDModsListPath(int at)
{
	need_scan();

	return at >= 0 && at < (int)lists.size() ? lists[(size_t)at].c_str() : "";
}

void HDModsForget()
{
	scanned = false;
	mods.clear();
	wavs.clear();
	lists.clear();
	notes.clear();
	reported.clear();
	notes_flushed = 0;
	folder_used.clear();
}

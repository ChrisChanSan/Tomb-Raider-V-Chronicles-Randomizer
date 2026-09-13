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

	struct Mod
	{
		std::string path;	// absolute, the mod folder itself
		std::string name;	// relative to Mods, with backslashes
		std::string sort;	// `name` lower-cased - the order key
		int origin = 0;		// mod.txt origin=tr1|tr2|tr3, 0 = none
		std::string bank;	// 5\DATA\MAIN.SFX, empty when there is none
		HDModShips ships = { 0, 0, 0, 0, 0 };
	};

	bool scanned = false;
	std::string folder_wanted;	// what HDModsFolder was given
	std::string folder_used;	// what the scan actually looked at

	std::vector<Mod> mods;
	std::vector<Wav> wavs;

	std::vector<std::string> notes;
	size_t notes_flushed = 0;
	void (*note_sink)(const char*) = 0;

	/* Every model name this session has already reported. The resolver is
	 * asked once per level and the answer never changes, so the log gets one
	 * line per replaced model for the whole run rather than one per level. */
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
	int count_files(const std::string& folder, const char* ext, int* outfits)
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

			if (outfits && low.compare(0, 7, "outfit_") == 0)
				(*outfits)++;
		}

		return count;
	}

	/* mod.txt: one key=value per line, "#" starts a comment. The only key
	 * round M1 knows is origin. An unknown key is a note and never a fault -
	 * a mod written for a later round has to stay loadable by this one. */
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

			if (!it->is_directory(ec) || ec)
				continue;

			const std::string leaf = it->path().filename().string();
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
			note("MODLOG folder=%s present=no - nothing is overlaid",
				folder_used.c_str());
			return;
		}

		std::vector<Mod> found;

		gather(folder_used, std::string(), 0, found);

		std::sort(found.begin(), found.end(),
			[](const Mod& a, const Mod& b) { return a.sort < b.sort; });

		if ((int)found.size() > HD_PAGE_MOD_MAX)
		{
			note("MODLOG folder=%s has %d mods and this build carries %d - "
				"the rest are ignored", folder_used.c_str(),
				(int)found.size(), HD_PAGE_MOD_MAX);
			found.resize((size_t)HD_PAGE_MOD_MAX);
		}

		note("MODLOG folder=%s present=yes mods=%d", folder_used.c_str(),
			(int)found.size());

		mods = found;

		for (size_t i = 0; i < mods.size(); i++)
		{
			Mod& mod = mods[i];
			const int index = (int)i;

			read_mod_txt(mod, index);

			for (int game = 5; game >= 4; game--)
			{
				const std::string gf = join(mod.path, game == 5 ? "5" : "4");

				mod.ships.models += count_files(join(gf, "ITEM"), ".trm", &mod.ships.outfits);
				mod.ships.pages += count_files(join(gf, "TEX"), ".dds", 0);
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

			if (mod.ships.outfits)
				note("MODLOG mod%d %s ships %d OUTFIT_*.TRM - outfit - round M2",
					index, mod.name.c_str(), mod.ships.outfits);
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

const char* HDModsFindModel(const char* name, int* modIndex, char* buffer, int size)
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

	/* THE ONE NAME ROUND M1 REFUSES. See the header: Lara's composite is a
	 * measured table and a foreign outfit is round M2's problem, not this
	 * round's. Refused here rather than at the scan so that the refusal is
	 * the same wherever the name comes from - the wardrobe, a slot, a tool. */
	if (upper_name.compare(0, 7, "OUTFIT_") == 0)
		return buffer;

	for (size_t i = 0; i < mods.size(); i++)
	{
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
			bool said = false;

			for (size_t r = 0; r < reported.size(); r++)
				if (reported[r] == upper_name)
				{
					said = true;
					break;
				}

			if (!said)
			{
				reported.push_back(upper_name);
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

void HDModsForget()
{
	scanned = false;
	mods.clear();
	wavs.clear();
	notes.clear();
	reported.clear();
	notes_flushed = 0;
	folder_used.clear();
}

/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The Steam side of this file - the registry lookup, the libraryfolders.vdf
 * reader, the "recognise by content" rule and the wording of the diagnostics -
 * is carried over from TROne, Copyright 2026 Todesengel, GPLv3
 * (host\fs\discovery.cpp). Only the IV-VI branch of it was taken, the rest was
 * dropped, and everything below is written against a bundle that holds a
 * folder "5". See THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

// Declared here rather than through tomb5.h: this translation unit is built as
// C++17 with <filesystem>, and tomb5.h drags in global\types.h with its packed
// engine structures. One char array is a smaller seam than the whole engine,
// and it lets the diagnostic tools link this file with a definition of their
// own. The definition lives in tomb5\tomb5.cpp.
extern char tomb5_hd_install[260];

// The same for the OTHER bundle, Tomb Raider I-III Remastered. Only the four
// Tomb Raider III guests the randomizer imports need it, and only their
// models - so an installation without it loses those four enemies' HD models
// and nothing else. Registry value: hd_install_tr3.
extern char tomb5_hd_install_tr3[260];

namespace fs = std::filesystem;

namespace
{
	bool searched = false;
	std::string found_root;
	std::string found_root_tr3;
	std::string notes;

	void note(const std::string& line)
	{
		if (!notes.empty())
			notes += '\n';
		notes += line;
	}

	std::string lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	/* Steam writes its own path with forward slashes, the library file with
	 * doubled backslashes. std::filesystem reads both; the log should still
	 * look uniform. */
	std::string tidy(const std::string& path)
	{
		if (path.empty())
			return path;
		std::string out = fs::path(path).lexically_normal().string();
		while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
			out.pop_back();
		return out;
	}

	/* A child entry by name, case-insensitively. Windows itself does not care,
	 * but std::filesystem compares paths as strings and the spelling varies
	 * between the games ("DATA" versus "data"). Walking the directory is
	 * therefore more accurate than assembling a path and testing it - and it
	 * returns the on-disk spelling, which is what logs should show. */
	fs::path child(const fs::path& dir, const std::string& name, bool want_dir)
	{
		std::error_code ec;
		if (dir.empty() || !fs::is_directory(dir, ec))
			return {};
		const std::string want = lower(name);
		for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
		{
			if (ec)
				break;
			if (lower(e.path().filename().string()) != want)
				continue;
			if (want_dir ? e.is_directory(ec) : e.is_regular_file(ec))
				return e.path();
		}
		return {};
	}

	fs::path child_dir(const fs::path& dir, const std::string& name)
	{
		return child(dir, name, true);
	}

	fs::path child_file(const fs::path& dir, const std::string& name)
	{
		return child(dir, name, false);
	}

	bool read_text_file(const fs::path& path, std::string& out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	/* A string value from the registry, or empty.
	 *
	 * RegGetValueA rather than RegQueryValueExA: it guarantees the terminating
	 * zero that REG_SZ does not. RRF_SUBKEY_WOW6432KEY is not needed here the
	 * way it is in TROne - this build is 32 bit and already lands in the
	 * WOW6432Node branch by itself - but it is left in for the HKLM read so
	 * that a 64 bit build of this engine finds the same key. */
	std::string registry_string(HKEY hive, const char* key, const char* value, DWORD extra_flags)
	{
		char buffer[MAX_PATH * 2] = { 0 };
		DWORD size = sizeof(buffer);
		DWORD type = 0;
		const LSTATUS status = RegGetValueA(hive, key, value,
			RRF_RT_REG_SZ | extra_flags, &type, buffer, &size);
		if (status != ERROR_SUCCESS)
			return {};
		return std::string(buffer);
	}

	/* The folder Steam itself lives in, or empty; `why_not` then says what went
	 * wrong. HKEY_CURRENT_USER first: it says which Steam installation this
	 * account actually runs. HKEY_LOCAL_MACHINE is the fallback for the case
	 * that Steam never ran under this account. */
	std::string steam_root(std::string& why_not)
	{
		std::string path = registry_string(HKEY_CURRENT_USER,
			"Software\\Valve\\Steam", "SteamPath", 0);
		if (path.empty())
			path = registry_string(HKEY_LOCAL_MACHINE,
				"SOFTWARE\\Valve\\Steam", "InstallPath", RRF_SUBKEY_WOW6432KEY);
		if (path.empty())
		{
			why_not = "Steam is not in the registry (neither HKCU\\Software\\Valve\\Steam nor HKLM).";
			return {};
		}

		std::error_code ec;
		if (!fs::is_directory(path, ec))
		{
			why_not = "The registry names \"" + path + "\" but there is no folder there.";
			return {};
		}
		return tidy(path);
	}

	/* The shape of libraryfolders.vdf changed twice over the years: first the
	 * entries themselves were the paths ("1" "D:\\Games"), today every entry is
	 * a block with a "path" field inside. Rather than guessing which shape a
	 * given file has, look for the "path" keyword and take the next quoted
	 * value - that matches the current shape exactly and the legacy one not at
	 * all, which is why the legacy branch below exists separately. */
	std::vector<std::string> vdf_library_paths(const std::string& text)
	{
		std::vector<std::string> out;

		auto quoted_after = [&](std::size_t from, std::string& value) -> std::size_t
		{
			std::size_t open = text.find('"', from);
			if (open == std::string::npos)
				return std::string::npos;
			std::size_t close = text.find('"', open + 1);
			if (close == std::string::npos)
				return std::string::npos;
			value = text.substr(open + 1, close - open - 1);
			return close + 1;
		};

		std::size_t at = 0;
		while ((at = text.find("\"path\"", at)) != std::string::npos)
		{
			std::string value;
			std::size_t next = quoted_after(at + 6, value);
			if (next == std::string::npos)
				break;
			at = next;
			if (!value.empty())
				out.push_back(value);
		}

		if (out.empty())
		{
			/* The legacy shape: a numeric key whose value already is the path.
			 * Counting pairs from the front goes wrong - the root key
			 * ("LibraryFolders") stands alone before the opening brace and
			 * shifts everything by one. Instead ALL quoted strings are
			 * collected and every adjacent pair is tested: a numeric key
			 * followed by something that looks like a path ("D:\...") is a
			 * library; timestamps and ids in the same file look different. */
			std::vector<std::string> words;
			std::size_t pos = 0;
			while (true)
			{
				std::string word;
				std::size_t next = quoted_after(pos, word);
				if (next == std::string::npos)
					break;
				pos = next;
				words.push_back(word);
			}

			for (std::size_t i = 0; i + 1 < words.size(); ++i)
			{
				const std::string& key = words[i];
				const std::string& value = words[i + 1];
				const bool numeric = !key.empty() &&
					std::all_of(key.begin(), key.end(),
						[](unsigned char c) { return c >= '0' && c <= '9'; });
				const bool looks_like_path = value.size() > 2 && value[1] == ':' &&
					(value[2] == '\\' || value[2] == '/');
				if (numeric && looks_like_path)
					out.push_back(value);
			}
		}

		/* Inside the file the backslashes are doubled. */
		for (std::string& p : out)
		{
			std::string collapsed;
			collapsed.reserve(p.size());
			for (std::size_t i = 0; i < p.size(); ++i)
			{
				collapsed.push_back(p[i]);
				if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\')
					++i;
			}
			p = collapsed;
		}
		return out;
	}

	/* Every Steam library folder that actually exists - Steam's own first, then
	 * the ones from libraryfolders.vdf in file order, duplicates removed. */
	std::vector<fs::path> steam_libraries()
	{
		std::vector<fs::path> out;
		std::string why_not;
		const std::string root = steam_root(why_not);
		if (root.empty())
		{
			note(why_not);
			return out;
		}

		std::vector<std::string> candidates;
		candidates.push_back(root);

		const fs::path vdf = fs::path(root) / "steamapps" / "libraryfolders.vdf";
		std::error_code ec;
		if (!fs::is_regular_file(vdf, ec))
		{
			/* Not an error: a Steam installation with a single library gets by
			 * without the file, and that library is already in the list. */
			note("No libraryfolders.vdf under " + root + " - assuming a single-library Steam.");
		}
		else
		{
			std::string text;
			if (read_text_file(vdf, text))
			{
				for (const std::string& p : vdf_library_paths(text))
					candidates.push_back(p);
			}
			else
				note("Could not read " + vdf.string() + ".");
		}

		std::vector<std::string> seen;
		for (const std::string& candidate : candidates)
		{
			const std::string clean = tidy(candidate);
			if (clean.empty() || !fs::is_directory(clean, ec))
				continue;
			const std::string folded = lower(clean);
			if (std::find(seen.begin(), seen.end(), folded) != seen.end())
				continue;
			seen.push_back(folded);
			out.push_back(fs::path(clean));
		}
		return out;
	}

	/* A IV-VI bundle is recognised by its executable; that game 5 is really
	 * inside it says the subfolder. A subfolder counts only with DATA (the
	 * levels), TEX (the HD pages) and ITEM (the HD models) - an empty leftover
	 * folder is not a game. Returns "<dir>\5" or an empty string.
	 *
	 * TROne's rule, unchanged: recognise by CONTENT, never by folder name.
	 * Steam names the same game differently per language and depot, and a
	 * folder somebody moved by hand is named whatever they liked. */
	std::string classify_remaster456(const fs::path& dir)
	{
		if (child_file(dir, "tomb456.exe").empty())
			return {};

		const fs::path sub = child_dir(dir, "5");
		if (sub.empty())
		{
			note(dir.string() + ": a tomb456.exe but no folder \"5\" - Chronicles is not in this bundle.");
			return {};
		}

		const fs::path data = child_dir(sub, "DATA");
		const fs::path tex = child_dir(sub, "TEX");
		const fs::path item = child_dir(sub, "ITEM");
		if (data.empty() || tex.empty() || item.empty())
		{
			note(sub.string() + ": folder \"5\" without DATA, TEX and ITEM - skipped.");
			return {};
		}
		return sub.string();
	}

	/* The other bundle: Tomb Raider I-III Remastered, for the four Tomb
	 * Raider III guests the randomizer imports. Same rule as above and the
	 * same source - TROne's discovery table names
	 * tomb123.exe beside subfolders 1..3 with DATA and TEX
	 * (host\fs\discovery.cpp) - with the game folder being "3" instead of
	 * "5".
	 *
	 * DATA and TEX are required and ITEM is NOT, which is the one place this
	 * differs from the IV-VI rule above. A bundle whose 3\ITEM is missing or
	 * empty is still worth finding: the model then comes back "missing" with
	 * the file name in the log, which says far more than an install that
	 * silently was not there at all. Nothing but models is read from it. */
	std::string classify_remaster123(const fs::path& dir)
	{
		if (child_file(dir, "tomb123.exe").empty())
			return {};

		const fs::path sub = child_dir(dir, "3");
		if (sub.empty())
		{
			note(dir.string() + ": a tomb123.exe but no folder \"3\" - the third game is not in this bundle.");
			return {};
		}

		const fs::path data = child_dir(sub, "DATA");
		const fs::path tex = child_dir(sub, "TEX");
		if (data.empty() || tex.empty())
		{
			note(sub.string() + ": folder \"3\" without DATA and TEX - skipped.");
			return {};
		}

		if (child_dir(sub, "ITEM").empty())
			note(sub.string() + ": folder \"3\" has no ITEM - the Tomb Raider III guests stay classic.");

		return sub.string();
	}

	void search()
	{
		searched = true;
		found_root.clear();
		found_root_tr3.clear();
		notes.clear();

		std::error_code ec;
		std::vector<fs::path> scan_roots;

		/* The configured override folders first, so they win. Either may be the
		 * bundle folder itself or a folder containing it - both are scanned, so
		 * a player does not have to guess which one is meant. Both overrides go
		 * into the same list: a folder that is not the bundle it was named
		 * for simply does not classify as one, and naming the other one is
		 * then a kindness rather than a mistake. */
		const std::string over = tomb5_hd_install;
		if (!over.empty())
		{
			if (fs::is_directory(over, ec))
				scan_roots.push_back(fs::path(over));
			else
				note("Configured folder does not exist: " + over);
		}

		const std::string over3 = tomb5_hd_install_tr3;
		if (!over3.empty())
		{
			if (fs::is_directory(over3, ec))
				scan_roots.push_back(fs::path(over3));
			else
				note("Configured I-III folder does not exist: " + over3);
		}

		for (const fs::path& lib : steam_libraries())
		{
			const fs::path steamapps = child_dir(lib, "steamapps");
			if (steamapps.empty())
				continue;
			const fs::path common = child_dir(steamapps, "common");
			if (!common.empty())
				scan_roots.push_back(common);
		}

		if (scan_roots.empty())
		{
			note("Nowhere to search: no Steam library found and no hd_install path set.");
			return;
		}

		/* One walk for both bundles. The two live side by side in the same
		 * Steam library in every normal installation, so walking the tree
		 * twice would open the same directories twice for nothing. The loop
		 * ends when both are found; a missing I-III is not a reason to keep
		 * looking for the IV-VI that is already in hand, and the other way
		 * round it is not a reason to stop. */
		for (const fs::path& root : scan_roots)
		{
			/* The folder itself first, then its children - one level deep.
			 * Anything deeper belongs to the games, not to this search. */
			if (found_root.empty())
				found_root = classify_remaster456(root);
			if (found_root_tr3.empty())
				found_root_tr3 = classify_remaster123(root);

			if (!found_root.empty() && !found_root_tr3.empty())
				break;

			for (const fs::directory_entry& e : fs::directory_iterator(root, ec))
			{
				if (ec)
					break;
				if (!e.is_directory(ec))
					continue;
				if (found_root.empty())
					found_root = classify_remaster456(e.path());
				if (found_root_tr3.empty())
					found_root_tr3 = classify_remaster123(e.path());
				if (!found_root.empty() && !found_root_tr3.empty())
					break;
			}
			if (!found_root.empty() && !found_root_tr3.empty())
				break;
		}

		if (found_root.empty())
			note("No Tomb Raider IV-VI Remastered recognised - HD art stays off.");
		else
			note("HD art source: " + found_root);

		if (found_root_tr3.empty())
			note("No Tomb Raider I-III Remastered recognised - the four Tomb Raider III guests stay classic.");
		else
			note("Tomb Raider III guest models: " + found_root_tr3);
	}
}

const char* HDInstallRoot()
{
	if (!searched)
		search();
	return found_root.c_str();
}

const char* HDInstallDiagnostics()
{
	if (!searched)
		search();
	return notes.c_str();
}

const char* HDInstallRootTr3()
{
	if (!searched)
		search();
	return found_root_tr3.c_str();
}

void HDInstallForget()
{
	searched = false;
	found_root.clear();
	found_root_tr3.clear();
	notes.clear();
}

const char* HDInstallOverride()
{
	return tomb5_hd_install;
}

const char* HDInstallOverrideTr3()
{
	return tomb5_hd_install_tr3;
}

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
#include "hd_install_names.h"	// the wide names, the code page test

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <exception>
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

	/* Library folders a tool adds to the search (HDInstallAddLibrary). The
	 * engine adds none. */
	std::vector<std::wstring> extra_libraries;

	void note(const std::string& line)
	{
		if (!notes.empty())
			notes += '\n';
		notes += line;
	}

	// A path as a note shows it: '?' where the code page lacks a letter.
	std::string shown(const fs::path& path)
	{
		return HDNameShown(path.native());
	}

	/* Steam writes its own path with forward slashes, the library file with
	 * doubled backslashes. std::filesystem reads both; the log should still
	 * look uniform. Wide throughout: a wide path is never converted, so a
	 * name of any script passes. */
	fs::path tidy(const std::wstring& path)
	{
		if (path.empty())
			return fs::path();
		std::wstring out = fs::path(path).lexically_normal().native();
		while (!out.empty() && (out.back() == L'\\' || out.back() == L'/'))
			out.pop_back();
		return fs::path(out);
	}

	/* A child entry by name, case-insensitively. Windows itself does not care,
	 * but std::filesystem compares paths as strings and the spelling varies
	 * between the games ("DATA" versus "data"). Walking the directory is
	 * therefore more accurate than assembling a path and testing it - and it
	 * returns the on-disk spelling, which is what logs should show.
	 *
	 * Every name is compared wide (HDNameIs) and the walk steps with an
	 * error code: an entry the system code page cannot spell is passed over
	 * like any other non-match instead of throwing out of the search, and a
	 * folder that fails half way ends the walk where it failed. */
	fs::path child(const fs::path& dir, const wchar_t* name, bool want_dir)
	{
		std::error_code ec;
		if (dir.empty() || !fs::is_directory(dir, ec))
			return {};
		fs::directory_iterator it(dir, ec), end;
		if (ec)
			return {};
		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;
			if (!HDNameIs(it->path().filename().c_str(), name))
				continue;
			std::error_code kind;
			if (want_dir ? it->is_directory(kind) : it->is_regular_file(kind))
				return it->path();
		}
		return {};
	}

	fs::path child_dir(const fs::path& dir, const wchar_t* name)
	{
		return child(dir, name, true);
	}

	fs::path child_file(const fs::path& dir, const wchar_t* name)
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
	 * that a 64 bit build of this engine finds the same key.
	 *
	 * Read wide (RegGetValueW): a Steam folder whose name the system code
	 * page cannot spell reads back as '?' through the narrow call, and that
	 * names no folder. */
	std::wstring registry_string(HKEY hive, const wchar_t* key,
		const wchar_t* value, DWORD extra_flags)
	{
		wchar_t buffer[MAX_PATH * 2] = { 0 };
		DWORD size = sizeof(buffer);
		DWORD type = 0;
		const LSTATUS status = RegGetValueW(hive, key, value,
			RRF_RT_REG_SZ | extra_flags, &type, buffer, &size);
		if (status != ERROR_SUCCESS)
			return {};
		return std::wstring(buffer);
	}

	/* The folder Steam itself lives in, or empty; `why_not` then says what went
	 * wrong. HKEY_CURRENT_USER first: it says which Steam installation this
	 * account actually runs. HKEY_LOCAL_MACHINE is the fallback for the case
	 * that Steam never ran under this account. */
	fs::path steam_root(std::string& why_not)
	{
		std::wstring path = registry_string(HKEY_CURRENT_USER,
			L"Software\\Valve\\Steam", L"SteamPath", 0);
		if (path.empty())
			path = registry_string(HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Valve\\Steam", L"InstallPath",
				RRF_SUBKEY_WOW6432KEY);
		if (path.empty())
		{
			why_not = "Steam is not in the registry (neither HKCU\\Software\\Valve\\Steam nor HKLM).";
			return {};
		}

		std::error_code ec;
		if (!fs::is_directory(fs::path(path), ec))
		{
			why_not = "The registry names \"" + HDNameShown(path) +
				"\" but there is no folder there.";
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

	/* Every Steam library folder that actually exists - the ones a tool added
	 * first (HDInstallAddLibrary), then Steam's own, then the ones from
	 * libraryfolders.vdf in file order, duplicates removed.
	 *
	 * libraryfolders.vdf spells its paths in UTF-8, so they are read as UTF-8
	 * and kept wide; a narrow reading turns every letter beyond ASCII into
	 * the wrong bytes and passes that library over without a word. A
	 * value that is not valid UTF-8 is read in the system code page, the
	 * spelling an older file may carry. */
	std::vector<fs::path> steam_libraries()
	{
		std::vector<fs::path> out;
		std::vector<std::wstring> candidates(extra_libraries);
		std::string why_not;
		const fs::path root = steam_root(why_not);
		std::error_code ec;

		if (root.empty())
			note(why_not);
		else
		{
			candidates.push_back(root.native());

			const fs::path vdf = root / L"steamapps" / L"libraryfolders.vdf";
			if (!fs::is_regular_file(vdf, ec))
			{
				/* Not an error: a Steam installation with a single library
				 * gets by without the file, and that library is already in
				 * the list. */
				note("No libraryfolders.vdf under " + shown(root) +
					" - assuming a single-library Steam.");
			}
			else
			{
				std::string text;
				if (read_text_file(vdf, text))
				{
					for (const std::string& p : vdf_library_paths(text))
					{
						std::wstring wide = HDNameWide(p, CP_UTF8);
						if (wide.empty())
							wide = HDNameWide(p, CP_ACP);
						if (!wide.empty())
							candidates.push_back(wide);
					}
				}
				else
					note("Could not read " + shown(vdf) + ".");
			}
		}

		for (const std::wstring& candidate : candidates)
		{
			const fs::path clean = tidy(candidate);
			if (clean.empty() || !fs::is_directory(clean, ec))
				continue;
			bool seen = false;
			for (const fs::path& have : out)
				if (HDNameIs(have.c_str(), clean.c_str()))
					seen = true;
			if (!seen)
				out.push_back(clean);
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
	 * folder somebody moved by hand is named whatever they liked.
	 *
	 * The path comes back wide; search() decides whether the narrow file
	 * calls of the engine can open it. */
	fs::path classify_remaster456(const fs::path& dir)
	{
		if (child_file(dir, L"tomb456.exe").empty())
			return {};

		const fs::path sub = child_dir(dir, L"5");
		if (sub.empty())
		{
			note(shown(dir) + ": a tomb456.exe but no folder \"5\" - "
				"Chronicles is not in this bundle.");
			return {};
		}

		const fs::path data = child_dir(sub, L"DATA");
		const fs::path tex = child_dir(sub, L"TEX");
		const fs::path item = child_dir(sub, L"ITEM");
		if (data.empty() || tex.empty() || item.empty())
		{
			note(shown(sub) +
				": folder \"5\" without DATA, TEX and ITEM - skipped.");
			return {};
		}
		return sub;
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
	fs::path classify_remaster123(const fs::path& dir)
	{
		if (child_file(dir, L"tomb123.exe").empty())
			return {};

		const fs::path sub = child_dir(dir, L"3");
		if (sub.empty())
		{
			note(shown(dir) + ": a tomb123.exe but no folder \"3\" - "
				"the third game is not in this bundle.");
			return {};
		}

		const fs::path data = child_dir(sub, L"DATA");
		const fs::path tex = child_dir(sub, L"TEX");
		if (data.empty() || tex.empty())
		{
			note(shown(sub) + ": folder \"3\" without DATA and TEX - skipped.");
			return {};
		}

		if (child_dir(sub, L"ITEM").empty())
			note(shown(sub) + ": folder \"3\" has no ITEM - "
				"the Tomb Raider III guests stay classic.");

		return sub;
	}

	/* A root found wide, handed on in the spelling the engine's narrow
	 * file calls take (fopen, char[260]) - or not at all. A root the system
	 * code page cannot spell would reach every one of those calls with '?'
	 * in it and fail there, file by file, with a half-HD level as the
	 * result; one line here and HD off is the honest answer.
	 *
	 * Turning those calls wide instead was counted: seventeen narrow opens
	 * and listings in twelve files take a path made from a root (the page
	 * cache three, the .TRM and .TRL readers two each, the binder two, the
	 * .TRG scan, the font, the springs, the debris, the classic pages, the
	 * sound bank, the level key's stamp and the folder listing one each). */
	std::string narrow_root(const fs::path& root, const char* line)
	{
		std::string narrow;

		if (root.empty())
			return narrow;

		if (HDNameFits(root.native(), CP_ACP, narrow))
			return narrow;

		note(std::string(line) + " - " + shown(root));
		return std::string();
	}

	void search_walk()
	{
		std::error_code ec;
		std::vector<fs::path> scan_roots;
		fs::path root456;
		fs::path root123;

		/* The configured override folders first, so they win. Either may be the
		 * bundle folder itself or a folder containing it - both are scanned, so
		 * a player does not have to guess which one is meant. Both overrides go
		 * into the same list: a folder that is not the bundle it was named
		 * for simply does not classify as one, and naming the other one is
		 * then a kindness rather than a mistake. */
		const std::string over = tomb5_hd_install;
		if (!over.empty())
		{
			const fs::path wide = HDNameWide(over, CP_ACP);
			if (!wide.empty() && fs::is_directory(wide, ec))
				scan_roots.push_back(wide);
			else
				note("Configured folder does not exist: " + over);
		}

		const std::string over3 = tomb5_hd_install_tr3;
		if (!over3.empty())
		{
			const fs::path wide = HDNameWide(over3, CP_ACP);
			if (!wide.empty() && fs::is_directory(wide, ec))
				scan_roots.push_back(wide);
			else
				note("Configured I-III folder does not exist: " + over3);
		}

		for (const fs::path& lib : steam_libraries())
		{
			const fs::path steamapps = child_dir(lib, L"steamapps");
			if (steamapps.empty())
				continue;
			const fs::path common = child_dir(steamapps, L"common");
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
			if (root456.empty())
				root456 = classify_remaster456(root);
			if (root123.empty())
				root123 = classify_remaster123(root);

			if (!root456.empty() && !root123.empty())
				break;

			/* Stepped with an error code, as child() is: a folder that
			 * fails half way ends this root's walk, never the search. */
			fs::directory_iterator it(root, ec), end;
			if (ec)
				continue;
			for (; it != end; it.increment(ec))
			{
				if (ec)
					break;
				std::error_code kind;
				if (!it->is_directory(kind))
					continue;
				if (root456.empty())
					root456 = classify_remaster456(it->path());
				if (root123.empty())
					root123 = classify_remaster123(it->path());
				if (!root456.empty() && !root123.empty())
					break;
			}
			if (!root456.empty() && !root123.empty())
				break;
		}

		found_root = narrow_root(root456,
			"install path not representable in the system code page: HD off");
		found_root_tr3 = narrow_root(root123,
			"I-III install path not representable in the system code page: "
			"the Tomb Raider III guests stay classic");

		if (root456.empty())
			note("No Tomb Raider IV-VI Remastered recognised - HD art stays off.");
		else if (!found_root.empty())
			note("HD art source: " + found_root);

		if (root123.empty())
			note("No Tomb Raider I-III Remastered recognised - the four Tomb Raider III guests stay classic.");
		else if (!found_root_tr3.empty())
			note("Tomb Raider III guest models: " + found_root_tr3);
	}

	/* The search, with nothing let out of it. Every walk above steps with
	 * an error code and compares names wide, so nothing in it is known to
	 * throw; this is the net under that. Without it a throw ends the game,
	 * and caught anywhere above, it leaves both roots empty AND the notes
	 * empty - the search counts as done, and nothing says why HD is off -
	 * so the catch writes the reason down. A root is set only once the
	 * walk is through, so a search stopped half way hands on none. */
	void search()
	{
		searched = true;
		found_root.clear();
		found_root_tr3.clear();
		notes.clear();

		try
		{
			search_walk();
		}
		catch (const std::exception& e)
		{
			found_root.clear();
			found_root_tr3.clear();
			note(std::string("The installation search stopped on an error (") +
				e.what() + ") - HD art stays off.");
		}
		catch (...)
		{
			found_root.clear();
			found_root_tr3.clear();
			note("The installation search stopped on an error - "
				"HD art stays off.");
		}
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

void HDInstallAddLibrary(const wchar_t* folder)
{
	if (folder && *folder)
		extra_libraries.push_back(folder);
}

const char* HDInstallOverride()
{
	return tomb5_hd_install;
}

const char* HDInstallOverrideTr3()
{
	return tomb5_hd_install_tr3;
}

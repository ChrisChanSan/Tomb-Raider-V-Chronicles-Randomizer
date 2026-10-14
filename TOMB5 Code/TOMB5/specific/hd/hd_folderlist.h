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
#pragma once

/*
 * Whether a folder holds a file, answered from one listing of the folder.
 *
 * A level's models are found by asking folder after folder whether a name is
 * there - the install's 5\ITEM, then 4\ITEM, every mod's own two before them,
 * a face's file beside its body's - and a level asks that some thousand times:
 * RICH2's models opened 999 files to find 58 (the models line's trm_open),
 * and with seventeen mods every name is asked of 34 more folders. Each folder
 * is listed here once and every later question about it is a lookup. The
 * order the callers ask in, and so which folder's file wins, is theirs and
 * does not change.
 *
 * A file the listing names is taken as there, as the open it stands in for
 * would have found it; names compare without regard to case, as the file
 * system does. A folder that is not there holds nothing. A folder that is
 * there and cannot be listed answers -1, and the caller opens the file as it
 * did before. HDFolderListForget drops every listing, so that the next
 * question lists again - a level's scan does that (hd_scan.cpp).
 *
 * Header-only, so that every unit that asks shares the one store without a
 * new unit in every build: the engine, and the tools that link hd_trm.cpp,
 * hd_bind.cpp or hd_mods.cpp.
 */

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cwctype>

namespace hdfolderlist
{
	struct Store
	{
		std::mutex guard;
		// Per folder, upper case: its files' names, upper case.
		std::unordered_map<std::wstring, std::unordered_set<std::wstring> >
			listed;
		std::unordered_set<std::wstring> unlistable;
		int listings = 0;	// folders listed since the last forget
	};

	inline Store& store()
	{
		static Store one;
		return one;
	}

	inline std::wstring upper(std::wstring text)
	{
		for (wchar_t& c : text)
			c = (wchar_t)std::towupper(c);

		return text;
	}
}

/* 1 when the folder of `path` holds its file, 0 when it does not (or the
 * folder is not there), -1 when the folder could not be listed. `utf8` says
 * how the path is spelt: UTF-8 as std::filesystem::u8path takes it, or the
 * process's own code page as fopen takes it. */
inline int HDFolderHas(const std::string& path, bool utf8)
{
	namespace fs = std::filesystem;
	using namespace hdfolderlist;

	std::error_code ec;
	const fs::path whole = utf8 ? fs::u8path(path) : fs::path(path);
	const std::wstring folder = upper(whole.parent_path().wstring());
	const std::wstring name = upper(whole.filename().wstring());

	if (name.empty())
		return 0;

	Store& s = store();
	std::lock_guard<std::mutex> hold(s.guard);

	if (s.unlistable.count(folder))
		return -1;

	auto at = s.listed.find(folder);

	if (at == s.listed.end())
	{
		std::unordered_set<std::wstring> names;
		const fs::file_status kind = fs::status(whole.parent_path(), ec);

		/* A folder that is not there is no error and holds nothing; MSVC
		 * says not_found and sets the error code beside it. */
		if (kind.type() != fs::file_type::not_found && ec)
		{
			s.unlistable.insert(folder);
			return -1;
		}

		if (fs::is_directory(kind))
		{
			fs::directory_iterator it(whole.parent_path(), ec);

			if (ec)
			{
				s.unlistable.insert(folder);
				return -1;
			}

			for (; it != fs::directory_iterator(); it.increment(ec))
			{
				if (ec)
				{
					s.unlistable.insert(folder);
					return -1;
				}

				std::error_code file;

				if (it->is_regular_file(file))
					names.insert(upper(it->path().filename().wstring()));
			}
		}

		s.listings++;
		at = s.listed.emplace(folder, names).first;
	}

	return at->second.count(name) ? 1 : 0;
}

// Every listing dropped: the next question lists its folder again.
inline void HDFolderListForget()
{
	using namespace hdfolderlist;

	Store& s = store();
	std::lock_guard<std::mutex> hold(s.guard);

	s.listed.clear();
	s.unlistable.clear();
	s.listings = 0;
}

// Folders listed since the last forget, for a log line.
inline int HDFolderListings()
{
	using namespace hdfolderlist;

	Store& s = store();
	std::lock_guard<std::mutex> hold(s.guard);

	return s.listings;
}

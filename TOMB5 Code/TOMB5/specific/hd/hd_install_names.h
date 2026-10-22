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

/* The names of the installation search, as pure functions: hd_install.cpp
 * and hd_mods.cpp walk folders with them, and ModernTest checks them with
 * names made up for the test.
 *
 * The engine runs in the system's ANSI code page (no manifest names
 * another, the character set is "not set"). A folder name that code page
 * cannot hold - Cyrillic or CJK on a Western system - makes
 * std::filesystem::path::string() throw, and a path spelt in that code page
 * comes out with '?' where such a letter stood, which names no folder at
 * all. So the walks keep every name wide, as the file system gives it, and
 * compare it wide; a name is turned into the narrow spelling only where a
 * narrow file call needs it, and only when it survives the trip. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string.h>
#include <wchar.h>

#include <string>

/* Two names alike apart from the case of A-Z: Windows does not care
 * about case, and the games spell one folder "DATA" and "data". */
inline bool HDNameIs(const wchar_t* a, const wchar_t* b)
{
	return a && b && _wcsicmp(a, b) == 0;
}

/* `wide` in code page `cp`, into `out`, and true - or false when a letter
 * of it has no place in that code page and would come out as a stand-in
 * (or as a "best fit" look-alike, which names another file). The trip back
 * must give the same name, so a spelling that the code page does hold but
 * that reads back differently does not pass either. An empty name passes
 * as an empty spelling. */
inline bool HDNameFits(const std::wstring& wide, unsigned cp, std::string& out)
{
	out.clear();

	if (wide.empty())
		return true;

	BOOL lossy = FALSE;
	BOOL* ask = (cp == CP_UTF8 || cp == CP_UTF7) ? 0 : &lossy;
	const DWORD flags = ask ? WC_NO_BEST_FIT_CHARS : 0;
	const int n = WideCharToMultiByte(cp, flags, wide.c_str(),
		(int)wide.size(), 0, 0, 0, ask);

	if (n <= 0 || lossy)
		return false;

	std::string narrow((size_t)n, '\0');

	if (WideCharToMultiByte(cp, flags, wide.c_str(), (int)wide.size(),
		&narrow[0], n, 0, ask) != n || lossy)
		return false;

	const int back = MultiByteToWideChar(cp, 0, narrow.c_str(), n, 0, 0);

	if (back != (int)wide.size())
		return false;

	std::wstring again((size_t)back, L'\0');

	MultiByteToWideChar(cp, 0, narrow.c_str(), n, &again[0], back);

	if (again != wide)
		return false;

	out = narrow;
	return true;
}

/* A narrow spelling read in code page `cp` (the override paths in the
 * registry are ANSI, libraryfolders.vdf is UTF-8). A byte sequence that is
 * not valid in that code page gives an empty name, never a guess. */
inline std::wstring HDNameWide(const std::string& narrow, unsigned cp)
{
	if (narrow.empty())
		return std::wstring();

	const DWORD flags = (cp == CP_UTF8) ? MB_ERR_INVALID_CHARS : 0;
	const int n = MultiByteToWideChar(cp, flags, narrow.c_str(),
		(int)narrow.size(), 0, 0);

	if (n <= 0)
		return std::wstring();

	std::wstring wide((size_t)n, L'\0');

	MultiByteToWideChar(cp, flags, narrow.c_str(), (int)narrow.size(),
		&wide[0], n);
	return wide;
}

/* A name for a log line, in the system code page, with '?' for a letter
 * that code page lacks. Only for reading: a path spelt this way may name
 * no file. */
inline std::string HDNameShown(const std::wstring& wide)
{
	if (wide.empty())
		return std::string();

	const int n = WideCharToMultiByte(CP_ACP, 0, wide.c_str(),
		(int)wide.size(), 0, 0, 0, 0);

	if (n <= 0)
		return std::string("?");

	std::string shown((size_t)n, '\0');

	WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(),
		&shown[0], n, 0, 0);
	return shown;
}

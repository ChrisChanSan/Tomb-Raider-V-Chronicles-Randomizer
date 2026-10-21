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

/* The crash seam: one line and a minidump when the process dies of an
 * exception nobody caught.
 *
 * Without it a crash ends the log mid-frame, and the last line written says
 * where the game was, not what failed. The filter writes
 *
 *   CRASH code=0x<exception code> at=0x<address> module=<file>+0x<offset>
 *         thread=main|game|other
 *
 * through the writer the caller hands in (the engine's is Log, so the line
 * lands in log.txt with -log), and always into crash.txt in the working
 * directory as well when the log is off - the place log.txt would be. Then
 * PCtomb5_<yyyymmdd>_<hhmmss>.dmp beside it: MiniDumpWriteDump of
 * dbghelp.dll, which is loaded only now, so a machine without it loses the
 * dump and keeps the line ("dump=none" at the end of crash.txt). The module
 * and offset are what the build's own .pdb resolves to a source line.
 *
 * MiniDumpNormal: the threads' stacks and the loaded modules - 31,860
 * bytes from RoomProbe -crash's child, more from the engine with its
 * more threads and modules - enough for the faulting line and its
 * callers, no game data.
 * The filter returns EXCEPTION_CONTINUE_SEARCH, so Windows' own report
 * follows as before.
 *
 * Header only, so the engine and the diagnostic tool (RoomProbe -crash)
 * take the very same code without a build file to change. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (*CrashSeamWriter)(const char* line);

struct CrashSeamState
{
	CrashSeamWriter writer;		// the log's, or 0
	bool fileAlways;		// crash.txt even when the writer exists
	DWORD mainThread;		// the thread that installed the seam
	DWORD gameThread;		// named later, 0 until then
	char prefix[32];		// the dump's name before the date
};

inline CrashSeamState& CrashSeam()
{
	static CrashSeamState state = { 0, true, 0, 0, "PCtomb5" };
	return state;
}

// MiniDumpWriteDump's signature (dbghelp.h), declared here so the header
// needs neither the import library nor the SDK's dbghelp.h.
typedef BOOL (WINAPI* CrashSeamDumpFn)(HANDLE process, DWORD processId,
	HANDLE file, int type, PEXCEPTION_POINTERS exception, void* user,
	void* callback);

struct CrashSeamDumpInfo
{
	DWORD threadId;
	PEXCEPTION_POINTERS pointers;
	BOOL clientPointers;
};

inline LONG WINAPI CrashSeamFilter(PEXCEPTION_POINTERS info)
{
	CrashSeamState& state = CrashSeam();
	const DWORD code = info && info->ExceptionRecord ?
		info->ExceptionRecord->ExceptionCode : 0;
	void* at = info && info->ExceptionRecord ?
		info->ExceptionRecord->ExceptionAddress : 0;
	char module[MAX_PATH] = "?";
	unsigned long long offset = 0;
	HMODULE owner = 0;

	if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &owner) &&
		owner)
	{
		char path[MAX_PATH];
		const DWORD n = GetModuleFileNameA(owner, path, MAX_PATH);

		if (n && n < MAX_PATH)
		{
			const char* base = strrchr(path, '\\');

			strncpy_s(module, sizeof(module), base ? base + 1 : path,
				_TRUNCATE);
		}

		offset = (unsigned long long)((const char*)at - (const char*)owner);
	}

	const DWORD self = GetCurrentThreadId();
	const char* thread = self == state.mainThread ? "main" :
		(state.gameThread && self == state.gameThread) ? "game" : "other";
	char line[512];

	_snprintf_s(line, sizeof(line), _TRUNCATE,
		"CRASH code=0x%08lX at=0x%p module=%s+0x%llX thread=%s",
		(unsigned long)code, at, module, offset, thread);

	if (state.writer)
		state.writer(line);

	// The dump's name: the prefix and the local time to the second.
	SYSTEMTIME now;
	char dumpName[96];

	GetLocalTime(&now);
	_snprintf_s(dumpName, sizeof(dumpName), _TRUNCATE,
		"%s_%04u%02u%02u_%02u%02u%02u.dmp", state.prefix, now.wYear,
		now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

	bool dumped = false;
	HMODULE help = LoadLibraryA("dbghelp.dll");
	CrashSeamDumpFn dump = help ?
		(CrashSeamDumpFn)GetProcAddress(help, "MiniDumpWriteDump") : 0;

	if (dump)
	{
		HANDLE file = CreateFileA(dumpName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, 0);

		if (file != INVALID_HANDLE_VALUE)
		{
			CrashSeamDumpInfo ex = { self, info, FALSE };

			// 0 is MiniDumpNormal.
			dumped = dump(GetCurrentProcess(), GetCurrentProcessId(), file,
				0, info ? (PEXCEPTION_POINTERS)&ex : 0, 0, 0) != FALSE;
			CloseHandle(file);

			if (!dumped)
				DeleteFileA(dumpName);
		}
	}

	if (!state.writer || state.fileAlways)
	{
		FILE* out = 0;

		if (!fopen_s(&out, "crash.txt", "a") && out)
		{
			fprintf(out, "%s dump=%s\n", line, dumped ? dumpName : "none");
			fclose(out);
		}
	}

	if (state.writer && dumped)
	{
		char note[160];

		_snprintf_s(note, sizeof(note), _TRUNCATE, "CRASH dump=%s", dumpName);
		state.writer(note);
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

/* Installed once, as early as the caller can: the installing thread is
 * "main". `writer` may be 0 (crash.txt alone); `fileAlways` false leaves
 * crash.txt out while the writer exists. */
inline void CrashSeamInstall(CrashSeamWriter writer, bool fileAlways,
	const char* prefix)
{
	CrashSeamState& state = CrashSeam();

	state.writer = writer;
	state.fileAlways = fileAlways;
	state.mainThread = GetCurrentThreadId();

	if (prefix)
		strncpy_s(state.prefix, sizeof(state.prefix), prefix, _TRUNCATE);

	SetUnhandledExceptionFilter(CrashSeamFilter);
}

// The thread the game runs on, so a crash there says "game".
inline void CrashSeamGameThread(DWORD id)
{
	CrashSeam().gameThread = id;
}

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
 * After the dump's name the line says how far the dump got: err=<the
 * failing call's GetLastError, 0 when written> step=<load|proc|file|write>,
 * the last step tried - dbghelp loaded, MiniDumpWriteDump found, the file
 * made, the dump written.
 *
 * MiniDumpNormal: the threads' stacks and the loaded modules - 31,860
 * bytes from RoomProbe -crash's child, more from the engine with its
 * more threads and modules - enough for the faulting line and its
 * callers, no game data.
 * The filter returns EXCEPTION_CONTINUE_SEARCH, so Windows' own report
 * follows as before.
 *
 * The hang seam shares the tail: CrashSeamHang, for a thread that stopped
 * answering, writes HANG at=... module=...+0x... thread=... id=... when=...
 * the same way, with the dump named <prefix>_hang_<date>_<time>.dmp.
 *
 * Header only, so the engine and the diagnostic tool (RoomProbe -crash,
 * -hang) take the very same code without a build file to change. */
#include <windows.h>
#include <stddef.h>
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

/* MINIDUMP_EXCEPTION_INFORMATION's shape. dbghelp.h declares it under
 * pack(4) (pshpack4.h in minidumpapiset.h), so on a 64-bit build the
 * pointer lies at offset 4 and the struct is 16 bytes; laid out naturally
 * it was 24, the pointer at 8, and MiniDumpWriteDump read the exception's
 * pointers from the wrong bytes and refused - no 64-bit dump was ever
 * written. On a 32-bit build both layouts are the same 12 bytes. */
#pragma pack(push, 4)
struct CrashSeamDumpInfo
{
	DWORD threadId;
	PEXCEPTION_POINTERS pointers;
	BOOL clientPointers;
};
#pragma pack(pop)

static_assert(sizeof(CrashSeamDumpInfo) == 8 + sizeof(void*) &&
	offsetof(CrashSeamDumpInfo, pointers) == 4,
	"CrashSeamDumpInfo is MINIDUMP_EXCEPTION_INFORMATION");

// The module an address lies in, by file name, and the offset into it.
inline void CrashSeamWhere(void* at, char* module, size_t size,
	unsigned long long* offset)
{
	HMODULE owner = 0;

	strncpy_s(module, size, "?", _TRUNCATE);
	*offset = 0;

	if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &owner) &&
		owner)
	{
		char path[MAX_PATH];
		const DWORD n = GetModuleFileNameA(owner, path, MAX_PATH);

		if (n && n < MAX_PATH)
		{
			const char* base = strrchr(path, '\\');

			strncpy_s(module, size, base ? base + 1 : path, _TRUNCATE);
		}

		*offset = (unsigned long long)((const char*)at - (const char*)owner);
	}
}

// What a thread of this process is called on the line.
inline const char* CrashSeamThreadName(DWORD id)
{
	const CrashSeamState& state = CrashSeam();

	return id == state.mainThread ? "main" :
		(state.gameThread && id == state.gameThread) ? "game" : "other";
}

/* The tail both seams share: the line through the writer, the dump
 * (<prefix>_<infix><date>_<time>.dmp, MiniDumpNormal), the line with dump=
 * into crash.txt, and the dump= note through the writer. `info` is the
 * crash's exception, 0 for a hang. */
inline void CrashSeamReport(const char* kind, const char* infix,
	const char* line, PEXCEPTION_POINTERS info, DWORD threadId)
{
	CrashSeamState& state = CrashSeam();

	if (state.writer)
		state.writer(line);

	// The dump's name: the prefix, the kind and the local time to the second.
	SYSTEMTIME now;
	char dumpName[96];

	GetLocalTime(&now);
	_snprintf_s(dumpName, sizeof(dumpName), _TRUNCATE,
		"%s_%s%04u%02u%02u_%02u%02u%02u.dmp", state.prefix, infix, now.wYear,
		now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

	bool dumped = false;
	const char* step = "load";
	DWORD err = 0;
	HMODULE help = LoadLibraryA("dbghelp.dll");
	CrashSeamDumpFn dump = 0;

	if (!help)
		err = GetLastError();
	else
	{
		step = "proc";
		dump = (CrashSeamDumpFn)GetProcAddress(help, "MiniDumpWriteDump");

		if (!dump)
			err = GetLastError();
	}

	if (dump)
	{
		step = "file";

		HANDLE file = CreateFileA(dumpName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, 0);

		if (file == INVALID_HANDLE_VALUE)
			err = GetLastError();
		else
		{
			CrashSeamDumpInfo ex = { threadId, info, FALSE };

			// 0 is MiniDumpNormal.
			step = "write";
			dumped = dump(GetCurrentProcess(), GetCurrentProcessId(), file,
				0, info ? (PEXCEPTION_POINTERS)&ex : 0, 0, 0) != FALSE;

			if (!dumped)
				err = GetLastError();

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
			fprintf(out, "%s dump=%s err=%lu step=%s\n", line,
				dumped ? dumpName : "none", (unsigned long)err, step);
			fclose(out);
		}
	}

	if (state.writer)
	{
		char note[160];

		_snprintf_s(note, sizeof(note), _TRUNCATE, "%s dump=%s err=%lu step=%s",
			kind, dumped ? dumpName : "none", (unsigned long)err, step);
		state.writer(note);
	}
}

inline LONG WINAPI CrashSeamFilter(PEXCEPTION_POINTERS info)
{
	const DWORD code = info && info->ExceptionRecord ?
		info->ExceptionRecord->ExceptionCode : 0;
	void* at = info && info->ExceptionRecord ?
		info->ExceptionRecord->ExceptionAddress : 0;
	char module[MAX_PATH];
	unsigned long long offset;
	char line[512];

	CrashSeamWhere(at, module, sizeof(module), &offset);
	_snprintf_s(line, sizeof(line), _TRUNCATE,
		"CRASH code=0x%08lX at=0x%p module=%s+0x%llX thread=%s",
		(unsigned long)code, at, module, offset,
		CrashSeamThreadName(GetCurrentThreadId()));
	CrashSeamReport("CRASH", "", line, info, GetCurrentThreadId());

	return EXCEPTION_CONTINUE_SEARCH;
}

/* The hang seam: where a thread that stopped answering stands. The caller
 * names the thread and the moment - `when` is "focus" when the window lost
 * the focus and the game thread never reached its frame wait, "close" when
 * the window closed and it never ended. The thread is suspended for the
 * read of its registers and resumed at once, before anything here that
 * takes a lock - the log's stream, the heap, the loader - since the
 * suspended thread may hold one. Then the line
 *
 *   HANG at=0x<address> module=<file>+0x<offset> thread=game id=0x<id>
 *        when=<moment>
 *
 * and the dump as a crash's, named <prefix>_hang_<date>_<time>.dmp:
 * MiniDumpWriteDump suspends the threads itself for the write, so the dump
 * holds the thread where it circles. */
inline void CrashSeamHang(HANDLE thread, DWORD threadId, const char* when)
{
	CONTEXT ctx;
	void* at = 0;

	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_CONTROL;

	if (SuspendThread(thread) != (DWORD)-1)
	{
		if (GetThreadContext(thread, &ctx))
#ifdef _WIN64
			at = (void*)ctx.Rip;
#else
			at = (void*)ctx.Eip;
#endif

		ResumeThread(thread);
	}

	char module[MAX_PATH];
	unsigned long long offset;
	char line[512];

	CrashSeamWhere(at, module, sizeof(module), &offset);
	_snprintf_s(line, sizeof(line), _TRUNCATE,
		"HANG at=0x%p module=%s+0x%llX thread=%s id=0x%lX when=%s", at, module,
		offset, CrashSeamThreadName(threadId), (unsigned long)threadId, when);
	CrashSeamReport("HANG", "hang_", line, 0, threadId);
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

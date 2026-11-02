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
#include "pool_guard.h"

// Empty on every build but the Asan one. NOT the engine's pch.h: the seam
// needs nothing of the engine but the crash seam's header.
#ifdef __SANITIZE_ADDRESS__
#include "crashseam.h"
#include <stdlib.h>

/* The sanitizer's runtime interface (sanitizer\asan_interface.h and
 * common_interface_defs.h of the compiler's runtime sources), declared here
 * as pool_guard.h declares the poisoning: those headers are not on the
 * include path. */
extern "C"
{
	void __asan_set_death_callback(void (*callback)(void));
	void __asan_set_error_report_callback(void (*callback)(const char*));
	void __sanitizer_set_report_path(const char* path);
	void* __asan_get_report_pc(void);
	void* __asan_get_report_address(void);
	int __asan_get_report_access_type(void);
	size_t __asan_get_report_access_size(void);
	const char* __asan_get_report_description(void);
}

static void (*g_writer)(const char* line);
static char g_reportFile[MAX_PATH];	// <path>.<pid>, or empty
static void* g_framePc;			// the report's first frame

/* At an error the runtime hands the whole report here before it dies:
 * every line of it into the log as ASANLOG, and the address of its first
 * frame (#0 0x...) kept for the CRASH line. For an access violation or a
 * division by zero, which the runtime reports itself (the engine's
 * exception filter never sees them in this build), the runtime's own pc
 * is 0 and this address is the only one. */
static void AsanReportLines(const char* text)
{
	const char* frame = strstr(text, "#0 0x");
	char line[512];

	if (frame)
		g_framePc = (void*)(uintptr_t)_strtoui64(frame + 3, 0, 16);

	if (!g_writer)
		return;

	for (const char* at = text; *at; )
	{
		const char* end = strchr(at, '\n');
		size_t n = end ? (size_t)(end - at) : strlen(at);

		if (n && at[n - 1] == '\r')
			n--;

		if (n)
		{
			_snprintf_s(line, sizeof(line), _TRUNCATE, "ASANLOG %.*s", (int)n,
				at);
			g_writer(line);
		}

		if (!end)
			break;

		at = end + 1;
	}
}

/* The runtime's last call before the process ends (TerminateProcess, exit
 * code 1): the crash seam's line and dump, the line in the CRASH form with
 * code=asan and the report's own words behind thread=. */
static void AsanDeath()
{
	void* pc = __asan_get_report_pc();
	const char* kind = __asan_get_report_description();
	const size_t size = __asan_get_report_access_size();
	char module[MAX_PATH];
	unsigned long long offset;
	char line[768];

	if (!pc)
		pc = g_framePc;

	CrashSeamWhere(pc, module, sizeof(module), &offset);

	const bool file = g_reportFile[0] &&
		GetFileAttributesA(g_reportFile) != INVALID_FILE_ATTRIBUTES;

	_snprintf_s(line, sizeof(line), _TRUNCATE,
		"CRASH code=asan at=0x%p module=%s+0x%llX thread=%s kind=%s "
		"addr=0x%p access=%s size=%zu report=%s", pc, module, offset,
		CrashSeamThreadName(GetCurrentThreadId()), kind ? kind : "?",
		__asan_get_report_address(), !size ? "-" :
		__asan_get_report_access_type() ? "write" : "read", size,
		file ? g_reportFile : "none");
	CrashSeamReport("CRASH", "", line, 0, GetCurrentThreadId());
}

void AsanSeamInstall(void (*writer)(const char* line))
{
	const char* options = getenv("ASAN_OPTIONS");
	const char* given = options ? strstr(options, "log_path=") : 0;
	char path[MAX_PATH] = "asan_report";

	g_writer = writer;

	/* The report's file: log_path from ASAN_OPTIONS when it names one (up
	 * to the next ':' or blank, or in quotes), else asan_report in the
	 * working directory, beside log.txt. The runtime adds .<pid>. */
	if (given)
	{
		const char* at = given + 9;
		const char stop = *at == '"' || *at == '\'' ? *at++ : 0;
		size_t n = 0;

		while (at[n] && n + 1 < sizeof(path) &&
			(stop ? at[n] != stop : at[n] != ':' && at[n] != ' '))
			n++;

		memcpy(path, at, n);
		path[n] = 0;
	}
	else
		__sanitizer_set_report_path(path);

	if (path[0] && strcmp(path, "stderr") && strcmp(path, "stdout"))
		_snprintf_s(g_reportFile, sizeof(g_reportFile), _TRUNCATE, "%s.%lu",
			path, (unsigned long)GetCurrentProcessId());

	__asan_set_error_report_callback(AsanReportLines);
	__asan_set_death_callback(AsanDeath);
}
#endif

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

/*
 * AddrLine - addresses of an engine build as `file:line function`.
 *
 *   AddrLine.exe <exe> [-ret] <rva> [<rva> ...]
 *   AddrLine.exe <exe> [-ret] @<file>
 *
 * An address is relative to the module's base (hex, with or without 0x),
 * as -rngtrace writes them (specific\function_stubs.cpp): the EXE's
 * preferred base from its PE header makes it a virtual address of the
 * module loaded there. A file holds addresses separated by blanks, commas
 * or line ends. -ret: the addresses are return addresses, looked up one
 * byte back, inside the call that left them - a call that ends its line
 * would otherwise be named by the line after it.
 *
 * The symbols come from the PDB our build wrote beside the EXE, through
 * Windows' own dbghelp.dll and nothing else: the search path is the EXE's
 * folder alone, _NT_SYMBOL_PATH is ignored and cleared in this process, no
 * symbol server is asked. When the PDB the EXE names is not in that folder
 * (an archived copy renamed with the EXE), the PDB of the EXE's own name
 * beside it is loaded directly - and either way its GUID and age are
 * compared with the EXE's debug record, and a PDB of another build is
 * refused rather than read.
 *
 * Output: one line per address, `<rva> <file>:<line> <function>+0x<disp>`,
 * the file after its last "\TOMB5\" (game\control.cpp), the displacement
 * that of the byte looked up; `?` where the PDB has no answer.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dbghelp.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

#ifndef SYMOPT_DISABLE_SYMSRV_AUTODETECTION
#define SYMOPT_DISABLE_SYMSRV_AUTODETECTION 0x02000000
#endif

struct PeInfo
{
	unsigned long long base;	// the preferred base
	unsigned long size;			// SizeOfImage
	GUID guid;					// the CodeView record's
	unsigned long age;
	char pdb[MAX_PATH];			// the PDB name the EXE carries
	bool codeview;
};

static bool ReadWhole(const char* path, std::vector<unsigned char>& out)
{
	FILE* f = fopen(path, "rb");

	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? (size_t)n : 0);
	const size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
	fclose(f);
	return n > 0 && got == (size_t)n;
}

// A file offset for an RVA, through the section table; 0 when none holds it.
static size_t RvaToOffset(const std::vector<unsigned char>& file,
	const IMAGE_SECTION_HEADER* sec, int count, unsigned long rva)
{
	for (int i = 0; i < count; i++)
	{
		const unsigned long va = sec[i].VirtualAddress;
		const unsigned long len = sec[i].SizeOfRawData;

		if (rva >= va && rva < va + len)
		{
			const size_t at = sec[i].PointerToRawData + (rva - va);
			return at < file.size() ? at : 0;
		}
	}

	return 0;
}

/* The PE header's base and size and the CodeView record (RSDS: GUID, age,
 * PDB name), from a PE32 or a PE32+ file. */
static bool ReadPe(const char* path, PeInfo* pe)
{
	std::vector<unsigned char> file;
	const IMAGE_DATA_DIRECTORY* debug;
	const IMAGE_SECTION_HEADER* sec;
	int count;

	memset(pe, 0, sizeof(*pe));

	if (!ReadWhole(path, file) || file.size() < sizeof(IMAGE_DOS_HEADER))
		return false;

	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)file.data();

	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
		(size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > file.size())
		return false;

	const unsigned char* nt = file.data() + dos->e_lfanew;
	const IMAGE_FILE_HEADER* fh = (const IMAGE_FILE_HEADER*)(nt + 4);
	const WORD magic = *(const WORD*)(nt + 4 + sizeof(IMAGE_FILE_HEADER));

	if (*(const DWORD*)nt != IMAGE_NT_SIGNATURE)
		return false;

	if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		const IMAGE_NT_HEADERS64* h = (const IMAGE_NT_HEADERS64*)nt;
		pe->base = h->OptionalHeader.ImageBase;
		pe->size = h->OptionalHeader.SizeOfImage;
		debug = &h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	}
	else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		const IMAGE_NT_HEADERS32* h = (const IMAGE_NT_HEADERS32*)nt;
		pe->base = h->OptionalHeader.ImageBase;
		pe->size = h->OptionalHeader.SizeOfImage;
		debug = &h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	}
	else
		return false;

	sec = (const IMAGE_SECTION_HEADER*)(nt + 4 + sizeof(IMAGE_FILE_HEADER) +
		fh->SizeOfOptionalHeader);
	count = fh->NumberOfSections;

	if ((const unsigned char*)(sec + count) > file.data() + file.size())
		return false;

	const size_t at = RvaToOffset(file, sec, count, debug->VirtualAddress);

	for (unsigned long i = 0; at && debug->Size &&
		i < debug->Size / sizeof(IMAGE_DEBUG_DIRECTORY); i++)
	{
		const size_t d = at + i * sizeof(IMAGE_DEBUG_DIRECTORY);

		if (d + sizeof(IMAGE_DEBUG_DIRECTORY) > file.size())
			break;

		const IMAGE_DEBUG_DIRECTORY* dd =
			(const IMAGE_DEBUG_DIRECTORY*)(file.data() + d);
		const size_t r = dd->PointerToRawData;

		if (dd->Type != IMAGE_DEBUG_TYPE_CODEVIEW || r + 24 > file.size() ||
			memcmp(file.data() + r, "RSDS", 4))
			continue;

		memcpy(&pe->guid, file.data() + r + 4, sizeof(GUID));
		memcpy(&pe->age, file.data() + r + 20, 4);

		size_t k = 0;

		for (; r + 24 + k < file.size() && k + 1 < sizeof(pe->pdb) &&
			file[r + 24 + k]; k++)
			pe->pdb[k] = (char)file[r + 24 + k];

		pe->pdb[k] = 0;
		pe->codeview = true;
		break;
	}

	return true;
}

static bool FileThere(const std::string& path)
{
	const DWORD a = GetFileAttributesA(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// A file's path after its last "\TOMB5\", any case; the whole path else.
static const char* ShortFile(const char* path)
{
	const char* best = 0;

	for (const char* p = path; *p; p++)
		if (!_strnicmp(p, "\\TOMB5\\", 7))
			best = p + 7;

	return best ? best : path;
}

static void ReadAddrs(const char* text, std::vector<unsigned long long>& out)
{
	const char* p = text;

	while (*p)
	{
		while (*p && !isxdigit((unsigned char)*p))
			p++;

		if (!*p)
			break;

		if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
			p += 2;

		char* end;
		const unsigned long long v = _strtoui64(p, &end, 16);

		if (end == p)
		{
			p++;
			continue;
		}

		out.push_back(v);
		p = end;
	}
}

int main(int argc, char** argv)
{
	std::vector<unsigned long long> addrs;
	bool ret = false;
	PeInfo pe;

	if (argc < 3)
	{
		printf("usage: AddrLine <exe> [-ret] <rva> [<rva> ...]\n"
			"       AddrLine <exe> [-ret] @<file>\n");
		return 2;
	}

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-ret"))
			ret = true;
		else if (argv[i][0] == '@')
		{
			std::vector<unsigned char> text;

			if (!ReadWhole(argv[i] + 1, text))
			{
				printf("AddrLine: cannot read %s\n", argv[i] + 1);
				return 1;
			}

			text.push_back(0);
			ReadAddrs((const char*)text.data(), addrs);
		}
		else
			ReadAddrs(argv[i], addrs);
	}

	char full[MAX_PATH];

	if (!GetFullPathNameA(argv[1], MAX_PATH, full, 0) || !ReadPe(full, &pe))
	{
		printf("AddrLine: %s is no PE file\n", argv[1]);
		return 1;
	}

	std::string dir = full;
	const size_t slash = dir.find_last_of("\\/");
	dir = slash == std::string::npos ? "." : dir.substr(0, slash);

	/* No symbol server, no environment path: only the EXE's folder. The
	 * variables are cleared in this process before dbghelp starts, and
	 * SYMOPT_IGNORE_NT_SYMPATH says the same to dbghelp itself. */
	SetEnvironmentVariableA("_NT_SYMBOL_PATH", 0);
	SetEnvironmentVariableA("_NT_ALTERNATE_SYMBOL_PATH", 0);
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES |
		SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS |
		SYMOPT_IGNORE_NT_SYMPATH | SYMOPT_DISABLE_SYMSRV_AUTODETECTION);

	HANDLE proc = GetCurrentProcess();

	if (!SymInitialize(proc, dir.c_str(), FALSE))
	{
		printf("AddrLine: SymInitialize failed (%lu)\n", GetLastError());
		return 1;
	}

	SymSetSearchPath(proc, dir.c_str());

	// The PDB the EXE names, in its folder; else the one of the EXE's name.
	const char* named = strrchr(pe.pdb, '\\');
	named = named ? named + 1 : pe.pdb;
	std::string pdb = dir + "\\" + named;
	std::string load = full;

	if (!pe.codeview || !named[0] || !FileThere(pdb))
	{
		pdb = full;
		const size_t dot = pdb.find_last_of('.');

		if (dot != std::string::npos && dot > dir.size())
			pdb = pdb.substr(0, dot);

		pdb += ".pdb";
		load = pdb;
	}

	const DWORD64 base = SymLoadModuleEx(proc, 0, load.c_str(), 0, pe.base,
		pe.size, 0, 0);
	IMAGEHLP_MODULE64 mod;

	memset(&mod, 0, sizeof(mod));
	mod.SizeOfStruct = sizeof(mod);

	if (!base || !SymGetModuleInfo64(proc, base, &mod) ||
		mod.SymType != SymPdb)
	{
		printf("AddrLine: no PDB for %s in %s\n", full, dir.c_str());
		SymCleanup(proc);
		return 1;
	}

	const bool match = pe.codeview && !memcmp(&mod.PdbSig70, &pe.guid,
		sizeof(GUID)) && mod.PdbAge == pe.age;

	printf("module %s base=0x%llx size=0x%lx pdb=%s match=%s\n", full,
		(unsigned long long)base, pe.size, mod.LoadedPdbName,
		match ? "yes" : "NO");

	if (!match)
	{
		printf("AddrLine: the PDB is not this build's (GUID or age)\n");
		SymCleanup(proc);
		return 1;
	}

	char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
	SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;

	for (size_t i = 0; i < addrs.size(); i++)
	{
		const DWORD64 at = base + addrs[i] - (ret ? 1 : 0);
		DWORD64 disp = 0;
		DWORD ldisp = 0;
		IMAGEHLP_LINE64 line;
		char where[MAX_PATH + 32] = "?";
		char what[MAX_SYM_NAME + 32] = "?";

		memset(buf, 0, sizeof(buf));
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);

		if (SymGetLineFromAddr64(proc, at, &ldisp, &line))
			_snprintf(where, sizeof(where) - 1, "%s:%lu",
				ShortFile(line.FileName), line.LineNumber);

		if (SymFromAddr(proc, at, &disp, sym))
			_snprintf(what, sizeof(what) - 1, "%s+0x%llx", sym->Name,
				(unsigned long long)disp);

		printf("%llx %s %s\n", addrs[i], where, what);
	}

	SymCleanup(proc);
	return 0;
}

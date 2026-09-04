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
#include "../tomb5/pch.h"
#include "hdscreen.h"
#include "bc7.h"
#include "function_stubs.h"
#include "../game/gameflow.h"

// Optional high resolution loading screens.
//
// Chronicles keeps its 21 loading pictures in screens\SCREENS.STR: raw
// 640x480 A1R5G5B5, 614400 bytes each, no header of any kind, picture n at
// offset n * 614400. The remaster ships the same pictures redrawn at
// 1920x1080 as BC7 compressed .DDS files, one per level, named after the
// level: ANDREA1.DDS, JOBY2.DDS, RICH3.DDS and so on, plus TITLE.DDS and four
// localised logos LOGO_DE/EU/FR/US.DDS.
//
// If the player copies those into screens\HD\, this reads them instead. If
// the folder is not there, nothing here happens and the game shows Core's own
// pictures exactly as before - the same deal as the HQ sound bank in
// hqsfx.cpp and the HQ music in audio.cpp. The files are the player's own
// data and are NEVER shipped with this project.
//
// The name is what makes this work under the randomizer. The loading picture
// has to follow the level, not the slot it happens to sit in, and the level's
// own file name out of SCRIPT.DAT is the one thing that always says which
// level is really being loaded.
//
// Direct3D 9 predates BC7 by a decade and cannot sample it, so the blocks are
// unpacked on the CPU in bc7.cpp and handed over as plain 32 bit pixels.

static void HDScreenPath(char* dest, const char* name)
{
	strcpy(dest, "screens\\HD\\");
	strcat(dest, name);
	strcat(dest, ".DDS");
}

const char* HDScreenLogo()
{
	// Only four logo files exist. English, and everything the remaster did
	// not draw its own logo for, get the European one.
	switch (Gameflow->Language)
	{
	case LNG_GERMAN:
		return "LOGO_DE";

	case LNG_FRENCH:
		return "LOGO_FR";

	case LNG_US:
		return "LOGO_US";

	default:
		return "LOGO_EU";
	}
}

const char* HDScreenLevelName(const char* wadName)
{
	const char* s;

	if (!wadName || !*wadName)
		return 0;

	s = strrchr(wadName, '\\');
	s = s ? s + 1 : wadName;
	if (!*s)
		return 0;

	// The Remaster ships no picture for the Security Breach cutscene;
	// it borrows Red Alert's (user decision, 2026-09-04).
	if (!_stricmp(s, "RICHCUT2"))
		return "RICH3";

	return s;
}

ulong* HDScreenRead(const char* name, long maxSide, long* width, long* height)
{
	FILE* fp;
	char path[80];
	uchar* raw;
	ulong* px;
	const char* why;
	long size;

	*width = 0;
	*height = 0;

	if (!name || !*name || strlen(name) > 40)
		return 0;

	HDScreenPath(path, name);
	fp = fopen(path, "rb");

	if (!fp)
		return 0;		// no HD folder, or no picture for this level - normal

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size < 148 || size > 64 * 1024 * 1024)
	{
		Log("HD screen: %s has an implausible size (%d bytes) - using the original picture", path, size);
		fclose(fp);
		return 0;
	}

	raw = (uchar*)malloc(size);

	if (!raw)
	{
		Log("HD screen: no memory for %s - using the original picture", path);
		fclose(fp);
		return 0;
	}

	if (fread(raw, 1, size, fp) != (size_t)size)
	{
		Log("HD screen: %s could not be read - using the original picture", path);
		free(raw);
		fclose(fp);
		return 0;
	}

	fclose(fp);
	px = BC7DecodeDDS(raw, size, maxSide, width, height, &why);
	free(raw);

	if (!px)
	{
		Log("HD screen: %s refused - %s - using the original picture", path, why);
		return 0;
	}

	Log("HD screen: %s decoded, %d x %d", path, *width, *height);
	return px;
}

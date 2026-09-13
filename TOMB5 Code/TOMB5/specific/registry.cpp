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
#include "registry.h"
#include "LoadSave.h"
#include "winmain.h"
#include "input.h"
#include "setupdlg.h"
#include "../game/gameflow.h"	// LanguageChoice

static HKEY phkResult;
static DWORD dwDisposition;
static bool REG_Setup;

bool REG_OpenKey(LPCSTR lpSubKey)
{
	return RegCreateKeyEx(HKEY_CURRENT_USER, lpSubKey, 0, (CHAR*)"", REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 0, &phkResult, &dwDisposition) == ERROR_SUCCESS;
}

bool OpenRegistry(LPCSTR SubKeyName)
{
	char buffer[256];

	if (!SubKeyName)
		return REG_OpenKey("Software\\Core Design\\Tomb Raider Chronicles");

	sprintf(buffer, "%s\\%s", "Software\\Core Design\\Tomb Raider Chronicles", SubKeyName);
	return REG_OpenKey(buffer);
}

void REG_CloseKey()
{
	RegCloseKey(phkResult);
}

void CloseRegistry()
{
	REG_CloseKey();
}

bool REG_KeyWasCreated()
{
	return dwDisposition == REG_CREATED_NEW_KEY;
}

/* READ ONE VALUE WITHOUT OPENING THE DOOR (#148, order 6).
 *
 * Every other reader here goes through OpenRegistry, and OpenRegistry
 * CREATES the key it is handed (RegCreateKeyEx above) and records in
 * dwDisposition whether it had to - which is how init_tomb5_stuff knows to
 * write the whole set of defaults on a first run. So a second reader that
 * opened the tomb5 key EARLIER would silently take that first run away, and
 * the player would be left with a key holding one value and no defaults.
 *
 * This is that second reader: it opens read-only, creates nothing, touches
 * neither phkResult nor dwDisposition, and answers false when the key or the
 * value is not there. One caller - the VSync option, which the device needs
 * before the options are read (specific\winmain.cpp, DXCreate). */
bool REG_PeekLong(LPCSTR SubKeyName, LPCSTR ValueName, ulong& value)
{
	char buffer[256];
	HKEY key;
	ulong type, cbData;
	bool got;

	if (SubKeyName)
		sprintf(buffer, "%s\\%s", "Software\\Core Design\\Tomb Raider Chronicles", SubKeyName);
	else
		strcpy(buffer, "Software\\Core Design\\Tomb Raider Chronicles");

	if (RegOpenKeyEx(HKEY_CURRENT_USER, buffer, 0, KEY_READ, &key) != ERROR_SUCCESS)
		return false;

	cbData = 4;
	got = RegQueryValueEx(key, ValueName, 0, &type, (LPBYTE)&value, &cbData) == ERROR_SUCCESS &&
		type == REG_DWORD && cbData == 4;
	RegCloseKey(key);
	return got;
}

/* THE SAME DOOR, FOR ONE REG_SZ (#153, round M2, the fix round).
 *
 * REG_ReadString is the ordinary reader and writes its default into the key
 * on a miss, which is right for a setting the options menu owns. hd_outfit is
 * not one: it is written by the in-game outfit changer and read at every
 * level build, an absent value means "dress her by the level's own facts",
 * and writing an empty string there on the first level of a fresh install
 * would take away init_tomb5_stuff's first run exactly as REG_PeekLong's note
 * describes. So this one opens read-only, creates nothing, touches neither
 * phkResult nor dwDisposition, and hands back "" when there is nothing there.
 *
 * Only a REG_SZ counts. A value of another type is a value somebody put there
 * by hand for another purpose, and reading its bytes as a string is how a
 * registry reader ends up printing a page of rubbish into a log. */
bool REG_PeekString(LPCSTR SubKeyName, LPCSTR ValueName, char* value, ulong size)
{
	char buffer[256];
	HKEY key;
	ulong type, cbData;
	bool got;

	if (!value || !size)
		return false;

	value[0] = 0;

	if (SubKeyName)
		sprintf(buffer, "%s\\%s", "Software\\Core Design\\Tomb Raider Chronicles", SubKeyName);
	else
		strcpy(buffer, "Software\\Core Design\\Tomb Raider Chronicles");

	if (RegOpenKeyEx(HKEY_CURRENT_USER, buffer, 0, KEY_READ, &key) != ERROR_SUCCESS)
		return false;

	cbData = size;
	got = RegQueryValueEx(key, ValueName, 0, &type, (LPBYTE)value, &cbData) == ERROR_SUCCESS &&
		type == REG_SZ;
	RegCloseKey(key);

	if (!got)
	{
		value[0] = 0;
		return false;
	}

	// RegQueryValueEx does not promise a terminator: it hands back the bytes
	// that were written, and a string written without one comes back without
	// one. cbData is what it wrote, capped at the buffer.
	if (cbData >= size)
		cbData = size - 1;

	value[cbData] = 0;
	return value[0] != 0;
}

void REG_WriteLong(char* SubKeyName, ulong value)
{
	RegSetValueEx(phkResult, SubKeyName, 0, REG_DWORD, (CONST BYTE*) & value, sizeof(ulong));
}

void REG_WriteBool(char* SubKeyName, bool value)
{
	ulong Lvalue;

	Lvalue = (ulong)value;
	RegSetValueEx(phkResult, SubKeyName, 0, REG_DWORD, (CONST BYTE*) & Lvalue, sizeof(ulong));
}

bool REG_ReadLong(char* SubKeyName, ulong& value, ulong defaultValue)
{
	ulong type;
	ulong cbData;

	cbData = 4;

	if (RegQueryValueEx(phkResult, SubKeyName, 0, &type, (LPBYTE)&value, &cbData) == ERROR_SUCCESS && type == REG_DWORD && cbData == 4)
		return 1;

	REG_WriteLong(SubKeyName, defaultValue);	//value not found, write default
	value = defaultValue;
	return 0;
}

bool REG_ReadBool(char* SubKeyName, bool& value, bool defaultValue)
{
	ulong type;
	ulong cbData;
	ulong data;

	cbData = 4;

	if (RegQueryValueEx(phkResult, SubKeyName, 0, &type, (LPBYTE)&data, &cbData) == ERROR_SUCCESS && type == REG_DWORD && cbData == 4)
	{
		value = (bool)data;
		return 1;
	}

	REG_WriteBool(SubKeyName, defaultValue);
	value = defaultValue;
	return 0;
}

void REG_WriteString(char* SubKeyName, const char* value)
{
	if (!value)
		value = "";

	RegSetValueEx(phkResult, SubKeyName, 0, REG_SZ, (CONST BYTE*)value, (ulong)strlen(value) + 1);
}

bool REG_ReadString(char* SubKeyName, char* value, ulong size, const char* defaultValue)
{
	ulong type;
	ulong cbData;

	if (!value || !size)
		return 0;

	if (!defaultValue)
		defaultValue = "";

	cbData = size;

	// RegQueryValueEx does NOT promise a terminating zero for REG_SZ - a
	// value somebody wrote by hand may have none - so the last byte is
	// nailed down afterwards, and a value that fills the whole buffer is
	// treated as a miss rather than silently cut in half.
	if (RegQueryValueEx(phkResult, SubKeyName, 0, &type, (LPBYTE)value, &cbData) == ERROR_SUCCESS &&
		type == REG_SZ && cbData < size)
	{
		value[cbData] = 0;
		value[size - 1] = 0;
		return 1;
	}

	REG_WriteString(SubKeyName, defaultValue);
	strncpy(value, defaultValue, size - 1);
	value[size - 1] = 0;
	return 0;
}

bool LoadSettings()
{
	ulong key;
	bool val;

	if (!OpenRegistry("System"))
		return 0;

	REG_ReadBool((char*)"Setup", REG_Setup, 0);

	if (REG_Setup)
	{
		App.TextureSize = 256;
		App.BumpMapSize = 256;
		App.StartFlags = DXF_FPUSETUP;

		REG_ReadLong((char*)"DD", (ulong&)App.DXInfo.nDD, 0);
		REG_ReadLong((char*)"D3D", (ulong&)App.DXInfo.nD3D, 0);
		REG_ReadLong((char*)"VMode", (ulong&)App.DXInfo.nDisplayMode, 0);
		REG_ReadLong((char*)"TFormat", (ulong&)App.DXInfo.nTexture, 0);
		REG_ReadLong((char*)"DS", (ulong&)App.DXInfo.nDS, 0);

		// Those five are indices into the lists DXGetInfo has just built - it
		// runs before LoadSettings (winmain.cpp) - and they come out of the
		// registry exactly as they were written once: possibly by a different
		// build, on different hardware, or before a monitor was unplugged.
		// LoadTextures then walks all of them in a single line:
		//
		//     DDInfo[nDD].D3DDevices[nD3D].TextureInfos[nTexture]
		//
		// Nothing checked any of it. A saved "DD=2" on a machine that now has
		// one screen reads past the end of the adapter list, and so does any
		// TFormat above 0 written by an older DirectX 3 build - the D3D9
		// enumeration yields exactly one texture format per device, where the
		// old one offered several. Same class as the item and room numbers in
		// LoadItems: a value from outside indexing an array that has no bounds
		// check of its own.
		//
		// Corrected rather than refused. An out-of-date monitor number should
		// start the game on the primary screen, not stop it - the setup dialog
		// writes a fresh, valid choice the moment it is opened again.
		if (App.DXInfo.nDDInfo > 0)
		{
			DXDIRECTDRAWINFO* dd;
			DXD3DDEVICE* dev;

			if (App.DXInfo.nDD < 0 || App.DXInfo.nDD >= App.DXInfo.nDDInfo)
				App.DXInfo.nDD = 0;

			dd = &App.DXInfo.DDInfo[App.DXInfo.nDD];

			if (App.DXInfo.nD3D < 0 || App.DXInfo.nD3D >= dd->nD3DDevices)
				App.DXInfo.nD3D = 0;

			if (dd->nD3DDevices > 0)
			{
				dev = &dd->D3DDevices[App.DXInfo.nD3D];

				if (App.DXInfo.nTexture < 0 || App.DXInfo.nTexture >= dev->nTextureInfos)
					App.DXInfo.nTexture = 0;

				if (App.DXInfo.nDisplayMode < 0 || App.DXInfo.nDisplayMode >= dev->nDisplayModes)
					App.DXInfo.nDisplayMode = 0;
			}
		}

		if (App.DXInfo.nDS < 0 || App.DXInfo.nDS >= App.DXInfo.nDSInfo)
			App.DXInfo.nDS = 0;

		App.BumpMapping = 0;
		REG_ReadBool((char*)"Filter", App.Filtering, 1);
		REG_ReadBool((char*)"DisableSound", App.SoundDisabled, 0);
		REG_ReadBool((char*)"Volumetric", App.Volumetric, 1);
		REG_ReadBool((char*)"NoFMV", fmvs_disabled, 0);

		REG_ReadBool((char*)"TextLow", val, 0);

		if (val)
			App.TextureSize = 128;

		App.StartFlags |= DXF_HWR | DXF_ZBUFFER;

		REG_ReadBool((char*)"Window", val, 0);

		if (val)
			App.StartFlags |= DXF_WINDOWED;
		else
			App.StartFlags |= DXF_FULLSCREEN;
	}

	CloseRegistry();

	OpenRegistry("Game");

	REG_ReadLong((char*)"Key0", key, layout[0][0]);
	layout[1][0] = (short)key;

	REG_ReadLong((char*)"Key1", key, layout[0][1]);
	layout[1][1] = (short)key;

	REG_ReadLong((char*)"Key2", key, layout[0][2]);
	layout[1][2] = (short)key;

	REG_ReadLong((char*)"Key3", key, layout[0][3]);
	layout[1][3] = (short)key;

	REG_ReadLong((char*)"Key4", key, layout[0][4]);
	layout[1][4] = (short)key;

	REG_ReadLong((char*)"Key5", key, layout[0][5]);
	layout[1][5] = (short)key;

	REG_ReadLong((char*)"Key6", key, layout[0][6]);
	layout[1][6] = (short)key;

	REG_ReadLong((char*)"Key7", key, layout[0][7]);
	layout[1][7] = (short)key;

	REG_ReadLong((char*)"Key8", key, layout[0][8]);
	layout[1][8] = (short)key;

	REG_ReadLong((char*)"Key9", key, layout[0][9]);
	layout[1][9] = (short)key;

	REG_ReadLong((char*)"Key10", key, layout[0][10]);
	layout[1][10] = (short)key;

	REG_ReadLong((char*)"Key11", key, layout[0][11]);
	layout[1][11] = (short)key;

	REG_ReadLong((char*)"Key12", key, layout[0][12]);
	layout[1][12] = (short)key;

	REG_ReadLong((char*)"Key13", key, layout[0][13]);
	layout[1][13] = (short)key;

	REG_ReadLong((char*)"Key14", key, layout[0][14]);
	layout[1][14] = (short)key;

	REG_ReadLong((char*)"Key15", key, layout[0][15]);
	layout[1][15] = (short)key;

	REG_ReadLong((char*)"Key16", key, layout[0][16]);
	layout[1][16] = (short)key;

	REG_ReadLong((char*)"Key17", key, layout[0][17]);
	layout[1][17] = (short)key;

	REG_ReadLong((char*)"JDck", (ulong&)jLayout[0], 5);
	REG_ReadLong((char*)"JDsh", (ulong&)jLayout[1], 3);
	REG_ReadLong((char*)"JWlk", (ulong&)jLayout[2], 4);
	REG_ReadLong((char*)"JJmp", (ulong&)jLayout[3], 0);
	REG_ReadLong((char*)"JAct", (ulong&)jLayout[4], 1);
	REG_ReadLong((char*)"JDrw", (ulong&)jLayout[5], 2);
	REG_ReadLong((char*)"JFlr", (ulong&)jLayout[6], 9);
	REG_ReadLong((char*)"JLok", (ulong&)jLayout[7], 6);
	REG_ReadLong((char*)"JRol", (ulong&)jLayout[8], 7);
	REG_ReadLong((char*)"JInv", (ulong&)jLayout[9], 8);
	REG_ReadLong((char*)"JPau", (ulong&)jLayout[10], 10);
	REG_ReadLong((char*)"JSav", (ulong&)jLayout[11], 11);
	REG_ReadLong((char*)"JLod", (ulong&)jLayout[12], 12);
	REG_ReadLong((char*)"JSmd", (ulong&)jLayout[13], 13);
	REG_ReadLong((char*)"JBmd", (ulong&)jLayout[14], 14);

	REG_ReadLong((char*)"MusicVolume", (ulong&)MusicVolume, 80);
	REG_ReadLong((char*)"SFXVolume", (ulong&)SFXVolume, 90);
	REG_ReadLong((char*)"ControlMethod", (ulong&)ControlMethod, 0);

	// 2 (the old reset position) could get saved along in earlier states -
	// since reset became a pure menu action, only 0/1 exist.
	if (ControlMethod > 1)
		ControlMethod = 0;

	REG_ReadLong((char*)"JoyStyle", (ulong&)JoyStyle, (ulong)-1);	// -1 = automatic: XInput pad -> Xbox page, else PS5 (input.cpp)
	REG_ReadLong((char*)"Language", (ulong&)LanguageChoice, (ulong)-1);	// -1 = automatic: first loadable language file (gameflow.cpp)
	REG_ReadLong((char*)"SoundQuality", (ulong&)SoundQuality, 1);
	REG_ReadLong((char*)"AutoTarget", (ulong&)App.AutoTarget, 1);
	REG_ReadLong((char*)"WindowX", (ulong&)App.dx.rScreen.left, 0);
	REG_ReadLong((char*)"WindowY", (ulong&)App.dx.rScreen.top, 0);

	CloseRegistry();
	CheckKeyConflicts();
	return REG_Setup;
}

void SaveSettings()
{
	OpenRegistry("Game");
	REG_WriteLong((char*)"Key0", layout[1][0]);
	REG_WriteLong((char*)"Key1", layout[1][1]);
	REG_WriteLong((char*)"Key2", layout[1][2]);
	REG_WriteLong((char*)"Key3", layout[1][3]);
	REG_WriteLong((char*)"Key4", layout[1][4]);
	REG_WriteLong((char*)"Key5", layout[1][5]);
	REG_WriteLong((char*)"Key6", layout[1][6]);
	REG_WriteLong((char*)"Key7", layout[1][7]);
	REG_WriteLong((char*)"Key8", layout[1][8]);
	REG_WriteLong((char*)"Key9", layout[1][9]);
	REG_WriteLong((char*)"Key10", layout[1][10]);
	REG_WriteLong((char*)"Key11", layout[1][11]);
	REG_WriteLong((char*)"Key12", layout[1][12]);
	REG_WriteLong((char*)"Key13", layout[1][13]);
	REG_WriteLong((char*)"Key14", layout[1][14]);
	REG_WriteLong((char*)"Key15", layout[1][15]);
	REG_WriteLong((char*)"Key16", layout[1][16]);
	REG_WriteLong((char*)"Key17", layout[1][17]);

	REG_WriteLong((char*)"JDck", jLayout[0]);
	REG_WriteLong((char*)"JDsh", jLayout[1]);
	REG_WriteLong((char*)"JWlk", jLayout[2]);
	REG_WriteLong((char*)"JJmp", jLayout[3]);
	REG_WriteLong((char*)"JAct", jLayout[4]);
	REG_WriteLong((char*)"JDrw", jLayout[5]);
	REG_WriteLong((char*)"JFlr", jLayout[6]);
	REG_WriteLong((char*)"JLok", jLayout[7]);
	REG_WriteLong((char*)"JRol", jLayout[8]);
	REG_WriteLong((char*)"JInv", jLayout[9]);
	REG_WriteLong((char*)"JPau", jLayout[10]);
	REG_WriteLong((char*)"JSav", jLayout[11]);
	REG_WriteLong((char*)"JLod", jLayout[12]);
	REG_WriteLong((char*)"JSmd", jLayout[13]);
	REG_WriteLong((char*)"JBmd", jLayout[14]);

	REG_WriteLong((char*)"ControlMethod", ControlMethod);
	REG_WriteLong((char*)"JoyStyle", JoyStyle);
	REG_WriteLong((char*)"Language", LanguageChoice);
	REG_WriteLong((char*)"MusicVolume", MusicVolume);
	REG_WriteLong((char*)"SFXVolume", SFXVolume);
	REG_WriteLong((char*)"SoundQuality", SoundQuality);
	REG_WriteLong((char*)"AutoTarget", App.AutoTarget);
	REG_WriteLong((char*)"WindowX", App.dx.rScreen.left);
	REG_WriteLong((char*)"WindowY", App.dx.rScreen.top);
	CloseRegistry();

	OpenRegistry("System");
	REG_WriteLong((char*)"VMode", App.DXInfo.nDisplayMode);
	REG_WriteBool((char*)"Window", (App.dx.Flags & DXF_WINDOWED) != 0);
	CloseRegistry();
}

bool SaveSetup(HWND hDlg)
{
	OpenRegistry("System");

	REG_WriteLong((char*)"DD", SendMessage(GetDlgItem(hDlg, IDC_GRAPHICS_ADAPTER), CB_GETCURSEL, 0, 0));
	// D3D, DS and TFormat no longer have a control behind them - one device
	// per adapter, one texture format, and XAudio2 always on the default
	// endpoint. Written as 0 rather than left alone, so a value from an older
	// build is actively cleared instead of sitting in the registry waiting to
	// index past an array (the read side clamps them too, see LoadSettings).
	REG_WriteLong((char*)"D3D", 0);
	REG_WriteLong((char*)"VMode", SendMessage(GetDlgItem(hDlg, IDC_RESOLUTION), CB_GETITEMDATA, SendMessage(GetDlgItem(hDlg, IDC_RESOLUTION), CB_GETCURSEL, 0, 0), 0));
	REG_WriteLong((char*)"DS", 0);
	REG_WriteLong((char*)"TFormat", 0);

	REG_WriteBool((char*)"Filter", SendMessage(GetDlgItem(hDlg, IDC_BILINEAR), BM_GETCHECK, 0, 0));
	REG_WriteBool((char*)"DisableSound", SendMessage(GetDlgItem(hDlg, IDC_DISABLE_SOUND), BM_GETCHECK, 0, 0));
	REG_WriteBool((char*)"TextLow", SendMessage(GetDlgItem(hDlg, IDC_LOW_QUALITY_TEX), BM_GETCHECK, 0, 0));
	REG_WriteBool((char*)"Window", SendMessage(GetDlgItem(hDlg, IDC_WINDOWED), BM_GETCHECK, 0, 0));
	REG_WriteBool((char*)"NoFMV", SendMessage(GetDlgItem(hDlg, IDC_DISABLE_FMV), BM_GETCHECK, 0, 0));
	REG_WriteBool((char*)"Setup", 1);

	CloseRegistry();
	CloseRegistry();
	return 1;
}

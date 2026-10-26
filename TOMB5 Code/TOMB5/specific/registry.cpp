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
#include "../game/key_scheme.h"	// KEY_SCHEME_ROLES - the two keyboard sets
#include "../game/menu_frame.h"	// MenuSliderHeld - the two volume sliders

static HKEY phkResult;
static DWORD dwDisposition;
static bool REG_Setup;

/* The Separate pad set's own names: the same fifteen suffixes the One-key
 * set carries, with an S in front and the Secondary role appended, so a
 * reader of either set can see at a glance which is which, and an older
 * build that looks for none of them reads a registry written here exactly
 * as it reads one written by itself.
 *
 * A table and a loop rather than unrolled lines like the J* block below:
 * those keep the shape of Core's original, these are new, and one loop is
 * one place for a typo instead of sixteen. */
static const char* const JoySplitRegNames[JOY_ROLES] =
{
	"SJDck", "SJDsh", "SJWlk", "SJJmp", "SJAct", "SJDrw", "SJFlr", "SJLok",
	"SJRol", "SJInv", "SJPau", "SJSav", "SJLod", "SJSmd", "SJBmd", "SJSec",
	// Appended: the Modifier, the six weapon hotkeys in DoWeaponHotkey's
	// order, the five cheats.
	"SJMod", "SJWp1", "SJWp2", "SJWp3", "SJWp4", "SJWp5", "SJWp6",
	"SJCt1", "SJCt2", "SJCt3", "SJCt4", "SJCt5",
	// And the HD/SD switch.
	"SJHd"
};

/* The same thirteen for the One-key set. JDck..JSec are Core's own suffixes
 * and stay unrolled REG_ReadLong lines below, because that is the shape
 * they have had since base; the thirteen behind them are new, so they get a
 * table and a loop - one place for a typo instead of thirteen. They are the
 * One-key names, which is why they have no S. */
static const char* const JoyOneRegNames[JOY_ROLES - JOY_ROLE_MODIFIER] =
{
	"JMod", "JWp1", "JWp2", "JWp3", "JWp4", "JWp5", "JWp6",
	"JCt1", "JCt2", "JCt3", "JCt4", "JCt5", "JHd"
};

/* The chord flags are one value per set: 29 roles fit in a long, so a mask
 * says it all and the registry gains two names rather than 29.
 *
 * Each set's chord mask has a name of its own rather than a bit hidden in
 * a binding. Every J* and SJ* value keeps its plain meaning - a button bit
 * index, or -1 - so a registry written by this build loads in an older
 * build without harm: that build never asks for these two names, reads
 * every binding exactly as it wrote it, and has no chord to be wrong
 * about. */
static const char* const JoyChordRegNames[2] = { "JChord", "SJChord" };

/* Beside them, one more mask per set on the same terms: the roles the scheme
 * repair left unbound because the Modifier holds their default, waiting to be
 * seated again (JoyYieldSet, specific\input.cpp). A registry without these
 * names has nothing waiting, which is 0. */
static const char* const JoyYieldRegNames[2] = { "JYield", "SJYield" };

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

/* Reads one value without opening the door that OpenRegistry opens.
 *
 * Every other reader here goes through OpenRegistry, and OpenRegistry
 * creates the key it is handed (RegCreateKeyEx above) and records in
 * dwDisposition whether it had to - which is how init_tomb5_stuff knows to
 * write the whole set of defaults on a first run. A second reader that
 * opened the tomb5 key earlier would silently take that first run away,
 * leaving the player with a key holding one value and no defaults.
 *
 * This reader opens read-only, creates nothing, touches neither phkResult
 * nor dwDisposition, and answers false when the key or the value is not
 * there. Callers: the VSync option, which the device needs before the
 * options are read (specific/winmain.cpp, DXCreate), and the window's
 * remembered place (REG_ReadWindowPlace). */
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

/* The same read-only approach, for one REG_SZ.
 *
 * REG_ReadString is the ordinary reader and writes its default into the
 * key on a miss, which is right for a setting the options menu owns.
 * hd_outfit is not one: it is written by the in-game outfit changer and
 * read at every level build, an absent value means "dress her by the
 * level's own facts", and writing an empty string there on the first
 * level of a fresh install would take away init_tomb5_stuff's first run
 * exactly as REG_PeekLong's note describes. So this one opens read-only,
 * creates nothing, touches neither phkResult nor dwDisposition, and hands
 * back "" when there is nothing there.
 *
 * Only a REG_SZ counts. A value of another type is a value somebody put
 * there by hand for another purpose, and reading its bytes as a string is
 * how a registry reader ends up printing a page of rubbish into a log. */
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

void REG_DeleteValue(char* SubKeyName)
{
	RegDeleteValue(phkResult, SubKeyName);
}

// The five values of the window's place, in the order of the arguments.
static const char* const WindowPlaceNames[5] = { "WindowLeft", "WindowTop",
	"WindowWidth", "WindowHeight", "WindowMaximized" };

bool REG_ReadWindowPlace(long* left, long* top, long* w, long* h,
	long* maximized)
{
	ulong v[5];

	for (int i = 0; i < 5; i++)
	{
		if (!REG_PeekLong("System", WindowPlaceNames[i], v[i]))
			return false;
	}

	*left = (long)v[0];
	*top = (long)v[1];
	*w = (long)v[2];
	*h = (long)v[3];
	*maximized = v[4] ? 1 : 0;
	return true;
}

void REG_WriteWindowPlace(long left, long top, long w, long h,
	long maximized)
{
	const ulong v[5] = { (ulong)left, (ulong)top, (ulong)w, (ulong)h,
		(ulong)(maximized ? 1 : 0) };
	char buffer[256];
	HKEY key;

	sprintf(buffer, "%s\\%s", "Software\\Core Design\\Tomb Raider Chronicles",
		"System");

	if (RegCreateKeyEx(HKEY_CURRENT_USER, buffer, 0, 0,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, 0, &key, 0) != ERROR_SUCCESS)
		return;

	for (int i = 0; i < 5; i++)
		RegSetValueEx(key, WindowPlaceNames[i], 0, REG_DWORD,
			(CONST BYTE*)&v[i], sizeof(ulong));

	RegCloseKey(key);
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

	/* The Secondary action: a nineteenth name in the same shape as the
	 * eighteen above, and a new one. A registry written by an older build
	 * has no Key18, so it reads as the Tank default row's own slot 18 -
	 * never touched, which is true - and a registry written here carries
	 * one value that the older build simply does not look for. */
	REG_ReadLong((char*)"Key18", key, layout[0][18]);
	layout[1][18] = (short)key;

	/* The sixteen hotkey slots, in the same shape again and for the same
	 * reason: a registry written by an older build has no Key19..Key34, so
	 * each one reads as its row's default - which is the key
	 * specific/input.cpp used to read directly, so an untouched registry
	 * answers to exactly the keys it answered to before; Key34, the HD/SD
	 * switch, reads F12 (game/key_scheme.h says why no stored set holds
	 * it). A loop and not unrolled lines, like the MKey block below: the
	 * names are new, so there is no Core original whose shape has to be
	 * kept. */
	{
		short* cset = KeySchemeSet(0);

		for (long i = KEY_ROLE_SECOND + 1; i < KEY_SCHEME_ROLES; i++)
		{
			char name[12];

			sprintf(name, "Key%ld", i);
			REG_ReadLong(name, key, layout[0][i]);
			cset[i] = (short)key;
		}
	}

	/* The second (Modern) key set. Key0..Key18 above keep their meaning
	 * exactly - they are the classic set, whichever scheme the game was in
	 * when it last shut down - and the Modern set gets names of its own
	 * beside them. Missing means the Modern defaults (layout[2]), so a
	 * registry written by an earlier build reads as "never touched the
	 * Modern keys", which is true.
	 *
	 * The set goes in by scheme and not by row, because the seat has not
	 * happened yet and must not be assumed either way: LoadSettings runs
	 * in WinMain (specific/winmain.cpp) and init_tomb5_stuff - which is
	 * what reads tomb5.control_modern out of the other registry key - runs
	 * later, on the game thread (specific/gamemain.cpp). That is where
	 * ApplyKeyScheme() is called, one line after it, and until then Tank
	 * is seated.
	 *
	 * A loop and not unrolled lines like the block above: the names are
	 * new, so there is no Core original to keep the shape of, and one loop
	 * is one place for a typo instead of nineteen.
	 *
	 * A mouse button is stored as its code, 0x100..0x104 (game/key_scheme.h),
	 * in the same long as a scan code, in both sets. A registry that already
	 * holds MKey9, MKey11 and MKey18 keeps the player's keys there; the Modern
	 * row's buttons for those three roles still answer beside them, as every
	 * default key does that no binding has claimed (Key(),
	 * specific/input.cpp).
	 *
	 * A set saved by this build is not safe for an engine that predates the
	 * mouse codes: that one indexes its 256-entry keymap with the code and
	 * reads past the end, and its bindings page names the buttons with the old joystick names.
	 * Reset to defaults in that engine puts its own row back. */
	{
		short* mset = KeySchemeSet(1);

		for (long i = 0; i < KEY_SCHEME_ROLES; i++)
		{
			char name[12];

			sprintf(name, "MKey%ld", i);
			REG_ReadLong(name, key, layout[2][i]);
			mset[i] = (short)key;
		}
	}

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
	// The pad's Secondary role, missing = unbound (-1, Key()'s own sentinel).
	// It has no default in the four existing tables on purpose - the Separate
	// pad set is where a sixteenth button belongs (specific\input.cpp).
	REG_ReadLong((char*)"JSec", (ulong&)jLayout[JOY_ROLE_SECOND], (ulong)-1);

	/* The thirteen new One-key roles, and every one of them defaults to
	 * unbound. That also makes this set byte-identical at the default:
	 * with no Modifier bound no chord can fire, and PadRoleFires collapses
	 * to the bit test every producer used before (game/pad_scheme.h). A
	 * registry written by an older build carries none of these names, so
	 * it reads as exactly that. */
	for (long i = JOY_ROLE_MODIFIER; i < JOY_ROLES; i++)
		REG_ReadLong((char*)JoyOneRegNames[i - JOY_ROLE_MODIFIER], (ulong&)jLayout[i], (ulong)-1);

	// The One-key set's chord flags. 0 for a registry that has never seen this
	// build, which is the same sentence one line up in mask form.
	REG_ReadLong((char*)JoyChordRegNames[0], (ulong&)*JoyChordSet(0), 0);
	REG_ReadLong((char*)JoyYieldRegNames[0], (ulong&)*JoyYieldSet(0), 0);

	/* Separate's own set beside it. The fifteen lines above keep their
	 * meaning exactly - they are the One-key set, whichever setting the
	 * game was in when it last shut down - and they go straight into
	 * jLayout for the same reason the classic keyboard set goes straight
	 * into layout[1]: the seat has not happened yet and must not be
	 * assumed either way, so at this moment One key is seated and jLayout
	 * is JoySplitSet(0).
	 *
	 * Missing means the defaults, asked for rather than written out a
	 * second time here (specific/input.cpp holds the four tables). They
	 * are this path's best answer at this moment and not the final one -
	 * JoyStyle is read further down, tomb5.control_modern comes out of the
	 * other registry key later still, and joystick_is_xinput is answered
	 * by the first poll - which is the same approximation the J* defaults
	 * above have always been (they are the PS5 DirectInput numbers,
	 * literally). What makes it harmless is that a binding the active
	 * scheme cannot honour is repaired when the scheme is seated for real
	 * (ApplyJoyScheme), and that a player who rebinds anything overwrites
	 * the guess with their own choice. */
	{
		long* sset = JoySplitSet(1);
		long* schord = JoyChordSet(1);
		long stored = 0;

		JoySplitDefaults(sset, schord);

		// The sixteen names an older build may already have written.
		// REG_ReadLong says whether it found one, which is the only
		// question that cannot be answered from the value itself.
		for (long i = 0; i < JOY_ROLE_MODIFIER; i++)
			if (REG_ReadLong((char*)JoySplitRegNames[i], (ulong&)sset[i], (ulong)sset[i]))
				stored = 1;

		/* A stored Separate set keeps its old bindings, and gets no chord
		 * grafted onto them. An older build's registry has all sixteen
		 * SJ* values and no SJChord, so the loop above takes the player's
		 * own buttons - and if the new row's chord mask were then laid
		 * over them, several bindings would silently change meaning: a
		 * flare bound to plain R1 would become Modifier+R1, a quick save
		 * on the PS button would become Modifier+PS, and a binding on the
		 * bit the new Modifier occupies would fall silent altogether. The
		 * player's bindings were a deliberate choice; the chords were
		 * never asked for. So a stored set comes up exactly as the older
		 * build left it - no chord, no Modifier - and "Reset to defaults"
		 * once gives the whole new row.
		 *
		 * The correction is written back at once rather than left to the
		 * next clean exit, so the read above (which writes its default on
		 * a miss) cannot leave a mask behind that outlives this run.
		 *
		 * The SJChord read below must not be conditioned on `stored`.
		 * REG_ReadLong writes its default when a name is missing, so the
		 * loop above creates all sixteen SJ* names even on a virgin
		 * registry - and LoadSettings can run a second time in the same
		 * process whenever the first call leaves REG_Setup false, which a
		 * virgin registry always does. Short-circuited on `stored`, a
		 * second pass would see sixteen SJ* names it had just created
		 * itself, miss the SJChord nobody had written, and strip the
		 * fresh Separate row of the chords it had just been given -
		 * leaving several buttons doing two things at once. Reading
		 * unconditionally avoids this: `stored` then decides only what
		 * the absence of SJChord means. */
		const bool chord_stored = REG_ReadLong((char*)JoyChordRegNames[1], (ulong&)*schord, (ulong)*schord);

		if (stored && !chord_stored)
		{
			*schord = 0;
			REG_WriteLong((char*)JoyChordRegNames[1], 0);

			for (long i = JOY_ROLE_MODIFIER; i < JOY_ROLES; i++)
				sset[i] = -1;
		}

		for (long i = JOY_ROLE_MODIFIER; i < JOY_ROLE_HD_SWITCH; i++)
			REG_ReadLong((char*)JoySplitRegNames[i], (ulong&)sset[i], (ulong)sset[i]);

		/* The HD/SD switch's SJHd. A set stored without it carries an
		 * SJChord without its bit, so its default button read alone would
		 * be a plain Options - a second pause. PadSeatAppended
		 * (game/pad_scheme.h) seats the default pair whole or leaves the
		 * role unbound, and both values are written back at once, so the
		 * next start reads them as they are now. A set the block above
		 * stripped of its chords has -1 here and stays unbound. */
		{
			const long hd = JOY_ROLE_HD_SWITCH;
			const long def = sset[hd];
			long fresh[JOY_ROLES];
			long freshChord = 0;

			JoySplitDefaults(fresh, &freshChord);

			if (!REG_ReadLong((char*)JoySplitRegNames[hd], (ulong&)sset[hd],
				(ulong)def))
			{
				PadSeatAppended(sset, schord, hd, def,
					(freshChord >> hd) & 1);
				REG_WriteLong((char*)JoySplitRegNames[hd], sset[hd]);
				REG_WriteLong((char*)JoyChordRegNames[1], *schord);
			}
		}

		REG_ReadLong((char*)JoyYieldRegNames[1], (ulong&)*JoyYieldSet(1), 0);
	}

	REG_ReadLong((char*)"MusicVolume", (ulong&)MusicVolume, 80);
	REG_ReadLong((char*)"SFXVolume", (ulong&)SFXVolume, 90);

	// Held to the sliders' span at once, so that the menu's number and bar
	// and the mixer see the same value from the first frame on; a value
	// outside it goes to the nearer end.
	MusicVolume = MenuSliderHeld(MusicVolume, MENU_SLIDER_NEAREST);
	SFXVolume = MenuSliderHeld(SFXVolume, MENU_SLIDER_NEAREST);

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

	/* Both key sets are written, whichever one is active. Key0..Key18 are
	 * the classic set by name and must keep being it, so they can no
	 * longer be written from layout[1]: under Modern that row holds the
	 * other set. At Tank, KeySchemeSet(0) is layout[1], and the first
	 * eighteen of these lines write byte for byte what they wrote at
	 * base; Key18 is the Secondary action, new in the registry. */
	const short* cset = KeySchemeSet(0);
	const short* mset = KeySchemeSet(1);

	REG_WriteLong((char*)"Key0", cset[0]);
	REG_WriteLong((char*)"Key1", cset[1]);
	REG_WriteLong((char*)"Key2", cset[2]);
	REG_WriteLong((char*)"Key3", cset[3]);
	REG_WriteLong((char*)"Key4", cset[4]);
	REG_WriteLong((char*)"Key5", cset[5]);
	REG_WriteLong((char*)"Key6", cset[6]);
	REG_WriteLong((char*)"Key7", cset[7]);
	REG_WriteLong((char*)"Key8", cset[8]);
	REG_WriteLong((char*)"Key9", cset[9]);
	REG_WriteLong((char*)"Key10", cset[10]);
	REG_WriteLong((char*)"Key11", cset[11]);
	REG_WriteLong((char*)"Key12", cset[12]);
	REG_WriteLong((char*)"Key13", cset[13]);
	REG_WriteLong((char*)"Key14", cset[14]);
	REG_WriteLong((char*)"Key15", cset[15]);
	REG_WriteLong((char*)"Key16", cset[16]);
	REG_WriteLong((char*)"Key17", cset[17]);
	REG_WriteLong((char*)"Key18", cset[18]);	// the Secondary action

	// And the sixteen hotkey slots beside them, under names an older build
	// does not look for - so a registry this build writes still loads
	// there, with the hotkeys back to hard-wired keys, which is what that
	// build does anyway.
	for (long i = KEY_ROLE_SECOND + 1; i < KEY_SCHEME_ROLES; i++)
	{
		char name[12];

		sprintf(name, "Key%ld", i);
		REG_WriteLong(name, cset[i]);
	}

	// And the Modern set beside it, under names of its own. Written at Tank
	// too, and harmlessly: nothing at Tank reads MKey*, and an older build
	// ignores values it does not know.
	for (long i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		char name[12];

		sprintf(name, "MKey%ld", i);
		REG_WriteLong(name, mset[i]);
	}

	/* Both pad sets are written, whichever one is active - the same
	 * principle as the keyboard sets above, one array lower. JDck..JSec
	 * are the One-key set by name and must keep being it, so they can no
	 * longer be written from jLayout: under Separate that array holds the
	 * other set. At One key, JoySplitSet(0) is jLayout and these sixteen
	 * lines write byte for byte what they wrote at base. */
	const long* oneset = JoySplitSet(0);
	const long* sepset = JoySplitSet(1);

	REG_WriteLong((char*)"JDck", oneset[0]);
	REG_WriteLong((char*)"JDsh", oneset[1]);
	REG_WriteLong((char*)"JWlk", oneset[2]);
	REG_WriteLong((char*)"JJmp", oneset[3]);
	REG_WriteLong((char*)"JAct", oneset[4]);
	REG_WriteLong((char*)"JDrw", oneset[5]);
	REG_WriteLong((char*)"JFlr", oneset[6]);
	REG_WriteLong((char*)"JLok", oneset[7]);
	REG_WriteLong((char*)"JRol", oneset[8]);
	REG_WriteLong((char*)"JInv", oneset[9]);
	REG_WriteLong((char*)"JPau", oneset[10]);
	REG_WriteLong((char*)"JSav", oneset[11]);
	REG_WriteLong((char*)"JLod", oneset[12]);
	REG_WriteLong((char*)"JSmd", oneset[13]);
	REG_WriteLong((char*)"JBmd", oneset[14]);
	REG_WriteLong((char*)"JSec", oneset[JOY_ROLE_SECOND]);

	// The thirteen new roles behind them, from the same set. A loop and not
	// thirteen lines for the reason the table above gives.
	for (long i = JOY_ROLE_MODIFIER; i < JOY_ROLES; i++)
		REG_WriteLong((char*)JoyOneRegNames[i - JOY_ROLE_MODIFIER], oneset[i]);

	// And Separate's set under names of its own. Written at One key too, and
	// harmlessly: nothing at One key reads SJ*, and an older build ignores
	// values it does not know.
	for (long i = 0; i < JOY_ROLES; i++)
		REG_WriteLong((char*)JoySplitRegNames[i], sepset[i]);

	// The two chord masks, one per set. Written beside their bindings and
	// never inside them, so every J*/SJ* value on disk stays the plain
	// button number an older build reads it as.
	REG_WriteLong((char*)JoyChordRegNames[0], *JoyChordSet(0));
	REG_WriteLong((char*)JoyChordRegNames[1], *JoyChordSet(1));
	REG_WriteLong((char*)JoyYieldRegNames[0], *JoyYieldSet(0));
	REG_WriteLong((char*)JoyYieldRegNames[1], *JoyYieldSet(1));

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
	const ulong mode = (ulong)SendMessage(GetDlgItem(hDlg, IDC_RESOLUTION),
		CB_GETITEMDATA, SendMessage(GetDlgItem(hDlg, IDC_RESOLUTION),
		CB_GETCURSEL, 0, 0), 0);
	ulong was;

	/* A resolution chosen anew in the dialog is the window's size at the
	 * next start: the remembered place goes, and the window opens at the
	 * new size, centred. The same resolution keeps it. */
	const bool forget = !REG_PeekLong("System", "VMode", was) || was != mode;

	OpenRegistry("System");

	if (forget)
	{
		for (int i = 0; i < 5; i++)
			REG_DeleteValue((char*)WindowPlaceNames[i]);
	}

	REG_WriteLong((char*)"DD", SendMessage(GetDlgItem(hDlg, IDC_GRAPHICS_ADAPTER), CB_GETCURSEL, 0, 0));
	// D3D, DS and TFormat no longer have a control behind them - one device
	// per adapter, one texture format, and XAudio2 always on the default
	// endpoint. Written as 0 rather than left alone, so a value from an older
	// build is actively cleared instead of sitting in the registry waiting to
	// index past an array (the read side clamps them too, see LoadSettings).
	REG_WriteLong((char*)"D3D", 0);
	REG_WriteLong((char*)"VMode", mode);
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

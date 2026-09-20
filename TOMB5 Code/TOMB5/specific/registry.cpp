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
#include "../game/key_scheme.h"	// KEY_SCHEME_ROLES - the two keyboard sets (#161, round 161b2a)

static HKEY phkResult;
static DWORD dwDisposition;
static bool REG_Setup;

/* SEPARATE'S OWN PAD SET, BY NAME (#161, round 161b2c, item 3). The same
 * fifteen suffixes the One-key set has carried since base, with an S in front
 * and the Secondary role appended - so a reader of either set can see at a
 * glance which is which, and the 161b2a build, which looks for none of them,
 * reads a registry written here exactly as it read one written by itself.
 *
 * A table and a loop rather than sixteen unrolled lines like the J* block
 * below: those keep the shape of Core's original, these are new, and one loop
 * is one place for a typo instead of sixteen. */
static const char* const JoySplitRegNames[JOY_ROLES] =
{
	"SJDck", "SJDsh", "SJWlk", "SJJmp", "SJAct", "SJDrw", "SJFlr", "SJLok",
	"SJRol", "SJInv", "SJPau", "SJSav", "SJLod", "SJSmd", "SJBmd", "SJSec"
};

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

	/* THE SECONDARY ACTION (#161, round 161b2c, item 2). A nineteenth name in
	 * the same shape as the eighteen above, and a NEW one: a registry written
	 * by the 161b2a build has no Key18, so it reads as the Tank default row's
	 * own slot 18 - never touched, which is true - and a registry written here
	 * carries one value that the older build simply does not look for. */
	REG_ReadLong((char*)"Key18", key, layout[0][18]);
	layout[1][18] = (short)key;

	/* THE SECOND SET (#161, round 161b2a, item 1). Key0..Key18 above keep
	 * their meaning exactly - they are the CLASSIC set, whichever scheme the
	 * game was in when it last shut down - and the Modern set gets names of its
	 * own beside them. Missing means the Modern defaults (layout[2]), so a
	 * registry written by any earlier build reads as "never touched the Modern
	 * keys", which is true.
	 *
	 * THE SET GOES IN BY SCHEME AND NOT BY ROW, because the seat has not
	 * happened yet and must not be assumed either way: LoadSettings runs in
	 * WinMain (specific\winmain.cpp) and init_tomb5_stuff - which is what reads
	 * tomb5.control_modern out of the OTHER registry key - runs later, on the
	 * game thread (specific\gamemain.cpp). That is where ApplyKeyScheme() is
	 * called, one line after it, and until then Tank is seated.
	 *
	 * A loop and not nineteen unrolled lines like the block above: the names
	 * are new, so there is no Core original to keep the shape of, and one loop
	 * is one place for a typo instead of nineteen. */
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

	/* AND SEPARATE'S OWN SET BESIDE IT (#161, round 161b2c, item 3). The
	 * fifteen lines above keep their meaning exactly - they are the ONE-KEY
	 * set, whichever setting the game was in when it last shut down - and they
	 * go straight into jLayout for the same reason the classic keyboard set
	 * goes straight into layout[1]: the seat has not happened yet and must not
	 * be assumed either way, so at this moment One key is seated and jLayout IS
	 * JoySplitSet(0).
	 *
	 * MISSING MEANS THE DEFAULTS, and the defaults are asked for rather than
	 * written out a second time here (specific\input.cpp holds the four tables).
	 * They are this path's best answer at THIS moment and not the final one -
	 * JoyStyle is read forty lines further down, tomb5.control_modern comes out
	 * of the other registry key later still, and joystick_is_xinput is answered
	 * by the first poll - which is the same approximation the J* defaults above
	 * have always been (they are the PS5 DirectInput numbers, literally). What
	 * makes it harmless is that a binding the active scheme cannot honour is
	 * repaired when the scheme is seated for real (ApplyJoyScheme), and that a
	 * player who rebinds anything overwrites the guess with his own choice. */
	{
		long* sset = JoySplitSet(1);

		JoySplitDefaults(sset);

		for (long i = 0; i < JOY_ROLES; i++)
			REG_ReadLong((char*)JoySplitRegNames[i], (ulong&)sset[i], (ulong)sset[i]);
	}

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

	/* BOTH SETS, WHICHEVER ONE IS ACTIVE (#161, round 161b2a, item 1).
	 * Key0..Key18 are the CLASSIC set BY NAME and must keep being it, so they
	 * can no longer be written from layout[1]: under Modern that row holds the
	 * other set. At Tank KeySchemeSet(0) IS layout[1], and the first eighteen
	 * of these lines write byte for byte what they wrote at base; Key18 is the
	 * Secondary action, new with round 161b2c and new in the registry. */
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
	REG_WriteLong((char*)"Key18", cset[18]);	// the Secondary action (#161, round 161b2c)

	// And the Modern set beside it, under names of its own. Written at Tank
	// too, and harmlessly: nothing at Tank reads MKey*, and the accepted 161b1
	// build ignores values it does not know.
	for (long i = 0; i < KEY_SCHEME_ROLES; i++)
	{
		char name[12];

		sprintf(name, "MKey%ld", i);
		REG_WriteLong(name, mset[i]);
	}

	/* BOTH PAD SETS, WHICHEVER ONE IS ACTIVE (#161, round 161b2c, item 3) -
	 * word for word the keyboard's sentence above, one array lower. JDck..JSec
	 * are the ONE-KEY set BY NAME and must keep being it, so they can no longer
	 * be written from jLayout: under Separate that array holds the other set.
	 * At One key JoySplitSet(0) IS jLayout and these sixteen lines write byte
	 * for byte what they wrote at base. */
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

	// And Separate's set under names of its own. Written at One key too, and
	// harmlessly: nothing at One key reads SJ*, and the accepted 161b2a build
	// ignores values it does not know.
	for (long i = 0; i < JOY_ROLES; i++)
		REG_WriteLong((char*)JoySplitRegNames[i], sepset[i]);

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

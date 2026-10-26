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
#include "../global/types.h"

bool REG_OpenKey(LPCSTR lpSubKey);
bool OpenRegistry(LPCSTR SubKeyName);
void REG_CloseKey();
void CloseRegistry();
bool REG_KeyWasCreated();
/* One DWORD, read-only, creating nothing - for a setting that is needed
 * before the options are read. The long note over the definition says why
 * it may not go through OpenRegistry. */
bool REG_PeekLong(LPCSTR SubKeyName, LPCSTR ValueName, ulong& value);
/* The same for one REG_SZ, and for the same reason. `value` always comes
 * back terminated, "" on a miss. Used for hd_outfit, which the in-game
 * outfit changer writes and every level build reads - so it may not go
 * through OpenRegistry either, and it may not write a default: a value
 * that is not there means "the level decides", and that is not a setting
 * to put into a player's registry unasked. */
bool REG_PeekString(LPCSTR SubKeyName, LPCSTR ValueName, char* value, ulong size);
void REG_WriteLong(char* SubKeyName, ulong value);
void REG_WriteBool(char* SubKeyName, bool value);
bool REG_ReadLong(char* SubKeyName, ulong& value, ulong defaultValue);
bool REG_ReadBool(char* SubKeyname, bool& value, bool defaultValue);
// A REG_SZ value. `size` is the whole buffer including the terminating zero,
// and `value` always comes back terminated - also on the miss, where the
// default is written into the key the way the two above do it. Added for
// settings that are a path or a word and not a number: hd_install,
// hd_install_tr3 and hd_texture_format.
void REG_WriteString(char* SubKeyName, const char* value);
bool REG_ReadString(char* SubKeyName, char* value, ulong size, const char* defaultValue);
// Removes one value from the open key. The result is not asked, as the
// writes above do not ask theirs: a value that is not there
// (ERROR_FILE_NOT_FOUND) is the usual case and no error. For a value an
// older build wrote and this one no longer reads.
void REG_DeleteValue(char* SubKeyName);
/* The windowed state's place (DXWINDOWPLACE, dxshell.h), under System:
 * WindowLeft and WindowTop, the client's top left corner in screen
 * coordinates (signed - a monitor left of or above the primary one is
 * negative), WindowWidth and WindowHeight, the client's size, and
 * WindowMaximized. Both open a key of their own and leave the one
 * OpenRegistry holds alone: the window procedure writes them, and the game
 * thread may be between an OpenRegistry and its CloseRegistry at that
 * moment. The read creates nothing and answers false unless all five are
 * there. */
bool REG_ReadWindowPlace(long* left, long* top, long* w, long* h,
	long* maximized);
void REG_WriteWindowPlace(long left, long top, long w, long h,
	long maximized);
bool LoadSettings();
void SaveSettings();
bool SaveSetup(HWND hDlg);

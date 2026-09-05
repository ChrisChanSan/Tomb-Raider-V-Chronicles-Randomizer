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
void REG_WriteLong(char* SubKeyName, ulong value);
void REG_WriteBool(char* SubKeyName, bool value);
bool REG_ReadLong(char* SubKeyName, ulong& value, ulong defaultValue);
bool REG_ReadBool(char* SubKeyname, bool& value, bool defaultValue);
// A REG_SZ value. `size` is the whole buffer including the terminating zero,
// and `value` always comes back terminated - also on the miss, where the
// default is written into the key the way the two above do it. Added for the
// one setting that is a path and not a number: hd_install (#123).
void REG_WriteString(char* SubKeyName, const char* value);
bool REG_ReadString(char* SubKeyName, char* value, ulong size, const char* defaultValue);
bool LoadSettings();
void SaveSettings();
bool SaveSetup(HWND hDlg);

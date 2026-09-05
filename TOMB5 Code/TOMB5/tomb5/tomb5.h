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

extern tomb5_options tomb5;

// The HD art source (#123): the folder of the installed Tomb Raider IV-VI
// Remastered, or an empty string for "let the Steam discovery look". Not part
// of tomb5_options because that struct holds only what the registry likes as
// a DWORD. Registry value: hd_install, in the same key as every tomb5.* one.
extern char tomb5_hd_install[260];
void init_tomb5_stuff();
void save_new_tomb5_settings();
void RPC_Init();
void RPC_Update();
void RPC_close();

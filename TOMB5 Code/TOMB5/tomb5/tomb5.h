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

// The other bundle (#123, stage S7-2): Tomb Raider I-III Remastered, which
// holds the HD models of the four Tomb Raider III guests the randomizer can
// import into a level. Empty for "let the Steam discovery look". Registry
// value: hd_install_tr3, in the same key. Nothing else in the game reads it,
// and an installation without it loses those four models and nothing more.
extern char tomb5_hd_install_tr3[260];

// How the HD pages are held on the card (#123, stage S6): "rgba" for the
// lossless A8R8G8B8 the user chose as the default, "dxt5" for a quarter of
// the video memory and a measurable loss of quality. A string for the same
// reason hd_install is one. Registry value: hd_texture_format.
extern char tomb5_hd_texture_format[16];
void init_tomb5_stuff();
void save_new_tomb5_settings();
void RPC_Init();
void RPC_Update();
void RPC_close();

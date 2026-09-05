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

// THE LEVEL BRACKET OF THE HD PATH (#123).
//
// Two calls, and in stage S0 they change no pixel: HDLevelLoad looks at what
// the Remaster has for this level, measures it into the log and frees it all
// again; HDLevelUnload has nothing to free yet and says so. Everything behind
// them is switched off unless the registry setting hd_enable is 1 - which it
// is not out of the box, so an unchanged installation does not read a single
// byte more than it did before (the guard is the first line of HDLevelLoad).
//
// The seam sits in specific\file.cpp: HDLevelLoad after LoadTextureInfos, so
// that the rooms, the models and the object slots are all in memory in their
// RANDOMIZED state, and HDLevelUnload in FreeLevel.

// `wadName` is the gameflow's level name, with or without a folder and an
// extension - "DATA\ANDREA1" and "ANDREA1.TRC" both work.
void HDLevelLoad(const char* wadName);
void HDLevelUnload();

// Whether the HD path is switched on AND an installed Remaster was found.
// Nothing else in the engine may read HD data without asking this first.
bool HDEnabled();

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

void init_game_malloc();
void* game_malloc(long size);
long GetRandomControl();
void SeedRandomControl(long seed);

/* Reads back the logic RNG's seed, for saving it across an interpolated
 * frame. The draw path calls GetRandomControl in several places - the star
 * twinkle alone takes four thousand draws a frame - so at sixty presents a
 * second the logic stream would be consumed twice per tick and the
 * simulation would drift apart between the two frame rates. interpolate.cpp
 * saves this over an in-between frame and restores it with
 * SeedRandomControl, so consumption per logic tick stays exactly what it is
 * at thirty. Nothing else may use it: a seed is not a random number. The
 * one other reader is the level header of -record (specific\replay.cpp),
 * which writes it and the draw generator's down, never a number from them. */
long GetRandomControlSeed();
long GetRandomDraw();
void SeedRandomDraw(long seed);
long GetRandomDrawSeed();	// -record's level header only, as above
void Log(const char* s, ...);

/* An address as a log line prints it with %x: its low 32 bits, so the line
 * has the same width on a 32-bit and a 64-bit build. For the eye only - an
 * address differs from run to run anyway, and no reader of a log takes it
 * for more than a mark. */
inline unsigned int LogAddr(const void* p)
{
	return (unsigned int)(uintptr_t)p;
}

// -log: Log() writes nothing until this is set. Diagnostic seams that have
// to build something before they can log (walking a list, formatting a
// name) must test it themselves at the call site - the gate inside Log()
// only saves the writing, not the preparing.
extern bool gLogEnabled;

extern FILE* logF;
extern char* malloc_buffer;
extern char* malloc_ptr;
extern long malloc_size;
extern long malloc_free;
extern long nPolyType;

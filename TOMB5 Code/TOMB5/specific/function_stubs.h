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
void reset_game_malloc();	// FreeLevel: every block handed back
void free_game_malloc();	// GameClose: the pool itself
long GetRandomControl();
void SeedRandomControl(long seed);

/* -rngtrace <from> <to>: GetRandomControl's callers by return address in
 * the spans between two input calls whose closing tick lies in the window
 * (function_stubs.cpp says the lines). RngTraceArm takes the window once,
 * false when its two buffers cannot be had; RngTraceInputSeam closes and
 * opens a span at every input call, with -log only; RngTraceMark notes a
 * section boundary - its name, the calls before it, the generator's
 * state - in an open span, and does nothing otherwise. `name` must be a
 * string literal: it is kept until the span is written. */
#ifdef TOMB5_TEST_BUILD
bool RngTraceArm(long from, long to);
void RngTraceInputSeam(long tick, long fmv);
void RngTraceMark(const char* name);
#else
// A test build's only, as the recorder (replay.h): nothing in a shipping EXE.
inline void RngTraceInputSeam(long, long) {}
inline void RngTraceMark(const char*) {}
#endif

/* __declspec(noinline) in a test build, nothing in a shipping one: the
 * functions a diagnosis names by return address (GetRandomControl for
 * -rngtrace, GetFreeSpark for SPARKLOG draw_alloc) are calls of their own
 * there, and inlined as before elsewhere. */
#ifdef TOMB5_TEST_BUILD
#define TOMB5_NOINLINE_IN_TEST	__declspec(noinline)
#else
#define TOMB5_NOINLINE_IN_TEST
#endif

/* Reads back the logic RNG's seed, for saving it across an interpolated
 * frame. The draw path calls GetRandomControl in several places - the star
 * twinkle alone takes four thousand draws a frame - so at sixty presents a
 * second the logic stream would be consumed twice per tick and the
 * simulation would drift apart between the two frame rates. interpolate.cpp
 * saves this over an in-between frame and restores it with
 * SeedRandomControl, so consumption per logic tick stays exactly what it is
 * at thirty. Nothing else may use it: a seed is not a random number. The
 * one other reader is the recorder (specific\replay.cpp): -record's level
 * header, the input record's check and the STATELOG line, which write it
 * and the draw generator's down, never a number from them. */
long GetRandomControlSeed();
long GetRandomDraw();
void SeedRandomDraw(long seed);
long GetRandomDrawSeed();	// the recorder only, as above
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

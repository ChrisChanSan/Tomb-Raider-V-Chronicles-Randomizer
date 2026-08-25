#pragma once
#include "../global/types.h"

void init_game_malloc();
void* game_malloc(long size);
long GetRandomControl();
void SeedRandomControl(long seed);
long GetRandomDraw();
void SeedRandomDraw(long seed);
void Log(const char* s, ...);

// -log (#125): Log() writes nothing until this is set. Diagnostic seams
// that have to BUILD something before they can log (walking a list,
// formatting a name) must test it themselves at the call site - the gate
// inside Log() only saves the writing, not the preparing.
extern bool gLogEnabled;

extern FILE* logF;
extern char* malloc_buffer;
extern char* malloc_ptr;
extern long malloc_size;
extern long malloc_free;
extern long nPolyType;

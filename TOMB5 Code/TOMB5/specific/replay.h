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

/* -record <file> and -replay <file>: the devices' readings written once per
 * call of the input function, and played back in their place (replay.cpp,
 * the format in replay_format.h). Without either switch every function
 * here returns at its first line and changes nothing.
 *
 * A test build's only (TOMB5_TEST_BUILD, tomb5.vcxproj: Asan|x64 and
 * be64.bat test). The shipping EXEs compile replay.cpp to nothing and take
 * the empty functions at the end of this file: the call sites stay as they
 * are, each handing its own value back. */

#define REPLAY_OFF	0
#define REPLAY_RECORD	1
#define REPLAY_PLAY	2

#ifdef TOMB5_TEST_BUILD

/* `live_clock`: -liveclock was given; under REPLAY_PLAY only, the frames
 * are blended by this machine's clock (ReplaySubTick). */
void ReplayOpen(long mode, const char* path, const char* stamp,
	bool live_clock);
void ReplayClose();

// The header's tomb5 settings, after init_tomb5_stuff (replay.cpp).
void ReplaySettings();

/* The one input point (S_UpdateInput, specific\input.cpp), after the
 * keyboard, the mouse and the pad are read and before anything is built
 * from them. `fmv` is a film's own loop, which is left out of the file: a
 * 64-bit build plays no film. With -log each call also writes a STATELOG
 * line, the state the input record's check mixes, field by field. */
void ReplayInputSeam(char* keys, long* mouse_dx, long* mouse_dy,
	long* mouse_buttons, long* captured, long fmv);

/* A present's tick count, where the logic takes it; the recorded one under
 * -replay. */
long ReplayFrames(long frames);

/* A drawn frame's place between two ticks, as the clock gave it
 * (TIME_SubTick); the recorded one under -replay, the given one under
 * -replay -liveclock, the record taken and checked either way. */
double ReplaySubTick(double subtick);

/* A level's first tick: the generators' seeds and the frame counter. */
void ReplayLevelStart();

/* A save ('s') or a load ('l') of `slot`. */
void ReplaySlot(char kind, long slot);

#else

// The shipping build: no file, no line, the given value back.
inline void ReplayClose() {}
inline void ReplaySettings() {}
inline void ReplayInputSeam(char*, long*, long*, long*, long*, long) {}
inline long ReplayFrames(long frames) { return frames; }
inline double ReplaySubTick(double subtick) { return subtick; }
inline void ReplayLevelStart() {}
inline void ReplaySlot(char, long) {}

#endif

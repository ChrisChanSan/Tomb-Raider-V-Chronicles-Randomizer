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

// Control-method pages for the joystick: only the display's VOCABULARY.
// The bit order is determined by the active input path (joystick_is_xinput).
#define JOYSTYLE_PS5	0
#define JOYSTYLE_XBOX	1
#define JOYSTYLE_GENERIC	2

long Key(long number);
long S_UpdateInput();
long ReadJoystick(long& x, long& y);
long ActiveJoyStyle();
long JoyMenuStep();
long JoyMenuDeflected();
const char* JoyButtonName(long idx);
void ApplyJoyDefaults();
void ApplyJoyScheme();	// #161, round 161a2: the active scheme repairs the bindings it cannot honour
void ApplyKeyScheme();	// #161, round 161b2a: ... and the keyboard's two sets change places
const short* KeyDefaultRow();	// the active scheme's default row - Key()'s fallback and CheckKeyConflicts()'s subject
short* KeySchemeSet(long modern);	// one scheme's stored set, wherever it is sitting (specific\registry.cpp)
long JoyBitConfirm();
long JoyBitBack();
long ModernLookTankActive();	// #161, round 161a3, item 3: the look key is steering her as at Tank this tick

extern const char* KeyboardButtons[272];
extern const char* GermanKeyboard[272];
extern long JoyStyle;
extern long jLayout[32];

extern short layout[4][18];	// 0 Tank defaults, 1 the active set, 2 Modern defaults, 3 the parked set (#161, round 161b2a)
extern long conflict[18];
extern long input;
extern long dbinput;
extern long inputBusy;
extern long joystick_read;
extern long joystick_read_x;
extern long joystick_read_y;
extern long joystick_raw_rx;	// the RIGHT stick's axes, same scale, before its per-axis zone (#161, rounds 161a2 and 161a2b)
extern long joystick_raw_ry;
extern long joystick_raw_lx;	// the LEFT stick ALONE and raw - joystick_read_x/y without the d-pad over them and without the per-axis zone (#161, same two rounds)
extern long joystick_raw_ly;
extern long joystick_read_fire;
extern long joystick_is_xinput;
extern short ammo_change_timer;
extern char ammo_change_buf[12];

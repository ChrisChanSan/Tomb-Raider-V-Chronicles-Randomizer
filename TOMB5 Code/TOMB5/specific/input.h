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

long Key(long number);
long S_UpdateInput();
long ReadJoystick(long& x, long& y);

extern const char* KeyboardButtons[272];
extern const char* GermanKeyboard[272];
extern const char* JoyStickButtons[20];
extern long jLayout[32];
extern long defaultJLayout[32];

extern short layout[2][18];
extern long conflict[18];
extern long input;
extern long dbinput;
extern long inputBusy;
extern long joystick_read;
extern long joystick_read_x;
extern long joystick_read_y;
extern long joystick_read_fire;
extern short ammo_change_timer;
extern char ammo_change_buf[12];

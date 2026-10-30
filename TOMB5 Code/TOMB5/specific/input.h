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
#include "../game/key_scheme.h"	// KEY_SCHEME_ROLES and KEY_ROLE_SECOND - the keyboard's slot count is the shape of `layout` below
#include "../game/pad_scheme.h"	// JOY_ROLES, JOY_ROLE_SECOND and PadSetSeat - the pad's slot count, and the seat of its two sets

// Control-method pages for the joystick: they choose only the button names shown.
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
void ApplyJoyScheme();	// the active scheme repairs the pad bindings it cannot honour
void ApplyKeyScheme();	// the keyboard's two sets change places when the control scheme flips
void ApplyJoySplit();	// ... and so do the pad's two sets, when the Action row flips
void JoySplitDefaults(long* out, long* chord);	// the Separate set's defaults for the active page and path - what a registry without SJ* means
long* JoySplitSet(long split);	// one Action setting's stored pad set, wherever it is sitting (specific\registry.cpp)
long* JoyChordSet(long split);	// ... and that set's chord flags, one bit per role
long* JoyYieldSet(long split);	// ... and the roles the scheme repair left unbound for its Modifier, one bit per role
long JoyRoleIsChord(long role);	// whether the active set's role is a chord - asked by the binding page and the repair
void JoyRoleSetChord(long role, long on);
long JoyRole(long role);	// whether the active set's pad role fires this tick - the one question every producer asks (PadRoleFires, game\pad_scheme.h)
const short* KeyDefaultRow();	// the active scheme's default row - Key()'s fallback and CheckKeyConflicts()'s subject
short* KeySchemeSet(long modern);	// one scheme's stored set, wherever it is sitting (specific\registry.cpp)
long InputMouseButtons();	// the mouse's five buttons as the last S_UpdateInput took them, bit n = KEY_MOUSE_FIRST + n
long InputPadDirection();	// the pad pair's own direction bits, last call
long JoyBitConfirm();
long JoyBitBack();
long ModernLookTankActive();	// the look key is steering Lara as in the Tank scheme this tick
void InputModeLatchReset();	// Sprint mode / Duck mode let go - on a level start, a savegame load, a flip of either row

extern const char* KeyboardButtons[272];
extern const char* GermanKeyboard[272];
extern long JoyStyle;
/* The active pad set, one binding per role; the other set is stored apart
 * (JoySplitSet). JOY_ROLES and JOY_ROLE_SECOND live in game\pad_scheme.h beside
 * the seat that counts with them, so that MenuTest asserts on the engine's own
 * number; this header includes it, so every includer reaches them here. */
extern long jLayout[JOY_ROLES];

extern short layout[4][KEY_SCHEME_ROLES];	// 0 Tank defaults, 1 the active set, 2 Modern defaults, 3 the parked set
extern long conflict[KEY_SCHEME_ROLES];
extern long input;
extern long dbinput;
extern long inputBusy;
extern long joystick_read;
extern long joystick_read_x;
extern long joystick_read_y;
extern long joystick_raw_rx;	// the right stick's axes, same scale, before its per-axis zone
extern long joystick_raw_ry;
extern long joystick_raw_lx;	// the left stick alone and raw - joystick_read_x/y without the d-pad over them and without the per-axis zone
extern long joystick_raw_ly;
extern long joystick_read_fire;
extern long joystick_is_xinput;
extern short ammo_change_timer;
extern char ammo_change_buf[64];

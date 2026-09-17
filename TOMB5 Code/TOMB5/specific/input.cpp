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
#include "../tomb5/pch.h"
#include "input.h"
#include "dxshell.h"
#include "../game/lara_states.h"
#include "../game/sound.h"
#include "3dmath.h"
#include "../game/text.h"
#include "winmain.h"
#include "../game/camera.h"
#include "../game/spotcam.h"
#include "../game/control.h"
#include "LoadSave.h"
#include "../game/lara.h"
#include "../game/savegame.h"
#include "../game/gameflow.h"
#include "../game/newinv2.h"
#include "../game/deltapak.h"		// GLOBAL_playing_cutseq - the modern seam's guard
#include "../game/modern_state.h"	// the modern control's one tick (#161)
#include "../tomb5/tomb5.h"

const char* KeyboardButtons[272] =
{
	0,
	"Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+", "Bksp",
	"Tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "<", ">", "Return",
	"Ctrl", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "`",
	"Shift", "#", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "Shift",
	"Padx", "Alt", "Space", "Caps", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Nmlk", 0,
	"Pad7", "Pad8", "Pad9", "Pad-",
	"Pad4", "Pad5", "Pad6", "Pad+",
	"Pad1", "Pad2", "Pad3",
	"Pad0", "Pad.", 0, 0, "\\", 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Enter", "Ctrl", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Shift", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Pad/", 0, 0, "Alt", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Home", "Up", "PgUp", 0, "Left", 0, "Right", 0, "End", "Down", "PgDn", "Ins", "Del",
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Joy 1", "Joy 2", "Joy 3", "Joy 4", "Joy 5", "Joy 6", "Joy 7", "Joy 8",
	"Joy 9", "Joy 10", "Joy 11", "Joy 12", "Joy 13", "Joy 14", "Joy 15", "Joy 16"
};

const char* GermanKeyboard[272] =
{
	0,
	"Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "=", "`", "Entf",
	"Tab", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "~u", "x", "Enter",
	"Strg", "A", "S", "D", "F", "G", "H", "J", "K", "L", "~o", "~a", "( ",
	"Shift", "#", "Y", "X", "C", "V", "B", "N", "M", ",", ".", "-", "Shift",
	"Num x", "Alt", "Leert.", "Caps", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Nmlk", 0,
	"Num7", "Num8", "Num9", "Num-",
	"Num4", "Num5", "Num6", "Num+",
	"Num1", "Num2", "Num3",
	"Num0", "Num.", 0, 0, ">", 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Enter", "Strg", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Shift", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Num/", 0, 0, "Alt", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Pos1", "Hoch", "BdAuf", 0, "Links", 0, "Rechts", 0, "Ende", "Runter", "BdAb", "Einfg", "Entf",
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"Joy 1", "Joy 2", "Joy 3", "Joy 4", "Joy 5", "Joy 6", "Joy 7", "Joy 8",
	"Joy 9", "Joy 10", "Joy 11", "Joy 12", "Joy 13", "Joy 14", "Joy 15", "Joy 16"
};

//----------------------------------------------------------------------------
// Controller name tables. A button's bit position depends on the INPUT
// PATH, not on the controller type:
//   - XInput (real Xbox pads, but also a DualSense behind Steam Input or
//     DS4Windows): bit order from DXUpdateJoystick (dxshell.cpp) -
//     0=A 1=B 2=X 3=Y 4=LB 5=RB 6=View 7=Menu 8=L3 9=R3 10=LT 11=RT.
//     The Guide button never arrives via XInput, bits 12-15 stay dead.
//   - DirectInput fallback (DualSense raw, no-name pads): bit i = HID
//     button i. Measured for the DualSense on the real device (the order
//     is not standardized in the HID report): 0=Square 1=Cross 2=Circle
//     3=Triangle 4=L1 5=R1 6=L2 7=R2 8=Share 9=Options 10=L3 11=R3
//     12=PS Button.
// Bits 16-19 are on both paths the right stick's four tilt directions
// (dxshell.cpp), so it can be bound like the d-pad.
//
// The control-method page (JoyStyle) only picks the VOCABULARY; which bit
// order applies, the engine knows itself (joystick_is_xinput). That way
// the names are right even for a DualSense behind DS4Windows: PS5 page
// plus XInput bit order.
#define JOYNAME_STICK "Stick R Up", "Stick R Down", "Stick R Left", "Stick R Right"

static const char* JoyNamesPS5DInput[20] =
{
	"Square", "Cross", "Circle", "Triangle", "L1", "R1", "L2", "R2",
	"Share", "Options", "L3", "R3", "PS Button", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK
};

static const char* JoyNamesPS5XInput[20] =
{
	"Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options",
	"L3", "R3", "L2", "R2", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK
};

static const char* JoyNamesXbox[20] =
{
	"A", "B", "X", "Y", "LB", "RB", "View", "Menu",
	"L3", "R3", "LT", "RT", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK
};

static const char* JoyNamesGeneric[20] =
{
	"Joy 1", "Joy 2", "Joy 3", "Joy 4", "Joy 5", "Joy 6", "Joy 7", "Joy 8",
	"Joy 9", "Joy 10", "Joy 11", "Joy 12", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK
};

// -1 = automatic (XInput device -> Xbox page, else PS5 page); from the
// user's first choice on 0=PS5, 1=Xbox, 2=generic (registry "JoyStyle").
long JoyStyle = -1;

long ActiveJoyStyle()
{
	if (JoyStyle >= 0)
		return JoyStyle;

	return joystick_is_xinput ? JOYSTYLE_XBOX : JOYSTYLE_PS5;
}

const char* JoyButtonName(long idx)
{
	if (idx < 0 || idx > 19)
		return "---";

	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		return joystick_is_xinput ? JoyNamesPS5XInput[idx] : JoyNamesPS5DInput[idx];
	case JOYSTYLE_XBOX:
		return joystick_is_xinput ? JoyNamesXbox[idx] : JoyNamesGeneric[idx];
	default:
		return JoyNamesGeneric[idx];
	}
}

// Default bindings per page AND input path - bindings are bit indices, the
// same intent ("duck on R2") lands on different bits per path.
// Rows: Duck Dash Walk Jump Action Draw Flare Look Roll Inv Pause Save Load
// SmallMedi LargeMedi. -1 = unbound (display "---", Key() ignores it).
static const long jDefPS5DInput[15] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14 };
static const long jDefPS5XInput[15] = { 5, 3, 4, 2, 0, 1, 7, 10, 11, 6, 8, 9, -1, 18, 19 };
// Xbox default after the Xbox tester's layout: RT=Duck LT=Dash RB=Walk
// X=Jump A=Action Y=Draw LB=Look B=Roll View=Inventory Menu=Pause.
static const long jDefXbox[15] = { 11, 10, 5, 2, 0, 3, 8, 4, 1, 6, 7, -1, -1, 18, 19 };
static const long jDefGeneric[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };

void ApplyJoyDefaults()
{
	const long* source;

	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		source = joystick_is_xinput ? jDefPS5XInput : jDefPS5DInput;
		break;
	case JOYSTYLE_XBOX:
		source = joystick_is_xinput ? jDefXbox : jDefGeneric;
		break;
	default:
		source = jDefGeneric;
		break;
	}

	for (int i = 0; i < 15; i++)
		jLayout[i] = source[i];

	for (int i = 15; i < 32; i++)
		jLayout[i] = 0;
}

// Confirm (Cross/A) and back (Circle/B) as the active path's bit.
// Physically it is the same position on both pads - only the bit moves.
long JoyBitConfirm()
{
	return joystick_is_xinput ? 0 : 1;
}

long JoyBitBack()
{
	return joystick_is_xinput ? 1 : 2;
}

long jLayout[32] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

short layout[2][18] =
{
	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN },

	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN }
};

long conflict[18];
long input;
long dbinput;
long inputBusy;
long joystick_read;
long joystick_read_x;
long joystick_read_y;
long joystick_read_fire;
long joystick_is_xinput;	// 1 = XInput is delivering (Xbox pad, or DS4Windows/Steam), 0 = DirectInput fallback; set anew on every poll (dxshell.cpp)

static long joy_fire;
static long joy_x, joy_y;

// The pad's stick/d-pad as a single menu step, edge triggered: -1 left,
// +1 right, 0 nothing. Deliberately UNGATED by ControlMethod.
//
// The controls screen needs it. ControlMethod carries two meanings at once -
// which binding page is shown, and whether the pad is read at all - and on
// the keyboard page the second one leaves the controller completely dead. A
// player who paged there with the pad could not page back with it; only the
// arrow keys came back (reported in game, #115). Since that page is where
// the setting itself lives, it is a trap: the pad cannot undo what the pad
// just did.
//
// Only the CONTROLS SCREEN uses this. Everywhere else ControlMethod keeps its
// meaning - somebody who chose keyboard does not want a resting stick moving
// Lara.
long JoyMenuStep()
{
	static char held = 0;
	long dir;

	dir = 0;

	if (joy_x < -8)
		dir = -1;
	else if (joy_x > 8)
		dir = 1;

	if (!dir)
	{
		held = 0;
		return 0;
	}

	if (held)
		return 0;

	held = 1;
	return dir;
}

// Is the stick/d-pad deflected at all right now? The controls screen needs it
// to keep ONE deflection from being counted twice.
//
// The two paths hand the same deflection over at the moment ControlMethod
// changes: JoyMenuStep takes the step on the keyboard page, the method flips
// to the pad, and in the next frame the still-held direction reaches dbinput
// for the first time - which its edge detection reads as a fresh press. One
// push, two pages. So while the stick is deflected, the step belongs to
// JoyMenuStep alone and dbinput is not listened to.
long JoyMenuDeflected()
{
	return joy_x < -8 || joy_x > 8;
}

short ammo_change_timer = 0;
char ammo_change_buf[12];

//----------------------------------------------------------------------------
// One key per weapon, the same key everywhere (#139).
//
// Core bound four keys and let two of them change meaning:
// "if (gfLevelId < LVL5_THIRTEENTH_FLOOR)" made key 1 the pistols early
// and the HK later, key 2 the shotgun early and the crossbow later. That
// works when the campaign is played in its built order, because by then
// Lara has stopped carrying the early weapons.
//
// The randomizer breaks both halves of that assumption. gfLevelId is the
// level's IDENTITY, not its position, so with a rolled order "from the
// 13th Floor on" no longer means "later" - it means "in levels that happen
// to have a high id", scattered through the run. And since the weapons are
// scattered too, Lara can carry an HK in the first level, where no key
// would draw it, and still carry the pistols in the last, where key 1 would
// reach for the HK instead.
//
// So: 1 pistols, 2 shotgun, 3 uzis, 4 revolver, 5 HK, 6 crossbow. Nothing
// depends on the level any more. Keys 5 to 8 were free in play - only the
// save/load screen uses 1 to 4, and that is a different context.
//
// Everything inside the branches is Core's code, moved unchanged: the
// "is it carried" test, the draw, and the ammunition cycling on a second
// press (which alone is what the "Ammotype hotkeys" option governs -
// drawing a weapon works whether it is on or off, and always did).
//----------------------------------------------------------------------------
static void DoWeaponHotkey()
{
	short state;
	bool goin;

	if (!lara_item)
		goin = 0;
	else
	{
		state = lara_item->current_anim_state;
		goin = !(gfLevelFlags & GF_YOUNGLARA) && (lara.water_status == LW_ABOVE_WATER || lara.water_status == LW_WADE) && !bDisableLaraControl &&
			(state != AS_ALL4S && state != AS_CRAWL && state != AS_ALL4TURNL && state != AS_ALL4TURNR && state != AS_CRAWLBACK &&
				state != AS_CRAWL2HANG && state != AS_DUCK && state != AS_DUCKROTL && state != AS_DUCKROTR);
	}

	if (!goin)
		return;
	
	if (keymap[DIK_1])
	{
		if (!(lara.pistols_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_PISTOLS;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_PISTOLS)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (keymap[DIK_2])
	{
		if (!(lara.shotgun_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_SHOTGUN;

		if (lara.gun_type == WEAPON_SHOTGUN)
		{
			if (lara.gun_status == LG_NO_ARMS)
				lara.gun_status = LG_DRAW_GUNS;
			else if (lara.gun_status == LG_READY && !ammo_change_timer)
			{
				if (!tomb5.ammotype_hotkeys)
					return;

				memset(ammo_change_buf, 0, sizeof(ammo_change_buf));

				if (lara.shotgun_type_carried & W_AMMO2)
				{
					lara.shotgun_type_carried &= ~W_AMMO2;
					lara.shotgun_type_carried |= W_AMMO1;
					ammo_change_timer = 30;
					sprintf(ammo_change_buf, "Normal");
				}
				else if (lara.shotgun_type_carried & W_AMMO1)
				{
					lara.shotgun_type_carried &= ~W_AMMO1;
					lara.shotgun_type_carried |= W_AMMO2;
					ammo_change_timer = 30;
					sprintf(ammo_change_buf, "Wideshot");
				}
			}
		}
	}
	else if (keymap[DIK_3])
	{
		if (!(lara.uzis_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_UZI;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_UZI)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (keymap[DIK_4])
	{
		if (!(lara.sixshooter_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_REVOLVER;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_REVOLVER)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (keymap[DIK_5])
	{
		// The HK had to share key 1 with the pistols; now it has its own.
		if (!(lara.hk_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_HK;

		if (lara.gun_type == WEAPON_HK)
		{
			if (lara.gun_status == LG_NO_ARMS)
				lara.gun_status = LG_DRAW_GUNS;
			else if (lara.gun_status == LG_READY && !ammo_change_timer)
			{
				if (!tomb5.ammotype_hotkeys)
					return;

				memset(ammo_change_buf, 0, sizeof(ammo_change_buf));

				if (lara.hk_type_carried & W_AMMO3)
				{
					lara.hk_type_carried &= ~W_AMMO3;
					lara.hk_type_carried |= W_AMMO2;
					ammo_change_timer = 30;
					sprintf(ammo_change_buf, "Burst");
				}
				else if (lara.hk_type_carried & W_AMMO2)
				{
					lara.hk_type_carried &= ~W_AMMO2;
					lara.hk_type_carried |= W_AMMO1;
					ammo_change_timer = 30;
					sprintf(ammo_change_buf, "Sniper");
				}
				else if (lara.hk_type_carried & W_AMMO1)
				{
					lara.hk_type_carried &= ~W_AMMO1;
					lara.hk_type_carried |= W_AMMO3;
					ammo_change_timer = 30;
					sprintf(ammo_change_buf, "Rapid");
				}
			}
		}
	}
	else if (keymap[DIK_6])
	{
		// The crossbow had to share key 2 with the shotgun; now it has its own.
		if (!(lara.crossbow_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_CROSSBOW;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_CROSSBOW)
			lara.gun_status = LG_DRAW_GUNS;
	}
}

long Key(long number)
{
	if (number == 255)
		// jLayout[n] was -1 (unbound - see LoadSave.cpp's auto-unbind-on-conflict),
		// so the caller's "+256" offset landed on 255 instead of a real >=256
		// joystick-button value. 255 is never a real keyboard role index (those
		// only go 0-17) or a real encoded joystick value (real button indices are
		// 0-19, i.e. 256-275), so it's safe to use as this sentinel.
		return 0;

	if (number >= 256)
		// number is jLayout[n]+256 (a "read from the joystick, not the keyboard"
		// signal) - joy_fire holds one bit per physical button starting at bit 0
		// (see DXUpdateJoystick), so the actual bit position needs the 256 taken
		// back out. Left shifting by number (up to ~265) directly here previously
		// always overflowed to 0 - this joystick-button path was dead even once
		// DXUpdateJoystick() itself was made to return real data.
		return joy_fire & (1 << (number - 256));

	short key = layout[1][number];

	if (keymap[key])
		return 1;

	switch (key)
	{
	case DIK_RCONTROL:
		return keymap[DIK_LCONTROL];
	case DIK_LCONTROL:
		return keymap[DIK_RCONTROL];
	case DIK_RSHIFT:
		return keymap[DIK_LSHIFT];
	case DIK_LSHIFT:
		return keymap[DIK_RSHIFT];
	case DIK_RMENU:
		return keymap[DIK_LMENU];
	case DIK_LMENU:
		return keymap[DIK_RMENU];
	}

	if (conflict[number])
		return 0;

	key = layout[0][number];

	if (keymap[key])
		return 1;

	switch (key)
	{
	case DIK_RCONTROL:
		return keymap[DIK_LCONTROL];
	case DIK_LCONTROL:
		return keymap[DIK_RCONTROL];
	case DIK_RSHIFT:
		return keymap[DIK_LSHIFT];
	case DIK_LSHIFT:
		return keymap[DIK_RSHIFT];
	case DIK_RMENU:
		return keymap[DIK_LMENU];
	case DIK_LMENU:
		return keymap[DIK_RMENU];
	}

	return 0;
}

long S_UpdateInput()
{
	static long linput;
	static long LookCnt;
	static long med_hotkey_timer;
	static long cheat_code;
	static long weird = 0;
	long flag;
	short state;
	static bool flare_no_db = 0;
	bool debounce;

	flag = 0;
	debounce = SetDebounce;

	DXUpdateJoystick();
	DXReadKeyboard(keymap);

	// Read unconditionally. Reading has no effect of its own - every use of
	// joy_x/joy_y/joy_fire below is still behind ControlMethod - but it keeps
	// the values current for JoyMenuStep, which the controls screen needs
	// precisely when ControlMethod says keyboard.
	joy_fire = ReadJoystick(joy_x, joy_y);

	linput = 0;

	if (ControlMethod == 1)
	{
		if (joy_x < -8)
			linput = IN_LEFT;
		else if (joy_x > 8)
			linput = IN_RIGHT;

		if (joy_y > 8)
			linput |= IN_BACK;
		else if (joy_y < -8)
			linput |= IN_FORWARD;
	}

	if (Key(0))
		linput |= IN_FORWARD;

	if (Key(1))
		linput |= IN_BACK;

	if (Key(2))
		linput |= IN_LEFT;

	if (Key(3))
		linput |= IN_RIGHT;

	/* THE MODERN CONTROL'S SEAM (#161, round 161a, design section 5.1).
	 *
	 * Here and nowhere else. Above, the stick and the four direction keys
	 * have written their bits; below, the key ROLES are read. So the whole of
	 * the modern scheme is "take those four bits away and put four of my own
	 * in", and when it decides it is not responsible - the stick at rest, a
	 * state where a direction bit does not mean a direction - the bits above
	 * simply stand and the pad behaves exactly as it does today. That is the
	 * reason this scheme needs no second input path anywhere in the tree.
	 *
	 * THE GUARD, term by term. tomb5.control_modern is first because it is
	 * the one that matters: at the default (Tank, 0) nothing below is
	 * reached, nothing new is called, and the engine is what it was.
	 * ControlMethod == 1 because the stick is only an input at all when the
	 * player has chosen the pad - otherwise a controller resting on the desk
	 * would take the keyboard's direction keys away from him. First person is
	 * skipped because there the camera IS Lara's head and a camera-relative
	 * heading means nothing; spotcams, cutscenes and bDisableLaraControl
	 * because she is not the player's to steer then (the last one is also a
	 * pickup, a switch and a lift, which the other two do not cover); the
	 * inventory and the title because there the direction bits steer a menu.
	 *
	 * AND THE LOOK KEY (the user's decision 4): while it is held the
	 * translation is bypassed altogether, so the stick turns her finely on
	 * the spot as it does in Tank, for the movements where she has to be
	 * placed exactly. `input` is the word of the PREVIOUS tick - IN_LOOK is
	 * read further down this same function - so the bypass starts a
	 * thirtieth of a second after the key and ends the same way, which is
	 * under the frame a player can see. With a weapon ready the look key is
	 * swallowed for six ticks further down (it becomes IN_TARGET), so there
	 * the bypass begins when the hold becomes a real look. */
	if (tomb5.control_modern && ControlMethod == 1 && !FirstPersonActive() && !bUseSpotCam &&
		!GLOBAL_playing_cutseq && !bDisableLaraControl && lara_item && !InventoryActive &&
		gfLevelId != LVL5_TITLE && !(input & IN_LOOK))
	{
		// The two key roles the scheme has to know about before they are read
		// into the word below - aiming asks for the action key, the
		// directional jump for the jump key. Same keys, same pad buttons, read
		// in the same order as the blocks that follow.
		const long mAction = Key(8) || Key(jLayout[4] + 256);
		const long mJump = Key(7) || Key(jLayout[3] + 256);

		linput = ModernUpdateInput(linput, joy_x, joy_y, mAction, mJump);
	}

	if (Key(4))
		linput |= IN_DUCK;

	if (Key(5))
		linput |= IN_SPRINT;

	if (Key(6))
		linput |= IN_WALK;

	if (Key(7))
		linput |= IN_JUMP;

	if (Key(8))
		linput |= IN_SELECT | IN_ACTION;

	if (Key(9))
		linput |= IN_DRAW;

	if (ControlMethod == 1)
	{
		if (Key(jLayout[3] + 256))
			linput |= IN_JUMP;

		if (Key(jLayout[4] + 256))
			linput |= IN_SELECT | IN_ACTION;

		if (Key(jLayout[5] + 256))
			linput |= IN_DRAW;

		if (Key(jLayout[1] + 256))
			linput |= IN_SPRINT;

		if (Key(jLayout[2] + 256))
			linput |= IN_WALK;

		if (Key(jLayout[0] + 256))
			linput |= IN_DUCK;

		if (Key(jLayout[6] + 256))
			flag = 1;
	}

	if (Key(10) || flag)
	{
		if (!flare_no_db)
		{
			state = lara_item->current_anim_state;

			if (state == AS_ALL4S || state == AS_CRAWL || state == AS_ALL4TURNL ||
				state == AS_ALL4TURNR || state == AS_CRAWLBACK || state == AS_CRAWL2HANG)
			{
				SoundEffect(SFX_LARA_NO, 0, SFX_ALWAYS);
				flare_no_db = 1;
			}
			else
			{
				flare_no_db = 0;
				linput |= IN_FLARE;
			}
		}
	}
	else
		flare_no_db = 0;

	if (Key(11))
		linput |= IN_LOOK;

	if (Key(12))
		linput |= IN_ROLL;

	if (Key(13))
		linput |= IN_OPTION;

	if (Key(14))
		linput |= IN_WALK | IN_LEFT;

	if (Key(15))
		linput |= IN_WALK | IN_RIGHT;

	if (Key(16))
		linput |= IN_PAUSE;

	if (Key(17))
		linput |= IN_SELECT;

	if (ControlMethod == 1)
	{
		if (Key(jLayout[7] + 256))
			linput |= IN_LOOK;

		if (Key(jLayout[8] + 256))
			linput |= IN_ROLL;

		if (Key(jLayout[9] + 256))
			linput |= IN_OPTION;

		if (Key(jLayout[10] + 256))
			linput |= IN_PAUSE;
	}

	if (keymap[DIK_ESCAPE])
		linput |= IN_DESELECT | IN_OPTION;

	// Fixed "Circle/B = back" menu convention, similar idea to Escape above but
	// deliberately WITHOUT IN_OPTION - unlike Escape, this is also a rebindable
	// gameplay button (jLayout), and IN_OPTION doubles as the inventory-open
	// toggle (see control.cpp) - adding it here would make every such press
	// during gameplay also try to open the inventory. IN_DESELECT alone is
	// enough for "back" in every menu screen that checks it.
	// Not user-rebindable, for the same reason Escape itself isn't. The BIT
	// depends on the active input path (JoyBitBack): raw DirectInput puts
	// Circle on bit 2, XInput puts B - and Circle via DS4Windows/Steam - on
	// bit 1. The hardcoded bit 2 here was why an Xbox pad could only leave
	// menus with its X button.
	if (ControlMethod == 1 && (joy_fire & (1 << JoyBitBack())))
		linput |= IN_DESELECT;

	if (lara.gun_status == LG_READY)
	{
		savegame.AutoTarget = (uchar)App.AutoTarget;

		if (linput & IN_LOOK)
		{
			if (LookCnt >= 6)
				LookCnt = 100;
			else
			{
				linput &= ~IN_LOOK;
				LookCnt++;
			}
		}
		else
		{
			if (LookCnt && LookCnt != 100)
				linput |= IN_TARGET;

			LookCnt = 0;
		}
	}

	DoWeaponHotkey();

	if (keymap[DIK_0] || (ControlMethod == 1 && Key(jLayout[13] + 256)))
	{
		if (!med_hotkey_timer)
		{
			if (lara_item->hit_points > 0 && lara_item->hit_points < 1000 || lara.poisoned)
			{
				if (lara.num_small_medipack)
				{
					if (lara.num_small_medipack != -1)
						lara.num_small_medipack--;

					lara.dpoisoned = 0;
					lara_item->hit_points += 500;
					SoundEffect(SFX_MENU_MEDI, 0, SFX_ALWAYS);
					savegame.Game.HealthUsed++;

					if (lara_item->hit_points > 1000)
						lara_item->hit_points = 1000;

					if (InventoryActive && !lara.num_small_medipack)
					{
						construct_object_list();
						setup_objectlist_startposition(INV_COMPASS_ITEM);
					}

					med_hotkey_timer = 15;
				}
			}
		}
	}
	else if (keymap[DIK_9] || (ControlMethod == 1 && Key(jLayout[14] + 256)))
	{
		if (!med_hotkey_timer)
		{
			if (lara_item->hit_points > 0 && lara_item->hit_points < 1000 || lara.poisoned)
			{
				if (lara.num_large_medipack)
				{
					if (lara.num_large_medipack != -1)
						lara.num_large_medipack--;

					lara.dpoisoned = 0;
					lara_item->hit_points = 1000;
					SoundEffect(SFX_MENU_MEDI, 0, SFX_ALWAYS);
					savegame.Game.HealthUsed++;
					med_hotkey_timer = 15;

					if (InventoryActive && !lara.num_large_medipack)
					{
						construct_object_list();

						if (lara.num_small_medipack)
							setup_objectlist_startposition(INV_SMALLMEDI_ITEM);
						else
							setup_objectlist_startposition(INV_COMPASS_ITEM);
					}
				}
			}
		}
	}
	else if (med_hotkey_timer)
		med_hotkey_timer--;

	if (keymap[DIK_F10])
		linput |= IN_E;

	if (linput & IN_WALK && !(linput & (IN_FORWARD | IN_BACK)))
	{
		if (linput & IN_LEFT)
			linput = (linput & ~IN_LEFT) | IN_LSTEP;
		else if (linput & IN_RIGHT)
			linput = (linput & ~IN_RIGHT) | IN_RSTEP;
	}

	if (linput & IN_FORWARD && linput & IN_BACK)
		linput |= IN_ROLL;

	if (linput & IN_ROLL && BinocularRange)
		linput &= ~IN_ROLL;

	// One press must not serve a menu AND the game.
	//
	// Circle is the fixed "back" button above, but it is also a rebindable
	// gameplay button - roll. So the press that closed the inventory or the
	// pause menu was still held on the first frame afterwards, and Lara
	// rolled without the player asking for it (reported in game, #115).
	// Same class as the FMV skip in #78: one press, two consumers, and the
	// second one has to wait for a fresh edge.
	//
	// The latch is armed while a menu is open - InventoryActive covers both,
	// the ring sets it and S_PauseMenu sets it - and clears only when the
	// button is really released. A roll that STARTS in the game is
	// untouched, because there the latch is never armed.
	{
		static char backHeldFromMenu = 0;
		const bool backDown = ControlMethod == 1 && (joy_fire & (1 << JoyBitBack())) != 0;

		if (!backDown)
			backHeldFromMenu = 0;
		else if (InventoryActive)
			backHeldFromMenu = 1;

		if (backHeldFromMenu)
			linput &= ~IN_ROLL;
	}

	if ((linput & (IN_RIGHT | IN_LEFT)) == (IN_RIGHT | IN_LEFT))
		linput -= IN_RIGHT | IN_LEFT;

	if (debounce)
		dbinput = inputBusy;

	if (!gfGameMode && Gameflow->LoadSaveEnabled)
	{
		if (keymap[DIK_F5] || (ControlMethod == 1 && Key(jLayout[11] + 256)))
			linput |= IN_SAVE;

		if (keymap[DIK_F6] || (ControlMethod == 1 && Key(jLayout[12] + 256)))
			linput |= IN_LOAD;
	}

	if (Gameflow->CheatEnabled)
	{
		if (linput)
			cheat_code = 0;

		switch (cheat_code)
		{
			case 0:

				if (keymap[DIK_D])
					cheat_code = 1;

				break;

			case 1:

				if (keymap[DIK_O])
					cheat_code = 2;

				break;

			case 2:

				if (keymap[DIK_Z])
					cheat_code = 3;

				break;

			case 3:

				if (keymap[DIK_Y])
					linput = IN_CHEAT;

				break;
		}
	}

	inputBusy = linput;

	if (lara.Busy)
	{
		linput &= IN_PAUSE | IN_LOOK | IN_OPTION | IN_RIGHT | IN_LEFT | IN_BACK | IN_FORWARD;

		if (linput & IN_FORWARD && linput & IN_BACK)
			linput ^= IN_BACK;
	}

	if (debounce)
		dbinput = inputBusy & (dbinput ^ inputBusy);

	input = linput;
	linput = weird;

	if (keymap[DIK_F])
		linput = 1;

	weird &= weird ^ linput;

	return 1;
}

long ReadJoystick(long& x, long& y)
{
	if (joystick_read)
	{
		x = joystick_read_x;
		y = joystick_read_y;
		return joystick_read_fire;
	}

	x = 0;
	y = 0;
	return 0;
}

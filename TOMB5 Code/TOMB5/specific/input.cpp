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
#include "../game/modern_state.h"	// the modern control's one tick
#include "../game/modern_move.h"	// the radial dead zone and its two widths
#include "../game/modern_camera.h"	// the mouse's gain, as arithmetic
#include "../game/key_scheme.h"		// the two keyboard sets, as arithmetic
#include "hd/hd_switch.h"		// the HD/SD switch's press
#include "../tomb5/tomb5.h"
#include "function_stubs.h"		// Log, gLogEnabled - MODERNLOG mode=tank

const char* KeyboardButtons[272] =
{
	0,
	"Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+", "Bksp",
	"Tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "<", ">", "Return",
	"Ctrl", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "`",
	"Shift", "#", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "Shift",
	/* Only the nine F keys the hotkey roles default to have names. An unnamed
	 * key cannot be shown by MenuValBinding nor bound by MenuCaptureBinding,
	 * which binds only named keys. F1, F2, F5, F6, F7, F8, F9, F11 and F12 are
	 * what the two default rows use; F3 and F4 (the outfit changer) and F10
	 * (IN_E) stay nameless on purpose, so the capture refuses them and no
	 * role can land on a key the engine already answers to. */
	"Padx", "Alt", "Space", "Caps", "F1", "F2", 0, 0, "F5", "F6", "F7", "F8", "F9", 0,
	"Nmlk", 0,
	"Pad7", "Pad8", "Pad9", "Pad-",
	"Pad4", "Pad5", "Pad6", "Pad+",
	"Pad1", "Pad2", "Pad3",
	"Pad0", "Pad.", 0, 0, "\\", "F11",	// F11 is the heal cheat's default key
	"F12", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
	// The same nine F keys as in the English table above, and for the same
	// reason - a German keyboard calls them F1..F12 as well.
	"Num x", "Alt", "Leert.", "Caps", "F1", "F2", 0, 0, "F5", "F6", "F7", "F8", "F9", 0,
	"Nmlk", 0,
	"Num7", "Num8", "Num9", "Num-",
	"Num4", "Num5", "Num6", "Num+",
	"Num1", "Num2", "Num3",
	"Num0", "Num.", 0, 0, ">", "F11",
	"F12", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
// Controller name tables. A button's bit position depends on the input
// path, not on the controller type:
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
// Bits 20-23 are the d-pad's own four directions, so the d-pad can be bound
// like the right stick once the Modern scheme frees it from moving Lara.
// Path- and style-independent for the same reason JOYNAME_STICK is:
// dxshell.cpp writes them from the d-pad, a fixed physical control on every
// supported pad, not something that moves between HID bit positions the way
// an ordinary face button does.
//
// The control-method page (JoyStyle) only picks the names shown; which bit
// order applies, the engine knows itself (joystick_is_xinput). That way
// the names are right even for a DualSense behind DS4Windows: PS5 page
// plus XInput bit order.
#define JOYNAME_STICK "Stick R Up", "Stick R Down", "Stick R Left", "Stick R Right"
#define JOYNAME_DPAD "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"

static const char* JoyNamesPS5DInput[24] =
{
	"Square", "Cross", "Circle", "Triangle", "L1", "R1", "L2", "R2",
	// Bit 13 is the DualSense's touchpad click on this path and the Modifier's
	// default there, so the binding page names it. Bit 14 is the mute button
	// and stays a number - nothing binds it.
	"Share", "Options", "L3", "R3", "PS Button", "Touchpad", "Joy 15", "Joy 16",
	JOYNAME_STICK, JOYNAME_DPAD
};

static const char* JoyNamesPS5XInput[24] =
{
	"Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options",
	"L3", "R3", "L2", "R2", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK, JOYNAME_DPAD
};

static const char* JoyNamesXbox[24] =
{
	"A", "B", "X", "Y", "LB", "RB", "View", "Menu",
	"L3", "R3", "LT", "RT", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK, JOYNAME_DPAD
};

static const char* JoyNamesGeneric[24] =
{
	"Joy 1", "Joy 2", "Joy 3", "Joy 4", "Joy 5", "Joy 6", "Joy 7", "Joy 8",
	"Joy 9", "Joy 10", "Joy 11", "Joy 12", "Joy 13", "Joy 14", "Joy 15", "Joy 16",
	JOYNAME_STICK, JOYNAME_DPAD
};

// -1 = automatic (XInput device -> Xbox page, else PS5 page); once the
// player chooses, 0=PS5, 1=Xbox, 2=generic (registry "JoyStyle").
long JoyStyle = -1;

long ActiveJoyStyle()
{
	if (JoyStyle >= 0)
		return JoyStyle;

	return joystick_is_xinput ? JOYSTYLE_XBOX : JOYSTYLE_PS5;
}

const char* JoyButtonName(long idx)
{
	if (idx < 0 || idx > 23)	// 20-23 are the d-pad's own bits
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
// Xbox default: RT=Duck LT=Dash RB=Walk
// X=Jump A=Action Y=Draw LB=Look B=Roll View=Inventory Menu=Pause.
static const long jDefXbox[15] = { 11, 10, 5, 2, 0, 3, 8, 4, 1, 6, 7, -1, -1, 18, 19 };
static const long jDefGeneric[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };

/* The four defaults above serve the "One key" Action setting in both control
 * schemes. The "Separate" setting has a layout of its own, stated as button
 * names:
 *
 *   walk Square   jump Cross    action Triangle   draw L2   look R3
 *   roll Circle   duck L1       sprint L3         flare R1  secondary R2
 *   inventory Share             pause Options     save PS button   load -
 *
 * The rows are derived from those names: each is looked up in the JoyNames*
 * table of its own page and its bit index goes in. That is why the two
 * XInput-ordered rows are identical while their names differ (Xbox reads
 * X/A/B/Y/LB/RB/LT/RT for Square/Cross/Circle/Triangle/L1/R1/L2/R2), and why
 * the generic row is the DirectInput numbers. Save is the PS button where a
 * page has one and unbound where it has not: the XInput pages stop at bit 11.
 * The medipacks (13, 14) are what the One-key tables give them for the page,
 * so the Modern move to the d-pad happens in one place (JoyDefaultRecord).
 *
 * The rows live in game\pad_scheme.h, 29 roles each with a chord mask: the
 * header holds the numbers, this file instantiates them, and
 * Preparation\Tools\Diagnose\MenuTest instantiates the same macros - so the
 * check that no two roles share a pair runs on the rows a player gets. */
static const long jSepPS5DInput[JOY_ROLES] = JOY_SEP_PS5_DINPUT;
static const long jSepPS5XInput[JOY_ROLES] = JOY_SEP_PS5_XINPUT;
static const long jSepXbox[JOY_ROLES] = JOY_SEP_XBOX;
static const long jSepGeneric[JOY_ROLES] = JOY_SEP_GENERIC;

/* The bits a scheme cannot honour. Each scheme takes one group of four
 * digital bits away from the bindings and gives it another job, so a role
 * sitting on the wrong group is broken, not a preference:
 *
 *   Modern   bits 16..19, the right stick. It is the camera, and
 *            S_UpdateInput masks the four bits out of joy_fire, so a role
 *            bound there can never fire.
 *   Tank     bits 20..23, the d-pad. It still moves Lara there
 *            (specific\dxshell.cpp writes the same four directions as a +-20
 *            stick reading unconditionally), so the role bound there fires
 *            alongside the movement - one press doing two things. */
#define JOY_RSTICK_BITS		0x000F0000L
#define JOY_DPAD_BITS		0x00F00000L

/* Which set has been seated for which scheme: -1 nothing yet, then 0 Tank /
 * 1 Modern. The registry keeps the pad tables (J*, SJ*) and their chord masks
 * but not which control scheme wrote them, so a table written under Tank, or
 * by an older build, can reach a Modern player with the medipacks still on
 * the right stick. This is what notices. */
static long joy_scheme_seated = -1;

/* The chord flags, one bit per role, one long per set (29 roles fit in 32
 * bits). `jChord` belongs to whichever Action setting is seated in jLayout and
 * `jSplitParkedChord` to the one parked in jSplitParked, as the two binding
 * arrays do; ApplyJoySplit exchanges both pairs together.
 *
 * The flags sit beside the binding, not encoded inside it: a registry binding
 * must stay a plain button number, because older builds read JDck..JSec and
 * SJDck..SJSec and would take an encoded flag for a button. The flags get
 * names of their own ("JChord", "SJChord") that older builds ignore.
 *
 * The starting values mean no chord, like jLayout's One-key initialiser:
 * LoadSettings overwrites both before the first frame. */
static long jChord = 0;
static long jSplitParkedChord = JOY_SEP_CHORD_DINPUT;

/* The yield masks, one bit per role, one long per set beside the chord
 * flags and travelling with them: the roles the scheme repair left unbound
 * because the Modifier holds their default, so that a later repair can seat
 * them again (PadSchemeRepair, game\pad_scheme.h). Stored as "JYield" and
 * "SJYield", which older builds ignore; 0 - nothing waiting - is what a
 * registry without them means. */
static long jYield = 0;
static long jSplitParkedYield = 0;

/* The one place that produces a 29-role default record and its chord mask, for
 * either scheme and either Action setting (`split`: 0 One key, 1 Separate). The
 * active style's own table is the source in all four cases, so the roles the
 * schemes share cannot drift apart. */
static void JoyDefaultRecord(long modern, long split, long* out, long* chord)
{
	const long* source;

	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		source = joystick_is_xinput
			? (split ? jSepPS5XInput : jDefPS5XInput)
			: (split ? jSepPS5DInput : jDefPS5DInput);
		break;
	case JOYSTYLE_XBOX:
		source = joystick_is_xinput
			? (split ? jSepXbox : jDefXbox)
			: (split ? jSepGeneric : jDefGeneric);
		break;
	default:
		source = split ? jSepGeneric : jDefGeneric;
		break;
	}

	/* The chord mask follows the source table chosen by the switch above: the
	 * DirectInput-numbered Separate rows and the XInput-numbered ones each have
	 * their own mask, and at One key there is nothing to chord (0).
	 *
	 * `chord` may be NULL: ApplyJoyScheme wants the other scheme's record for
	 * its buttons alone. */
	if (chord)
	{
		if (!split)
			*chord = 0;
		else if (source == jSepPS5DInput || source == jSepGeneric)
			*chord = JOY_SEP_CHORD_DINPUT;
		else
			*chord = JOY_SEP_CHORD_XINPUT;
	}

	// Fifteen and not JOY_ROLES: that is the length of the One-key tables; the
	// roles behind it are filled below, where the two settings are told apart.
	for (int i = 0; i < 15; i++)
		out[i] = source[i];

	/* At "One key" the Secondary role and the thirteen roles behind it have no
	 * default: the Secondary role is inert and the binding page does not offer
	 * it, the Modifier is unbound so no chord can exist, and the hotkey roles
	 * start unbound. Under Separate the tables of game\pad_scheme.h carry all
	 * of them, which is why source[i] is read only on that branch. */
	for (int i = 15; i < JOY_ROLES; i++)
		out[i] = split ? source[i] : -1;

	/* The Modern pad set. Duck..Load (roles 0-12) are the same as Tank's. Only
	 * the medipacks (roles 13-14) differ: their Tank default on an XInput-style
	 * page is the right stick (bits 18/19), which is the camera under Modern, so
	 * they move to the d-pad's own bits (specific\dxshell.cpp). D-pad Left/Right
	 * (bits 22/23) default to nothing and are left for the player's binds. */
	if (modern)
	{
		out[13] = 20;	// D-Pad Up   -> Small Medipack, specific\dxshell.cpp
		out[14] = 21;	// D-Pad Down -> Large Medipack
	}
}

void ApplyJoyDefaults()
{
	/* Reset restores the active set's own defaults, like the keyboard's Reset.
	 * The parked set holds the other Action setting's bindings and is not
	 * touched - resetting the Separate layout must not lose the One-key one. */
	const long split = tomb5.action_split ? 1 : 0;

	/* The chord flags are reset with the buttons: a player who had put quick
	 * save on a plain R3 must get Modifier+R3 back, not a plain R3. One call
	 * sets both, so the two cannot come back out of step. */
	JoyDefaultRecord(tomb5.control_modern ? 1 : 0, split, jLayout, &jChord);

	// Every role is on its default again, so nothing waits to be seated.
	jYield = 0;

	joy_scheme_seated = tomb5.control_modern ? 1 : 0;
}

/* The active scheme repairs the bindings it cannot honour, and touches nothing
 * else. Without it, switching to Modern leaves the medipacks on the right stick
 * (jDefXbox[13]/[14] are 18/19), which Modern masks; and a Reset under Modern
 * stores 20/21, so back at Tank the d-pad uses a medipack on every step.
 *
 * The rule has two parts:
 *  1. A role bound to a bit the active scheme has taken for something else
 *     goes back to that scheme's default for that role. On its own this
 *     repairs only the XInput-style pages: jDefPS5DInput and jDefGeneric put
 *     the medipacks on face buttons 13/14, which Modern does not mask.
 *  2. So a role still sitting on the other scheme's default for itself
 *     follows the switch as well. This only moves roles the two records
 *     disagree about - the two medipacks; roles 0-12 are equal in both.
 * Everything else is the player's own choice and is kept.
 *
 * It is idempotent. A freed bit that collides with a role already holding it
 * unbinds that older role, as the binding page does (specific\LoadSave.cpp) -
 * except that a displaced role gets this scheme's own default if that button
 * is free, and only falls to unbound if not. Without that, a medipack put on
 * the d-pad by hand under Tank is lost on the way into Modern.
 *
 * The Modifier is the one role that is never displaced: every chord of the
 * set hangs on it. A role whose default is the Modifier's pair is left
 * unbound instead and remembered in the set's yield mask, and a later repair
 * that finds the default free seats it again. On the PS5 DirectInput page at
 * One key, Tank's Small Medipack default is bit 13, the touchpad, which is
 * where the Separate row and a player put the Modifier; displacing it there
 * would silence every chord on the way into Tank and leave them silent on the
 * way back. The rule itself is PadSchemeRepair (game\pad_scheme.h).
 *
 * Known cost of part 2: a binding the player chose that equals the other
 * scheme's default for that role cannot be told from an unrepaired one, so it
 * follows the switch too; since the registry does not store which scheme wrote
 * the table, the one-time seat in S_UpdateInput repeats that on every launch.
 * It can only reach the two medipacks.
 *
 * "Taken" means the pair (button + chord flag), not the button: R3 and
 * Modifier+R3 coexist on purpose, and judging by the button alone would unbind
 * one of them whenever the other moved. */
static long JoyChordOf(long role)
{
	return (jChord >> role) & 1;
}

static void JoyChordWrite(long role, long on)
{
	if (on)
		jChord |= 1L << role;
	else
		jChord &= ~(1L << role);
}

void ApplyJoyScheme()
{
	long want[JOY_ROLES];
	long other[JOY_ROLES];
	long want_chord;
	long yielded;
	long seated;
	const long modern = tomb5.control_modern ? 1 : 0;
	// The repair works on the active set (jLayout), whichever Action setting
	// seated it, and measures it against the same setting's defaults - otherwise
	// a Separate player's action button would look like "the other scheme's
	// default" and follow a switch it has nothing to do with.
	const long split = tomb5.action_split ? 1 : 0;
	const long blocked = modern ? JOY_RSTICK_BITS : JOY_DPAD_BITS;

	JoyDefaultRecord(modern, split, want, &want_chord);
	// NULL and not a second mask: `other` is asked for its buttons only, and
	// the repair writes the active record's flags (want_chord). The two masks
	// are equal anyway, since both calls get the same `split`.
	JoyDefaultRecord(modern ? 0 : 1, split, other, NULL);

	PadSchemeRepair(jLayout, &jChord, &jYield, want, want_chord, other, blocked,
		&yielded, &seated);

	/* PADLOG repair: which role the Modifier kept off its default and which
	 * one went back to it - the two moves that are not a plain repair. */
	if (gLogEnabled)
		for (int i = 0; i < JOY_ROLES; i++)
		{
			if ((yielded >> i) & 1)
				Log("PADLOG repair role=%d default=%ld held by the modifier, left unbound",
					i, want[i]);

			if ((seated >> i) & 1)
				Log("PADLOG repair role=%d seated=%ld", i, jLayout[i]);
		}

	joy_scheme_seated = modern;
}

/* The parked pad set: the bindings of the Action setting that is not active.
 * Separate needs more roles and moves half the others, so the two settings
 * cannot share one table. As with the keyboard, the active setting's set sits
 * in jLayout, where the binding page, Key() and every producer read it, and the
 * other waits here. Flipping the Action row exchanges the two (PadSetSeat,
 * game\pad_scheme.h), so a player who tries Separate and comes back finds the
 * pad as it was.
 *
 * The starting value is the PS5 DirectInput Separate row, only a fallback for a
 * registry that cannot be opened: LoadSettings overwrites both sets before the
 * first frame (specific\registry.cpp). */
static long jSplitParked[JOY_ROLES] = JOY_SEP_PS5_DINPUT;

/* Which Action setting's set is in jLayout: 0 One key, 1 Separate. It starts at
 * One key and not at -1 (unlike joy_scheme_seated) because the two sets have
 * names of their own in the registry - J* and SJ* - so a table's setting never
 * has to be guessed; this only records whether the swap has been done. */
static long joy_split_seated = 0;

/* The stored set of one Action setting, wherever it is sitting. The registry
 * addresses the two by setting and not by array, because JDck..JSec must keep
 * meaning the One-key set whichever setting is active at shutdown - older
 * builds read those names and know nothing of SJ*. */
long* JoySplitSet(long split)
{
	return (joy_split_seated == (split ? 1 : 0)) ? jLayout : jSplitParked;
}

// The same for the chord flags: the registry addresses a set by setting, never
// by array, so the flags are reachable the same way the bindings are.
long* JoyChordSet(long split)
{
	return (joy_split_seated == (split ? 1 : 0)) ? &jChord : &jSplitParkedChord;
}

// And the yield mask, the same way.
long* JoyYieldSet(long split)
{
	return (joy_split_seated == (split ? 1 : 0)) ? &jYield : &jSplitParkedYield;
}

/* What a registry without SJ* means: this page's and this path's Separate
 * defaults, with their chord mask. Computed rather
 * than written out a second time in specific\registry.cpp, so game\pad_scheme.h
 * stays the only place the layout is stated. */
void JoySplitDefaults(long* out, long* chord)
{
	JoyDefaultRecord(tomb5.control_modern ? 1 : 0, 1, out, chord);
}

/* The active set's chord flag, for the binding page and for the capture. The
 * repair has its own (game\pad_scheme.h); these are the ones outside this
 * file. */
long JoyRoleIsChord(long role)
{
	return JoyChordOf(role);
}

void JoyRoleSetChord(long role, long on)
{
	JoyChordWrite(role, on);
}

/* The pad's half of the Action row, beside ApplyKeyScheme. Called from the row
 * itself (game\menu_frame.cpp) and once at startup, the first moment
 * tomb5.action_split is known (specific\gamemain.cpp).
 *
 * The scheme repair then runs on the set that just sat down, but only once a
 * scheme has been seated. ApplyJoyScheme judges a binding by ActiveJoyStyle()
 * and joystick_is_xinput, which are known only after the first poll - which is
 * why the Modern seat waits for `joystick_read` in S_UpdateInput. Running it
 * earlier would set joy_scheme_seated and cancel that pending seat; when no
 * seat has happened yet, the pending one repairs whichever set is active. */
void ApplyJoySplit()
{
	const long split = tomb5.action_split ? 1 : 0;

	if (joy_split_seated == split)
		return;

	/* The chord flags travel with their set. It sits after the guard above
	 * for the same reason PadSetSeat is idempotent: the Action row and the
	 * startup seat both call this and neither may swap twice. */
	{
		const long t = jChord;

		jChord = jSplitParkedChord;
		jSplitParkedChord = t;
	}

	// And so does the yield mask, for the same reason.
	{
		const long t = jYield;

		jYield = jSplitParkedYield;
		jSplitParkedYield = t;
	}

	joy_split_seated = PadSetSeat(jLayout, jSplitParked, (int)joy_split_seated, (int)split);

	if (joy_scheme_seated >= 0)
		ApplyJoyScheme();

	if (gLogEnabled)
		Log("KEYLOG split=%s seated", split ? "separate" : "onekey");
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

/* Whether a menu is reading the direction bits, so the d-pad keeps working in
 * the inventory and on the title screen alongside the stick.
 *
 * In Modern the movement pair is the left stick's own axes, which the d-pad
 * never writes over, and the menus read the same bits. While this is true the
 * pair goes back to joy_x/joy_y, which the d-pad writes into: stick and pad
 * both, as at Tank.
 *
 * These are the two terms the movement seam in S_UpdateInput carries with the
 * opposite sign; the two must never disagree. InventoryActive is not only the
 * ring: the pause menu, the load and save screens (specific\LoadSave.cpp) and
 * the outfit window (specific\hd) set it too. The title has no Lara to steer,
 * so it is named by its level id. The rest of the seam's guard - first person,
 * a spotcam, a cutscene, bDisableLaraControl, the look key - are game states,
 * and none of them reads a direction bit as a cursor step. */
static long ModernMenuReadsPad()
{
	return (InventoryActive || gfLevelId == LVL5_TITLE) ? 1 : 0;
}

// Whether this binding is one of the d-pad's own four bits (20..23,
// specific\dxshell.cpp). -1 (unbound) and every other binding answer no,
// which also keeps the shift off a negative count; the upper bound guards a
// value that comes out of the registry.
static long JoyBitIsDpad(long bit)
{
	return (bit >= 0 && bit <= 23 && ((1L << bit) & JOY_DPAD_BITS) != 0) ? 1 : 0;
}

/* The active pad set, 29 roles. Slots 0-14 are the classic roles, 15 the
 * Secondary action, 16 the Modifier, 17-22 the six weapon hotkeys, 23-27 the
 * five cheats, 28 the HD/SD switch. Whichever set the Action row has seated
 * lives here (JoySplitSet).
 *
 * The initialiser is the One-key PS5 DirectInput row with -1 behind it, only a
 * fallback for a registry that cannot be opened: LoadSettings overwrites the
 * whole array before the first frame. -1 is what Key() and PadRoleFires read
 * as "unbound"; 0 would be a real button bit (Square on that page). */
long jLayout[JOY_ROLES] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

/* The Modern row's scan codes live in game\key_scheme.h, so that MenuTest
 * checks the row the engine really uses. The header cannot include dinput.h,
 * so these asserts state at compile time which key each of its numbers is; a
 * typo in either place stops the build. */
static constexpr short kModernRow[KEY_SCHEME_ROLES] = KEY_SCHEME_MODERN_ROW;
static_assert(kModernRow[0] == DIK_W, "modern row: forward is W");
static_assert(kModernRow[1] == DIK_S, "modern row: back is S");
static_assert(kModernRow[2] == DIK_A, "modern row: left is A");
static_assert(kModernRow[3] == DIK_D, "modern row: right is D");
static_assert(kModernRow[4] == DIK_C, "modern row: duck is C");
static_assert(kModernRow[5] == DIK_LSHIFT, "modern row: dash is left shift");
static_assert(kModernRow[6] == DIK_LMENU, "modern row: walk is left alt");
static_assert(kModernRow[7] == DIK_SPACE, "modern row: jump is space");
static_assert(kModernRow[8] == DIK_E, "modern row: action is E");
static_assert(kModernRow[9] == KEY_MOUSE_RIGHT, "modern row: draw is the right mouse button");
static_assert(kModernRow[10] == DIK_F, "modern row: flare is F");
static_assert(kModernRow[11] == KEY_MOUSE_MIDDLE, "modern row: look is the middle mouse button");
static_assert(kModernRow[12] == DIK_LCONTROL, "modern row: roll is left ctrl");
static_assert(kModernRow[13] == DIK_ESCAPE, "modern row: inventory is escape");
static_assert(kModernRow[14] == DIK_Z, "modern row: step left is Z");
static_assert(kModernRow[15] == DIK_X, "modern row: step right is X");
static_assert(kModernRow[16] == DIK_P, "modern row: pause is P");
static_assert(kModernRow[17] == DIK_RETURN, "modern row: select is return");
static_assert(kModernRow[18] == KEY_MOUSE_LEFT, "modern row: secondary action is the left mouse button");
/* The mouse's codes lie above every index keymap has, so no scan code can be
 * mistaken for a button and no button is ever looked up in keymap. */
static_assert(KEY_MOUSE_FIRST >= sizeof(keymap), "the mouse codes start above keymap");
static_assert(KEY_MOUSE_FIRST + KEY_MOUSE_BUTTONS - 1 <= 0x7FFF, "a mouse code fits the short a bind slot is");
/* The fifteen hotkey slots. Each default is the fixed key Core reads for that
 * function, so with an untouched registry Key(role) - whose first test is
 * keymap[layout[1][role]] - reads the same key as Core did; quick load is the
 * one exception (F9 instead of F6). */
static_assert(kModernRow[KEY_ROLE_MEDI_SMALL] == DIK_0, "modern row: small medipack is 0");
static_assert(kModernRow[KEY_ROLE_MEDI_LARGE] == DIK_9, "modern row: large medipack is 9");
static_assert(kModernRow[KEY_ROLE_SAVE] == DIK_F5, "modern row: quick save is F5");
static_assert(kModernRow[KEY_ROLE_LOAD] == DIK_F9, "modern row: quick load is F9 - the one deliberate default change");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 0] == DIK_1, "modern row: pistols are 1");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 1] == DIK_2, "modern row: shotgun is 2");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 2] == DIK_3, "modern row: uzi is 3");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 3] == DIK_4, "modern row: revolver is 4");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 4] == DIK_5, "modern row: HK is 5");
static_assert(kModernRow[KEY_ROLE_WEAPON_FIRST + 5] == DIK_6, "modern row: crossbow is 6");
static_assert(kModernRow[KEY_ROLE_CHEAT_FLY] == DIK_F2, "modern row: fly mode is F2");
static_assert(kModernRow[KEY_ROLE_CHEAT_GUNS] == DIK_F7, "modern row: all weapons is F7");
static_assert(kModernRow[KEY_ROLE_CHEAT_ITEMS] == DIK_F8, "modern row: all items is F8");
static_assert(kModernRow[KEY_ROLE_CHEAT_HEAL] == DIK_F11, "modern row: heal is F11");
static_assert(kModernRow[KEY_ROLE_CHEAT_SKIP] == DIK_F1, "modern row: skip level is F1");
static_assert(kModernRow[KEY_ROLE_HD_SWITCH] == DIK_F12,
	"modern row: the HD/SD switch is F12");

/* The keyboard rows. Every reader and writer in the tree uses row 1, so the
 * second keyboard set is seated into it rather than read from somewhere new -
 * specific\registry.cpp, specific\LoadSave.cpp and game\menu_frame.cpp need no
 * second path.
 *
 *   0  the Tank default row - Core's own, plus the slots below
 *   1  the active set: whichever scheme is seated (KeySchemeSet, ApplyKeyScheme)
 *   2  the Modern default row (game\key_scheme.h)
 *   3  the parked set: the other scheme's, kept while it is not in use
 *
 * Row 3 starts as the Modern defaults because row 1 starts as the Tank set and
 * Tank is the seated scheme at startup (key_scheme_seated below).
 *
 * 35 columns. Slot 18 is the Secondary action; Tank's is DIK_HOME, since Core's
 * cluster uses End, Delete, Page Down and the arrows but never Home, so the row
 * keeps every slot bound and no key twice (KeySchemeRowSound,
 * game\key_scheme.h) without taking a key off an existing role. Slots 19-33
 * are the hotkeys Core reads as fixed keys - the medipacks' 0 and 9, save's F5,
 * load's F6, the weapon keys 1..6 - plus the five free F keys the cheat roles
 * take. The Modern row differs from Tank's there in one slot: quick load, F9.
 * Slot 34 is the HD/SD switch, F12 in both rows. */
short layout[4][KEY_SCHEME_ROLES] =
{
	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN, DIK_HOME,
	DIK_0, DIK_9, DIK_F5, DIK_F6, DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6,
	DIK_F2, DIK_F7, DIK_F8, DIK_F11, DIK_F1, DIK_F12 },

	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN, DIK_HOME,
	DIK_0, DIK_9, DIK_F5, DIK_F6, DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6,
	DIK_F2, DIK_F7, DIK_F8, DIK_F11, DIK_F1, DIK_F12 },

	KEY_SCHEME_MODERN_ROW,

	KEY_SCHEME_MODERN_ROW
};

/* Which scheme's set is in row 1: 0 Tank, 1 Modern. It starts at Tank and not
 * at -1 (unlike the pad's joy_scheme_seated) because the two sets have names of
 * their own in the registry - Key0..Key17 and MKey0..MKey17 - so a table's
 * scheme never has to be guessed; this only records whether the swap has been
 * done. */
static long key_scheme_seated = 0;

/* The default row the active scheme falls back to - Key()'s second chance and
 * CheckKeyConflicts()'s subject. Under Modern it must be the Modern row: its
 * movement keys are W/S/A/D, and Tank's arrow keys falling through behind them
 * would move Lara relative to a camera she no longer faces along. At Tank this
 * is layout[0], Core's row. */
const short* KeyDefaultRow()
{
	return layout[tomb5.control_modern ? 2 : 0];
}

/* The stored set of one scheme, wherever it is sitting. The registry addresses
 * the two sets by scheme and not by row, because Key0..17 must keep meaning the
 * classic set whichever scheme is active at shutdown (older builds read those
 * names and ignore MKey*). */
short* KeySchemeSet(long modern)
{
	return (key_scheme_seated == (modern ? 1 : 0)) ? layout[1] : layout[3];
}

/* The keyboard's half of the scheme switch, beside ApplyJoyScheme. The pad
 * repairs one stored set per Action setting; the keyboard exchanges two,
 * because each scheme keeps its own - a player who rebound Modern's W/S/A/D and
 * went back to Tank finds the Tank keys, and the Modern keys are still there on
 * return.
 *
 * The conflict table is recomputed because it is derived from both rows that
 * just moved: layout[1] is the other set now, and KeyDefaultRow() answers with
 * the other default row. Stale conflict[] entries would make a role's default
 * key silent - or loud - for the wrong reason until the next capture. */
void ApplyKeyScheme()
{
	const long modern = tomb5.control_modern ? 1 : 0;

	if (key_scheme_seated == modern)
		return;

	key_scheme_seated = KeySchemeSeat(layout[1], layout[3], (int)key_scheme_seated, (int)modern);
	CheckKeyConflicts();

	if (gLogEnabled)
		Log("KEYLOG scheme=%s seated", modern ? "modern" : "tank");
}

long conflict[KEY_SCHEME_ROLES];
long input;
long dbinput;
long inputBusy;
long joystick_read;
long joystick_read_x;
long joystick_read_y;
long joystick_raw_rx;		// the right stick as two axes, before its per-axis zone - the modern camera
long joystick_raw_ry;
long joystick_raw_lx;		// the left stick alone and raw - the modern movement, see S_UpdateInput
long joystick_raw_ly;
long joystick_read_fire;
long joystick_is_xinput;	// 1 = XInput is delivering (Xbox pad, or DS4Windows/Steam), 0 = DirectInput fallback; set anew on every poll (dxshell.cpp)

static long joy_fire;
static long joy_x, joy_y;

/* The mouse's five buttons for this S_UpdateInput, bit n = button n in the
 * order of KEY_MOUSE_* (game\key_scheme.h): down now, or pressed and let go
 * since the last call (WinMouseTake, specific\winmain.cpp). */
static long mouse_buttons;

// The counts each axis still owes the orbit (ModernCameraMouseSpend,
// game\modern_camera.cpp).
static long mouse_owed_x, mouse_owed_y;

// The same mask for the Key bindings page, which captures a button the way it
// captures a key (game\menu_frame.cpp).
long InputMouseButtons()
{
	return mouse_buttons;
}

/* The look key, as the Modern scheme remembers it. While the stick steers Lara
 * as in Tank the look bit is taken out of the input word - the engine must not
 * answer it with the look camera - so the gate cannot read `input & IN_LOOK`:
 * it would read back the bit it had just cleared, let the seam run again, set
 * the bit again and flicker between the two schemes at 30 Hz.
 *
 * So this holds what `input & IN_LOOK` would have said: the look bit of the
 * last word this function built, taken before that word is stripped, with the
 * six-tick IN_TARGET swallow already applied. Used only under
 * tomb5.control_modern. */
static long modern_look_tank;

/* Which of the two sectors the thumb was in last tick. The hysteresis of
 * MODERN_TANK_SECTOR_HYST needs one tick of memory, and it lives here rather
 * than in game\modern_move.cpp because this file knows when a hold begins and
 * ends: the block that calls ModernTankSector clears it on every tick that is
 * not a bypass tick, so a fresh hold starts from MODERN_TANK_SECTOR_NONE and
 * the plain boundary. Used only under tomb5.control_modern. */
static long modern_tank_prev;

/* Sprint mode and Duck mode, their whole memory: two longs of MODERN_LATCH_*
 * bits, one per mode. The arithmetic is pure and lives in game\modern_move.h;
 * the state is the engine's and lives here. Written only from the mode seam in
 * S_UpdateInput, entered only while one of the two rows says Toggle, and from
 * InputModeLatchReset below. At the defaults they stay 0. */
static long sprint_latch;
static long duck_latch;

/* Grab mode's latch, of the same kind and written from the same two places. It
 * holds only MODERN_GRAB_RELEASE and MODERN_LATCH_KEY - it remembers which press
 * let go, never that Lara is holding on, because her state says that
 * (game\modern_move.cpp). At the default it stays 0. */
static long grab_latch;

/* Drops the mode latches from outside a tick. Callers:
 *
 * - game\gameflow.cpp, beside the level's priming tick. That line covers a
 *   level start and every savegame restore, because sgRestoreGame has exactly
 *   one caller, earlier in the same function - as for ModernResetControls'
 *   call there. Not under tomb5.control_modern, unlike that call: these modes
 *   are read in both control types.
 * - game\menu_frame.cpp, from each mode row's step function. Without it a latch
 *   could outlive its option: the seam is not entered while all rows say Hold,
 *   so a duck latched before the row was switched off would still stand when
 *   it is switched back on.
 *
 * The key memory is left alone on purpose: a key still held across a level
 * boundary or out of a menu is not a fresh press and must not latch
 * (game\modern_move.cpp). */
void InputModeLatchReset()
{
	ModernModeLatchDrop(&sprint_latch);
	ModernModeLatchDrop(&duck_latch);
	ModernModeLatchDrop(&grab_latch);
}

/* Whether the look key steers Lara as in Tank this tick. The bypass runs
 * instead of ModernUpdateInput, whose MODERNLOG line is then silent, so a log
 * could not tell "steered as in Tank" from "the seam never ran"; this is what
 * MODERNLOG prints as `mode=tank`. */
long ModernLookTankActive()
{
	return modern_look_tank;
}

// The pad's stick/d-pad as a single menu step, edge triggered: -1 left,
// +1 right, 0 nothing. Deliberately not gated by ControlMethod.
//
// ControlMethod carries two meanings at once - which binding page is shown,
// and whether the pad is read at all - so on the keyboard page the pad is
// dead. Without this, a player who paged there with the pad could not page
// back with it, and that page is where the setting itself lives.
//
// Only the controls screen uses this. Everywhere else ControlMethod keeps its
// meaning - a player who chose keyboard does not want a resting stick moving
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

// Whether the stick/d-pad is deflected at all. The controls screen uses it to
// keep one deflection from being counted twice.
//
// When ControlMethod changes, JoyMenuStep takes the step on the keyboard page,
// the method flips to the pad, and in the next frame the still-held direction
// reaches dbinput for the first time - which its edge detection reads as a
// fresh press: one push, two pages. So while the stick is deflected, the step
// belongs to JoyMenuStep alone and dbinput is ignored.
long JoyMenuDeflected()
{
	return joy_x < -8 || joy_x > 8;
}

short ammo_change_timer = 0;
char ammo_change_buf[12];

//----------------------------------------------------------------------------
// One key per weapon, the same key everywhere.
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
// depends on the level. Keys 5 to 8 are free in play - only the save/load
// screen uses 1 to 4, and that is a different context.
//
// Everything inside the branches is Core's code, unchanged: the "is it
// carried" test, the draw, and the ammunition cycling on a second press
// (which alone is what the "Ammotype hotkeys" option governs - drawing a
// weapon works whether it is on or off).
// The one difference: the caption a cycle shows goes through CustomText
// (game\gameflow.cpp) like every word the project draws, in German too.
// Every column of its row fits the caption's twelve bytes.
//----------------------------------------------------------------------------
/* One routine taking which weapon, called by the keyboard and the pad alike:
 * each weapon role is read as Key(role) OR the pad's role, the same way as the
 * medipacks and quick save/load. Core's else-if chain became a switch with the
 * same order and the same early `return`s; it runs one branch, because the
 * caller stops at the first weapon whose role answers. */
static void DoWeaponRequest(long which)
{
	switch (which)
	{
	case 0:
		if (!(lara.pistols_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_PISTOLS;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_PISTOLS)
			lara.gun_status = LG_DRAW_GUNS;
		break;

	case 1:
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
					strcpy(ammo_change_buf, CustomText("Normal"));
				}
				else if (lara.shotgun_type_carried & W_AMMO1)
				{
					lara.shotgun_type_carried &= ~W_AMMO1;
					lara.shotgun_type_carried |= W_AMMO2;
					ammo_change_timer = 30;
					strcpy(ammo_change_buf, CustomText("Wideshot"));
				}
			}
		}
		break;

	case 2:
		if (!(lara.uzis_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_UZI;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_UZI)
			lara.gun_status = LG_DRAW_GUNS;
		break;

	case 3:
		if (!(lara.sixshooter_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_REVOLVER;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_REVOLVER)
			lara.gun_status = LG_DRAW_GUNS;
		break;

	case 4:
		// The HK has its own key instead of sharing key 1 with the pistols.
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
					strcpy(ammo_change_buf, CustomText("Burst"));
				}
				else if (lara.hk_type_carried & W_AMMO2)
				{
					lara.hk_type_carried &= ~W_AMMO2;
					lara.hk_type_carried |= W_AMMO1;
					ammo_change_timer = 30;
					strcpy(ammo_change_buf, CustomText("Sniper"));
				}
				else if (lara.hk_type_carried & W_AMMO1)
				{
					lara.hk_type_carried &= ~W_AMMO1;
					lara.hk_type_carried |= W_AMMO3;
					ammo_change_timer = 30;
					strcpy(ammo_change_buf, CustomText("Rapid"));
				}
			}
		}
		break;

	case 5:
		// The crossbow has its own key instead of sharing key 2 with the shotgun.
		if (!(lara.crossbow_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_CROSSBOW;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_CROSSBOW)
			lara.gun_status = LG_DRAW_GUNS;
		break;
	}
}

/* Which weapon role is asking: Core's guard (not young Lara, not in deep water,
 * not crawling or ducking), then the six roles in order. The loop stops at the
 * first role that answers, as Core's else-if chain did.
 *
 * The role is read every tick while held, for the pad as for the keyboard.
 * Drawing a weapon is idempotent (request_gun_type is a request), and the one
 * thing a repeat could run away with - the ammunition cycle - is held off by
 * Core's ammo_change_timer. */
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

	for (long i = 0; i < KEY_ROLE_WEAPONS; i++)
	{
		if (Key(KEY_ROLE_WEAPON_FIRST + i) ||
			(ControlMethod == 1 && JoyRole(JOY_ROLE_WEAPON_FIRST + i)))
		{
			DoWeaponRequest(i);
			return;
		}
	}
}

long Key(long number)
{
	if (number == 255)
		// jLayout[n] was -1 (unbound - see LoadSave.cpp's auto-unbind-on-conflict),
		// so the caller's "+256" offset landed on 255 instead of a real >=256
		// joystick-button value. 255 is never a keyboard role index (0-33) or an
		// encoded joystick value (button indices 0-23, i.e. 256-279), so it is
		// safe as this sentinel.
		//
		// The engine itself no longer reaches this: every pad read goes through
		// JoyRole/PadRoleFires, which handles -1 itself. The sentinel stays
		// because Key() is public and the encoding is still the one JoyRole
		// undoes.
		return 0;

	if (number >= 256)
		// number is jLayout[n]+256 (a "read from the joystick, not the keyboard"
		// signal) - joy_fire holds one bit per physical button starting at bit 0
		// (see DXUpdateJoystick), so the actual bit position needs the 256 taken
		// back out. Shifting by `number` itself (256 and up) would always give 0.
		return joy_fire & (1 << (number - 256));

	short key = layout[1][number];

	/* A mouse button's code is read from the mouse's mask, a key's from
	 * keymap; a code above 0xFF that is no button (a hand-edited registry)
	 * answers 0 instead of reading past keymap's end. The switch below has no
	 * case a mouse code could match, so a button falls through it untouched. */
	if (key >= KEY_MOUSE_FIRST ? (mouse_buttons & KeySchemeMouseBit(key)) != 0 : keymap[key] != 0)
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

	// The active scheme's default row, which at Tank is layout[0] (KeyDefaultRow).
	// Under Modern three of its slots are mouse buttons, read the same way.
	key = KeyDefaultRow()[number];

	if (key >= KEY_MOUSE_FIRST ? (mouse_buttons & KeySchemeMouseBit(key)) != 0 : keymap[key] != 0)
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

/* The pad's own Key(); every pad producer goes through it. A role is a pair -
 * a button and whether the Modifier has to be held with it - and the pair is
 * answered in one place, PadRoleFires (game\pad_scheme.h, pure), so the
 * medipacks, the flare and the weapons cannot each judge it differently. This
 * supplies the state it needs.
 *
 * With the Modifier unbound (-1, as at "One key" on every page and path) every
 * chord flag is 0 and the rule is exactly "is the role's button down in
 * joy_fire", as a plain bit test.
 *
 * ControlMethod is not tested here: every call site already stands inside
 * `if (ControlMethod == 1)` or carries it in its own condition. */
long JoyRole(long role)
{
	return PadRoleFires((unsigned long)joy_fire, jLayout[role], (int)JoyChordOf(role),
		jLayout[JOY_ROLE_MODIFIER]);
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

	/* The mouse, taken once per call. The window thread gathers its counts and
	 * buttons between two calls (specific\winmain.cpp), and this is the one
	 * place that takes them, so in play - one call per logic tick
	 * (game\control.cpp) - the counts of every drawn frame between two ticks
	 * reach the one tick, at 30, 60 and the display's own rate alike. The two
	 * terms the game thread owns of the capture rule are reported first; the
	 * window thread acts on a change. */
	long mouse_dx, mouse_dy;
	long mouse_spent_x = 0;
	long mouse_spent_y = 0;
	long mouse_yaw = 0;
	long mouse_pitch = 0;

	WinMouseWant(tomb5.control_modern ? 1 : 0, App.fmv ? 1 : 0);
	WinMouseTake(&mouse_dx, &mouse_dy, &mouse_buttons);

	// Read unconditionally. Reading has no effect of its own - every use of
	// joy_x/joy_y/joy_fire below is behind ControlMethod - but it keeps the
	// values current for JoyMenuStep, which the controls screen needs
	// precisely when ControlMethod says keyboard.
	joy_fire = ReadJoystick(joy_x, joy_y);

	/* In Modern the right stick is the camera, and not four buttons as well.
	 *
	 * specific\dxshell.cpp turns its four directions into four extra digital
	 * buttons, bits 16..19 of joy_fire past the sixteen physical ones, and the
	 * default XInput pages bind them: jDefPS5XInput[13]/[14] and jDefXbox's are
	 * 18 and 19, so pushing the stick left or right would use a medipack. Under
	 * Modern the four bits are masked out here (the medipacks move to the
	 * d-pad, see JoyDefaultRecord).
	 *
	 * The axes go to the orbit instead - stored, not acted on, because this
	 * function also serves the menus, the inventory and the FMV player, and the
	 * orbit may only move once per logic tick (game\camera.cpp).
	 *
	 * The binding screen still sees the whole word: specific\LoadSave.cpp calls
	 * ReadJoystick itself, so the four directions can still be bound there. */
	if (tomb5.control_modern)
	{
		/* The table has to match the scheme before the mask below. The table
		 * this session starts with may have been written under Tank or by an
		 * older build, with the medipacks still on the bits masked below. The
		 * scheme switch repairs the table when the player flips it
		 * (specific\LoadSave.cpp); this is the same repair for a session that
		 * starts in Modern.
		 *
		 * Once, and only with a pad really answering: joystick_is_xinput - and
		 * with it which default table is right - is set by the poll and not by
		 * the init, and the DirectInput half of that init runs in a thread of
		 * its own (specific\dxshell.cpp). */
		if (joy_scheme_seated != 1 && ControlMethod == 1 && joystick_read)
			ApplyJoyScheme();

		const long rstick_bits = JOY_RSTICK_BITS;	// bits 16..19, specific\dxshell.cpp
		long cam_x = 0;
		long cam_y = 0;

		/* The camera stick goes through its own radial dead zone, on the raw
		 * pair: the per-axis zeroing in specific\dxshell.cpp judges each
		 * component alone and so snaps a nearly-axial push onto the axis. The
		 * width is XInput's own 8689 of 32767, except on the DirectInput path,
		 * where the measured pad's Z/Rz rest off centre and only a wider zone keeps
		 * a resting stick from turning the camera (game\modern_move.h carries
		 * both numbers).
		 *
		 * The second argument is the per-axis square that zone replaces: a disc
		 * reaches less far into the corners than the square of the same
		 * half-width, so a stick resting in a corner would otherwise start
		 * turning the camera. The stick is dead in either.
		 *
		 * ControlMethod == 1, so a controller resting on the desk cannot steer a
		 * keyboard player's camera; otherwise cam_x/cam_y stay zero, so a stick
		 * that was out when the player switched to the keyboard cannot leave the
		 * orbit turning.
		 *
		 * Both widths are scaled by the player's tomb5.dead_zone through
		 * ModernDeadZoneUser; the default 50 returns them unchanged
		 * (game\modern_move.cpp). Asked at every use, so moving the slider takes
		 * effect on the next tick. */
		if (ControlMethod == 1)
			ModernStickDeadZone(
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_RIGHT : MODERN_DEAD_RIGHT_DINPUT,
					(long)tomb5.dead_zone),
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_AXIS_RIGHT : MODERN_DEAD_AXIS_RIGHT_DINPUT,
					(long)tomb5.dead_zone),
				joystick_raw_rx, joystick_raw_ry, &cam_x, &cam_y);

		ModernCameraStick(cam_x, cam_y);

		/* The mouse turns the same orbit. Its counts are there only while the
		 * cursor is captured, which is only ever under Modern, and they need no
		 * dead zone: a resting mouse reports nothing. Not behind ControlMethod,
		 * for the reason a key is not: the mouse is a device of its own, and a
		 * player on the pad page can still reach for it.
		 *
		 * The counts go through ModernCameraMouseSpend first, which spends at
		 * most a step's worth a tick and owes the rest to the next one, so no
		 * tick's step is large enough to be taken for a camera cut at 60 fps
		 * (game\modern_camera.h). What is owed is forgotten when the cursor is
		 * let go, as the gathered counts are. */
		if (!WinMouseCaptured())
		{
			mouse_owed_x = 0;
			mouse_owed_y = 0;
		}

		mouse_spent_x = ModernCameraMouseSpend(mouse_dx, (long)tomb5.mouse_sensitivity, &mouse_owed_x);
		mouse_spent_y = ModernCameraMouseSpend(mouse_dy, (long)tomb5.mouse_sensitivity, &mouse_owed_y);
		ModernCameraMouseSteps(mouse_spent_x, mouse_spent_y, (long)tomb5.mouse_sensitivity,
			tomb5.mouse_invert ? 1 : 0, &mouse_yaw, &mouse_pitch);
		ModernCameraMouse(mouse_yaw, mouse_pitch);

		joy_fire &= ~rstick_bits;
	}

	/* MOUSELOG, behind -log: one line per call on which the mouse moved or a
	 * button changed, never on a still one. `dx`/`dy` are the counts this call
	 * took, `buttons` the mask Key() reads, `yaw`/`pitch` the steps handed to
	 * the orbit (0 under Tank, where nothing is handed over), `sens` the
	 * sensitivity they were scaled by, `captured` whether the cursor is held,
	 * `owed_x`/`owed_y` the counts left over for later ticks. A call that took
	 * nothing but still pays out owed counts logs as well, with dx=0 dy=0 and
	 * the step it turned. The capture's own line, on every change of it, comes
	 * from specific\winmain.cpp. */
	if (gLogEnabled)
	{
		static long mouse_buttons_logged = 0;

		if (mouse_dx || mouse_dy || mouse_yaw || mouse_pitch || mouse_buttons != mouse_buttons_logged)
		{
			mouse_buttons_logged = mouse_buttons;
			Log("MOUSELOG f=%ld dx=%ld dy=%ld buttons=0x%lX yaw=%ld pitch=%ld sens=%ld captured=%ld"
				" owed_x=%ld owed_y=%ld",
				(long)GlobalCounter, mouse_dx, mouse_dy, mouse_buttons, mouse_yaw, mouse_pitch,
				(long)tomb5.mouse_sensitivity, WinMouseCaptured(), mouse_owed_x, mouse_owed_y);
		}
	}

	/* In Modern the d-pad does not move Lara; it is free for bindings. This is
	 * a local copy of the stick, not a change to joy_x/joy_y: those statics are
	 * also what JoyMenuStep/JoyMenuDeflected read for the controls screen's
	 * navigation in either scheme, and the d-pad must keep steering menus.
	 *
	 * specific\dxshell.cpp does not OR the d-pad into the axes, it assigns: a
	 * press overwrites the stick reading. Erasing the d-pad's +-20 signature
	 * here could therefore only turn a partial push into a dead stop (a
	 * medipack tap while running) or veer the course. So the movement reads the
	 * stick's own axes, which dxshell.cpp keeps beside the overwritten pair.
	 * joy_x/joy_y still carry the d-pad for the menus, joy_fire still carries
	 * its four bits for the bindings. The joystick_read test is ReadJoystick's
	 * own: no pad answering means no axes, not the last poll's.
	 *
	 * Where a menu reads the direction bits as a cursor step (the inventory,
	 * the title - ModernMenuReadsPad(), the mirror of the movement seam's two
	 * menu terms), the pair stays joy_x/joy_y: stick and d-pad both, as at Tank.
	 *
	 * Otherwise the raw left stick goes through a radial zone of XInput's own
	 * 7849 of 32767, so a circling thumb does not snap onto the four cardinals
	 * and the course does not move in jumps - with the path's per-axis square
	 * beside it, so a stick resting in the square's corners cannot start Lara
	 * walking. The menus keep the per-axis pair, all a four-way cursor needs.
	 *
	 * With the keyboard chosen (ControlMethod 0) the pair stays 0/0: the
	 * movement seam below is open to the keyboard page as well, for the four
	 * movement keys, and a controller resting on the desk - or a d-pad press,
	 * which reads +-20 in joy_x/joy_y - must not steer a keyboard player. */
	long move_x = joy_x;
	long move_y = joy_y;

	if (tomb5.control_modern && !ModernMenuReadsPad())
	{
		move_x = 0;
		move_y = 0;

		/* Both widths are chosen by path. The DirectInput axes come in at a
		 * different scale with a per-axis gate of 18, so XInput's radial 31
		 * would kill every push between 18 and 31 and give a slow thumb a step;
		 * MODERN_DEAD_LEFT_DINPUT is 24, above the gate and below 31.
		 *
		 * As for the camera stick, both widths are scaled by tomb5.dead_zone,
		 * whose default 50 returns them unchanged. */
		if (ControlMethod == 1 && joystick_read)
			ModernStickDeadZone(
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_LEFT : MODERN_DEAD_LEFT_DINPUT,
					(long)tomb5.dead_zone),
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_AXIS_LEFT : MODERN_DEAD_AXIS_LEFT_DINPUT,
					(long)tomb5.dead_zone),
				joystick_raw_lx, joystick_raw_ly, &move_x, &move_y);
	}

	/* The Modern seam's condition, asked here - before the classic assembly -
	 * because the sector rule below must know whether this is one of the look
	 * key's ticks while move_x/move_y are still on their way into the four
	 * direction bits. Nothing between here and the seam writes one of its
	 * terms; it is named once so its three readers cannot drift apart.
	 *
	 * Term by term: tomb5.control_modern first, so at the default (Tank) this
	 * is one short-circuit and a local 0. ControlMethod is not a term: the four
	 * movement keys are the scheme's stick on either binding page. The pad's
	 * stick is an input only when the player chose the pad, and that is
	 * answered above, where move_x/move_y stay 0/0 on the keyboard page, so a
	 * controller resting on the desk cannot take the keyboard's keys away. First
	 * person is skipped because the camera is Lara's head there and a
	 * camera-relative heading means nothing; spotcams, cutscenes and
	 * bDisableLaraControl because Lara is not the player's to steer then (the
	 * last also covers pickups, switches and lifts); the inventory and the
	 * title because the direction bits steer a menu there. */
	const long modern_seam = tomb5.control_modern && !FirstPersonActive() &&
		!bUseSpotCam && !GLOBAL_playing_cutseq && !bDisableLaraControl && lara_item &&
		!InventoryActive && gfLevelId != LVL5_TITLE;

	/* Whether the look key is steering Lara this tick, i.e. the seam stands
	 * aside: the inverse of the bypass gate below, read by all three readers.
	 * The latch half goes through ModernLookTankActive(), so the engine has one
	 * spelling of "the look key is steering her as at Tank". */
	const long modern_tank = modern_seam && ((input & IN_LOOK) || ModernLookTankActive());

	/* Whether that look key is aiming instead. With a weapon ready the look key
	 * is not a steering modifier: the strip further below asks
	 * `lara.gun_status != LG_READY` before it takes IN_LOOK out.
	 *
	 * The bit does reach these ticks, by two roads: the IN_TARGET swallow below
	 * lets it through from the seventh tick of a hold (LookCnt 100), and
	 * ControlPhase (game\control.cpp) sets `input |= IN_LOOK` on every tick of
	 * a binocular or lasersight session. On those ticks the classic direction bits are what
	 * LookUpDown and LookLeftRight (game\lara.cpp) consume to aim
	 * the head, the binoculars and the crosshair, and a sector that lets one
	 * axis through would make a diagonal impossible.
	 *
	 * So only the sector rule is confined to the ticks the look key really
	 * steers Lara; `modern_tank` itself, the bypass gate and the log line keep
	 * their behaviour with a weapon ready. */
	const long modern_tank_aim = modern_tank && lara.gun_status == LG_READY;

	/* On the look key's steering ticks the stick is read by sector.
	 *
	 * The classic assembly below is two independent per-axis thresholds of 8,
	 * Core's arithmetic behind the hardware's per-axis zone. In Modern the pair
	 * has come through the radial zone instead, so those 8 are a window of
	 * +-3.6 degrees round the horizontal, and a thumb pushing "left" sits ten to
	 * sixteen degrees low: measured, 680 of 962 pushed tank ticks came out
	 * LEFT+BACK. ModernTankSector lets exactly one axis through, so a turn is a
	 * turn and a step is a step; the arithmetic and its constants are in
	 * game\modern_move.*, where a test can hold them. Every other Modern tick
	 * goes to ModernUpdateInput, which is handed move_x/move_y itself - or,
	 * with the stick at rest, the four movement keys as a stick. The keys have
	 * no sector: on these ticks their own classic bits stand, one per key, as
	 * at Tank.
	 *
	 * The raw pair is kept beside the sectored one because MODERNLOG's tank
	 * line prints both: `stick=` is what the thumb did, `out=` what the
	 * assembly was handed.
	 *
	 * The outer `if` clears the previous sector on every tick that is not a
	 * steering tick - a hold that ends, the inventory, a cutscene, first
	 * person, and also the aiming ticks - so a fresh push always starts from no
	 * sector and the plain boundary, never biased by an earlier hold. It tests
	 * tomb5.control_modern and not modern_seam so that at Tank the static is
	 * neither read nor written; modern_tank carries the seam's terms. */
	long tank_x = move_x;
	long tank_y = move_y;
	long tank_sector = MODERN_TANK_SECTOR_NONE;

	if (tomb5.control_modern)
	{
		if (modern_tank && !modern_tank_aim)
			tank_sector = ModernTankSector(move_x, move_y, modern_tank_prev, &tank_x, &tank_y);

		modern_tank_prev = tank_sector;
	}

	linput = 0;

	if (ControlMethod == 1)
	{
		if (tank_x < -8)
			linput = IN_LEFT;
		else if (tank_x > 8)
			linput = IN_RIGHT;

		if (tank_y > 8)
			linput |= IN_BACK;
		else if (tank_y < -8)
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

	/* The Modern control's seam, here and nowhere else. Above, the stick and
	 * the four direction keys have written their bits; below, the key roles
	 * are read. The whole Modern scheme is "take those four bits away and put
	 * four of its own in"; when it is not responsible - the stick and the four
	 * movement keys at rest, a state where a direction bit does not mean a
	 * direction - the bits above stand and the pad and the keys behave as at
	 * Tank. So the scheme needs no second input path anywhere in the tree. The
	 * guard, `modern_seam`, is declared above.
	 *
	 * While the look key is held the translation is bypassed, so the stick
	 * turns Lara finely on the spot as in Tank, for movements where she has to
	 * be placed exactly. `input` is the previous tick's word (IN_LOOK is read
	 * further down), so a hold of n ticks is bypassed for n ticks, from the
	 * tick after the key goes down to the first tick it reads up; the scheme is
	 * back on the second tick after the last held one. One tick at 30 Hz is
	 * under a visible frame; ModernTest's look_tank() counts both ends. With a
	 * weapon ready the look key is swallowed for six ticks (it becomes
	 * IN_TARGET), so there the bypass begins when the hold becomes a real look.
	 *
	 * `modern_tank` has two terms. The latch (ModernLookTankActive) answers
	 * when the block below the IN_TARGET swallow has taken the bit out of the
	 * word. The word answers during a binocular or lasersight session, where
	 * ControlPhase (game\control.cpp) holds IN_LOOK on whether the key is pressed or not;
	 * that session is LG_READY, so the block below leaves the bit alone. */
	if (modern_seam && !modern_tank)
	{
		// The two key roles the scheme has to know about before they are read
		// into the word below - aiming asks for the fire key, the directional
		// jump for the jump key. Same keys, same pad buttons, read in the same
		// order as the blocks that follow.
		/* The aiming role follows the fire key. MODE AIM is "weapon up and
		 * action held: strafe, do not turn" (game\modern_move.h), a weapon
		 * behaviour, and under "Separate" the weapons fire on the Secondary
		 * role. So the role is chosen by the same bit that chooses which role
		 * produces IN_ACTION below, and the two cannot disagree.
		 *
		 * The pad's buttons carry ControlMethod == 1 as every other JoyRole
		 * call site does: the seam is open to the keyboard page, and there a
		 * connected pad's action or jump button must not steer the translation
		 * while its own IN_ACTION and IN_JUMP stay gated. */
		const long mAction = tomb5.action_split
			? (Key(KEY_ROLE_SECOND) || (ControlMethod == 1 && JoyRole(JOY_ROLE_SECOND)))
			: (Key(8) || (ControlMethod == 1 && JoyRole(4)));
		const long mJump = Key(7) || (ControlMethod == 1 && JoyRole(3));

		// move_x/move_y, not joy_x/joy_y - in Modern that pair is the left
		// stick's own reading, which the d-pad never writes over (see above),
		// so a d-pad press reaches neither this call nor the classic assembly
		// above as a movement stick.
		/* The four movement keys are the same stick. With the pad's pair at
		 * rest, Key(0..3) - the same four reads as above - become a
		 * full-deflection vector in the pad's units and signs, the forward
		 * key on -y as the stick pushed forward reads, and the call is
		 * handed that; the pad off centre is handed on unchanged and the
		 * keys are not added (ModernKeyStick, game\modern_move.cpp). A local
		 * pair, so move_x/move_y, tank_x/tank_y and the classic assembly
		 * keep the pad's own reading. The keys' classic bits are already in
		 * linput: when the translation takes the vector it replaces them, as
		 * it replaces the pad's, and when it steps aside - a classic state, a
		 * ladder, a push block - they stand. Only this call is handed the
		 * vector, so the look key's bypass, first person, Tank and the menus
		 * never see it. */
		long seam_x, seam_y;
		const long feed = ModernKeyStick(move_x, move_y, Key(0), Key(1), Key(2), Key(3),
			&seam_x, &seam_y);

		linput = ModernUpdateInput(linput, seam_x, seam_y, mAction, mJump, feed);
	}

	/* MODERNLOG `mode=tank`: the seam above never runs on a bypass tick, so
	 * without this line a log falls silent while the look key is held and
	 * cannot tell that from the scheme standing aside for another reason. The
	 * condition is `modern_tank`, the inverse of the seam's gate, so exactly
	 * one of the two lines fires per Modern seam tick; the latch is written
	 * further below, so this is still this tick's answer.
	 *
	 * Fields: `stick` is move_x/move_y, the pad's pair, as the seam would have
	 * received them (the four movement keys have no field here: on a bypass
	 * tick their classic bits stand); `sector` is 0 none, 1 turn, 2 step;
	 * `out` is the pair the classic assembly was handed; `aim` is 1 when the
	 * look key is aiming a weapon, the binoculars or the lasersight - why a
	 * tank tick can show sector=0 with the stick still pushed. */
	if (gLogEnabled && modern_tank)
		Log("MODERNLOG f=%ld src=tank mode=tank stick=%ld/%ld sector=%ld out=%ld/%ld aim=%ld",
			(long)GlobalCounter, move_x, move_y, tank_sector, tank_x, tank_y, modern_tank_aim);

	/* The walk key survives the seam. MODERN_MOVE_MASK includes IN_WALK, so
	 * ModernUpdateInput clears whatever the stick wrote for it - but the walk
	 * key, Key(6) and the pad's jLayout[2], is read below, after the seam has
	 * run, so it only ORs its bit into what the seam decided. Holding the walk
	 * key gives IN_WALK in Modern exactly as in Tank, whatever the stick's
	 * magnitude (MODERN_WALK_BELOW, game\modern_move.h, is 0). Read before the
	 * seam, the key's bit would be wiped on every Modern tick. */
	/* The Action role's whole word, computed once. Both producers below - the
	 * keyboard's Key(8) and the pad's jLayout[4] - OR this same value, so they
	 * cannot disagree about what the Action button means. With "One key" it is
	 * IN_SELECT | IN_INTERACT | IN_ACTION, and nothing between here and any
	 * reader takes one action bit without the other: lara.Busy's mask at the
	 * end keeps neither, `inputBusy` and `dbinput` copy the whole word, the
	 * Modern seam writes only MODERN_MOVE_MASK and IN_DRAW, and the one mask in
	 * game\lara.cpp clears both at this setting (see the comment there).
	 *
	 * With "Separate" the Action role drops IN_ACTION and the Secondary role
	 * below picks it up. IN_SELECT stays with the Action role on purpose: it
	 * confirms in every menu and in the inventory ring, and the split does not
	 * move menus. */
	const long action_bits = IN_SELECT | IN_INTERACT | (tomb5.action_split ? 0 : IN_ACTION);

	/* KEYLOG action=, logged on change and never per frame. game\menu_frame.cpp's
	 * MENULOG line records the row being set; this one records what the input
	 * assembly made of it - `bits` is action_bits, `second` the keyboard and pad
	 * bindings of the Secondary role. A run without -log never reaches it. */
	static long action_bits_logged = -1;

	if (gLogEnabled && action_bits != action_bits_logged)
	{
		action_bits_logged = action_bits;
		Log("KEYLOG action=%s bits=0x%lX second=%ld/%ld", tomb5.action_split ? "separate" : "onekey",
			action_bits, (long)layout[1][KEY_ROLE_SECOND], jLayout[JOY_ROLE_SECOND]);
	}

	/* PADLOG, the pad's chord layer, logged on change and never per frame. A line
	 * of its own because the Modifier and the mask change when a binding is
	 * captured, when the Action row flips and on Reset, none of which touches
	 * action_bits. `mod` is the Modifier's bit (-1 unbound: no chord can fire
	 * and every pad role is a plain bit test), `chord` the active set's chord
	 * mask, `split` the Action setting. */
	static long modifier_logged = -2;
	static long chord_logged = -1;

	if (gLogEnabled && (jLayout[JOY_ROLE_MODIFIER] != modifier_logged || jChord != chord_logged))
	{
		modifier_logged = jLayout[JOY_ROLE_MODIFIER];
		chord_logged = jChord;
		Log("PADLOG mod=%ld chord=0x%lX split=%s", modifier_logged, chord_logged,
			tomb5.action_split ? "separate" : "onekey");
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
		linput |= action_bits;

	/* The Secondary role, not even asked at "One key": the `&&` short-circuits,
	 * so at the default Key() is not called for slot 18 and the role cannot
	 * produce a bit however it is bound. */
	if (tomb5.action_split && Key(KEY_ROLE_SECOND))
		linput |= IN_ACTION;

	if (Key(9))
		linput |= IN_DRAW;

	if (ControlMethod == 1)
	{
		if (JoyRole(3))
			linput |= IN_JUMP;

		if (JoyRole(4))
			linput |= action_bits;

		// The pad's half of the Secondary role, under the same guard and for
		// the same reason as the keyboard's above.
		if (tomb5.action_split && JoyRole(JOY_ROLE_SECOND))
			linput |= IN_ACTION;

		if (JoyRole(5))
			linput |= IN_DRAW;

		if (JoyRole(1))
			linput |= IN_SPRINT;

		if (JoyRole(2))
			linput |= IN_WALK;

		if (JoyRole(0))
			linput |= IN_DUCK;

		if (JoyRole(6))
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

	/* The menus keep the arrows and Return under Modern. Modern binds W/S/A/D to
	 * the four direction slots, and every menu steps with IN_FORWARD/IN_BACK/
	 * IN_LEFT/IN_RIGHT out of Key(0..3) - the title, the pause menu and its
	 * option pages, the inventory ring, load/save and the outfit changer.
	 * Without this a player who rebound W/S/A/D elsewhere would be locked out of
	 * the menu he rebound them in.
	 *
	 * The predicate is ModernMenuReadsPad(), the one answer to "is a menu
	 * reading the direction bits rather than Lara"; it is the inverse of the
	 * movement seam's last two terms, so in play the arrows do only what the
	 * active table says.
	 *
	 * Raw keymap and not Key(), because these five are the menus' fixed keys,
	 * not a role (the capture refuses to bind them over the navigation,
	 * game\menu_frame.cpp). Escape reaches IN_DESELECT unconditionally below. */
	if (tomb5.control_modern && ModernMenuReadsPad())
	{
		if (keymap[DIK_UP])
			linput |= IN_FORWARD;

		if (keymap[DIK_DOWN])
			linput |= IN_BACK;

		if (keymap[DIK_LEFT])
			linput |= IN_LEFT;

		if (keymap[DIK_RIGHT])
			linput |= IN_RIGHT;

		if (keymap[DIK_RETURN])
			linput |= IN_SELECT;
	}

	if (ControlMethod == 1)
	{
		if (JoyRole(7))
			linput |= IN_LOOK;

		if (JoyRole(8))
			linput |= IN_ROLL;

		if (JoyRole(9))
			linput |= IN_OPTION;

		if (JoyRole(10))
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
	// Not rebindable, for the same reason Escape isn't. The bit depends on
	// the active input path (JoyBitBack): raw DirectInput puts Circle on
	// bit 2, XInput puts B - and Circle via DS4Windows/Steam - on bit 1; a
	// fixed bit 2 would make an Xbox pad's X button the back button.
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

	/* In Modern the look key is a steering modifier, not a camera: held, it
	 * lets Lara turn on the spot as in Tank, with her real turn animations.
	 *
	 * The bypass above does half the job: it stands the translation down, so
	 * the stick's own IN_LEFT / IN_RIGHT / IN_FORWARD / IN_BACK stand as at
	 * Tank. But the engine answers the look key, and the answer eats exactly
	 * those bits:
	 *   game\lara.cpp        LaraAboveWater calls LookLeftRight() while the bit
	 *                        is held, and LookLeftRight sets
	 *                        camera.type = LOOK_CAMERA and SUBTRACTS IN_LEFT or
	 *                        IN_RIGHT from the word for the head's own yaw.
	 *   game\lara.cpp        fifteen state routines call LookUpDown(),
	 *                        which does the same to IN_FORWARD / IN_BACK.
	 *   game\laraswim.cpp, game\larasurf.cpp, game\laraclmb.cpp
	 *                        the same in the water (LaraUnderWater), on the
	 *                        surface (LaraSurface, lara_as_surftread) and on a
	 *                        ladder (lara_as_climbstnc) - lara_as_surftread
	 *                        RETURNS, so nothing else of that tick runs at all.
	 * Left alone, she turns with no animation: measured, 140 ticks of 18
	 * degrees in AS_STOP with the state never leaving 2.
	 *
	 * Taking the bit out here - the last place before `input` is written, and
	 * the only place that knows the scheme - hands those bits back to
	 * lara_as_stop, and with them AS_TURN_L / AS_TURN_R / AS_FASTTURN and their
	 * animations. In Modern the right stick is the look (the orbit), so nothing
	 * is lost.
	 *
	 * Not with a weapon ready: there the look key is the six-tick IN_TARGET
	 * swallow just above, and the seventh tick opens the binoculars and the
	 * lasersight (ControlPhase in game\control.cpp, which asks for LG_READY and the
	 * revolver, the crossbow or the HK). That case keeps the classic look.
	 *
	 * Known side effect, left unbranched: DOZY ends on IN_WALK "without look"
	 * (LaraCheat in game\laramisc.cpp), so in Modern the walk key ends the cheat even
	 * while the look key is held. It is a cheat behind tomb5.enable_cheats. */
	if (tomb5.control_modern)
	{
		modern_look_tank = (linput & IN_LOOK) ? 1 : 0;

		if (modern_seam && lara.gun_status != LG_READY)
			linput &= ~IN_LOOK;
	}

	DoWeaponHotkey();

	/* One press must not mean two things (the same class as the Circle latch
	 * further down). Under Modern the medipacks default to the d-pad, and the
	 * d-pad also steers the menus: in the inventory a press of Up would step
	 * the cursor and use a small medipack. So while a menu is reading the
	 * d-pad, a medipack bound to one of its four bits does not fire.
	 *
	 * Only those four bits, and only in Modern. The keyboard's 0 and 9 keep
	 * working in the inventory, and so does a medipack on a face button. At
	 * Tank the flag is 0, so JoyBitIsDpad is not reached. */
	const long medi_dpad = tomb5.control_modern && ModernMenuReadsPad();

	/* The keyboard half is a role (Key() of an untouched slot is keymap[DIK_0])
	 * and the pad half goes through the chord rule. The d-pad guard reads the
	 * button, not the pair: a medipack chorded onto a d-pad bit is still
	 * silenced in the inventory, because the Modifier does not stop Up from
	 * moving the cursor. */
	if (Key(KEY_ROLE_MEDI_SMALL) || (ControlMethod == 1 && !(medi_dpad && JoyBitIsDpad(jLayout[13])) && JoyRole(13)))
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
	else if (Key(KEY_ROLE_MEDI_LARGE) || (ControlMethod == 1 && !(medi_dpad && JoyBitIsDpad(jLayout[14])) && JoyRole(14)))
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
	// gameplay button - roll. The press that closes the inventory or the
	// pause menu is still held on the first frame afterwards, and Lara would
	// roll without the player asking for it. One press, two consumers: the
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

	/* The mode latches: Sprint mode, Duck mode and Grab mode turn a held key
	 * into a toggle, for keyboard and pad in both control types.
	 *
	 * They sit here and not at the producers because the walk-sidestep rewrite
	 * above turns IN_LEFT and IN_RIGHT into the two step bits, so a direction
	 * test earlier would read a sidestep as "pushing nothing" and drop the
	 * sprint. IN_DUCK and IN_SPRINT have two producers each in this file (the
	 * keyboard's Key(4)/Key(5), the pad's jLayout[0]/jLayout[1]) and IN_ACTION
	 * four (Key(8), Key(KEY_ROLE_SECOND), JoyRole(4), JoyRole(JOY_ROLE_SECOND));
	 * nothing between them and here reads or writes these bits, so `linput`
	 * carries them as the keyboard or pad wrote them - one seam, no
	 * per-producer branch. Under Separate the grab key is the Secondary, the
	 * role that produces IN_ACTION there; the either-key sites (game\lara.cpp's
	 * IN_ACTION_ANY) are served by it too.
	 *
	 * With all three rows at Hold only the test on the next line runs, and the
	 * word leaves unchanged. */
	if (tomb5.sprint_toggle || tomb5.duck_toggle || tomb5.grab_toggle)
	{
		/* The ticks no latch may stand on. `mode_base`: a cutscene and
		 * bDisableLaraControl are ticks Lara is not the player's to steer, the
		 * inventory and the pause menu (InventoryActive covers both) and the
		 * title are ticks a menu owns the word, App.fmv is a video, and a dead
		 * Lara drops every latch.
		 *
		 * The other terms are what the sprint and duck bits need:
		 * - dead: lara_as_dash and lara_as_duckl/duckr all end on hit_points
		 *   <= 0 (game\lara.cpp), so the engine drops both
		 *   anyway and a latch that outlived her would be the only thing left
		 *   holding a key.
		 * - the water: every IN_DUCK reader in game\lara.cpp excludes LW_WADE
		 *   by hand and lara_as_dash names it as its fourth exit; under water
		 *   and at the surface neither AS_DUCK nor AS_DASH is reachable at all.
		 *   So anything but LW_ABOVE_WATER is a tick the bits mean nothing on.
		 * - LG_HANDS_BUSY stands for "ladder and vehicle": a ladder, a rope,
		 *   the water and a pickup all leave gun_status there
		 *   (TestLaraVault in game\lara.cpp among others), and LARA_INFO has no
		 *   Vehicle field (the name does not occur in global\types.h).
		 * - the binoculars and the lasersight read IN_SPRINT and IN_DUCK out of
		 *   `inputBusy` as ZOOM IN and ZOOM OUT (BinocularCamera, game\camera.cpp).
		 *   The zoom steps once per tick while held, so the sight is a drop,
		 *   and a drop tick hands the raw key through untouched.
		 *
		 * `mode_drop` adds bUseSpotCam for the sprint and duck; the grab asks
		 * only `mode_base`. InitialiseSpotCam sets bDisableLaraControl to 0 and
		 * raises it again only for SP_NOLARACONTROL (game\spotcam.cpp), so an
		 * ordinary flyby leaves Lara playable with bUseSpotCam up;
		 * and ControlPhase (game\control.cpp) holds bUseSpotCam at 1 while a GF_OFFICE
		 * level's flipmap is above 10, with full control throughout. There a
		 * grab drop would make a Toggle player let go of the ledge while a Hold
		 * player hangs on. `!lara_item` stays first, guarding the hit_points
		 * dereference. */
		const long mode_base = !lara_item || App.fmv || GLOBAL_playing_cutseq ||
			bDisableLaraControl || InventoryActive ||
			gfLevelId == LVL5_TITLE || lara_item->hit_points <= 0;
		const long mode_drop = mode_base || bUseSpotCam;

		// The terms not every latch shares, read once. `mode_state` is
		// last tick's state, which is what the family tables are asked about -
		// S_UpdateInput runs before LaraControl, so it is the state she is in
		// while the word is being built.
		const long mode_water = lara.water_status != LW_ABOVE_WATER;
		const long mode_hands = lara.gun_status == LG_HANDS_BUSY;
		const long mode_sight = BinocularRange != 0;
		const short mode_state = lara_item ? lara_item->current_anim_state : (short)0;

		/* The sprint latch also drops when no direction is pushed (this word's
		 * six direction bits) or the dash bar is empty: lara_as_dash leaves
		 * AS_DASH on `!DashTimer` and LaraControl (game\laramisc.cpp) refills it the moment
		 * she is out of AS_DASH, so a latch that sat through an empty bar would
		 * put her back into a sprint on the single unit of the refill. Both
		 * belong to the sprint alone; a duck holds her standing still on
		 * purpose. */
		const long moving = (linput & (IN_FORWARD | IN_BACK | IN_LEFT | IN_RIGHT |
			IN_LSTEP | IN_RSTEP)) != 0;
		const long sprint_drop = mode_drop || mode_water || mode_hands ||
			mode_sight || !DashTimer || !moving;

		/* The duck asks LG_HANDS_BUSY together with where Lara is: entering the
		 * crawl runs ANIM_DUCK_TO_ALL4S, which sets LG_HANDS_BUSY itself
		 * (lara_as_all4s in game\lara.cpp), so the plain term would end the duck latch when
		 * she starts crawling. Inside the duck-and-crawl family LG_HANDS_BUSY
		 * means "she is crawling"; a ladder, a rope and the crawl-to-hang still
		 * end the latch. ModernStateIsDuck (game\modern_move.cpp) is that
		 * family, checked there against the engine's own list in
		 * use_current_item (game\newinv2.cpp). */
		const long duck_drop = mode_drop || mode_water || mode_sight ||
			(mode_hands && !ModernStateIsDuck(mode_state));

		/* The grab's own list is `mode_base` alone: level start (through
		 * InputModeLatchReset), death, a cutscene, a menu owning the input.
		 * bUseSpotCam is left out for the reason given above mode_base. The
		 * other terms are left out as well:
		 * - LG_HANDS_BUSY is the family. A hang, a ladder, a rope and a pole
		 *   all leave gun_status there (lara_col_reach and lara_col_upjump in
		 *   game\lara.cpp), so
		 *   the term would drop the latch on every tick it was meant to stand.
		 * - the water: she can climb a ladder out of shallow water, where
		 *   water_status is LW_WADE, and must not be dropped off it. No family
		 *   state is reachable under water at all.
		 * - the binoculars: with the sight up she is standing, so the family is
		 *   0 and the bit is the key's either way. A term would say nothing. */
		const long grab_family = ModernStateIsGrab(mode_state);
		const long grab_drop = mode_base;

		linput = ModernModeLatch(tomb5.sprint_toggle, (linput & IN_SPRINT) != 0,
			sprint_drop, &sprint_latch, IN_SPRINT, linput);
		linput = ModernModeLatch(tomb5.duck_toggle, (linput & IN_DUCK) != 0,
			duck_drop, &duck_latch, IN_DUCK, linput);
		/* The let-go mask, which is not the delivered bit.
		 *
		 * The press that lets go has to take out every bit the grab key itself
		 * raises, because thirteen of the thirty-five family states test
		 * IN_ACTION_ANY and not IN_ACTION (the pole, the rope, the zip line,
		 * the parallel bars). Under "One key" `action_bits` puts IN_ACTION and
		 * IN_INTERACT into the word together, so clearing IN_ACTION alone would
		 * leave those states reading "still held" - and on a rope or the bars
		 * there is no other exit.
		 *
		 * Under "Separate" IN_INTERACT comes from a different physical key, so
		 * the mask stays IN_ACTION alone - the distinction the crossbow line at
		 * the end of LaraAboveWater (game\lara.cpp) makes too. IN_SELECT is
		 * never in it: it confirms in menus, and no menu is open while she
		 * hangs.
		 *
		 * The delivery stays IN_ACTION alone: every state that asks ANY is
		 * satisfied by it, and an IN_INTERACT handed out without a key would
		 * open doors by itself. */
		const long grab_release = tomb5.action_split ? IN_ACTION : IN_ACTION_ANY;

		linput = ModernGrabLatch(tomb5.grab_toggle, (linput & IN_ACTION) != 0,
			grab_family, grab_drop, &grab_latch, IN_ACTION, grab_release, linput);

		/* MODELOG, behind -log, written on the change of a latch and not every
		 * tick: a latched bit stands for hundreds of ticks, and the moments that
		 * matter are the press that latched it and whatever let it go. Fields:
		 * `sprint`/`duck` the latch states, `drop` the sprint and duck drop
		 * terms, `mode` the two row options, `grab` as below. -1 so the first
		 * line of a session is always written. */
		if (gLogEnabled)
		{
			static long mode_logged = -1;
			/* `grab` is the family and not a latch bit, because that is what
			 * the grab latch delivers: the bit is in the word while she is
			 * holding on, whatever the key says. It changes when she reaches the
			 * ledge or lets go, and MODERN_GRAB_RELEASE goes with it on the same
			 * edge. */
			const long now = ((sprint_latch & MODERN_LATCH_ON) ? 1 : 0) |
				((duck_latch & MODERN_LATCH_ON) ? 2 : 0) |
				((tomb5.grab_toggle && grab_family && !(grab_latch & MODERN_GRAB_RELEASE)) ? 4 : 0);

			if (now != mode_logged)
			{
				mode_logged = now;
				// New fields go at the end, so older log readers keep working.
				Log("MODELOG f=%ld sprint=%d duck=%d drop=%d/%d mode=%d/%d grab=%d",
					(long)GlobalCounter, (int)(now & 1), (int)((now >> 1) & 1),
					(int)(sprint_drop != 0), (int)(duck_drop != 0),
					(int)tomb5.sprint_toggle, (int)tomb5.duck_toggle,
					(int)((now >> 2) & 1));
			}
		}
	}

	/* Combat mode Hold: the weapon is out while the draw key is held. This is
	 * the last thing that touches IN_DRAW before the word leaves; its only
	 * producers in this file are Key(9) and the pad's jLayout[5] above.
	 *
	 * The input word has no edge for the draw key: Key(9) is a level read of
	 * keymap[], `input` is `linput` unlatched, and nobody asks dbinput about
	 * IN_DRAW. Lara's state machine is the debounce: LaraGun
	 * (game\larafire.cpp) asks about IN_DRAW only at LG_NO_ARMS and
	 * LG_READY, and in LG_DRAW_GUNS / LG_UNDRAW_GUNS the animation runs.
	 * (A held key in Core draws and holsters without end; a tap is shorter than
	 * the animation.) So ModernWeaponBits asks for the disagreement, not the
	 * key: one request per tick on which the state and the key disagree, none
	 * while the animation runs, nothing that can queue up and fire later. The
	 * next tick reads the state the request produced.
	 *
	 * The guard excludes the states in which nobody would consume the request,
	 * where it would sit in the word while the state stands still.
	 * PlayFmv (specific\fmv.cpp) skips a video on a rising edge of
	 * IN_OPTION|IN_DRAW|IN_SELECT, so a bit on for every frame of the outro FMV
	 * (LG_READY at the level's end, key up) would make the video unskippable -
	 * hence App.fmv. The other terms mean what they mean in modern_seam: a
	 * cutscene, a spotcam and bDisableLaraControl are ticks Lara is not the
	 * player's to steer; the inventory, the pause menu and the title are ticks
	 * a menu owns the word. ControlMethod and tomb5.control_modern are not
	 * terms: combat mode applies to both control types, keyboard and pad.
	 *
	 * Busy hands need no term: a ladder, the water, a vehicle and a pickup
	 * leave gun_status at LG_HANDS_BUSY, neither LG_READY nor LG_NO_ARMS, so
	 * the function is quiet by its own rule; lara.Busy strips IN_DRAW below for
	 * the binocular and lasersight sessions.
	 *
	 * The torch is a guard term, not part of `ready`: a burning torch sits at
	 * LG_READY with gun_type WEAPON_TORCH (DoFlameTorch, game\flmtorch.cpp),
	 * LaraGun (game\larafire.cpp) returns before its chain for it, and
	 * DoFlameTorch reads the bit to throw the torch. Only making `ready` false
	 * would still clear the key's bit, and the torch could never be thrown in
	 * Hold. The term clears itself: DoFlameTorch sets gun_type back to
	 * last_gun_type or WEAPON_NONE when the torch leaves her.
	 *
	 * The flare needs no exception: ready_flare() (game\laraflar.cpp)
	 * parks a burning flare at LG_NO_ARMS, not LG_READY, so it reads as
	 * `holstered`. With the key up she keeps it; with the key down LaraGun
	 * throws the flare and draws the gun, as a draw-key press does in Core. */
	if (tomb5.combat_hold)
	{
		const long combat_seam = lara_item && !App.fmv && !GLOBAL_playing_cutseq &&
			!bUseSpotCam && !bDisableLaraControl && !InventoryActive &&
			gfLevelId != LVL5_TITLE && lara.gun_type != WEAPON_TORCH;

		// The draw key or button physically down this tick - which is what
		// this bit is at this point, from either producer.
		const long held = (linput & IN_DRAW) != 0;
		const long ready = (combat_seam && lara.gun_status == LG_READY) ? 1 : 0;
		const long holstered = (combat_seam && lara.gun_status == LG_NO_ARMS) ? 1 : 0;

		if (combat_seam)
			linput = ModernWeaponBits(1, ready, holstered, held, linput);

		/* COMBATLOG, behind -log, written on the change of the bit or of the
		 * seam and not every tick, because the bit is one tick long in play.
		 * `held` is the key, `ready`/`holstered` the two state truths, so a
		 * reader can see which rule answered; -1 so the first line of a session
		 * is always written.
		 *
		 * `draw` is the bit that leaves this function, and `seam` says whether
		 * the weapon half produced it. With the seam off - a torch in the hand,
		 * an FMV, the inventory - the key's own IN_DRAW is still in the word and
		 * still consumed (game\flmtorch.cpp throws the burning torch on it,
		 * specific\fmv.cpp skips a video on it), so it is printed as it is. */
		if (gLogEnabled)
		{
			static long combat_last_state = -1;
			const long draw = (linput & IN_DRAW) != 0;
			const long state = draw | (combat_seam ? 2 : 0);

			if (state != combat_last_state)
			{
				Log("COMBATLOG f=%ld held=%d ready=%d holstered=%d draw=%d seam=%d",
					(long)GlobalCounter, (int)held, (int)ready, (int)holstered, (int)draw,
					(int)(combat_seam != 0));
				combat_last_state = state;
			}
		}
	}

	if (debounce)
		dbinput = inputBusy;

	if (!gfGameMode && Gameflow->LoadSaveEnabled)
	{
		/* Quick save and quick load as roles; the pad halves go through the
		 * chord rule. At Tank the defaults are Core's F5 and F6; under Modern
		 * quick load is F9 (game\key_scheme.h). On the pad's Separate
		 * DirectInput pages the defaults are Modifier+R3 and Modifier+L3. */
		if (Key(KEY_ROLE_SAVE) || (ControlMethod == 1 && JoyRole(11)))
			linput |= IN_SAVE;

		if (Key(KEY_ROLE_LOAD) || (ControlMethod == 1 && JoyRole(12)))
			linput |= IN_LOAD;
	}

	/* The HD/SD switch: its key or its pad chord, read on every call so the
	 * press edge is seen wherever the key goes down (hd/hd_switch.h). It
	 * switches in a level only and under no menu - the ring, the pause menu,
	 * load and save, the outfit window and the Key bindings page all set
	 * InventoryActive - so the press that binds F12 there switches nothing.
	 * It sets no input bit: nothing of the game's own state hears it. */
	HDSwitchInput(Key(KEY_ROLE_HD_SWITCH) != 0,
		ControlMethod == 1 && JoyRole(JOY_ROLE_HD_SWITCH),
		!gfGameMode && !InventoryActive);

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

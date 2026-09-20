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
#include "../game/modern_move.h"	// the radial dead zone and its two widths (#161, round 161a2b, item 6)
#include "../game/key_scheme.h"		// the two keyboard sets, as arithmetic (#161, round 161b2a, item 1)
#include "../tomb5/tomb5.h"
#include "function_stubs.h"		// Log, gLogEnabled - MODERNLOG mode=tank (round 161a3, step 5)

const char* KeyboardButtons[272] =
{
	0,
	"Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+", "Bksp",
	"Tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "<", ">", "Return",
	"Ctrl", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "`",
	"Shift", "#", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "Shift",
	/* THE EIGHT F KEYS THE HOTKEY ROLES DEFAULT TO, AND NO MORE (#161, round
	 * 161b2d, item 2). Base named none of DIK_F1..DIK_F12 at all, so
	 * MenuValBinding would have printed a null pointer for the new rows and
	 * MenuCaptureBinding - which binds "any NAMED key" - could not have bound
	 * one. F1, F2, F5, F6, F7, F8, F9 and F11 are what the two default rows
	 * use; F3 and F4 (the outfit changer), F10 (IN_E) and F12
	 * (specific\output.cpp) stay nameless on purpose, so the capture keeps
	 * refusing them and no player can bind a role onto a key the engine
	 * already answers to elsewhere. */
	"Padx", "Alt", "Space", "Caps", "F1", "F2", 0, 0, "F5", "F6", "F7", "F8", "F9", 0,
	"Nmlk", 0,
	"Pad7", "Pad8", "Pad9", "Pad-",
	"Pad4", "Pad5", "Pad6", "Pad+",
	"Pad1", "Pad2", "Pad3",
	"Pad0", "Pad.", 0, 0, "\\", "F11",	// F11 is the heal cheat's default key (#161, round 161b2d)
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
	// The same eight F keys as in the English table above, and for the same
	// reason - a German keyboard calls them F1..F12 as well.
	"Num x", "Alt", "Leert.", "Caps", "F1", "F2", 0, 0, "F5", "F6", "F7", "F8", "F9", 0,
	"Nmlk", 0,
	"Num7", "Num8", "Num9", "Num-",
	"Num4", "Num5", "Num6", "Num+",
	"Num1", "Num2", "Num3",
	"Num0", "Num.", 0, 0, ">", "F11",
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
// Bits 20-23 (#161, round 161a2, step 3) are the d-pad's own four
// directions, the same idea the other way round - so IT can be bound like
// the right stick already was, once Modern frees it from moving her (the
// user's decision 8). Path- and style-independent for the same reason
// JOYNAME_STICK is: dxshell.cpp writes them from the D-PAD, which is a
// fixed physical control on every supported pad, not something that moves
// between HID bit positions the way an ordinary face button does.
//
// The control-method page (JoyStyle) only picks the VOCABULARY; which bit
// order applies, the engine knows itself (joystick_is_xinput). That way
// the names are right even for a DualSense behind DS4Windows: PS5 page
// plus XInput bit order.
#define JOYNAME_STICK "Stick R Up", "Stick R Down", "Stick R Left", "Stick R Right"
#define JOYNAME_DPAD "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"

static const char* JoyNamesPS5DInput[24] =
{
	"Square", "Cross", "Circle", "Triangle", "L1", "R1", "L2", "R2",
	"Share", "Options", "L3", "R3", "PS Button", "Joy 14", "Joy 15", "Joy 16",
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
	if (idx < 0 || idx > 23)	// 20-23 added (#161, round 161a2, step 3) - the d-pad's own bits
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

/* AND FOUR MORE BESIDE THEM FOR "SEPARATE" (#161, round 161b2c, item 3). The
 * four above are the user's own pad layout and his decision of 2026-09-16 keeps
 * them as the default in both control schemes; this round does not touch them.
 * The Separate setting is a layout of its OWN, read off the binding page of the
 * game he compares with, and he wrote it down as button NAMES:
 *
 *   walk Square   jump Cross    action Triangle   draw L2   look R3
 *   roll Circle   duck L1       sprint L3         flare R1  secondary R2
 *   inventory Share             pause Options     save PS button   load -
 *
 * SO THE ROWS ARE DERIVED AND NOT TYPED. Each name is looked up in the
 * JoyNames* table of its own page (above) and the bit index it sits at is what
 * goes in - which is why the two XInput-ordered rows come out identical while
 * their names differ (Xbox reads X/A/B/Y/LB/RB/LT/RT for Square/Cross/Circle/
 * Triangle/L1/R1/L2/R2, the user's own mapping), and why the generic row is the
 * DirectInput numbers. Save is the PS button where a page has one and unbound
 * where it has not: the two XInput pages stop naming buttons at 11.
 *
 * Role order is the tables' above, with the SIXTEENTH entry - the Secondary
 * action - added at the end, which is the entry those tables have no business
 * carrying (JoyDefaultRecord). The medipacks (13, 14) are what the One-key
 * tables give them for this page, so that decision 8's move to the d-pad under
 * Modern keeps happening in one place and not in five. */
static const long jSepPS5DInput[JOY_ROLES] = { 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 12, -1, 13, 14, 7 };
static const long jSepPS5XInput[JOY_ROLES] = { 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11 };
static const long jSepXbox[JOY_ROLES] = { 4, 8, 2, 0, 3, 10, 5, 9, 1, 6, 7, -1, -1, 18, 19, 11 };
static const long jSepGeneric[JOY_ROLES] = { 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 12, -1, 13, 14, 7 };

/* THE BITS A SCHEME CANNOT HONOUR (#161, round 161a2, fix round - the
 * skeptical review's second finding). Each scheme takes one group of four
 * digital bits away from the bindings and gives it another job, so a role
 * sitting on the wrong group is not a preference, it is broken:
 *
 *   Modern   bits 16..19, the right stick. It is the camera now, and
 *            S_UpdateInput masks the four bits out of joy_fire, so the role
 *            bound there can never fire again.
 *   Tank     bits 20..23, the d-pad. It still MOVES her there
 *            (specific\dxshell.cpp writes the same four directions as a +-20
 *            stick reading unconditionally), so the role bound there fires
 *            alongside the movement - one press doing two things. */
#define JOY_RSTICK_BITS		0x000F0000L
#define JOY_DPAD_BITS		0x00F00000L

/* Which set has been seated for which scheme: -1 nothing yet, then 0 Tank /
 * 1 Modern. The registry keeps ONE table (specific\registry.cpp writes
 * jLayout[0..14] and nothing else), so the table on disk belongs to whichever
 * scheme was active when it was written - and a table written by Tank, or by a
 * build older than this round, can reach a Modern player with the medipacks
 * still on the right stick. This is what notices. */
static long joy_scheme_seated = -1;

/* ONE PLACE THAT PRODUCES A 16-ROLE RECORD, for either scheme AND either Action
 * setting (`split`, round 161b2c item 3 - 0 One key, 1 Separate). The active
 * style's own table is the source in all four cases, so the roles no decision
 * ever asked to change cannot drift apart. */
static void JoyDefaultRecord(long modern, long split, long* out)
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

	// Fifteen and not JOY_ROLES: that is the length the One-key tables have,
	// and the sixteenth entry is read one statement down, where it is known to
	// exist.
	for (int i = 0; i < 15; i++)
		out[i] = source[i];

	/* THE SECONDARY ROLE HAS NO DEFAULT AT "ONE KEY" (#161, round 161b2c,
	 * item 2). The four One-key tables are the ones the user's decision of
	 * 2026-09-16 keeps as our pad layout in both schemes, and this round does
	 * not touch them - they have fifteen entries and they stay fifteen. With
	 * "One key" the Secondary role is inert and the binding page does not offer
	 * it, so unbound is not a hole; it is the truth. Under Separate the role is
	 * the whole point, and the four tables added for it (item 3) carry its
	 * button in their sixteenth entry - which is why this reads source[15] only
	 * on that branch. */
	if (split)
		out[JOY_ROLE_SECOND] = source[JOY_ROLE_SECOND];
	else
		out[JOY_ROLE_SECOND] = -1;

	/* THE MODERN PAD SET (#161, round 161a2, step 3). Duck..Load (roles 0-12)
	 * are the SAME choice as Tank's and stay sourced from it, unchanged - the
	 * user's own layout is the default in either scheme. Only the medipacks
	 * (roles 13-14) differ: the button they used to default to (the right
	 * stick, bits 18/19 on an XInput-style page) is the camera now, so they
	 * move to the d-pad's own new bits instead (specific\dxshell.cpp) - the
	 * user's decision 8. Left/Right (bits 22/23) default to nothing: freed for
	 * the player's own binds, as the decision asked, not pre-spent on a guess. */
	if (modern)
	{
		out[13] = 20;	// D-Pad Up   -> Small Medipack, specific\dxshell.cpp
		out[14] = 21;	// D-Pad Down -> Large Medipack
	}
}

void ApplyJoyDefaults()
{
	/* RESET IS THE ACTIVE SET'S OWN DEFAULTS (#161, round 161b2c, item 3), the
	 * same sentence the keyboard's Reset has carried since 161b2a: "the
	 * defaults of the setting I am playing in". The parked set is the other
	 * setting's bindings and is not touched here - a player resetting his
	 * Separate layout has not asked to lose his One-key one. */
	const long split = tomb5.action_split ? 1 : 0;

	JoyDefaultRecord(tomb5.control_modern ? 1 : 0, split, jLayout);

	/* The zero-fill of jLayout[15..31] that stood here is gone with round
	 * 161b2c (item 2), and nothing is left uninitialised by its going: the
	 * call above now fills 0..15 and the call below 16..31, which is the whole
	 * array. Slot 15 used to be zeroed because it was nothing; it is the
	 * Secondary role now and JoyDefaultRecord answers for it. */

	/* jLayout[16..31] - allocated and unused before round 161a2 - is the OTHER
	 * scheme's record: what the set would be if the player switched. It is the
	 * span A1's decision 4 named, and it is kept true wherever the scheme
	 * changes (ApplyJoyScheme) rather than only here, so Reset - the call site
	 * of this function, specific\LoadSave.cpp - restores the ACTIVE scheme's
	 * set and leaves the other one describing the truth.
	 *
	 * IT IS A RECORD AND NOT A SOURCE, which the second fix round had to say
	 * out loud (the review's fifth finding: the span is written twice and read
	 * nowhere, under a comment that promised a reader). Nothing reads it back,
	 * and ApplyJoyScheme deliberately recomputes the other record instead of
	 * taking this one: JoyDefaultRecord answers for the style the pad is
	 * ACTUALLY on, and both ActiveJoyStyle() and joystick_is_xinput can change
	 * between the two calls - the player pages JoyStyle, or the pad answers on
	 * the other path after a reconnect - so a stored copy would be the stale
	 * one exactly when it mattered. What lives here is a snapshot for the
	 * binding page and for a later round to show, never an input to the
	 * repair. */
	JoyDefaultRecord(tomb5.control_modern ? 0 : 1, split, &jLayout[16]);
	joy_scheme_seated = tomb5.control_modern ? 1 : 0;
}

/* THE ACTIVE SCHEME REPAIRS WHAT IT CANNOT HONOUR, AND TOUCHES NOTHING ELSE
 * (#161, round 161a2, fix round).
 *
 * The review found the two halves of the same hole. Switching to Modern left
 * the medipacks on the right stick, which Modern masks - so on an Xbox or
 * PS5-in-XInput page, where jDefXbox[13]/[14] are 18/19, decision 8's own
 * medipacks-on-the-d-pad simply never arrived unless the player happened to
 * find the Reset row. And pressing Reset in Modern wrote 20/21 into the table
 * and into the registry, so switching back to Tank left the d-pad eating a
 * medipack on every step forward.
 *
 * THE RULE IS TWO SENTENCES, AND THE SECOND ONE WAS BOUGHT WITH A
 * MEASUREMENT. A role bound to a bit the active scheme has taken for something
 * else goes back to that scheme's own default for that role - that is the
 * broken case, and on its own it repairs only the two XInput-style pages,
 * because jDefPS5DInput and jDefGeneric put the medipacks on ordinary face
 * buttons (13/14) that Modern masks nothing of. The table proof of this fix
 * printed exactly that: two of the four styles switched to Modern and kept
 * their medipacks off the d-pad, which is decision 8 not arriving. So a role
 * still sitting on the OTHER scheme's default for itself follows the switch as
 * well. Everything else is the player's own choice and is kept.
 *
 * The second sentence can only ever move a role the two records DISAGREE
 * about, which today is the two medipacks and nothing else - roles 0-12 are
 * one and the same choice in both schemes by construction (JoyDefaultRecord),
 * so their two defaults are equal and the test never fires on them.
 *
 * It is idempotent, so running it twice costs nothing. Freeing a bit can
 * collide with a role that already holds it; that case has an answer in the
 * tree already - the binding page unbinds the older role
 * (specific\LoadSave.cpp) rather than letting one button mean two things - and
 * this uses the same one rather than inventing a second. With ONE addition the
 * second fix round had to make (the review's sixth finding): a role WE displace
 * is not a role the player just rebound, so it gets this scheme's own default
 * for itself if that button is free, and only falls to nothing if it is not.
 * Without it, a player who had put a medipack on the d-pad by hand under Tank
 * lost it altogether on the way into Modern - the switch took the button for
 * the other medipack and left his behind with nothing.
 *
 * THE ONE COST OF THE SECOND SENTENCE, NAMED RATHER THAN HIDDEN. A binding the
 * player chose himself that happens to EQUAL the other scheme's default for
 * that same role is indistinguishable from one nobody ever repaired, so it
 * follows the switch too - and because the registry keeps the table but not
 * which scheme wrote it (specific\registry.cpp), the one-time seat in
 * S_UpdateInput repeats that on every launch. It can only ever reach the two
 * medipacks, since roles 0-12 have the same default in both records. Curing it
 * would mean persisting the seated scheme, which is a new stored setting for a
 * case the play test has not reported. */
static long JoyBitTaken(long bit, long except)
{
	for (int i = 0; i < JOY_ROLES; i++)
		if (i != except && jLayout[i] == bit)
			return 1;

	return 0;
}

void ApplyJoyScheme()
{
	long want[JOY_ROLES];
	long other[JOY_ROLES];
	const long modern = tomb5.control_modern ? 1 : 0;
	// The repair is the ACTIVE set's, whichever Action setting seated it (#161,
	// round 161b2c, item 3): jLayout is that set by construction, and the
	// defaults it is measured against must be the same setting's, or a Separate
	// player's action button would look like "the other scheme's default" and
	// follow a switch it has nothing to do with.
	const long split = tomb5.action_split ? 1 : 0;
	const long blocked = modern ? JOY_RSTICK_BITS : JOY_DPAD_BITS;

	JoyDefaultRecord(modern, split, want);
	JoyDefaultRecord(modern ? 0 : 1, split, other);

	for (int i = 0; i < JOY_ROLES; i++)
	{
		// 23 is the highest bit either joystick path writes (specific\dxshell.cpp);
		// the bound is here because the value comes out of the registry.
		const long broken = jLayout[i] >= 0 && jLayout[i] <= 23 &&
			((1L << jLayout[i]) & blocked) != 0;
		const long theirs = other[i] != want[i] && jLayout[i] == other[i];

		if (!broken && !theirs)
			continue;

		jLayout[i] = want[i];

		if (want[i] >= 0)
		{
			for (int k = 0; k < JOY_ROLES; k++)
				if (k != i && jLayout[k] == want[i])
					jLayout[k] = (want[k] >= 0 && !JoyBitTaken(want[k], k)) ? want[k] : -1;
		}
	}

	for (int i = 0; i < JOY_ROLES; i++)
		jLayout[JOY_ROLES + i] = other[i];

	joy_scheme_seated = modern;
}

/* SEPARATE'S OWN PAD SET (#161, round 161b2c, item 3).
 *
 * "Unsere Tastenbelegung bleibt der Standard" was about the ONE-KEY pad, and
 * Separate needs a sixteenth button and moves half the others, so the two
 * cannot share one table. They do not have to: the pad now works the way the
 * keyboard has since 161b2a - the ACTIVE setting's set sits in jLayout, where
 * the binding page, Key() and every producer already read it, and the other
 * setting's set waits here. Flipping the Action row exchanges the two
 * (PadSetSeat, game\pad_scheme.h), so a player who tries Separate and comes
 * back finds his own pad exactly as he left it.
 *
 * THE STARTING VALUE IS THE PS5 DIRECTINPUT SEPARATE ROW, for the same reason
 * jLayout's initialiser is the One-key one: LoadSettings overwrites both before
 * the first frame (specific\registry.cpp), and what stands here is only what a
 * registry that cannot be opened at all falls back to.
 *
 * WHY THIS IS A SECOND ARRAY AND NOT jLayout[16..31]. That span is the other
 * CONTROL SCHEME's record - a snapshot for the binding page, written by
 * ApplyJoyScheme and read back by nothing. This is a user set: it is persisted,
 * it is edited by the player, and it must survive. Two different things with
 * two different lifetimes do not share sixteen longs. */
static long jSplitParked[JOY_ROLES] = { 4, 10, 0, 1, 3, 6, 5, 11, 2, 8, 9, 12, -1, 13, 14, 7 };

/* Which Action setting's set is in jLayout: 0 One key, 1 Separate. It starts at
 * One key and not at -1 (joy_scheme_seated does start there) because the two
 * sets have NAMES of their own in the registry - J* and SJ* - so there is never
 * a table whose setting has to be guessed; what is needed here is only the
 * answer to "has the swap been done yet", and at startup it has not. */
static long joy_split_seated = 0;

/* The stored set of ONE Action setting, wherever it happens to be sitting. The
 * registry addresses the two by setting and not by array, because JDck..JSec
 * must keep meaning the One-key set whichever setting is active when the game
 * shuts down - the accepted 161b2a build reads those names and knows nothing of
 * SJ*. */
long* JoySplitSet(long split)
{
	return (joy_split_seated == (split ? 1 : 0)) ? jLayout : jSplitParked;
}

/* What a registry without SJ* means: this page's and this path's Separate
 * defaults. Computed rather than written out a second time in
 * specific\registry.cpp, so the four tables above stay the only place the
 * layout is stated. */
void JoySplitDefaults(long* out)
{
	JoyDefaultRecord(tomb5.control_modern ? 1 : 0, 1, out);
}

/* THE PAD'S HALF OF THE ACTION ROW, beside ApplyKeyScheme's and
 * ApplyJoyScheme's. Called from the row itself (game\menu_frame.cpp) and once
 * at startup, where it is the first moment tomb5.action_split is known
 * (specific\gamemain.cpp).
 *
 * AND THE REPAIR FOLLOWS THE SET THAT JUST SAT DOWN, but only once there is a
 * seated scheme to repair against. ApplyJoyScheme judges a binding by
 * ActiveJoyStyle() and joystick_is_xinput, and both are answered by the POLL,
 * not by the init - which is why the Modern seat waits for `joystick_read` in
 * S_UpdateInput. Calling it here before that has happened would set
 * joy_scheme_seated and so cancel the seat that was still waiting to do the job
 * properly. joy_scheme_seated >= 0 is exactly "a seat has happened", and when
 * it has not, the pending one repairs whichever set is active by then - which
 * is the one this function just seated. */
void ApplyJoySplit()
{
	const long split = tomb5.action_split ? 1 : 0;

	if (joy_split_seated == split)
		return;

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

/* THE D-PAD KEEPS THE INVENTORY AND THE TITLE (#161, round 161a2b, item 9):
 * "Das D-Pad im Inventar und im Titel waere auch cool zu behalten, das ist
 * viel besser als der Stick. Wir koennen ja beides zulassen sprich Stick &
 * Pad."
 *
 * IS A MENU READING THE DIRECTION BITS? That is the honest question, and the
 * honest answer is the two terms the movement seam in S_UpdateInput already
 * carries for the same reason with the sign the other way round - this is its
 * mirror image, and the two must never disagree. InventoryActive is not only
 * the ring: the pause menu sets it (specific\LoadSave.cpp), the load and save
 * screens set it, and so does the outfit window (specific\hd). The title has
 * no Lara to steer at all, so it is named by its level id instead. Everything
 * else in the seam's guard - first person, a spotcam, a cutscene,
 * bDisableLaraControl, the look key - is a state of the GAME and not a menu,
 * and none of them reads a direction bit as a cursor step.
 *
 * WHAT IT IS FOR. In Modern the movement pair is the left stick's own axes,
 * which the d-pad never writes over (round 161a2 step 3, the user's decision
 * 8), and that took the d-pad away from the menus as well - they read the
 * same bits. While this is true the pair goes back to being joy_x/joy_y, the
 * one the d-pad ASSIGNS into: stick and pad both, exactly as at Tank. */
static long ModernMenuReadsPad()
{
	return (InventoryActive || gfLevelId == LVL5_TITLE) ? 1 : 0;
}

// Is this binding one of the d-pad's own four bits (20..23,
// specific\dxshell.cpp)? -1, the unbound row, and every other binding answer
// no - which is also what keeps the shift below off a negative count. The
// bound is ApplyJoyScheme's own, and for the same reason: the value comes out
// of the registry.
static long JoyBitIsDpad(long bit)
{
	return (bit >= 0 && bit <= 23 && ((1L << bit) & JOY_DPAD_BITS) != 0) ? 1 : 0;
}

/* SIXTEEN ROLES SINCE ROUND 161b2c (#161, item 2), AND THE RECORD MOVED UP ONE
 * TO MAKE ROOM. Slot 15 is the Secondary action; it held a hard 0 before -
 * written by nothing, read by nothing, but 0 is a real button bit (Square on
 * the PS5 DirectInput page), so it starts UNBOUND at -1 instead, which is the
 * value Key() has a sentinel for. Its own default arrived one step later, with
 * the Separate pad set (item 3): R2 there, nothing here, because with "One key"
 * the role is inert and the binding page does not offer it. jLayout[0..15] is
 * whichever set the Action row has seated (JoySplitSet) and jLayout[16..31] is
 * the other CONTROL SCHEME's record - the latter was [16..30]
 * while there were fifteen roles, and the array's 32 entries are exactly two
 * sixteen-role sets, with nothing left over. */
long jLayout[32] = { 5, 3, 4, 0, 1, 2, 9, 6, 7, 8, 10, 11, 12, 13, 14, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* THE MODERN ROW'S SCAN CODES ARE THE HEADER'S (game\key_scheme.h), so that
 * MenuTest checks the row the ENGINE really uses and not a copy of it. These
 * thirty-four lines are the other half of that bargain: the header can hold no
 * dinput.h, so they say at compile time WHICH key each of its numbers is. A
 * typo in either place stops the build instead of reaching a player. */
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
static_assert(kModernRow[9] == DIK_Q, "modern row: draw is Q");
static_assert(kModernRow[10] == DIK_F, "modern row: flare is F");
static_assert(kModernRow[11] == DIK_R, "modern row: look is R");
static_assert(kModernRow[12] == DIK_LCONTROL, "modern row: roll is left ctrl");
static_assert(kModernRow[13] == DIK_ESCAPE, "modern row: inventory is escape");
static_assert(kModernRow[14] == DIK_Z, "modern row: step left is Z");
static_assert(kModernRow[15] == DIK_X, "modern row: step right is X");
static_assert(kModernRow[16] == DIK_P, "modern row: pause is P");
static_assert(kModernRow[17] == DIK_RETURN, "modern row: select is return");
static_assert(kModernRow[18] == DIK_NUMPAD0, "modern row: secondary action is numpad 0");
/* AND THE FIFTEEN HOTKEY SLOTS (#161, round 161b2d, item 2). Each one is the
 * key specific\input.cpp read directly before this round, so this block is
 * also the byte-identity claim written as a compile-time test: the reads below
 * became Key(role), and Key(role)'s first test is keymap[layout[1][role]],
 * which for an untouched registry is keymap of exactly the key named here. */
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

/* FOUR ROWS SINCE ROUND 161b2a, AND THE FIRST TWO ARE WHAT THEY ALWAYS WERE
 * (#161, item 1). Every reader in the tree reads row 1 and every writer writes
 * it, which is why the second keyboard set is seated INTO it rather than read
 * from somewhere new - specific\registry.cpp, specific\LoadSave.cpp and
 * game\menu_frame.cpp needed no second path at all.
 *
 *   0  the Tank default row - Core's own, byte for byte as at base
 *   1  the ACTIVE set: whichever scheme is seated (KeySchemeSet, ApplyKeyScheme)
 *   2  the Modern default row (game\key_scheme.h)
 *   3  the PARKED set: the other scheme's, kept while it is not in use
 *
 * Row 3 starts as the Modern defaults because row 1 starts as the Tank set and
 * Tank is the seated scheme at startup (key_scheme_seated below).
 *
 * NINETEEN COLUMNS SINCE ROUND 161b2c (item 2), and slot 18 is the Secondary
 * action. Tank's is DIK_HOME: Core's own cluster uses End, Delete, Page Down
 * and the arrows but never Home, so the row keeps its two properties - every
 * slot bound, no key twice (KeySchemeRowSound, game\key_scheme.h) - without
 * taking a key off any of the eighteen roles a Tank player already has.
 *
 * THIRTY-FOUR SINCE ROUND 161b2d (item 2), and the fifteen new columns are not
 * a new layout: each is the scan code this file used to read directly a few
 * hundred lines below - the medipacks' 0 and 9, save's F5, load's F6, the six
 * weapon keys 1..6 of DoWeaponHotkey, and the five free F keys the cheat roles
 * take. Tank's row is therefore Core's own behaviour written down as
 * bindings, and the Modern row differs from it in ONE slot, quick load (F9,
 * the user's own decision - game\key_scheme.h). */
short layout[4][KEY_SCHEME_ROLES] =
{
	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN, DIK_HOME,
	DIK_0, DIK_9, DIK_F5, DIK_F6, DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_F2, DIK_F7, DIK_F8, DIK_F11, DIK_F1 },

	{ DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_PERIOD, DIK_SLASH, DIK_RSHIFT, DIK_RALT, DIK_RCONTROL,
	DIK_SPACE, DIK_COMMA, DIK_NUMPAD0, DIK_END, DIK_ESCAPE, DIK_DELETE, DIK_PGDN, DIK_P, DIK_RETURN, DIK_HOME,
	DIK_0, DIK_9, DIK_F5, DIK_F6, DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_F2, DIK_F7, DIK_F8, DIK_F11, DIK_F1 },

	KEY_SCHEME_MODERN_ROW,

	KEY_SCHEME_MODERN_ROW
};

/* WHICH SCHEME'S SET IS IN ROW 1: 0 Tank, 1 Modern. It starts at Tank and not
 * at -1 (the pad's joy_scheme_seated does start there) because the two sets
 * have NAMES of their own in the registry - Key0..Key17 and MKey0..MKey17 -
 * so there is never a table whose scheme has to be guessed. What the keyboard
 * needs is only the answer to "has the swap been done yet", and at startup it
 * has not. */
static long key_scheme_seated = 0;

/* The default row the ACTIVE scheme falls back to - Key()'s second chance and
 * CheckKeyConflicts()'s subject. Under Modern that must be the Modern row: its
 * movement keys are W/S/A/D, and Tank's arrow keys falling through behind them
 * would move her relative to a camera she is no longer facing along (a later
 * round gives the arrows the camera instead). At Tank this returns layout[0],
 * which is the expression that stood here before the round. */
const short* KeyDefaultRow()
{
	return layout[tomb5.control_modern ? 2 : 0];
}

/* The stored set of ONE scheme, wherever it happens to be sitting. The
 * registry addresses the two sets by scheme and not by row, because Key0..17
 * must keep meaning the classic set whichever scheme is active when the game
 * shuts down (the accepted 161b1 build reads those names and ignores MKey*). */
short* KeySchemeSet(long modern)
{
	return (key_scheme_seated == (modern ? 1 : 0)) ? layout[1] : layout[3];
}

/* THE KEYBOARD'S HALF OF THE SCHEME SWITCH, beside ApplyJoyScheme's. The pad
 * REPAIRS one stored set because it has only one; the keyboard EXCHANGES two,
 * because each scheme keeps its own - so a player who rebound Modern's W/S/A/D
 * and then went back to Tank finds his Tank keys, and his Modern keys are
 * still there when he returns. Nothing is lost and nothing is overwritten.
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
long joystick_raw_rx;		// the right stick as two axes, before its per-axis zone (#161, rounds 161a2 and 161a2b) - the modern camera
long joystick_raw_ry;
long joystick_raw_lx;		// the left stick alone and raw (#161, same two rounds) - the modern movement, see S_UpdateInput
long joystick_raw_ly;
long joystick_read_fire;
long joystick_is_xinput;	// 1 = XInput is delivering (Xbox pad, or DS4Windows/Steam), 0 = DirectInput fallback; set anew on every poll (dxshell.cpp)

static long joy_fire;
static long joy_x, joy_y;

/* THE LOOK KEY, AS THE MODERN SCHEME REMEMBERS IT (#161, round 161a3, item 3).
 *
 * The bypass used to read `input & IN_LOOK`, the word of the previous tick, and
 * that worked only as long as the bit was still IN that word. Item 3 takes it
 * out of the word - the engine must not answer the look key with the look
 * camera while the stick is steering her as in Tank - so the gate needs its own
 * memory, or it would read back the bit it had just cleared, let the seam run
 * again, set the bit again and flicker between the two schemes at 30 Hz.
 *
 * So this is what `input & IN_LOOK` WOULD have said: the look bit of the last
 * word this function built, taken before that word is stripped, with the
 * six-tick IN_TARGET swallow already applied to it. Written only under
 * tomb5.control_modern; at Tank nothing reads it and nothing writes it. */
static long modern_look_tank;

/* AND WHICH OF THE TWO SECTORS THE THUMB WAS IN LAST TICK (#161, round 161a3b,
 * item 1). The hysteresis of MODERN_TANK_SECTOR_HYST needs one tick of memory
 * and nothing more, and it belongs here rather than in game\modern_move.cpp
 * because this is the file that knows when a hold begins and ends: the block
 * that calls ModernTankSector clears it on every tick that is not a bypass
 * tick, so a fresh hold always starts from MODERN_TANK_SECTOR_NONE and the
 * plain boundary. Written only under tomb5.control_modern; at Tank nothing
 * reads it and nothing writes it. */
static long modern_tank_prev;

/* SPRINT MODE AND DUCK MODE, THEIR WHOLE MEMORY (#161, round 161b2c, item 4).
 * Two longs of MODERN_LATCH_* bits, one per mode, exactly as
 * game\modern_move.h divides the work: the arithmetic is pure and lives there,
 * the state is the engine's and lives here. Written only from the mode seam in
 * S_UpdateInput, which is entered only while one of the two rows says Toggle,
 * and from InputModeLatchReset below. At the defaults nothing reads or writes
 * them, and they stay at the 0 the loader gives every static. */
static long sprint_latch;
static long duck_latch;

/* "THIS LATCH IS OFF NOW", said from outside a tick. Three callers, each with
 * its own reason:
 *
 * - game\gameflow.cpp, beside the level's priming tick. That one line is a
 *   level start AND every savegame restore, because sgRestoreGame has exactly
 *   one caller and it is sixty lines above it in the same function - the same
 *   argument ModernResetControls' call there already rests on. NOT under
 *   tomb5.control_modern, unlike that call: these two modes are read in both
 *   control types.
 * - game\menu_frame.cpp, from each of the two rows' own step function. Without
 *   it a latch could outlive its option: the seam is not entered at all while
 *   both rows say Hold, so a player who latched the duck and then switched the
 *   row off would find the latch still standing when he switched it back on.
 *
 * The KEY memory is deliberately left alone: a key still held across a level
 * boundary or out of a menu is not a fresh press and must not latch
 * (game\modern_move.cpp says the same thing at greater length). */
void InputModeLatchReset()
{
	ModernModeLatchDrop(&sprint_latch);
	ModernModeLatchDrop(&duck_latch);
}

/* AND THE ONE QUESTION THE REST OF THE ENGINE MAY ASK ABOUT IT. The bypass
 * runs INSTEAD of ModernUpdateInput, so MODERNLOG's own line - which is
 * printed from inside that function - says nothing at all on these ticks, and
 * a log read afterwards cannot tell "she was steered as in Tank" from "the
 * seam never ran". This is what step 5 prints as `mode=tank`. */
long ModernLookTankActive()
{
	return modern_look_tank;
}

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
	
	/* THE SIX KEYS ARE SIX ROLES NOW (#161, round 161b2d, item 2), and nothing
	 * else in this function moved: the chain is the same else-if chain, the
	 * order is the same, every branch is Core's code as before. Key(role) with
	 * an untouched registry IS keymap[DIK_1..DIK_6] - its first test reads
	 * layout[1][role], which the loader filled from the row's default, and that
	 * default is the very key that stood here (the static_asserts above say so
	 * for the Modern row, the layout table for Tank's). */
	if (Key(KEY_ROLE_WEAPON_FIRST + 0))
	{
		if (!(lara.pistols_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_PISTOLS;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_PISTOLS)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (Key(KEY_ROLE_WEAPON_FIRST + 1))
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
	else if (Key(KEY_ROLE_WEAPON_FIRST + 2))
	{
		if (!(lara.uzis_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_UZI;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_UZI)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (Key(KEY_ROLE_WEAPON_FIRST + 3))
	{
		if (!(lara.sixshooter_type_carried & W_PRESENT))
			return;

		lara.request_gun_type = WEAPON_REVOLVER;

		if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_REVOLVER)
			lara.gun_status = LG_DRAW_GUNS;
	}
	else if (Key(KEY_ROLE_WEAPON_FIRST + 4))
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
	else if (Key(KEY_ROLE_WEAPON_FIRST + 5))
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
		// only go 0-18 since round 161b2c's Secondary role) or a real encoded
		// joystick value (real button indices are
		// 0-23 since the d-pad's own bits joined the right stick's, #161 round
		// 161a2 step 3, i.e. 256-279), so it's safe to use as this sentinel.
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

	// The ACTIVE scheme's default row, which at Tank is layout[0] - the
	// expression this line held before round 161b2a (see KeyDefaultRow).
	key = KeyDefaultRow()[number];

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

	/* THE RIGHT STICK IS THE CAMERA IN MODERN, AND IT IS NOT FOUR BUTTONS AS
	 * WELL (#161, round 161a2, step 2).
	 *
	 * HOW THE SECOND STICK REACHES US TODAY, measured before this was
	 * written: specific\dxshell.cpp turns its four directions into four extra
	 * digital buttons, bits 16..19 of joy_fire past the sixteen physical
	 * ones - and the default XInput pages BIND them. jDefPS5XInput[13] and
	 * [14] are 18 and 19, and so are jDefXbox's: on an Xbox or a DualSense in
	 * Xbox mode, pushing the right stick left or right EATS A MEDIPACK. That
	 * is fine while the stick is a spare four-way input and impossible the
	 * moment it becomes the camera, so the four bits are taken away here, in
	 * Modern only. Round 161a2 step 3 is what gives them a new home (the
	 * user's decision 8 moves the medipacks to the D-pad); this is only the
	 * half that must not wait for it.
	 *
	 * AND THE AXES GO TO THE ORBIT instead - stored, not acted on, because
	 * this function is also the menus', the inventory's and the FMV player's
	 * and the orbit may only move once per logic tick (game\camera.cpp).
	 * ControlMethod == 1 for the same reason the movement seam below tests it:
	 * a controller resting on the desk must not steer a keyboard player's
	 * camera. Zeroed rather than skipped in that case, so that a stick that
	 * was out when the player switched to the keyboard cannot leave the orbit
	 * turning for ever.
	 *
	 * The binding SCREEN still sees the whole word: specific\LoadSave.cpp
	 * calls ReadJoystick itself, so the four directions can still be bound and
	 * displayed there. */
	if (tomb5.control_modern)
	{
		/* AND THE TABLE HAS TO MATCH THE SCHEME BEFORE THE MASK BELOW CAN BE
		 * HONEST (#161, round 161a2, fix round). The registry keeps one set of
		 * bindings, so the table this session starts with may have been written
		 * by Tank or by a build older than this round - with the medipacks
		 * still on the two bits the next line takes away. The scheme switch
		 * itself repairs the table where the player asks for it
		 * (specific\LoadSave.cpp); this is the same repair for the session that
		 * starts in Modern without being asked, and it is inside this block, so
		 * Tank never reaches it.
		 *
		 * Once, and only with a pad really answering: joystick_is_xinput - and
		 * with it which of the four default tables is the right one - is set by
		 * the poll and not by the init, and the DirectInput half of that init
		 * runs in a thread of its own (specific\dxshell.cpp). */
		if (joy_scheme_seated != 1 && ControlMethod == 1 && joystick_read)
			ApplyJoyScheme();

		const long rstick_bits = JOY_RSTICK_BITS;	// bits 16..19, specific\dxshell.cpp
		long cam_x = 0;
		long cam_y = 0;

		/* AND THROUGH ITS OWN RADIAL ZONE (#161, round 161a2b, item 6). The
		 * RAW pair, because the per-axis zeroing in specific\dxshell.cpp
		 * judges each component on its own and so snaps a nearly-axial push
		 * onto the axis - which is the "Deadzone-Problem" the user suspected
		 * and the play test's direction histogram measured. The width is
		 * XInput's own 8689 of 32767, except on the DirectInput path, where
		 * this controller's Z/Rz rest off centre and only the wider zone
		 * keeps a resting stick from turning the camera by itself
		 * (game\modern_move.h carries both numbers and why).
		 *
		 * AND BESIDE IT THE SQUARE THAT ZONE HELD (the second argument, this
		 * round's fix round). A disc reaches less far than the square of the
		 * same half-width does in its corners, so a pad resting in one of
		 * those corners was silent before the radial zone and would have
		 * started turning the camera by itself after it. Both are asked; the
		 * stick is dead in either.
		 *
		 * ControlMethod == 1 as before, and ZEROED rather than skipped in the
		 * other case, so that a stick which was out when the player switched
		 * to the keyboard cannot leave the orbit turning for ever.
		 *
		 * AND BOTH WIDTHS ARE THE PLAYER'S SINCE ROUND 161b1 (item 5): the two
		 * constants go through ModernDeadZoneUser with tomb5.dead_zone, whose
		 * default 50 hands each of them back unchanged (game\modern_move.cpp).
		 * Asked here at the use and not once at startup, so that moving the
		 * slider is felt on the next tick and not on the next launch. */
		if (ControlMethod == 1)
			ModernStickDeadZone(
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_RIGHT : MODERN_DEAD_RIGHT_DINPUT,
					(long)tomb5.dead_zone),
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_AXIS_RIGHT : MODERN_DEAD_AXIS_RIGHT_DINPUT,
					(long)tomb5.dead_zone),
				joystick_raw_rx, joystick_raw_ry, &cam_x, &cam_y);

		ModernCameraStick(cam_x, cam_y);
		joy_fire &= ~rstick_bits;
	}

	/* THE D-PAD NO LONGER MOVES HER IN MODERN (the user's decision 8: "das
	 * D-Pad wird [...] fuer die Binds frei", round 161a2 step 3) - a LOCAL
	 * shadow of the stick, not a change to joy_x/joy_y themselves. Those
	 * two statics are also what JoyMenuStep/JoyMenuDeflected read (above)
	 * for the controls SCREEN's own menu navigation, in either scheme, and
	 * the d-pad has to keep steering a menu even once it has stopped
	 * steering her - zeroing the shared statics here would take that away
	 * too, on whichever tick the menu's own loop happens to call this
	 * function. So the shadow stays; only its SOURCE changed.
	 *
	 * IT USED TO ERASE THE D-PAD'S +-20 SIGNATURE HERE AND THAT WAS WRONG
	 * (the fix round's own first finding). specific\dxshell.cpp does not OR
	 * the d-pad into the axes, it ASSIGNS: the press overwrites the stick.
	 * By the time the pair arrives here the stick's own reading is already
	 * gone, so erasing the signature could only ever turn "she walks at a
	 * quarter push" into "she stops dead" - and once step 3 put the
	 * medipacks on the d-pad, tapping one while running did exactly that,
	 * or veered her course when the other axis was out and the pair did not
	 * match the signature at all.
	 *
	 * So the movement reads the stick's OWN axes, which that file now keeps
	 * beside the overwritten pair and no d-pad press ever touches. joy_x /
	 * joy_y still carry the d-pad for the menus, joy_fire still carries its
	 * four bits for the bindings, and a medipack on the d-pad costs her
	 * nothing. The joystick_read test is ReadJoystick's own: no pad
	 * answering means no axes, not last poll's.
	 *
	 * EXCEPT WHERE A MENU IS READING THE BITS (#161, round 161a2b, item 9):
	 * "Das D-Pad im Inventar und im Titel waere auch cool zu behalten [...]
	 * Wir koennen ja beides zulassen sprich Stick & Pad." A menu reads the
	 * very same four direction bits as a cursor step, so the shadow above
	 * took the d-pad out of the menus along with the movement - which is not
	 * what decision 8 asked for and is not what he wants. There the pair goes
	 * back to being joy_x/joy_y, the one the d-pad assigns into: stick AND
	 * pad, exactly as at Tank. ModernMenuReadsPad() is the predicate, and it
	 * is the mirror image of the movement seam's own two menu terms below.
	 *
	 * AND THROUGH THE RADIAL ZONE WHEN IT IS NOT (item 6): the raw left stick
	 * at XInput's own 7849 of 32767, so that a circling thumb stops snapping
	 * onto the four cardinals and the course stops moving in jumps - with
	 * the path's own per-axis SQUARE beside it (this round's fix round), so
	 * that a stick resting in that square's corners cannot start walking her
	 * where the hardware zone used to hold it still. The menus keep the
	 * per-axis pair, which is all a four-way cursor needs and is what they
	 * have always had. */
	long move_x = joy_x;
	long move_y = joy_y;

	if (tomb5.control_modern && ControlMethod == 1 && !ModernMenuReadsPad())
	{
		move_x = 0;
		move_y = 0;

		/* AND THE LEFT ZONE IS THE PATH'S OWN TOO SINCE ROUND 161a3 (item 7).
		 * Both arguments now pick by path, where the first one used to be
		 * XInput's 31 on either: the DirectInput axes come in at a different
		 * scale and their per-axis gate is 18, so a radial 31 above it killed
		 * every real push between the two - M9's own finding, and the reason a
		 * slow thumb felt like it had a step in it. MODERN_DEAD_LEFT_DINPUT is
		 * 24, which is above the gate and below the old radius.
		 *
		 * AND THROUGH THE PLAYER'S OWN PER CENT SINCE ROUND 161b1 (item 5),
		 * exactly as the camera stick above: tomb5.dead_zone at its default 50
		 * hands both constants back unchanged, so this line at Modern's own
		 * defaults is the line round 161a3 shipped. */
		if (joystick_read)
			ModernStickDeadZone(
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_LEFT : MODERN_DEAD_LEFT_DINPUT,
					(long)tomb5.dead_zone),
				ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_AXIS_LEFT : MODERN_DEAD_AXIS_LEFT_DINPUT,
					(long)tomb5.dead_zone),
				joystick_raw_lx, joystick_raw_ly, &move_x, &move_y);
	}

	/* THE SEAM'S CONDITION, ASKED HERE SINCE ROUND 161a3b (#161, item 1). It
	 * used to be declared with the `if` that reads it, a hundred lines below -
	 * and item 1 has to know whether this is one of the look key's own ticks
	 * while move_x/move_y are still on their way INTO the four direction bits.
	 * The one thing that must not happen is a second spelling of the eight
	 * terms, so the declaration moved instead of being copied. It is the same
	 * answer taken earlier: nothing between here and that `if` writes one of
	 * the terms - what lies between is the classic thresholds and the four
	 * direction keys, which read and write `linput` and nothing else.
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
	 * THE CONDITION IS A NAME because item 3 asked it twice and item 1 asks it
	 * a third time. Three copies of eight terms would be three chances to
	 * drift apart. tomb5.control_modern stays the first term, so at Tank this
	 * is one short-circuit and a local 0. */
	const long modern_seam = tomb5.control_modern && ControlMethod == 1 && !FirstPersonActive() &&
		!bUseSpotCam && !GLOBAL_playing_cutseq && !bDisableLaraControl && lara_item &&
		!InventoryActive && gfLevelId != LVL5_TITLE;

	/* ... AND WHETHER THIS IS A TICK THE LOOK KEY IS STEERING HER ON, which is
	 * the seam standing aside rather than running: exactly the inverse of the
	 * bypass gate below, written once here and read by all three of its
	 * readers. The latch half goes through ModernLookTankActive() - the
	 * accessor item 3 declared for this one question - so the engine keeps ONE
	 * spelling of "the look key is steering her as at Tank". */
	const long modern_tank = modern_seam && ((input & IN_LOOK) || ModernLookTankActive());

	/* ... AND WHETHER THAT LOOK KEY IS AIMING SOMETHING INSTEAD (#161, round
	 * 161a3b review, the one high finding). With a weapon ready the look key
	 * is NOT a steering modifier and never was: the strip two hundred lines
	 * below asks `lara.gun_status != LG_READY` before it takes IN_LOOK out,
	 * and its own comment says so in writing - "That case keeps the classic
	 * look, exactly as it is today."
	 *
	 * MEASURED, NOT ASSUMED. The bit really does survive into these ticks, by
	 * two roads. The IN_TARGET swallow below (`if (lara.gun_status ==
	 * LG_READY)`) lets it through from the seventh tick of a hold, LookCnt
	 * 100; and game\control.cpp:527 puts it back with `input |= IN_LOOK` on
	 * EVERY tick of a binocular or lasersight session. On exactly those ticks
	 * the classic direction bits are what LookUpDown (game\lara.cpp:887) and
	 * LookLeftRight (:920) consume to aim the head, the binoculars and the
	 * crosshair - and a sector that lets one axis through would make a
	 * diagonal impossible: left or right, up or down, never both.
	 *
	 * SO THE SECTOR IS CONFINED TO THE TICKS THE LOOK KEY REALLY IS STEERING
	 * HER. Only the sector - `modern_tank` itself is untouched, because the
	 * bypass gate and the log line below are round 161a3's behaviour with a
	 * weapon ready and this round does not get to change it. */
	const long modern_tank_aim = modern_tank && lara.gun_status == LG_READY;

	/* AND THERE THE STICK IS READ BY SECTOR (#161, round 161a3b, item 1).
	 *
	 * The classic assembly below is two independent per-axis thresholds of 8,
	 * which is Core's own arithmetic behind the hardware's per-axis zone. In
	 * Modern the pair has come through the RADIAL zone instead, so those 8 are
	 * a window of +-3.6 degrees round the horizontal - and M1 counted 680 of
	 * 962 pushed tank ticks coming out LEFT+BACK because a thumb pushing
	 * "left" sits ten to sixteen degrees low. ModernTankSector lets exactly one
	 * axis through, so a turn is a turn and a step is a step; the arithmetic
	 * and both constants are in game\modern_move.*, where a test can hold them.
	 *
	 * ON THE BYPASS TICKS AND NOWHERE ELSE. Every other Modern tick goes to
	 * ModernUpdateInput, which reads move_x/move_y itself and would be the
	 * wrong place entirely for a rule about the classic bits.
	 *
	 * THE RAW PAIR IS KEPT BESIDE THE SECTORED ONE, because MODERNLOG's tank
	 * line prints both - `stick=` is what the thumb did and `out=` is what the
	 * assembly was handed, and a play test that cannot see the difference
	 * cannot check this item at all.
	 *
	 * THE PREVIOUS SECTOR IS THIS FILE'S, and the outer `if` is what clears it:
	 * a hold that ends, an inventory, a cutscene, first person - anything that
	 * takes the seam away - leaves the next hold to start from no sector and
	 * the plain boundary, which is what "a fresh push is never biased" means.
	 * The outer term is tomb5.control_modern and not modern_seam so that at
	 * Tank the static is neither read nor written; modern_tank already carries
	 * every one of the seam's terms.
	 *
	 * AND THE CLEARING STAYS OUTSIDE THE AIMING GATE, deliberately. Drawing a
	 * weapon in the middle of a hold ends the steering; what follows the
	 * holster is a fresh push and has to start from the plain boundary. The
	 * other placement - keeping the memory across the aiming ticks - would
	 * bias that fresh push by whatever the thumb was doing before the gun came
	 * out, which is the one thing "a fresh push is never biased" forbids. */
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
	 * THE GUARD IS DECLARED ABOVE, with the stick's own reading, since round
	 * 161a3b's item 1 needed the same answer before the classic assembly ran -
	 * `modern_seam` there carries the term-by-term reasoning with it.
	 *
	 * AND THE LOOK KEY (the user's decision 4): while it is held the
	 * translation is bypassed altogether, so the stick turns her finely on
	 * the spot as it does in Tank, for the movements where she has to be
	 * placed exactly. `input` is the word of the PREVIOUS tick - IN_LOOK is
	 * read further down this same function - so the bypass starts a
	 * thirtieth of a second after the key and ends the same way, which is
	 * under the frame a player can see. EXACTLY, because round 161a3's fix
	 * pass was asked to be exact about it: a hold of n ticks is bypassed for
	 * n ticks, the first of them the tick after the key goes down and the
	 * last the first tick it reads up again, so the scheme is back on the
	 * SECOND tick after the last held one. Both ends are one tick behind and
	 * neither is behind the other; the latch below is exactly as old as the
	 * word it stands in for, so this is the latency the bypass has had since
	 * round 161a and not something item 3 added. ModernTest's look_tank()
	 * counts both ends against the hold. With a weapon ready the look key is
	 * swallowed for six ticks further down (it becomes IN_TARGET), so there
	 * the bypass begins when the hold becomes a real look.
	 *
	 * AND THE HOLD IS REMEMBERED BESIDE THE WORD SINCE ROUND 161a3 (item 3).
	 * The word alone can no longer answer, because the block below the
	 * IN_TARGET swallow takes the bit out of it - it would read back the bit it
	 * had just cleared, let the translation run again, clear it again, and
	 * change schemes at 30 Hz. modern_look_tank is that bit one tick later.
	 *
	 * BOTH TERMS, NOT THE NEW ONE INSTEAD OF THE OLD (measured before it was
	 * written): game\control.cpp writes IN_LOOK into `input` itself, and the
	 * one that matters is :527, where it holds the bit ON for the whole
	 * binocular and lasersight session whether the player is still pressing or
	 * not. That session is a LG_READY one, so the block below leaves the bit
	 * alone and this term is the one that answers, exactly as it did before the
	 * round. The latch answers the other case, where the bit is ours and gone.
	 *
	 * AND BOTH TERMS ARE `modern_tank` SINCE ROUND 161a3b (item 1), which is
	 * this gate inverted and is declared above because the sector rule needs it
	 * there. "The seam runs unless the look key is steering her" and "the look
	 * key is steering her" were two spellings of one question, in two places,
	 * and the log line below already had the second of them; they are one now,
	 * and the two are equal term for term. */
	if (modern_seam && !modern_tank)
	{
		// The two key roles the scheme has to know about before they are read
		// into the word below - aiming asks for the action key, the
		// directional jump for the jump key. Same keys, same pad buttons, read
		// in the same order as the blocks that follow.
		/* AND THE AIMING ONE FOLLOWS THE FIRE KEY (#161, round 161b2c,
		 * item 1). MODE AIM is "weapon up and action held: strafe, do not
		 * turn" (game\modern_move.h) - a weapon behaviour, and under
		 * "Separate" the weapons are the Secondary role's, not the Action
		 * role's. So the two role slots are chosen by the same bit that
		 * chooses which of them produces IN_ACTION forty lines below, and the
		 * two answers can never disagree. With "One key" - the default - this
		 * is the second branch, which is the expression that stood here
		 * before the round, character for character. */
		const long mAction = tomb5.action_split
			? (Key(KEY_ROLE_SECOND) || Key(jLayout[JOY_ROLE_SECOND] + 256))
			: (Key(8) || Key(jLayout[4] + 256));
		const long mJump = Key(7) || Key(jLayout[3] + 256);

		// move_x/move_y, not joy_x/joy_y - in Modern that pair is the left
		// stick's own reading, which the d-pad never writes over (see above),
		// so a d-pad press reaches neither this call nor the classic assembly
		// above as a movement stick.
		linput = ModernUpdateInput(linput, move_x, move_y, mAction, mJump);
	}

	/* MODERNLOG's `mode=tank` (round 161a3, step 5). The line above never
	 * runs on a bypass tick - that is the whole point of the bypass - so
	 * without a line of its own here a play test log falls silent for as
	 * long as the look key is held and cannot tell that from the scheme
	 * standing aside for some other reason. The condition is `modern_tank`,
	 * which is the SAME one the `if` above just tested, only inverted, and
	 * which reads the latch half through ModernLookTankActive() - the accessor
	 * item 3 declared for exactly this question, so that the engine keeps ONE
	 * spelling of "the look key is steering her as at Tank" and not two that
	 * can drift apart (round 161a3, fix pass; the reviewer of that round found
	 * the accessor with no caller at all - round 161a3b gave the spelling a
	 * name instead, and all three readers use it). It is still this tick's own
	 * answer: the latch's own write is three lines below, in the block that
	 * also strips IN_LOOK, so this cannot fire on a tick the line above also
	 * fired on, nor miss a tick where it did not. `stick` is move_x/move_y
	 * exactly as the line above would have handed them to ModernUpdateInput.
	 *
	 * AND `sector`/`out` ARE ITEM 1'S OWN TWO (round 161a3b), appended at the
	 * end of the line as every round's new fields are: `sector` is 0 none, 1
	 * turn, 2 step, and `out` is the pair the classic assembly was really
	 * handed, so a log read afterwards can hold a thumb's angle against the
	 * bits it produced instead of inferring them. */
	/* `aim=` was appended by the 161a3b review's high finding: it is the one
	 * field that tells a reader WHY a tank tick came out sector=0 with the
	 * stick still pushed - the look key is aiming a weapon, the binoculars or
	 * the lasersight, so the pair went through untouched on purpose. */
	if (gLogEnabled && modern_tank)
		Log("MODERNLOG f=%ld src=tank mode=tank stick=%ld/%ld sector=%ld out=%ld/%ld aim=%ld",
			(long)GlobalCounter, move_x, move_y, tank_sector, tank_x, tank_y, modern_tank_aim);

	/* THE WALK KEY SURVIVES THE SEAM (round 161a-fix, #161 item 1 - verified
	 * by reading this function rather than assumed). MODERN_MOVE_MASK
	 * includes IN_WALK, so the rewrite inside ModernUpdateInput clears
	 * whatever the stick wrote for it - but the dedicated walk key, Key(6)
	 * and the pad's jLayout[2] below, is read further down this same
	 * function, AFTER the block above has already run. So it only ever ORs
	 * its own bit into what the seam decided; it can never be cleared by it.
	 * A player holding the walk key therefore gets IN_WALK in Modern exactly
	 * as in Tank, independent of whatever the stick's own magnitude does or
	 * does not decide (see MODERN_WALK_BELOW, game\modern_move.h - round
	 * 161a-fix sets it to 0, so the magnitude alone no longer can). Had the
	 * order been the other way round - the walk key read BEFORE the seam -
	 * this would have been a real defect: the seam would wipe the key's own
	 * bit out from under it on every Modern tick. It is not; this paragraph
	 * is the record of having checked. */
	/* THE ACTION ROLE'S WHOLE WORD, COMPUTED ONCE (#161, round 161b2c, item 1).
	 *
	 * Both producers below - the keyboard's Key(8) and the pad's jLayout[4] -
	 * OR this same variable, so the two can never come to disagree about what
	 * the Action button means. That is the byte-identity argument in its
	 * shortest form: with "One key" the value is IN_SELECT | IN_INTERACT |
	 * IN_ACTION, one word, and the two action bits leave every producer in the
	 * engine in the same assignment. Nothing between here and any reader takes
	 * one without the other - lara.Busy's mask at the end of this function
	 * keeps neither, `inputBusy` and `dbinput` copy the whole word, the modern
	 * seam writes only MODERN_MOVE_MASK and IN_DRAW, and the one mask in
	 * game\lara.cpp clears both at this setting (it reaches further than the
	 * design thought - see the comment there).
	 *
	 * With "Separate" the Action role drops IN_ACTION and the Secondary role
	 * below picks it up. IN_SELECT stays with the Action role on purpose: it
	 * is what confirms in every menu and in the inventory ring, and the user's
	 * division does not move menus. */
	const long action_bits = IN_SELECT | IN_INTERACT | (tomb5.action_split ? 0 : IN_ACTION);

	/* ITS SEAM, ON CHANGE AND NEVER PER FRAME (#161, round 161b2c, item 1).
	 * game\menu_frame.cpp's MENULOG line already records the row being SET;
	 * this one records what the input assembly made of it, which is the half a
	 * play test needs when a key does the wrong thing - the row can be set
	 * before a level even loads, and the word below is the engine's own
	 * answer. The static is the only state it keeps, it depends on nothing but
	 * the option, and a run without -log never reaches it. */
	static long action_bits_logged = -1;

	if (gLogEnabled && action_bits != action_bits_logged)
	{
		action_bits_logged = action_bits;
		Log("KEYLOG action=%s bits=0x%lX second=%ld/%ld", tomb5.action_split ? "separate" : "onekey",
			action_bits, (long)layout[1][KEY_ROLE_SECOND], jLayout[JOY_ROLE_SECOND]);
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

	/* THE SECONDARY ROLE, AND IT IS NOT EVEN ASKED WITH "One key" (#161, round
	 * 161b2c, item 2). The `&&` short-circuits, so at the default Key() is not
	 * called for slot 18 at all and the role cannot produce a bit however it
	 * is bound - which is also why its pad twin may sit at -1 without
	 * Key(255) ever being reached. */
	if (tomb5.action_split && Key(KEY_ROLE_SECOND))
		linput |= IN_ACTION;

	if (Key(9))
		linput |= IN_DRAW;

	if (ControlMethod == 1)
	{
		if (Key(jLayout[3] + 256))
			linput |= IN_JUMP;

		if (Key(jLayout[4] + 256))
			linput |= action_bits;

		// The pad's half of the Secondary role, under the same guard and for
		// the same reason as the keyboard's above.
		if (tomb5.action_split && Key(jLayout[JOY_ROLE_SECOND] + 256))
			linput |= IN_ACTION;

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

	/* THE MENUS KEEP THE ARROWS AND RETURN UNDER MODERN (#161, round 161b2a,
	 * item 1).
	 *
	 * Modern binds W/S/A/D to the four direction slots, and those slots are
	 * what every menu in the tree steps with - IN_FORWARD/IN_BACK/IN_LEFT/
	 * IN_RIGHT out of Key(0..3), measured across the title, the pause menu and
	 * its option pages, the inventory ring, load/save and the outfit changer.
	 * So without this a Modern player would drive menus with W/S/A/D, and a
	 * player who rebound them onto something else would be locked out of the
	 * menu he rebound them in.
	 *
	 * THE PREDICATE IS THE PAD'S OWN, deliberately. ModernMenuReadsPad() is
	 * already the tree's one answer to "is a menu reading the direction bits
	 * rather than Lara" (round 161a2b, item 9), and a second spelling of it
	 * here would be a second thing to keep in step. It is exactly the inverse
	 * of the movement seam's last two terms, so in PLAY the arrows do only what
	 * the active table says - which is the other half of the user's decision.
	 *
	 * Raw keymap and not Key(), because these five are the menu's fixed keys
	 * and not a role: the capture refuses to bind any of them over the
	 * navigation (game\menu_frame.cpp) for the same reason. Escape needs no
	 * line - it already reaches IN_DESELECT unconditionally, below. */
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

	/* AND IN MODERN THE LOOK KEY IS A STEERING MODIFIER, NOT A CAMERA (#161,
	 * round 161a3, item 3). "Wenn ich im Remaster die Umsehen-Taste 'L1'
	 * druecke ... Lara kann sich dann auf der Stelle wie im Tank-Modus drehen
	 * mit ihrer alten Animation. Bei uns ... dreht sie sich zwar 360 Grad im
	 * Kreis, aber ohne ihre echte Animation."
	 *
	 * The bypass above has been in the tree since round 161a and does half the
	 * job: it stands the translation down, so the stick's own IN_LEFT / IN_RIGHT
	 * / IN_FORWARD / IN_BACK stand as they do at Tank. The other half is that
	 * the engine ANSWERS the look key, and the answer eats exactly those bits.
	 * Measured in the tree rather than assumed:
	 *   game\lara.cpp:4911   LaraAboveWater calls LookLeftRight() while the bit
	 *                        is held, and LookLeftRight (:920) sets
	 *                        camera.type = LOOK_CAMERA and SUBTRACTS IN_LEFT or
	 *                        IN_RIGHT from the word for the head's own yaw.
	 *   game\lara.cpp:1428+  fifteen state routines call LookUpDown() (:887),
	 *                        which does the same to IN_FORWARD / IN_BACK.
	 *   game\laraswim.cpp:554, game\larasurf.cpp:168/308, game\laraclmb.cpp:48
	 *                        the same in the water, on the surface and on a
	 *                        ladder - the surface one RETURNS, so nothing else
	 *                        of that tick runs at all.
	 * So she turned 360 degrees with no animation, which is M4's own count: 140
	 * ticks of 18 degrees in AS_STOP with the state never leaving 2.
	 *
	 * Taking the bit out here - the last place before `input` is written, and
	 * the only place that knows the scheme - hands those bits back to
	 * lara_as_stop, and with them AS_TURN_L / AS_TURN_R / AS_FASTTURN and the
	 * animations the user asked for. In Modern the right stick is his look
	 * (the orbit), so nothing is lost by it.
	 *
	 * NOT WITH A WEAPON READY. There the look key is already the six-tick
	 * IN_TARGET swallow just above, and the seventh tick is what opens the
	 * binoculars and the lasersight (game\control.cpp:462, which asks for
	 * LG_READY and for the revolver, the crossbow or the HK). Clearing the bit
	 * there would take the sight away from the scheme altogether. That case
	 * keeps the classic look, exactly as it is today.
	 *
	 * ONE MEASURED SIDE EFFECT, WRITTEN DOWN RATHER THAN BRANCHED AROUND: DOZY
	 * ends on IN_WALK "without look" (game\laramisc.cpp:132), so in Modern the
	 * walk key ends the cheat even while the look key is held. It is a cheat,
	 * behind tomb5.enable_cheats, and a branch for it would cost more than it
	 * buys. */
	if (tomb5.control_modern)
	{
		modern_look_tank = (linput & IN_LOOK) ? 1 : 0;

		if (modern_seam && lara.gun_status != LG_READY)
			linput &= ~IN_LOOK;
	}

	DoWeaponHotkey();

	/* ONE PRESS MUST NOT MEAN TWO THINGS (#161, round 161a2b, item 9 - the
	 * same class as the Circle latch further down and as the FMV skip in
	 * #78). Decision 8 put the medipacks on the d-pad's own new bits, and
	 * item 9 has just given the d-pad the menus back: in the inventory a
	 * press of Up would now step the cursor AND swallow a small medipack.
	 * So while a menu is reading the d-pad, a medipack bound to one of its
	 * four bits does not fire.
	 *
	 * ONLY THOSE FOUR BITS, and only in Modern. The keyboard's 0 and 9 keep
	 * working in the inventory as they always have, and so does a medipack a
	 * player has put on a face button - neither of them is steering anything.
	 * At Tank the flag is 0 at its first term, so JoyBitIsDpad is not even
	 * reached and the two conditions below are the expressions they were. */
	const long medi_dpad = tomb5.control_modern && ModernMenuReadsPad();

	/* The keyboard half is a role now (#161, round 161b2d, item 2) - Key() of
	 * an untouched slot is keymap[DIK_0] exactly. The PAD half is untouched
	 * here on purpose: the chord rule and the pad's own hotkey roles are the
	 * next step's, and mixing the two would have made this step's byte-identity
	 * argument two arguments instead of one. */
	if (Key(KEY_ROLE_MEDI_SMALL) || (ControlMethod == 1 && !(medi_dpad && JoyBitIsDpad(jLayout[13])) && Key(jLayout[13] + 256)))
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
	else if (Key(KEY_ROLE_MEDI_LARGE) || (ControlMethod == 1 && !(medi_dpad && JoyBitIsDpad(jLayout[14])) && Key(jLayout[14] + 256)))
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

	/* THE TWO MODE LATCHES (#161, round 161b2c, item 4; the user: "'Sprintmodus'
	 * und 'Duckmodus' kann auch mit rein").
	 *
	 * HERE AND NOT AT THE FOUR PRODUCERS, and the reason is measurable: the
	 * walk-sidestep rewrite forty lines above turns IN_LEFT and IN_RIGHT into
	 * the two step bits, so a direction test up there would read a sidestep as
	 * "pushing nothing" and drop the sprint under the player. Everything else is
	 * the weapon seam's own argument twenty lines below, and it holds for these
	 * two bits with the same grep behind it: IN_DUCK and IN_SPRINT have exactly
	 * two producers each in this file - the keyboard's Key(4)/Key(5) and the
	 * pad's jLayout[0]/jLayout[1], three hundred lines above - and nothing
	 * between them and here reads or writes either. So `linput` carries them at
	 * this point exactly as the keyboard or the pad wrote them, in both control
	 * types, which is what "in both control types, keyboard and pad" costs: one
	 * seam and no per-producer branch at all.
	 *
	 * AND THE WHOLE BLOCK IS BEHIND THE TWO OPTIONS. With both rows at Hold the
	 * only statement of this round that runs in this function is the test on the
	 * next line, and the word leaves exactly as it did at cb55d72. */
	if (tomb5.sprint_toggle || tomb5.duck_toggle)
	{
		/* THE TICKS NO LATCH MAY STAND ON. The first seven terms are the weapon
		 * seam's, read with the same meaning - a cutscene, a spotcam and
		 * bDisableLaraControl are ticks she is not the player's to steer, the
		 * inventory and the pause menu (InventoryActive covers both) and the
		 * title are ticks a menu owns the word, and App.fmv is a video.
		 *
		 * The other four are what these two bits themselves need, and each was
		 * measured before it was written:
		 * - dead: lara_as_dash and lara_as_duckl/duckr all end on hit_points
		 *   <= 0 (game\lara.cpp:1746, :4487, :4497), so the engine drops both
		 *   anyway and a latch that outlived her would be the only thing left
		 *   holding a key.
		 * - the water: EVERY IN_DUCK reader in game\lara.cpp excludes LW_WADE
		 *   by hand and lara_as_dash names it as its fourth exit; under water
		 *   and at the surface neither AS_DUCK nor AS_DASH is reachable at all.
		 *   So anything but LW_ABOVE_WATER is a tick the bits mean nothing on.
		 * - LG_HANDS_BUSY is TR5's own spelling of the brief's "ladder and
		 *   vehicle": a ladder, a rope, the water and a pickup all leave
		 *   gun_status there (game\lara.cpp:1156, :1168 among others), and
		 *   LARA_INFO has no Vehicle field to ask instead - measured, the name
		 *   does not occur in global\types.h.
		 * - the binoculars and the lasersight read IN_SPRINT and IN_DUCK out of
		 *   `inputBusy` as ZOOM IN and ZOOM OUT (game\camera.cpp:3520/:3529).
		 *   That is a held key by its nature - the zoom steps once per tick -
		 *   so the sight is a drop, and a drop tick hands the raw key through
		 *   untouched, which leaves the zoom exactly as it is today. */
		const long mode_drop = !lara_item || App.fmv || GLOBAL_playing_cutseq ||
			bUseSpotCam || bDisableLaraControl || InventoryActive ||
			gfLevelId == LVL5_TITLE || lara_item->hit_points <= 0 ||
			lara.water_status != LW_ABOVE_WATER || lara.gun_status == LG_HANDS_BUSY ||
			BinocularRange != 0;

		/* AND THE TWO THE SPRINT ADDS. "The sprint latch also drops when no
		 * direction is pushed or the engine ends the sprint" - the first is
		 * this word's own six direction bits, the second is the dash bar:
		 * lara_as_dash leaves AS_DASH on `!DashTimer` and game\laramisc.cpp:502
		 * refills it the moment she is out of AS_DASH, so a latch that sat
		 * through an empty bar would put her back into a sprint on the single
		 * unit of the refill. Both belong to the sprint alone; a duck holds her
		 * standing still on purpose. */
		const long moving = (linput & (IN_FORWARD | IN_BACK | IN_LEFT | IN_RIGHT |
			IN_LSTEP | IN_RSTEP)) != 0;
		const long sprint_drop = mode_drop || !DashTimer || !moving;
		const long duck_drop = mode_drop;

		linput = ModernModeLatch(tomb5.sprint_toggle, (linput & IN_SPRINT) != 0,
			sprint_drop, &sprint_latch, IN_SPRINT, linput);
		linput = ModernModeLatch(tomb5.duck_toggle, (linput & IN_DUCK) != 0,
			duck_drop, &duck_latch, IN_DUCK, linput);

		/* MODELOG, behind -log like every other seam of this round, and on the
		 * CHANGE of a latch and not every tick: a latched bit stands for
		 * hundreds of ticks and a per-tick line would bury the two moments a
		 * play test is actually after - the press that latched it and whatever
		 * let it go. The two drops are printed beside it so a reader can see
		 * WHICH of the eleven terms answered, and the two options so that a log
		 * read afterwards need not guess how the rows stood. -1 so the first
		 * line of a session is always written. */
		if (gLogEnabled)
		{
			static long mode_logged = -1;
			const long now = ((sprint_latch & MODERN_LATCH_ON) ? 1 : 0) |
				((duck_latch & MODERN_LATCH_ON) ? 2 : 0);

			if (now != mode_logged)
			{
				mode_logged = now;
				Log("MODELOG f=%ld sprint=%d duck=%d drop=%d/%d mode=%d/%d",
					(long)GlobalCounter, (int)(now & 1), (int)((now >> 1) & 1),
					(int)(sprint_drop != 0), (int)(duck_drop != 0),
					(int)tomb5.sprint_toggle, (int)tomb5.duck_toggle);
			}
		}
	}

	/* THE WEAPON HALF (#161, round 161b2a, item 2; design section 1.5). The
	 * second and much smaller seam of this function, and the last thing that
	 * touches IN_DRAW before the word leaves: the keyboard's Key(9) and the
	 * pad's jLayout[5] are both three hundred lines above, and nothing
	 * between them and here reads or writes that bit - measured by grep, it
	 * has exactly two producers in this file and no other consumer.
	 *
	 * WHERE THE EDGE LIVES TODAY, because Hold has to produce its request in
	 * the form the reader takes, and the reader is game\larafire.cpp. MEASURED:
	 * there is no edge in the input word at all. Key(9) is a level read of
	 * keymap[], `input` is `linput` unlatched, and dbinput is computed but
	 * never asked about IN_DRAW by anyone. What debounces the draw key is
	 * LARA'S OWN STATE MACHINE: LaraGun's opening chain asks about IN_DRAW
	 * only at LG_NO_ARMS (:229) and at LG_READY (:290), and between those two
	 * she is in LG_DRAW_GUNS or LG_UNDRAW_GUNS, where the chain has no branch
	 * at all and the animation simply runs. The animation IS the debounce. So
	 * a held key in the original really does draw and holster without end -
	 * base does that today - and a player never sees it only because a tap is
	 * shorter than the animation. That is why ModernWeaponBits asks for the
	 * disagreement and not for the key: one request per tick on which the
	 * state and the key disagree, none while the animation runs (both truths
	 * false), and therefore nothing that can queue up and fire a second later.
	 * It corrects itself, because the very next tick reads the state the
	 * request produced.
	 *
	 * THE GUARD IS THE STATES IN WHICH NOBODY WOULD CONSUME THE REQUEST. The
	 * self-correction above rests on LaraGun running on the next tick; where
	 * it does not, a request would sit in the word for as long as the state
	 * stood still. Measured consequence, and the reason this guard is not
	 * decoration: specific\fmv.cpp:263 skips a video on a RISING edge of
	 * IN_OPTION|IN_DRAW|IN_SELECT, so a bit that is on for every frame of the
	 * outro FMV - LG_READY from the end of the level, the key up - would
	 * saturate that OR and make the video unskippable by any key. Hence
	 * App.fmv. The other terms are the state half of the movement seam's own
	 * guard (see modern_seam above), read with the same meaning: a cutscene, a
	 * spotcam and bDisableLaraControl are ticks she is not the player's to
	 * steer, the inventory and the pause menu (InventoryActive covers both)
	 * and the title are ticks a menu owns the word. ControlMethod and
	 * tomb5.control_modern are deliberately NOT terms: combat mode is the
	 * user's decision for both control types and for keyboard and pad alike.
	 *
	 * AND THE HANDS BUSY NEED NO TERM. A ladder, the water, a vehicle and a
	 * pickup all leave lara.gun_status at LG_HANDS_BUSY, which is neither
	 * LG_READY nor LG_NO_ARMS, so both truths are false and the function is
	 * quiet by its own rule. lara.Busy strips IN_DRAW forty lines below for
	 * the binocular and lasersight sessions on top of that, exactly as it
	 * strips the original's.
	 *
	 * THE TORCH IS THE ONE HAND THAT IS NOT A WEAPON, and it is a term of the
	 * GUARD and not of `ready`, which design section 1.5 leaves open and a
	 * measurement settles. A burning torch sits at LG_READY with gun_type
	 * WEAPON_TORCH (game\flmtorch.cpp:196), LaraGun returns before its chain
	 * for it (:221), and the bit is read instead by DoFlameTorch (:117) to
	 * THROW the torch. Had the torch only made `ready` false, the weapon half
	 * would still have cleared the key's own bit - both truths false - and the
	 * torch could never be thrown again in Hold. With the torch in the guard
	 * the original passes through untouched, which is the only right answer
	 * for a hand the draw key does not govern. The term clears itself:
	 * gun_type goes back to last_gun_type or WEAPON_NONE the moment the torch
	 * leaves her (:147, :169).
	 *
	 * AND THE FLARE NEEDS NO EXCEPTION, which was worth measuring rather than
	 * assuming, because a rule that put a lit flare away on the first tick the
	 * key was not held would make flares unusable in Hold. It cannot happen:
	 * game\laraflar.cpp:175, ready_flare(), parks a BURNING flare at
	 * LG_NO_ARMS and not at LG_READY. So the flare reads as `holstered`, and
	 * with the key up neither rule fires and she keeps it; with the key down
	 * the request goes out and LaraGun throws the flare and draws the gun -
	 * which is what a draw-key press with a flare in hand does in the
	 * original too. */
	if (tomb5.combat_hold)
	{
		const long combat_seam = lara_item && !App.fmv && !GLOBAL_playing_cutseq &&
			!bUseSpotCam && !bDisableLaraControl && !InventoryActive &&
			gfLevelId != LVL5_TITLE && lara.gun_type != WEAPON_TORCH;

		// The draw key or button physically down THIS tick - which is what
		// this bit already is at this point, from either producer.
		const long held = (linput & IN_DRAW) != 0;
		const long ready = (combat_seam && lara.gun_status == LG_READY) ? 1 : 0;
		const long holstered = (combat_seam && lara.gun_status == LG_NO_ARMS) ? 1 : 0;

		if (combat_seam)
			linput = ModernWeaponBits(1, ready, holstered, held, linput);

		/* COMBATLOG, behind -log like every other seam of this round. On the
		 * CHANGE of the bit or of the seam and not every tick, because the bit
		 * is one tick long in play and a per-tick line would bury it; the two
		 * truths and the key are printed beside it so a reader can see WHICH
		 * of the four rules answered. -1 so the first line of a session is
		 * always written, whatever the bit turns out to be.
		 *
		 * `draw` IS THE BIT THAT LEAVES THIS FUNCTION, not the one the weapon
		 * half produced, and `seam` says which of the two a reader is looking
		 * at. Round 161b2a's review found that the difference decides whether
		 * the line can be believed: with the seam off - a torch in the hand,
		 * an FMV, the inventory - the key's own IN_DRAW is still in the word
		 * and is still consumed further down the frame (game\flmtorch.cpp
		 * throws the burning torch on exactly that bit, specific\fmv.cpp skips
		 * a video on it), so a field that printed 0 there would tell a reader
		 * chasing "why did my torch throw" the opposite of the truth. */
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
		/* Two roles now (#161, round 161b2d, item 2), same guard, same bits, and
		 * the pad halves untouched until the next step. At Tank both roles hold
		 * the keys that stood here, F5 and F6; under Modern quick load is F9
		 * instead - the one default this round moves, and the user's own
		 * decision (game\key_scheme.h). */
		if (Key(KEY_ROLE_SAVE) || (ControlMethod == 1 && Key(jLayout[11] + 256)))
			linput |= IN_SAVE;

		if (Key(KEY_ROLE_LOAD) || (ControlMethod == 1 && Key(jLayout[12] + 256)))
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

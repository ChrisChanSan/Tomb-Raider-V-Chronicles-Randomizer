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
#include "menu_frame.h"
#include "menu_nav.h"
#include "text.h"
#include "gameflow.h"
#include "sound.h"
#include "camera.h"			// ModernCameraResetOrbit - the Control type row (round 161a2)
#include "modern_state.h"		// ModernResetControls - the same row
#include "savegame.h"
#include "../specific/input.h"
#include "../specific/dxshell.h"		// keymap - the sliders and the key capture read it raw
#include "../specific/winmain.h"		// App - the Targeting row
#include "../specific/3dmath.h"		// phd_centerx - the geometry
#include "../specific/LoadSave.h"
#include "../specific/registry.h"
#include "../specific/drawbars.h"
#include "../specific/audio.h"
#include "../specific/dxsound.h"
#include "../specific/function_stubs.h"	// Log, gLogEnabled - the MENULOG seam
#include "../tomb5/tomb5.h"
#include "../tomb5/troyestuff.h"

/* ========================================================================= */
/* The rows' own logic. Every function below is a block of base's DoOptions()
 * (specific\LoadSave.cpp) or of a Page* function (tomb5\troyestuff.cpp),
 * carried over statement for statement - the sounds included, because several
 * rows play one even when the value does not move and that is what the player
 * hears today. The return value is the one thing that is new: true when the
 * SETTING really changed, which is what decides whether anything is written
 * and whether MENULOG prints a line.
 *
 * THE FRAME CALLS step() ON EVERY FRAME THE CURSOR STANDS ON THE ROW, with dir
 * 0 when no direction is held. That is deliberate: base's rows are written as
 * "if (sel & bit) { ... }" blocks and several of them do work with no input at
 * all (Music volume calls ACMSetVolume, Targeting mirrors into savegame), so a
 * block that only ran on a keypress would not be the same block.            */
/* ========================================================================= */

/* Backed up once per session, exactly as base's DoOptions did with its
 * sfx_backup_flag: the SFX row compares against sfx_bak to decide whether to
 * re-trigger the breath sample, and the quality row against sfx_quality_bak. */
static long sfx_bak;
static long sfx_quality_bak;
static long sfx_breath_db = -1;
static char sfx_backup_flag;

/* ---------------------------------------------------------------- Audio -- */

static long MenuNumMusicVolume(long)
{
	return MusicVolume;
}

static bool MenuStepMusicVolume(long, long dir)
{
	long old = MusicVolume;

	sfx_bak = SFXVolume;

	if (dir < 0)
		MusicVolume--;

	if (dir > 0)
		MusicVolume++;

	if (MusicVolume > 100)
		MusicVolume = 100;

	if (MusicVolume < 0)
		MusicVolume = 0;

	ACMSetVolume();
	return MusicVolume != old;
}

static long MenuNumSfxVolume(long)
{
	return SFXVolume;
}

static bool MenuStepSfxVolume(long, long dir)
{
	long old = SFXVolume;

	if (dir < 0)
		SFXVolume--;

	if (dir > 0)
		SFXVolume++;

	if (SFXVolume > 100)
		SFXVolume = 100;

	if (SFXVolume < 0)
		SFXVolume = 0;

	if (SFXVolume != sfx_bak)
	{
		if (sfx_breath_db == -1 || !DSIsChannelPlaying(0))
		{
			S_SoundStopAllSamples();
			sfx_bak = SFXVolume;
			sfx_breath_db = SoundEffect(SFX_LARA_BREATH, 0, SFX_DEFAULT);
			DSChangeVolume(0, -100 * (long(100 - SFXVolume) >> 1));
		}
		else if (sfx_breath_db != -1 && DSIsChannelPlaying(0))
			DSChangeVolume(0, -100 * ((100 - SFXVolume) >> 1));
	}

	return SFXVolume != old;
}

static const char* MenuValSoundQuality(long, char* buf)
{
	switch (SoundQuality)
	{
	case 0:
		strcpy(buf, SCRIPT_TEXT(TXT_Low));
		break;

	case 1:
		strcpy(buf, SCRIPT_TEXT(TXT_Medium));
		break;

	case 2:
		strcpy(buf, SCRIPT_TEXT(TXT_High));
		break;
	}

	return buf;
}

static bool MenuStepSoundQuality(long, long dir)
{
	long old = SoundQuality;

	sfx_bak = SFXVolume;

	if (dir < 0)
		SoundQuality--;

	if (dir > 0)
		SoundQuality++;

	if (SoundQuality > 2)
		SoundQuality = 2;

	if (SoundQuality < 0)
		SoundQuality = 0;

	if (SoundQuality != sfx_quality_bak)
	{
		S_SoundStopAllSamples();
		DXChangeOutputFormat(sfx_frequencies[SoundQuality], 0);
		sfx_quality_bak = SoundQuality;
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	return SoundQuality != old;
}

static const char* MenuValLanguage(long, char* buf)
{
	strcpy(buf, LanguageName(Gameflow->Language));
	return buf;
}

static bool MenuStepLanguage(long, long dir)
{
	if (!dir)
		return 0;

	// The next EXISTING language file in the chosen direction - missing
	// ones are skipped (SwitchLanguage would fail silently otherwise).
	for (long n = Gameflow->Language + dir; n >= 0 && n < gfLanguageCount; n += dir)
	{
		if (SwitchLanguage(n))
		{
			LanguageChoice = n;	// an explicit choice from now on, registry "Language"
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			return 1;
		}
	}

	return 0;
}

/* ------------------------------------------------------------- Controls -- */

static const char* MenuValControlType(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.control_modern ? "Modern" : "Tank"));
	return buf;
}

static bool MenuStepControlType(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.control_modern = !tomb5.control_modern;
	ModernResetControls();		// a stale held course/spin/blocked-counter must not survive a scheme change (game/modern_state.h)
	ModernCameraResetOrbit();	// nor a camera offset seated under the other scheme (#161, round 161a2)
	// AND NOR MAY A BINDING THE NEW SCHEME CANNOT HONOUR: Modern takes the
	// right stick for the camera and Tank keeps the d-pad moving her, so the
	// two roles the schemes disagree about - the medipacks - move with the
	// switch. Deliberately NOT ApplyJoyDefaults(): a player who customised
	// his pad must not lose it for trying the other scheme.
	ApplyJoyScheme();
	return 1;
}

static const char* MenuValCombatMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.combat_hold ? "Hold" : "Toggle"));
	return buf;
}

static bool MenuStepCombatMode(long, long dir)
{
	if (!dir)
		return 0;

	// Drawn and saved only - nothing reads tomb5.combat_hold yet (workflow B).
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.combat_hold = !tomb5.combat_hold;
	return 1;
}

static const char* MenuValAction(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.action_split ? "Separate" : "One key"));
	return buf;
}

static bool MenuStepAction(long, long dir)
{
	if (!dir)
		return 0;

	// Same as Combat mode above - drawn and saved, no reader yet.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.action_split = !tomb5.action_split;
	return 1;
}

/* "Art der Steuerung ... Schlecht zu differenzieren wegen der neuen modernen
 * Steuerung" - so the row is called Control layout now, and it says what it
 * always meant: which set of button names and defaults the bindings below it
 * are written in. Internally ControlMethod is still 0/1 (keyboard/joystick)
 * and the three controller positions are method 1 with JoyStyle 0/1/2, so
 * every "ControlMethod == 1" in the game keeps running unchanged.
 *
 * THE FIFTH POSITION IS GONE. "Reset" was a menu action wearing a setting's
 * clothes; it is the centred action on the Key bindings page now, and with it
 * go base's reset_row flag and the nMask pin that kept the cursor on this row
 * while it was showing. */
static const char* MenuValControlLayout(long, char* buf)
{
	if (!ControlMethod)
	{
		strcpy(buf, SCRIPT_TEXT(TXT_Keyboard));
		return buf;
	}

	// The joystick positions carry the chosen page's name. The page only
	// changes the DISPLAY of the button names (and the defaults on reset) -
	// the bit order comes from the active input path, see input.cpp.
	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		strcpy(buf, "PS5 Controller");
		break;

	case JOYSTYLE_XBOX:
		strcpy(buf, "Xbox Controller");
		break;

	default:
		strcpy(buf, SCRIPT_TEXT(TXT_Joystick));
		break;
	}

	return buf;
}

static bool MenuStepControlLayout(long, long)
{
	long pos, padStep, was_method, was_style;

	was_method = ControlMethod;
	was_style = JoyStyle;
	pos = ControlMethod ? 1 + ActiveJoyStyle() : 0;

	// THIS ROW IGNORES THE FRAME'S OWN DIRECTION on purpose. On the keyboard
	// position the pad delivers no direction bits at all - every use of the
	// stick in input.cpp hangs on ControlMethod - so whoever paged over here
	// with the controller could not page back with it. So the stick is read
	// directly, on BOTH positions: if only on the keyboard one, a single
	// deflection counts twice (JoyMenuStep takes the step, ControlMethod
	// flips to the pad, and next frame the direction still being held reaches
	// dbinput for the first time and its edge detection calls that a fresh
	// press - one push, two pages, seen in game). While the stick is
	// deflected the step belongs to JoyMenuStep alone; the arrow keys keep
	// working through dbinput whenever the stick rests.
	padStep = JoyMenuStep();

	if ((dbinput & IN_LEFT && !JoyMenuDeflected()) || padStep < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		pos--;
	}

	if ((dbinput & IN_RIGHT && !JoyMenuDeflected()) || padStep > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		pos++;
	}

	if (pos > 3)	// was 4 at base - the Reset position left (see above)
		pos = 3;

	if (pos < 0)
		pos = 0;

	if (!pos)
		ControlMethod = 0;
	else
	{
		ControlMethod = 1;
		JoyStyle = pos - 1;	// an explicit choice from now on, not automatic
	}

	return ControlMethod != was_method || JoyStyle != was_style;
}

/* Camera speed and (from step 3) Dead zone are the modern control's own feel,
 * and under Tank neither number is read by anything. A row that cannot do
 * anything is offered in colour 3 and skipped rather than silently ignored. */
static bool MenuEnabledModern(long)
{
	return tomb5.control_modern != 0;
}

static long MenuNumCameraSpeed(long)
{
	return (long)tomb5.camera_speed;
}

static bool MenuStepCameraSpeed(long, long dir)
{
	// "Der rechte Stick muss noch schneller werden, der ist viel zu langsam.
	// Am besten ein Slider wie die SFX und Musik." The NUMBER only ever means
	// anything through game\modern_camera.cpp's ModernCameraSpeedRates - this
	// row just stores and clamps it.
	ulong old = tomb5.camera_speed;

	if (dir < 0 && tomb5.camera_speed > 0)
		tomb5.camera_speed--;

	if (dir > 0 && tomb5.camera_speed < 100)
		tomb5.camera_speed++;

	return tomb5.camera_speed != old;
}

static const char* MenuValTargeting(long, char* buf)
{
	if (App.AutoTarget)
		strcpy(buf, SCRIPT_TEXT(TXT_Automatic));
	else
		strcpy(buf, SCRIPT_TEXT(TXT_Manual));

	return buf;
}

static bool MenuStepTargeting(long, long dir)
{
	long old = App.AutoTarget;

	if (dir < 0)
	{
		if (App.AutoTarget)
			App.AutoTarget = 0;

		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	if (dir > 0)
	{
		if (!App.AutoTarget)
			App.AutoTarget = 1;

		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	savegame.AutoTarget = (uchar)App.AutoTarget;
	return App.AutoTarget != old;
}

/* -------------------------------------------------------------- Display -- */

static const char* MenuValFrameRate(long, char* buf)
{
	/* 30, 60 or the monitor's own rate. "Display" needs VSync - it is the
	 * blocking present that paces it - so with VSync off it runs at 60 and
	 * the row says which of the two the player is really getting. */
	strcpy(buf, tomb5.fps_mode == 2 ? (tomb5.vsync ? "Display" : "Display (60)") :
	            tomb5.fps_mode == 1 ? "60" : "30");
	return buf;
}

static bool MenuStepFrameRate(long, long dir)
{
	// Three values, so LEFT and RIGHT walk the ring in opposite directions
	// rather than both toggling.
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.fps_mode = tomb5.fps_mode >= 2 ? 0 : tomb5.fps_mode + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.fps_mode = tomb5.fps_mode == 0 ? 2 : tomb5.fps_mode - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValVSync(long, char* buf)
{
	// The one row in the build that is not instant: VSync is a
	// device-creation parameter (D3D9 has no other way to ask for it), so it
	// is picked up the next time the game starts. The row says so on the
	// right rather than in a manual nobody has open.
	strcpy(buf, tomb5.vsync ? "on (restart)" : "off (restart)");
	return buf;
}

static bool MenuStepVSync(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.vsync = tomb5.vsync ? 0 : 1;
	return 1;
}

/* --------------------------------------------------------- Key bindings -- */

/* Waiting for the player to press the thing he wants bound. One role at a
 * time, and the role is the ROW'S OWN param - base had to shift the selection
 * bit back down into an array index at two places and recount the shift every
 * time a row moved (specific\LoadSave.cpp:619-633 and :667-680 at base). */
static bool menu_waiting_for_key;
static long menu_wait_role;
// The action button still pinned the row selection when the wait was entered.
static bool menu_action_still_held;

static const char** MenuKeyboardNames()
{
	// A hardcoded German/everything-else choice, independent of CustomText's
	// five-language table - the key NAMES come from their own scan-code table.
	return Gameflow->Language == 2 ? GermanKeyboard : KeyboardButtons;
}

static const char* MenuValBinding(long role, char* buf)
{
	if (menu_waiting_for_key && menu_wait_role == role)
	{
		strcpy(buf, SCRIPT_TEXT(TXT_Waiting));
		return buf;
	}

	if (!ControlMethod)
		return MenuKeyboardNames()[layout[1][role]];

	if (jLayout[role] == -1)
		return "---";

	return JoyButtonName(jLayout[role]);
}

static void MenuSelectBinding(long role)
{
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	menu_wait_role = role;
	menu_waiting_for_key = 1;
	// The hold guard below (do not immediately capture the action button as
	// the new binding) only applies when the row was REALLY selected with the
	// pad button. If the selection came from the keyboard (Enter), the guard
	// used to PERMANENTLY block any attempt to bind the action button itself.
	menu_action_still_held = ControlMethod == 1 && Key(jLayout[4] + 256);
	memset(keymap, 0, sizeof(keymap));
}

/* "Reset to defaults", the centred action above the list. It is exactly what
 * the Method row's fifth position did: the compiled-in keyboard table AND the
 * pad defaults matching the chosen page and the active input path. Per-scheme
 * reset is round B2. */
static void MenuResetBindings(long)
{
	SoundEffect(SFX_MENU_SELECT, 0, 2);
	memcpy(layout[1], layout, 72);
	ApplyJoyDefaults();	// defaults matching page AND input path (input.cpp)
}

/* ========================================================================= */
/* The pages. Read them as the tree: a centred row opens something, a
 * left-aligned row is a setting.                                            */
/* ========================================================================= */

#define MENU_TAB(lbl, page)			{ MROW_TAB, lbl, -1, 0, 0, MENU_SAVE_NONE, 0, page, 0, 0, 0 }
#define MENU_TAB_TXT_IF(lbl, txt, page, pred)	{ MROW_TAB, lbl, txt, 0, 0, MENU_SAVE_NONE, pred, page, 0, 0, 0 }
#define MENU_ACTION(lbl, act)			{ MROW_ACTION, lbl, -1, 0, 0, MENU_SAVE_NONE, 0, 0, act, 0, 0 }
#define MENU_VALUE(lbl, val, stp, sv)		{ MROW_VALUE, lbl, -1, val, stp, sv, 0, 0, 0, 0, 0 }
#define MENU_VALUE_TXT(lbl, txt, val, stp, sv)	{ MROW_VALUE, lbl, txt, val, stp, sv, 0, 0, 0, 0, 0 }
#define MENU_SLIDER(lbl, txt, num, stp, sv, pred) { MROW_SLIDER, lbl, txt, 0, stp, sv, pred, 0, 0, num, 0 }
#define MENU_BIND(lbl, txt, role)		{ MROW_BINDING, lbl, txt, MenuValBinding, 0, MENU_SAVE_GAME, 0, 0, MenuSelectBinding, 0, role }

// Special Features is a title-screen thing: gfGameMode is 1 only there, and
// the pause menu cannot reach it at all (game\control.cpp gates the pause key
// on !gfGameMode). Offered in colour 3 in a pause session rather than silently
// missing, which is what the old page did.
static bool MenuEnabledSpecialFeatures(long)
{
	return gfGameMode == 1;
}

static const menu_row menu_rows_options[] =
{
	MENU_TAB("Audio", MENU_PAGE_AUDIO),
	MENU_TAB("Controls", MENU_PAGE_CONTROLS),
	MENU_TAB("Graphics", MENU_PAGE_GRAPHICS),
	MENU_TAB("tomb5 options", MENU_PAGE_TOMB5),
	MENU_TAB_TXT_IF("Special Features", TXT_Special_Features, MENU_PAGE_SPECIAL, MenuEnabledSpecialFeatures),
	MENU_TAB("Credits", MENU_PAGE_CREDITS)
};

static const menu_row menu_rows_audio[] =
{
	MENU_SLIDER("Music Volume", TXT_Music_Volume, MenuNumMusicVolume, MenuStepMusicVolume, MENU_SAVE_GAME, 0),
	MENU_SLIDER("SFX Volume", TXT_SFX_Volume, MenuNumSfxVolume, MenuStepSfxVolume, MENU_SAVE_GAME, 0),
	MENU_VALUE_TXT("Sound Quality", TXT_Sound_Quality, MenuValSoundQuality, MenuStepSoundQuality, MENU_SAVE_GAME),
	MENU_VALUE("Language", MenuValLanguage, MenuStepLanguage, MENU_SAVE_GAME)
};

static const menu_row menu_rows_controls[] =
{
	MENU_TAB("Key bindings", MENU_PAGE_KEYBINDS),
	MENU_VALUE("Control type", MenuValControlType, MenuStepControlType, MENU_SAVE_TOMB5),
	MENU_VALUE("Combat mode", MenuValCombatMode, MenuStepCombatMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Action", MenuValAction, MenuStepAction, MENU_SAVE_TOMB5),
	MENU_VALUE("Control layout", MenuValControlLayout, MenuStepControlLayout, MENU_SAVE_GAME),
	MENU_SLIDER("Camera speed", -1, MenuNumCameraSpeed, MenuStepCameraSpeed, MENU_SAVE_TOMB5, MenuEnabledModern),
	MENU_VALUE_TXT("Targeting", TXT_Targeting, MenuValTargeting, MenuStepTargeting, MENU_SAVE_GAME)
};

static const menu_row menu_rows_graphics[] =
{
	// Step 2 of this round fills the eleven settings in beneath this tab;
	// until then they are still reachable through "tomb5 options".
	MENU_TAB("Display", MENU_PAGE_DISPLAY)
};

static const menu_row menu_rows_display[] =
{
	MENU_VALUE("Frame rate", MenuValFrameRate, MenuStepFrameRate, MENU_SAVE_TOMB5),
	MENU_VALUE("VSync", MenuValVSync, MenuStepVSync, MENU_SAVE_TOMB5)
};

/* The two bindable lists. Row 0 of each is the centred reset action, and it is
 * the page's one FIXED row - it is repeated on every page of the split list,
 * above it, which is where the user's screen has it.
 *
 * The keyboard list's first four rows are glyphs rather than words (up, down,
 * left, right) exactly as at base: they are layout slots 0-3, Lara's Tank
 * movement keys, which were never given a text label in the string wad.
 * CustomText hands an unknown literal straight back, so the glyphs go through
 * the same path as every other label without a special case. */
static const menu_row menu_rows_keybinds_kbd[] =
{
	MENU_ACTION("Reset to defaults", MenuResetBindings),
	MENU_BIND("\x18", -1, 0),
	MENU_BIND("\x1A", -1, 1),
	MENU_BIND("\x19", -1, 2),
	MENU_BIND("\x1B", -1, 3),
	MENU_BIND("Duck", TXT_Duck, 4),
	MENU_BIND("Dash", TXT_Dash, 5),
	MENU_BIND("Walk", TXT_Walk, 6),
	MENU_BIND("Jump", TXT_Jump, 7),
	MENU_BIND("Action", TXT_Action, 8),
	MENU_BIND("Draw Weapon", TXT_Draw_Weapon, 9),
	MENU_BIND("Use Flare", TXT_Use_Flare, 10),
	MENU_BIND("Look", TXT_Look, 11),
	MENU_BIND("Roll", TXT_Roll, 12),
	MENU_BIND("Inventory", TXT_Inventory, 13),
	MENU_BIND("Step Left", TXT_Step_Left, 14),
	MENU_BIND("Step Right", TXT_Step_Right, 15)
};

static const menu_row menu_rows_keybinds_joy[] =
{
	MENU_ACTION("Reset to defaults", MenuResetBindings),
	MENU_BIND("Duck", TXT_Duck, 0),
	MENU_BIND("Dash", TXT_Dash, 1),
	MENU_BIND("Walk", TXT_Walk, 2),
	MENU_BIND("Jump", TXT_Jump, 3),
	MENU_BIND("Action", TXT_Action, 4),
	MENU_BIND("Draw Weapon", TXT_Draw_Weapon, 5),
	MENU_BIND("Use Flare", TXT_Use_Flare, 6),
	MENU_BIND("Look", TXT_Look, 7),
	MENU_BIND("Roll", TXT_Roll, 8),
	MENU_BIND("Inventory", TXT_Inventory, 9),
	MENU_BIND("Pause", -1, 10),
	MENU_BIND("Save", TXT_Save, 11),
	MENU_BIND("Load", TXT_Load, 12),
	MENU_BIND("Small Medipack", TXT_Small_Medipack, 13),
	MENU_BIND("Large Medipack", TXT_Large_Medipack, 14)
};

static void MenuResolveKeybinds(const menu_row** rows, long* num)
{
	if (ControlMethod == 1)
	{
		*rows = menu_rows_keybinds_joy;
		*num = sizeof(menu_rows_keybinds_joy) / sizeof(menu_rows_keybinds_joy[0]);
	}
	else
	{
		*rows = menu_rows_keybinds_kbd;
		*num = sizeof(menu_rows_keybinds_kbd) / sizeof(menu_rows_keybinds_kbd[0]);
	}
}

#define MENU_ROWS(a)	a, sizeof(a) / sizeof(a[0])

static const menu_page menu_pages[MENU_PAGE_COUNT] =
{
	{ "Options",      TXT_Options, MENU_ROWS(menu_rows_options),  0, 0 },
	{ "Audio",        -1,          MENU_ROWS(menu_rows_audio),    0, 0 },
	{ "Controls",     -1,          MENU_ROWS(menu_rows_controls), 0, 0 },
	{ "Key bindings", -1,          0, 0,                          1, MenuResolveKeybinds },
	{ "Graphics",     -1,          MENU_ROWS(menu_rows_graphics), 0, 0 },
	{ "Display",      -1,          MENU_ROWS(menu_rows_display),  0, 0 },
	{ "tomb5 options", -1,         0, 0,                          0, 0 },	// the old TroyeMenu until step 2
	{ "Special Features", -1,      0, 0,                          0, 0 },	// the existing screen
	{ "Credits",      -1,          0, 0,                          0, 0 }	// the existing screen
};

/* ========================================================================= */
/* The frame itself.                                                         */
/* ========================================================================= */

// Where the player is. The stack is what makes IN_DESELECT mean "back to the
// page that opened this one, with the cursor on the row that did it" without
// any page having to know its own parent.
static long menu_stack[8];
static long menu_depth;
// False between two visits, so that the first frame of a visit knows it is one
// and MENULOG says so.
static bool menu_open;
static long menu_cursor[MENU_PAGE_COUNT];
static long menu_page_no[MENU_PAGE_COUNT];

/* ITEM 3: the "Game" registry block is written when it changes. Until this
 * round every key rebind, volume drag, sound quality change, targeting toggle,
 * language switch and control layout change lived in memory until a clean exit
 * (menu-map R4) - a crash threw the lot away while every tomb5 toggle was
 * already on disk. The flag is set by the rows whose save is MENU_SAVE_GAME
 * and cleared by the one call, when the page carrying them is left. Never per
 * frame: SaveSettings() opens the key and writes about forty values. */
static bool menu_game_dirty;

// A slider is held down on purpose, so it is written when it stops moving, not
// while it moves. Which slider is waiting is remembered rather than which page,
// because only one row can be selected at a time.
static const menu_row* menu_slider_unsaved;

static void MenuLogEnter(long page)
{
	if (gLogEnabled)
		Log("MENULOG enter page=%s", menu_pages[page].name);
}

static void MenuLogSet(long page, const menu_row* row, const char* value)
{
	if (gLogEnabled)
		Log("MENULOG set page=%s row=%s value=%s", menu_pages[page].name, row->label, value ? value : "");
}

// Block A is written whole on every edit, as it always was; block B is only
// marked here and written when the page is left.
static void MenuAfterChange(long page, const menu_row* row, const char* value)
{
	MenuLogSet(page, row, value);

	if (row->save == MENU_SAVE_TOMB5)
		save_new_tomb5_settings();
	else if (row->save == MENU_SAVE_GAME)
		menu_game_dirty = 1;
}

static void MenuFlushGame()
{
	if (!menu_game_dirty)
		return;

	menu_game_dirty = 0;
	// SaveSettings() also writes the window position and the display mode
	// (specific\registry.cpp:462-468). Both are live values at any moment, so
	// writing them mid-session writes exactly what a clean exit would have
	// written then - measured before this call was added.
	SaveSettings();
}

// A slider that stopped moving, or whose row or page was left.
static void MenuSettleSlider(long page)
{
	const menu_row* row = menu_slider_unsaved;
	char buf[32];

	if (!row)
		return;

	menu_slider_unsaved = 0;
	sprintf(buf, "%d", (int)(row->slider ? row->slider(row->param) : 0));
	MenuAfterChange(page, row, buf);
}

static void MenuGoTo(long page)
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();

	if (menu_depth < (long)(sizeof(menu_stack) / sizeof(menu_stack[0])) - 1)
		menu_depth++;

	menu_stack[menu_depth] = page;
	menu_cursor[page] = 0;
	menu_page_no[page] = 0;
	MenuLogEnter(page);
}

static void MenuBack()
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();

	if (menu_depth > 0)
		menu_depth--;

	MenuLogEnter(menu_stack[menu_depth]);
}

// Leaving Options altogether: the next entry starts on the Options page, from
// the title screen and from the pause menu alike. DoOptions() itself is one
// function shared by both callers (menu-map R1), so without this the menu
// would resume wherever the last session left it.
static void MenuLeave()
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();
	menu_depth = 0;
	menu_stack[0] = MENU_PAGE_OPTIONS;
	menu_cursor[MENU_PAGE_OPTIONS] = 0;
	menu_page_no[MENU_PAGE_OPTIONS] = 0;
	menu_waiting_for_key = 0;
	menu_open = 0;
}

/* The continuous word, not the debounced one: a feel slider is meant to be
 * held down, not tapped once. LEFT wins if both ever arrived together, which
 * no hand and no stick can do - base would have applied both and netted zero. */
static long MenuSliderDir()
{
	if (input & IN_LEFT || keymap[DIK_LEFT])
		return -1;

	if (input & IN_RIGHT || keymap[DIK_RIGHT])
		return 1;

	return 0;
}

static long MenuStepDir()
{
	if (dbinput & IN_LEFT)
		return -1;

	if (dbinput & IN_RIGHT)
		return 1;

	return 0;
}

/* Waiting for a key or a button. Carried over from base's DoOptions()
 * (specific\LoadSave.cpp:596-730) with one change: the role is
 * menu_wait_role, so the two shift-then-count blocks that turned the
 * selection bit back into an array index are gone. */
static void MenuCaptureBinding()
{
	long jread, jx, jy, lp, lp2, i;

	if (keymap[DIK_ESCAPE])
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		dbinput = 0;
		menu_waiting_for_key = 0;
		return;
	}

	if (!ControlMethod)
	{
		const char** names = MenuKeyboardNames();

		for (lp = 0; lp < 255; lp++)
		{
			if (keymap[lp] && names[lp])
			{
				// Return and the four arrows are the menu's own keys and
				// must not be bound over the navigation.
				if (lp != DIK_RETURN && lp != DIK_LEFT && lp != DIK_RIGHT && lp != DIK_UP && lp != DIK_DOWN)
				{
					menu_waiting_for_key = 0;
					layout[1][menu_wait_role] = (short)lp;
					menu_game_dirty = 1;
				}
			}
		}
	}

	if (ControlMethod == 1)
	{
		jread = ReadJoystick(jx, jy);

		// Drop the hold guard as soon as the button is actually up, and check
		// that on EVERY frame of the wait - it used to sit inside "if (jread)"
		// below, and jread is zero exactly when the user lets go, so the
		// release was never seen and the Action button could not be bound to
		// itself at all.
		if (!Key(jLayout[4] + 256))
			menu_action_still_held = 0;

		if (jread)
		{
			lp = jread >> 1;
			lp2 = 0;

			while (lp)
			{
				lp >>= 1;
				lp2++;
			}

			// Ignore the button currently bound to Action/Select - it is what
			// the user just pressed to select this row, and is very likely
			// still physically held on this very next frame (this capture
			// reads the raw, non-debounced joystick state). The keyboard loop
			// above excludes DIK_RETURN for exactly the same reason.
			if (lp2 != jLayout[4] || !menu_action_still_held)
			{
				jLayout[menu_wait_role] = lp2;

				// If another action already used this same button or
				// stick direction, unbind it (-1 marks unbound).
				for (i = 0; i < 15; i++)
					if (i != menu_wait_role && jLayout[i] == lp2)
						jLayout[i] = -1;

				menu_waiting_for_key = 0;
				menu_game_dirty = 1;
			}
		}
	}

	CheckKeyConflicts();
	dbinput = 0;
}

/* The three screens that keep their own code: the tomb5 options (until step 2
 * of this round), Special Features and the Credits. They are pages of the tree
 * like any other; only what happens INSIDE them is still theirs. */
static ulong menu_legacy_sel;

static bool MenuRunLegacy(long page)
{
	long legacy_menu;

	legacy_menu = 1;

	if (page == MENU_PAGE_TOMB5)
		TroyeMenu(font_height - (font_height >> 1), legacy_menu, menu_legacy_sel, menu_legacy_sel);
	else if (page == MENU_PAGE_CREDITS)
		CreditsScreen(font_height - (font_height >> 1), legacy_menu, menu_legacy_sel, menu_legacy_sel);
	else if (page == MENU_PAGE_SPECIAL)
		legacy_menu = SpecialFeaturesPageRun() ? 0 : 1;
	else
		return 0;

	if (!legacy_menu)
		MenuBack();

	return 1;
}

static void MenuEnterLegacy(long page)
{
	if (page == MENU_PAGE_TOMB5)
	{
		TroyeMenuPage(0);	// always start on the first page
		// Row 0 of that page, which is what base did: it called
		// TroyeMenuPage for its side effect and set its own sel to 1,
		// discarding the arrow-row selection the call hands back.
		menu_legacy_sel = 1;
	}
	else if (page == MENU_PAGE_SPECIAL)
		CalculateNumSpecialFeatures();
}

void MenuFrameRun()
{
	const menu_page* pg;
	const menu_row* rows;
	const menu_row* row;
	unsigned char enabled[MENU_MAX_ROWS + 2];
	long visible[MENU_MAX_ROWS + 1];
	char buf[256];
	long page, num, fixed, body, body_max, pages, page_no, first, shown;
	long nvis, ncur, cur, i, y, dir;
	uchar col;
	bool slider_page;

	if (!(sfx_backup_flag & 1))
	{
		sfx_backup_flag |= 1;
		sfx_bak = SFXVolume;
	}

	if (!(sfx_backup_flag & 2))
	{
		sfx_backup_flag |= 2;
		sfx_quality_bak = SoundQuality;
	}

	page = menu_stack[menu_depth];

	if (!menu_open)
	{
		menu_open = 1;
		MenuLogEnter(page);
	}

	if (MenuRunLegacy(page))
		return;

	pg = &menu_pages[page];

	if (pg->resolve)
		pg->resolve(&rows, &num);
	else
	{
		rows = pg->rows;
		num = pg->num_rows;
	}

	/* WHICH ROWS THIS PAGE SHOWS. The fixed rows come first and are on every
	 * page of a split list; the rest is paged in balanced halves
	 * (game\menu_nav.h). "Wenn nicht alle Keybinds auf eine Seite passen,
	 * machen wir das wie in den Tomb-Optionen mit mehreren Seiten nach links
	 * und rechts mit den Pfeilen am unteren Bildschirmrand." */
	fixed = pg->fixed_rows;
	body = num - fixed;
	body_max = MENU_MAX_ROWS - fixed;
	pages = MenuPageCount(body, body_max);
	page_no = menu_page_no[page];

	if (page_no >= pages)
		page_no = 0;

	first = MenuPageFirstRow(body, body_max, page_no);
	shown = MenuRowsOnPage(body, body_max, page_no);
	nvis = 0;

	for (i = 0; i < fixed; i++)
		visible[nvis++] = i;

	for (i = 0; i < shown; i++)
		visible[nvis++] = fixed + first + i;

	for (i = 0; i < nvis; i++)
	{
		row = &rows[visible[i]];
		enabled[i] = (unsigned char)(row->enabled ? (row->enabled(row->param) ? 1 : 0) : 1);
	}

	ncur = nvis;

	if (pages > 1)
		enabled[ncur++] = 1;	// the arrow row is a row of the cursor's, as it is in the tomb5 options

	cur = MenuClampCursor(enabled, ncur, menu_cursor[page]);
	menu_cursor[page] = cur;
	menu_page_no[page] = page_no;

	/* ----------------------------------------------------------- draw -- */

	if (pg->title_id >= 0)
		PrintString(phd_centerx, MENU_TITLE_ROW * font_height, 6, SCRIPT_TEXT(pg->title_id), FF_CENTER);
	else
		PrintString(phd_centerx, MENU_TITLE_ROW * font_height, 6, (char*)CustomText(pg->name), FF_CENTER);

	for (i = 0; i < nvis; i++)
	{
		row = &rows[visible[i]];
		y = (MENU_FIRST_ROW + i) * font_height;
		col = (uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 2));
		buf[0] = 0;

		if (row->kind == MROW_TAB || row->kind == MROW_ACTION)
		{
			// Centred: this row opens or does something. That is the whole
			// difference the player is meant to read off the screen.
			if (row->text_id >= 0)
				PrintString(phd_centerx, y, col, SCRIPT_TEXT(row->text_id), FF_CENTER);
			else
				PrintString(phd_centerx, y, col, (char*)CustomText(row->label), FF_CENTER);

			continue;
		}

		if (row->text_id >= 0)
			PrintString(phd_centerx >> 2, y, col, SCRIPT_TEXT(row->text_id), 0);
		else
			PrintString(phd_centerx >> 2, y, col, (char*)CustomText(row->label), 0);

		if (row->kind == MROW_SLIDER)
		{
			// x = 400 and the "+ 4 - (font_height >> 1)" centring term inside
			// a taller text row are the options page's own slider geometry,
			// kept rather than re-derived (specific\LoadSave.cpp:1021 at base).
			DoSlider(400, y + 4 - (font_height >> 1), 200, 16, row->slider ? row->slider(row->param) : 0,
				0x1F1F1F, 0x3F3FFF, cur == i && enabled[i] ? 0x7F7F7F : 0x3F3F3F);
			continue;
		}

		if (row->value)
			PrintString(phd_centerx + (phd_centerx >> 2), y, (uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 6)),
				(char*)row->value(row->param, buf), 0);
	}

	if (pages > 1)
	{
		// One line, pinned to the bottom of the screen whatever the page is
		// as long as. The glyphs are the ones the tomb5 options already page
		// with (tomb5\troyestuff.cpp:120-121).
		col = (uchar)(cur == nvis ? 1 : 6);
		PrintString(phd_centerx - (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x19", 0);
		PrintString(phd_centerx + (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x1B", 0);
	}

	/* ---------------------------------------------------------- drive -- */

	if (menu_waiting_for_key)
		MenuCaptureBinding();	// clears dbinput, so nothing below fires this frame

	if (dbinput & IN_FORWARD)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		cur = MenuCursorStep(enabled, ncur, cur, -1);
	}

	if (dbinput & IN_BACK)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		cur = MenuCursorStep(enabled, ncur, cur, 1);
	}

	if (cur != menu_cursor[page])
	{
		// The cursor left a slider that was still unwritten.
		MenuSettleSlider(page);
		menu_cursor[page] = cur;
	}

	slider_page = 0;

	if (pages > 1 && cur == nvis)
	{
		// The arrow row. Both arrows, both directions, and it wraps.
		dir = MenuStepDir();

		if (dir)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			menu_page_no[page] = MenuPageStep(page_no, pages, dir);
			// Stay on the arrows - the next page may have a different
			// number of rows, so the clamp is asked for the last one
			// rather than a number worked out here.
			menu_cursor[page] = MENU_MAX_ROWS + 1;
		}
	}
	else if (cur >= 0 && cur < nvis && enabled[cur])
	{
		row = &rows[visible[cur]];

		switch (row->kind)
		{
		case MROW_TAB:

			if (dbinput & IN_SELECT)
			{
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
				MenuEnterLegacy(row->target);
				MenuGoTo(row->target);
				return;
			}

			break;

		case MROW_ACTION:

			if (dbinput & IN_SELECT && row->select)
			{
				row->select(row->param);
				menu_game_dirty = 1;	// every action in the tree today changes block B
			}

			break;

		case MROW_BINDING:

			if (dbinput & IN_SELECT && row->select)
				row->select(row->param);

			break;

		case MROW_SLIDER:

			slider_page = 1;

			if (row->step)
			{
				dir = MenuSliderDir();

				if (row->step(row->param, dir))
					menu_slider_unsaved = row;
				else
					MenuSettleSlider(page);
			}

			break;

		case MROW_VALUE:
		default:

			if (row->step)
			{
				buf[0] = 0;

				if (row->step(row->param, MenuStepDir()))
					MenuAfterChange(page, row, row->value ? row->value(row->param, buf) : "");
			}

			break;
		}
	}

	if (!slider_page)
		MenuSettleSlider(page);

	if (dbinput & IN_DESELECT)
	{
		if (menu_depth > 0)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			MenuBack();
			dbinput &= ~IN_DESELECT;
		}
		else
			MenuLeave();	// the caller sees the bit and closes the menu
	}

	if (dbinput & IN_PAUSE)
		MenuLeave();		// the pause menu's own way out, from any depth
}

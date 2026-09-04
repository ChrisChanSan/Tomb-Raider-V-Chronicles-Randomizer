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
#include "LoadSave.h"
#include "../game/text.h"
#include "../game/gameflow.h"
#include "../game/sound.h"
#include "audio.h"
#include "dxsound.h"
#include "input.h"
#include "function_table.h"
#include "drawroom.h"
#include "polyinsert.h"
#include "winmain.h"
#include "output.h"
#include "dxshell.h"
#include "texture.h"
#include "function_stubs.h"
#include "../game/savegame.h"
#include "gamemain.h"
#include "specificfx.h"
#include "time.h"
#include "file.h"
#include "fmv.h"
#include "../game/newinv2.h"
#include "../game/control.h"
#include "3dmath.h"
#include "../tomb5/troyestuff.h"
#include "../tomb5/tomb5.h"
#include "drawbars.h"
#include "hdscreen.h"
#include "bc7.h"

long sfx_frequencies[3] = { 11025, 22050, 44100 };
long SoundQuality = 1;
long MusicVolume = 40;
long SFXVolume = 80;
long ControlMethod;

static DXTEXTURE MonoScreen;
char MonoScreenOn;

// Set while the picture in MonoScreen is an HD one out of screens\HD. The
// picture then occupies only the top left MonoScreenPicW x MonoScreenPicH of
// a square MonoScreenPicSide texture, and is drawn with its own aspect ratio
// instead of being stretched onto the window. See LoadScreenHD().
static char MonoScreenHD;
static long MonoScreenPicW;
static long MonoScreenPicH;
static long MonoScreenPicSide;

static long SpecialFeaturesNum = -1;
static long NumSpecialFeatures;

static LPDIRECT3DSURFACE9 screen_surface;
static SAVEFILE_INFO SaveGames[15];
static char SpecialFeaturesPage[5];

static const char* screen_paths[5] =
{
	"SCREENS\\STORY1.STR",
	"SCREENS\\NXG.STR",
	"SCREENS\\STORY2.STR",
	"SCREENS\\GALLERY.STR",
	"SCREENS\\SCREENS.STR"
};

void CheckKeyConflicts()
{
	short key;

	for (int i = 0; i < 18; i++)
	{
		key = layout[0][i];

		conflict[i] = 0;

		for (int j = 0; j < 18; j++)
		{
			if (key == layout[1][j])
			{
				conflict[i] = 1;
				break;
			}
		}
	}
}

void DisplayStatsUCunt()
{
	long ypos;
	short Days, Hours, Min, Sec;
	char buffer[40];

	ypos = phd_centery - 4 * font_height;
	PrintString(phd_centerx, ypos, 6, SCRIPT_TEXT(TXT_Statistics), FF_CENTER);
	PrintString(phd_centerx, ypos + 2 * font_height, 2, SCRIPT_TEXT(gfLevelNames[gfCurrentLevel]), FF_CENTER);
	PrintString(phd_centerx >> 2, ypos + 3 * font_height, 2, SCRIPT_TEXT(TXT_Time_Taken), 0);
	PrintString(phd_centerx >> 2, ypos + 4 * font_height, 2, SCRIPT_TEXT(TXT_Distance_Travelled), 0);
	PrintString(phd_centerx >> 2, ypos + 5 * font_height, 2, SCRIPT_TEXT(TXT_Ammo_Used), 0);
	PrintString(phd_centerx >> 2, ypos + 6 * font_height, 2, SCRIPT_TEXT(TXT_Health_Packs_Used), 0);
	PrintString(phd_centerx >> 2, ypos + 7 * font_height, 2, SCRIPT_TEXT(TXT_Secrets_Found), 0);

	Sec = short(GameTimer / 30);
	Days = Sec / (24 * 60 * 60);
	Hours = (Sec % (24 * 60 * 60)) / (60 * 60);
	Min = (Sec / 60) % 60;
	Sec = (Sec % 60);

	sprintf(buffer, "%02d:%02d:%02d", (Days * 24) + Hours, Min, Sec);
	PrintString(phd_centerx + (phd_centerx >> 2), ypos + 3 * font_height, 6, buffer, 0);
	sprintf(buffer, "%dm", savegame.Game.Distance / 419);
	PrintString(phd_centerx + (phd_centerx >> 2), ypos + 4 * font_height, 6, buffer, 0);
	sprintf(buffer, "%d", savegame.Game.AmmoUsed);
	PrintString(phd_centerx + (phd_centerx >> 2), ypos + 5 * font_height, 6, buffer, 0);
	sprintf(buffer, "%d", savegame.Game.HealthUsed);
	PrintString(phd_centerx + (phd_centerx >> 2), ypos + 6 * font_height, 6, buffer, 0);
	// Both counts on one line, under the one label that already covers them:
	// the campaign total as before, and the level in brackets behind it.
	// A second row needed a second label, and the language file has no word
	// for it - two integers need none.
	//
	// Nobody knows by heart how many roses each level was built with, and
	// with a rolled level order even a player who does cannot tell which
	// level they are in from its position in the sequence. Where a level
	// holds no secrets at all the bracket is left off, which is also what
	// keeps it off the title screen.
	if (LevelSecretTotal() > 0)
		sprintf(buffer, "%d / 36   (%d / %d)", savegame.Game.Secrets,
				savegame.Level.Secrets, LevelSecretTotal());
	else
		sprintf(buffer, "%d / 36", savegame.Game.Secrets);
	PrintString(phd_centerx + (phd_centerx >> 2), ypos + 7 * font_height, 6, buffer, 0);
}

long DoLoadSave(long LoadSave)
{
	SAVEFILE_INFO* pSave;
	static long selection;
	long txt, l, f;
	uchar color;
	char string[80];
	char name[41];

	if (LoadSave & IN_SAVE)
		txt = TXT_Save_Game;
	else
		txt = TXT_Load_Game;

	f = font_height;
	PrintString(phd_centerx, f, 6, SCRIPT_TEXT(txt), FF_CENTER);
	f += font_height;
	f += font_height >> 1;

	for (int i = 0; i < 15; i++)
	{
		pSave = &SaveGames[i];
		color = 2;

		if (i == selection)
			color = 1;

		memset(name, ' ', 40);
		l = strlen(pSave->name);

		if (l > 40)
			l = 40;

		strncpy(name, pSave->name, l);
		name[40] = 0;

		if (pSave->valid)
		{
			wsprintf(string, "%03d", pSave->num);
			PrintString(GetFixedScale(10), f, color, string, 0);
			PrintString(GetFixedScale(60), f, color, name, 0);
			wsprintf(string, "%d %s %02d:%02d:%02d", pSave->days, SCRIPT_TEXT(TXT_days), pSave->hours, pSave->minutes, pSave->seconds);
			PrintString(phd_centerx - long((float)phd_winwidth / 640.0F * -135.0), f, color, string, 0);
		}
		else
		{
			wsprintf(string, "%s", pSave->name);
			PrintString(phd_centerx, f, color, string, FF_CENTER);
		}

		f += font_height;
	}

	if (dbinput & IN_FORWARD)
	{
		selection--;
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_DEFAULT);
	}

	if (dbinput & IN_BACK)
	{
		selection++;
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_DEFAULT);
	}

	if (selection < 0)
		selection = 0;

	if (selection > 14)
		selection = 14;

	if (dbinput & IN_SELECT)
	{
		if (SaveGames[selection].valid || LoadSave == IN_SAVE)
			return selection;
		else
			SoundEffect(SFX_LARA_NO, 0, SFX_DEFAULT);
	}

	return -1;
}

long GetCampaignCheatValue()
{
	static long counter = 0;
	static long timer;
	long jump;

	if (timer)
		timer--;
	else
		counter = 0;

	jump = 0;

	switch (counter)
	{
	case 0:

		if (keymap[DIK_F])
		{
			timer = 450;
			counter = 1;
		}

		break;

	case 1:

		if (keymap[DIK_I])
			counter = 2;

		break;

	case 2:

		if (keymap[DIK_L])
			counter = 3;

		break;

	case 3:
		if (keymap[DIK_T])
			counter = 4;

		break;

	case 4:

		if (keymap[DIK_H])
			counter = 5;

		break;

	case 5:

		if (keymap[DIK_Y])
			counter = 6;

		break;

	case 6:

		if (keymap[DIK_1])
			jump = LVL5_STREETS_OF_ROME;

		if (keymap[DIK_2])
			jump = LVL5_BASE;

		if (keymap[DIK_3])
			jump = LVL5_GALLOWS_TREE;

		if (keymap[DIK_4])
			jump = LVL5_THIRTEENTH_FLOOR;

		if (jump)
		{
			counter = 0;
			timer = 0;
		}

		break;
	}

	return jump;
}

void DoOptions()
{
	const char** keyboard_buttons;
	static long menu;	//0: options, 1: controls, 100: special features, 200: tomb5 options, 201: credits
	static ulong sel = 1;	//selection
	static ulong last_sel;
	static ulong sel2;		//selection for when mapping keys
	static long mSliderCol = 0x3F3F3F;
	static long sSliderCol = 0x3F3F3F;
	static long sfx_bak;
	static long sfx_quality_bak;
	static long sfx_breath_db = -1;
	ulong nMask;
	long f, y, i, SFMask, jread, jx, jy, lp, lp2;
	const char* text;
	char buf[256];
	static char sfx_backup_flag;	//have we backed sfx stuff up?
	static bool waiting_for_key = 0;
	static bool action_still_held = 0;	// the action button still pinned the row selection when waiting_for_key was entered
	// The reset position of the method row is a MENU ACTION, not an input
	// mode: ControlMethod stays untouched while paging. The old
	// ControlMethod=2 switched off ALL pad processing (everything hangs on
	// "ControlMethod == 1") - on the reset position the controller was
	// thereby systemically dead, only the keyboard came back.
	static bool reset_row = 0;

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

	f = font_height - 4;

	if (menu == 1)	//controls menu
	{
		if (Gameflow->Language == 2)
			keyboard_buttons = GermanKeyboard;
		else
			keyboard_buttons = KeyboardButtons;

		if (reset_row)
			nMask = 1;	// only the Method row itself can be selected
		else if (ControlMethod == 1)
			nMask = 16;	// 15 bindable actions: Duck..Inventory (10) + Pause, Save, Load, Small Medi, Large Medi
		else
			nMask = 17;

		PrintString(phd_centerx >> 2, font_height, sel & 1 ? 1 : 2, SCRIPT_TEXT(TXT_Control_Method), 0);

		font_height = GetFixedScale(27);

		if (!ControlMethod)
		{
			y = 2;
			i = 1;

			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, "\x18", 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, "\x1A", 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, "\x19", 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, "\x1B", 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Duck), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Dash), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Walk), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Jump), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Action), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Draw_Weapon), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Use_Flare), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Look), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Roll), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Inventory), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Step_Left), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Step_Right), 0);

			y = 2;
			i = 1;

			for (lp = 0; lp < 16; lp++)
			{
				text = (waiting_for_key && (sel2 & (1 << i))) ? SCRIPT_TEXT(TXT_Waiting) : keyboard_buttons[layout[1][lp]];
				PrintString(phd_centerx + (phd_centerx >> 2), y++ * font_height, sel2 & (1 << i++) ? 1 : 6, text, 0);
			}
		}

		if (ControlMethod == 1)
		{
			y = 3;
			i = 1;

			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Duck), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Dash), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Walk), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Jump), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Action), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Draw_Weapon), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Use_Flare), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Look), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Roll), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Inventory), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, (char*)CustomText("Pause"), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Save), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Load), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Small_Medipack), 0);
			PrintString(phd_centerx >> 2, y++ * font_height, sel & (1 << i++) ? 1 : 2, SCRIPT_TEXT(TXT_Large_Medipack), 0);

			// The keyboard-equivalent-key hints that used to be printed here (one
			// column further right, e.g. "(Space)" next to "Jump") were removed -
			// they cluttered/overlapped the joystick binding names next to them
			// (worst with the longer "Stick R Left/Right" labels) and this screen
			// is specifically for joystick bindings, so a keyboard hint isn't
			// actually useful here.

			y = 3;
			i = 1;

			for (lp = 0; lp < 15; lp++)
			{
				if (waiting_for_key && (sel2 & (1 << i)))
					text = SCRIPT_TEXT(TXT_Waiting);
				else if (jLayout[lp] == -1)
					text = "---";
				else
					text = JoyButtonName(jLayout[lp]);
				PrintString(phd_centerx + (phd_centerx >> 2), y++ * font_height, (waiting_for_key && (sel & (1 << i++))) ? 1 : 6, text, 0);
			}
		}

		font_height = default_font_height;

		if (reset_row)
			PrintString(phd_centerx + (phd_centerx >> 2), font_height, sel2 & 1 ? 1 : 6, SCRIPT_TEXT(TXT_Reset), 0);
		else if (!ControlMethod)
			PrintString(phd_centerx + (phd_centerx >> 2), font_height, sel2 & 1 ? 1 : 6, SCRIPT_TEXT(TXT_Keyboard), 0);
		else if (ControlMethod == 1)
		{
			// The joystick row carries the chosen page's name. The page
			// only changes the DISPLAY of the button names (and the
			// defaults on reset) - the bit order comes from the active
			// input path, see input.cpp.
			switch (ActiveJoyStyle())
			{
			case JOYSTYLE_PS5:
				text = (char*)"PS5 Controller";
				break;
			case JOYSTYLE_XBOX:
				text = (char*)"Xbox Controller";
				break;
			default:
				text = SCRIPT_TEXT(TXT_Joystick);
				break;
			}

			PrintString(phd_centerx + (phd_centerx >> 2), font_height, sel2 & 1 ? 1 : 6, text, 0);
		}

		if (!reset_row && !waiting_for_key)
		{
			if (dbinput & IN_FORWARD)
			{
				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
				sel >>= 1;
			}

			if (dbinput & IN_BACK)
			{
				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
				sel <<= 1;
			}
		}

		if (waiting_for_key)
		{
			i = 0;

			if (keymap[DIK_ESCAPE])
			{
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
				sel2 = 0;
				dbinput = 0;
				waiting_for_key = 0;
				return;
			}

			if (!ControlMethod)
			{
				for (lp = 0; lp < 255; lp++)
				{
					if (keymap[lp] && keyboard_buttons[lp])
					{
						if (lp != DIK_RETURN && lp != DIK_LEFT && lp != DIK_RIGHT && lp != DIK_UP && lp != DIK_DOWN)
						{
							waiting_for_key = 0;

							sel2 >>= 2;

							while (sel2)
							{
								i++;
								sel2 >>= 1;
							}

							sel2 = 0;
							layout[1][i] = (short)lp;
						}
					}
				}
			}

			if (ControlMethod == 1)
			{
				jread = ReadJoystick(jx, jy);

				// Drop the hold guard as soon as the button is actually up, and
				// check that on EVERY frame of the wait.
				//
				// It used to sit inside "if (jread)" below - and jread is zero
				// exactly when the user lets go. So the release was never seen,
				// action_still_held stayed armed for good, and the Action button
				// could no longer be bound to itself at all: the row went into
				// Waiting and stayed there, whatever you pressed (reported in
				// game). The guard against the press that OPENED the wait is
				// still right; it just has to end when that press does.
				if (!Key(jLayout[4] + 256))
					action_still_held = 0;

				if (jread)
				{
					lp = sel >> 2;
					lp2 = 0;
					i = 0;

					while (lp)
					{
						lp >>= 1;
						i++;
					}

					lp = jread >> 1;

					while (lp)
					{
						lp >>= 1;
						lp2++;
					}

					// Ignore the button currently bound to Action/Select (jLayout[4]) -
					// it's what the user just pressed to select this row and enter
					// waiting_for_key in the first place, and is very likely still
					// physically held on this very next frame (dbinput's edge-detect
					// only guards the *selection* press, not this capture read, which
					// reads the raw, non-debounced joystick state). Without this, that
					// same still-held press immediately re-captures itself as the new
					// bind before the user can press anything else - this is the exact
					// joystick equivalent of the keyboard capture loop below excluding
					// DIK_RETURN for the same reason.
					// The guard is armed only while that selection press is still
					// physically down (action_still_held); it is dropped above, the
					// moment the button is really up.
					if (lp2 != jLayout[4] || !action_still_held)
					{
						jLayout[i] = lp2;

						// If another action already used this same button/stick-direction,
						// unbind it (jLayout entries have no "empty" state otherwise, so -1
						// marks unbound - see Key()'s explicit check for the encoded value
						// 255, and JoystickButtons' NULL check below).
						for (int k = 0; k < 15; k++)
							if (k != i && jLayout[k] == lp2)
								jLayout[k] = -1;

						waiting_for_key = 0;
					}
				}
			}

			CheckKeyConflicts();
			dbinput = 0;
		}

		if (dbinput & IN_SELECT && sel > 1 && !reset_row)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			sel2 = sel;
			waiting_for_key = 1;
			// The hold guard below (do not immediately capture the action
			// button as the new binding) only applies when the row was
			// REALLY selected with the pad button. If the selection came
			// from the keyboard (Enter), the guard used to PERMANENTLY
			// block any attempt to bind the action button itself - only
			// Esc got you out (the Xbox tester's finding).
			action_still_held = ControlMethod == 1 && Key(jLayout[4] + 256);
			memset(keymap, 0, sizeof(keymap));
		}

		if (dbinput & IN_SELECT && (sel & 1) && reset_row)
		{
			SoundEffect(SFX_MENU_SELECT, 0, 2);
			memcpy(layout[1], layout, 72);
			ApplyJoyDefaults();	// defaults matching page AND input path (input.cpp) - before, a fixed table in DualSense bit order, nonsense on Xbox pads
			// ControlMethod stays what it was (the original jumped hard
			// back to keyboard - for a controller user that meant: pad
			// dead after the reset). The row shows the method again.
			reset_row = 0;
		}

		if (sel & 1)
		{
			// Five visible positions: keyboard, PS5 controller, Xbox
			// controller, joystick (generic), reset. Internally
			// ControlMethod stays 0/1 (keyboard/joystick) - the three
			// controller positions are method 1 with JoyStyle 0/1/2, so
			// every existing "ControlMethod == 1" check in the game keeps
			// running unchanged. The reset position is only the local
			// reset_row flag (see above) and leaves the method - and with
			// it the pad - alone.
			long pos, padStep;

			if (reset_row)
				pos = 4;
			else if (!ControlMethod)
				pos = 0;
			else
				pos = 1 + ActiveJoyStyle();

			// On the keyboard page the pad delivers no direction bits at all -
			// every use of the stick in input.cpp hangs on ControlMethod - so
			// whoever paged over here with the controller could not page back
			// with it, and only the arrow keys still worked. This row IS the
			// setting, which makes it a trap: the pad cannot undo what the pad
			// just did.
			//
			// So the stick is read directly here - on BOTH pages, not only the
			// keyboard one. It has to be both, or one deflection counts twice:
			// JoyMenuStep takes the step on the keyboard page, ControlMethod
			// flips to the pad, and in the next frame the direction still being
			// held reaches dbinput for the first time, which its edge detection
			// reads as a fresh press. One push, two pages (seen in game).
			//
			// While the stick is deflected the step therefore belongs to
			// JoyMenuStep alone and dbinput is ignored on this row. The arrow
			// keys keep working through dbinput whenever the stick rests.
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

			if (pos > 4)
				pos = 4;

			if (pos < 0)
				pos = 0;

			reset_row = pos == 4;

			if (!pos)
				ControlMethod = 0;
			else if (pos < 4)
			{
				ControlMethod = 1;
				JoyStyle = pos - 1;	// an explicit choice from now on, not automatic
			}
		}

		if (!sel)
			sel = 1;

		if (sel > ulong(1 << (nMask - 1)))
			sel = 1 << (nMask - 1);

		if (dbinput & IN_DESELECT)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);

			if (reset_row)
				reset_row = 0;	// back from the reset position: onto the method first, not straight out of the menu
			else
				menu = 0;

			dbinput = 0;
			sel = 1;
		}
	}
	else if (menu == 100)	//special features
	{
		PrintString(phd_centerx, f + 3 * font_height, 6, SCRIPT_TEXT(TXT_Special_Features), FF_CENTER);
		PrintString(phd_centerx, f + 5 * font_height, SpecialFeaturesPage[0] ? (sel & 1 ? 1 : 2) : 3, SCRIPT_TEXT(TXT_Storyboards_Part_1), FF_CENTER);
		PrintString(phd_centerx, f + 6 * font_height, SpecialFeaturesPage[1] ? (sel & 2 ? 1 : 2) : 3, SCRIPT_TEXT(TXT_Next_Generation_Concept), FF_CENTER);
		PrintString(phd_centerx, f + 7 * font_height, SpecialFeaturesPage[2] ? (sel & 4 ? 1 : 2) : 3, SCRIPT_TEXT(TXT_Storyboards_Part_2), FF_CENTER);
		PrintString(phd_centerx, f + 8 * font_height, SpecialFeaturesPage[3] ? (sel & 8 ? 1 : 2) : 3, (char*)CustomText("Gallery"), FF_CENTER);

		if (NumSpecialFeatures)
		{
			if (dbinput & IN_FORWARD)
			{
				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
				sel = FindSFCursor(1, sel);
			}

			if (dbinput & IN_BACK)
			{
				SoundEffect(SFX_MENU_CHOOSE, 0, 2);
				sel = FindSFCursor(2, sel);
			}

			if (!sel)
				sel = 1;
			else if (sel > 8)
				sel = 8;

			if (dbinput & IN_SELECT)
			{
				if (sel & 1)
					SpecialFeaturesNum = 0;

				if (sel & 2)
					SpecialFeaturesNum = 1;

				if (sel & 4)
					SpecialFeaturesNum = 2;

				if (sel & 8)
					SpecialFeaturesNum = 3;

				if (sel & 0x10)
					SpecialFeaturesNum = 4;
			}
		}

		if (dbinput & IN_DESELECT)
		{
			menu = 0;
			sel = last_sel;
			dbinput &= ~IN_DESELECT;
		}
	}
	else if (menu == 0)	//main options menu
	{
		f= 3 * font_height;
		nMask = 8;	// + Language row + Credits
		PrintString(phd_centerx, 3 * font_height, 6, SCRIPT_TEXT(TXT_Options), FF_CENTER);
		PrintString(phd_centerx, f + font_height + (font_height >> 1), sel & 1 ? 1 : 2, SCRIPT_TEXT(TXT_Control_Configuration), FF_CENTER);
		PrintString(phd_centerx >> 2, f + 3 * font_height, sel & 2 ? 1 : 2, SCRIPT_TEXT(TXT_Music_Volume), 0);
		PrintString(phd_centerx >> 2, f + 4 * font_height, sel & 4 ? 1 : 2, SCRIPT_TEXT(TXT_SFX_Volume), 0);
		PrintString(phd_centerx >> 2, f + 5 * font_height, sel & 8 ? 1 : 2, SCRIPT_TEXT(TXT_Sound_Quality), 0);
		PrintString(phd_centerx >> 2, f + 6 * font_height, sel & 0x10 ? 1 : 2, SCRIPT_TEXT(TXT_Targeting), 0);
		DoSlider(400, f + 3 * font_height + 4 - (font_height >> 1), 200, 16, MusicVolume, 0x1F1F1F, 0x3F3FFF, mSliderCol);
		DoSlider(400, f + 4 * font_height + 4 - (font_height >> 1), 200, 16, SFXVolume, 0x1F1F1F, 0x3F3FFF, sSliderCol);

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

		PrintString(phd_centerx + (phd_centerx >> 2), f + 5 * font_height, sel & 8 ? 1 : 6, buf, 0);

		if (App.AutoTarget)
			strcpy(buf, SCRIPT_TEXT(TXT_Automatic));
		else
			strcpy(buf, SCRIPT_TEXT(TXT_Manual));

		PrintString(phd_centerx + (phd_centerx >> 2), f + 6 * font_height, sel & 0x10 ? 1 : 6, buf, 0);

		// Language choice: switches live to another of the language files
		// lying next to the EXE (gameflow.cpp). "Language" has no string ID
		// in the language wad, hence a literal - like "tomb5 options" below.
		PrintString(phd_centerx >> 2, f + 7 * font_height, sel & 0x20 ? 1 : 2, (char*)CustomText("Language"), 0);
		PrintString(phd_centerx + (phd_centerx >> 2), f + 7 * font_height, sel & 0x20 ? 1 : 6, (char*)LanguageName(Gameflow->Language), 0);

		SFMask = 0x40;

		if (gfGameMode == 1)
		{
			nMask = 9;
			PrintString(phd_centerx, (font_height >> 1) + f + 8 * font_height, sel & 0x40 ? 1 : 2, SCRIPT_TEXT(TXT_Special_Features), FF_CENTER);
		}
		else
			SFMask = 0;

		if (SFMask)
		{
			PrintString(phd_centerx, (font_height >> 1) + f + 9 * font_height, sel & 0x80 ? 1 : 2, (char*)CustomText("tomb5 options"), FF_CENTER);
			PrintString(phd_centerx, (font_height >> 1) + f + 10 * font_height, sel & 0x100 ? 1 : 2, (char*)CustomText("Credits"), FF_CENTER);
		}
		else
		{
			PrintString(phd_centerx, (font_height >> 1) + f + 8 * font_height, sel & 0x40 ? 1 : 2, (char*)CustomText("tomb5 options"), FF_CENTER);
			PrintString(phd_centerx, (font_height >> 1) + f + 9 * font_height, sel & 0x80 ? 1 : 2, (char*)CustomText("Credits"), FF_CENTER);
		}

		if (dbinput & IN_FORWARD)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
			sel >>= 1;
		}

		if (dbinput & IN_BACK)
		{
			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
			sel <<= 1;
		}

		if (dbinput & IN_SELECT && sel & 1)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			menu = 1;
		}

		i = !SFMask ? 0x40 : 0x80;	// the "tomb5 options" bit, shifted by the Language row

		if (dbinput & IN_SELECT && sel & (ulong)i)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			last_sel = sel;
			sel = 1;
			TroyeMenuPage(0);	// always start on the first page
			menu = 200;
		}

		// Credits (#33 addendum, user wish): its own row right under
		// "tomb5 options" - as the tomb5 menu's third page alone it was
		// too hidden. Its OWN screen (menu 201), not a page of the tomb5
		// options: as a page it turned up while paging through the
		// settings and the paging arrows were drawn across its text.
		i = !SFMask ? 0x80 : 0x100;

		if (dbinput & IN_SELECT && sel & (ulong)i)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			last_sel = sel;
			menu = 201;
		}

		if (!sel)
			sel = 1;

		if (sel > ulong(1 << (nMask - 1)))
			sel = 1 << (nMask - 1);

		mSliderCol = 0x3F3F3F;
		sSliderCol = 0x3F3F3F;

		if (sel & 2)
		{
			sfx_bak = SFXVolume;

			if (input & IN_LEFT || keymap[DIK_LEFT])
				MusicVolume--;

			if (input & IN_RIGHT || keymap[DIK_RIGHT])
				MusicVolume++;

			if (MusicVolume > 100)
				MusicVolume = 100;

			if (MusicVolume < 0)
				MusicVolume = 0;

			sSliderCol = 0x3F3F3F;
			mSliderCol = 0x7F7F7F;
			ACMSetVolume();
		}
		else if (sel & 4)
		{
			if (input & IN_LEFT || keymap[DIK_LEFT])
				SFXVolume--;

			if (input & IN_RIGHT || keymap[DIK_RIGHT])
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

			mSliderCol = 0x3F3F3F;
			sSliderCol = 0x7F7F7F;
		}
		else if (sel & 8)
		{
			sfx_bak = SFXVolume;
			
			if (dbinput & IN_LEFT)
				SoundQuality--;

			if (dbinput & IN_RIGHT)
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
		}
		else if (sel & 0x10)
		{
			if (dbinput & IN_LEFT)
			{
				if (App.AutoTarget)
					App.AutoTarget = 0;

				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			}

			if (dbinput & IN_RIGHT)
			{
				if (!App.AutoTarget)
					App.AutoTarget = 1;

				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			}

			savegame.AutoTarget = (uchar)App.AutoTarget;
		}
		else if (sel & 0x20)
		{
			long direction = 0;

			if (dbinput & IN_LEFT)
				direction = -1;

			if (dbinput & IN_RIGHT)
				direction = 1;

			if (direction)
			{
				// The next EXISTING language file in the chosen direction - missing
				// ones are skipped (SwitchLanguage would fail silently otherwise).
				for (long n = Gameflow->Language + direction; n >= 0 && n < gfLanguageCount; n += direction)
				{
					if (SwitchLanguage(n))
					{
						LanguageChoice = n;	// an explicit choice from now on, registry "Language"
						SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
						break;
					}
				}
			}
		}
		else if (sel & SFMask && dbinput & IN_SELECT)
		{
			CalculateNumSpecialFeatures();
			last_sel = sel;
			sel = 1;
			menu = 100;
		}
	}
	else if (menu == 200)
		TroyeMenu(font_height - (font_height >> 1), menu, sel, last_sel);
	else if (menu == 201)
		CreditsScreen(font_height - (font_height >> 1), menu, sel, last_sel);
}

void CreateMonoScreen()
{
	LPDIRECT3DSURFACE9 back, sysMem;
	D3DSURFACE_DESC desc;

	MonoScreenOn = 1;

	if (FAILED(App.dx.lpD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return;

	// A device back buffer isn't directly lockable in D3D9 (unlike the old DirectDraw
	// primary/back surfaces) - copy it into a system-memory surface first via
	// GetRenderTargetData, then lock that instead.
	back->GetDesc(&desc);

	if (SUCCEEDED(App.dx.lpD3DDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sysMem, 0)))
	{
		if (SUCCEEDED(App.dx.lpD3DDevice->GetRenderTargetData(back, sysMem)))
			ConvertSurfaceToTextures(sysMem);

		sysMem->Release();
	}

	back->Release();
}

void RGBM_Mono(uchar* r, uchar* g, uchar* b)
{
	uchar c;

	if (MonoScreenOn == 2)
		return;

	if (tomb5.inv_bg_mode == 3)
		return;

	c = (*r + *b) >> 1;
	*r = c;
	*g = c;
	*b = c;
}

static void BitMaskGetNumberOfBits(ulong bitMask, ulong* bitDepth, ulong* bitOffset)
{
	long i;

	if (!bitMask)
	{
		*bitOffset = 0;
		*bitDepth = 0;
		return;
	}

	for (i = 0; !(bitMask & 1); i++)
		bitMask >>= 1;

	*bitOffset = i;

	for (i = 0; bitMask != 0; i++)
		bitMask >>= 1;

	*bitDepth = i;
}

// D3D9 surfaces don't expose a DDPIXELFORMAT-style bitmask description the way DirectDraw
// surfaces did - GetDesc() only reports a D3DFORMAT enum. Since every surface that actually
// flows through here is one of exactly two known formats (the game's own X8R8G8B8 back
// buffer, or the R5G6B5 offscreen surface LoadScreen() builds itself), a tiny format->masks
// lookup replaces the old generic-any-DirectDraw-format runtime introspection.
struct BLTSURF
{
	void* pBits;
	long Pitch;
	ulong Bpp;
	ulong RMask, GMask, BMask, AMask;
};

static void FormatToMasks(D3DFORMAT format, BLTSURF* s)
{
	switch (format)
	{
	case D3DFMT_R5G6B5:
		s->Bpp = 2;
		s->RMask = 0xF800;
		s->GMask = 0x07E0;
		s->BMask = 0x001F;
		s->AMask = 0;
		break;

	default:	// D3DFMT_X8R8G8B8 / D3DFMT_A8R8G8B8
		s->Bpp = 4;
		s->RMask = 0x00FF0000;
		s->GMask = 0x0000FF00;
		s->BMask = 0x000000FF;
		s->AMask = 0xFF000000;
		break;
	}
}

static void WinVidGetColorBitMasks(COLOR_BIT_MASKS* bm, ulong rMask, ulong gMask, ulong bMask, ulong aMask)
{
	bm->dwRBitMask = rMask;
	bm->dwGBitMask = gMask;
	bm->dwBBitMask = bMask;
	bm->dwRGBAlphaBitMask = aMask;

	BitMaskGetNumberOfBits(bm->dwRBitMask, &bm->dwRBitDepth, &bm->dwRBitOffset);
	BitMaskGetNumberOfBits(bm->dwGBitMask, &bm->dwGBitDepth, &bm->dwGBitOffset);
	BitMaskGetNumberOfBits(bm->dwBBitMask, &bm->dwBBitDepth, &bm->dwBBitOffset);
	BitMaskGetNumberOfBits(bm->dwRGBAlphaBitMask, &bm->dwRGBAlphaBitDepth, &bm->dwRGBAlphaBitOffset);
}

static void CustomBlt(BLTSURF* dst, ulong dstX, ulong dstY, BLTSURF* src, LPRECT srcRect)
{
	COLOR_BIT_MASKS srcMask, dstMask;
	uchar* srcLine;
	uchar* dstLine;
	uchar* srcPtr;
	uchar* dstPtr;
	ulong srcX, srcY, width, height, srcBpp, dstBpp, color, high, low, r, g, b;

	srcX = srcRect->left;
	srcY = srcRect->top;
	width = srcRect->right - srcRect->left;
	height = srcRect->bottom - srcRect->top;
	srcBpp = src->Bpp;
	dstBpp = dst->Bpp;
	WinVidGetColorBitMasks(&srcMask, src->RMask, src->GMask, src->BMask, src->AMask);
	WinVidGetColorBitMasks(&dstMask, dst->RMask, dst->GMask, dst->BMask, dst->AMask);
	srcLine = (uchar*)src->pBits + srcY * src->Pitch + srcX * srcBpp;
	dstLine = (uchar*)dst->pBits + dstY * dst->Pitch + dstX * dstBpp;

	for (ulong j = 0; j < height; j++)
	{
		srcPtr = srcLine;
		dstPtr = dstLine;

		for (ulong i = 0; i < width; i++)
		{
			color = 0;
			memcpy(&color, srcPtr, srcBpp);
			r = ((color & srcMask.dwRBitMask) >> srcMask.dwRBitOffset);
			g = ((color & srcMask.dwGBitMask) >> srcMask.dwGBitOffset);
			b = ((color & srcMask.dwBBitMask) >> srcMask.dwBBitOffset);

			if (srcMask.dwRBitDepth < dstMask.dwRBitDepth)
			{
				high = dstMask.dwRBitDepth - srcMask.dwRBitDepth;
				low = (srcMask.dwRBitDepth > high) ? srcMask.dwRBitDepth - high : 0;
				r = (r << high) | (r >> low);
			}
			else if (srcMask.dwRBitDepth > dstMask.dwRBitDepth)
				r >>= srcMask.dwRBitDepth - dstMask.dwRBitDepth;

			if (srcMask.dwGBitDepth < dstMask.dwGBitDepth)
			{
				high = dstMask.dwGBitDepth - srcMask.dwGBitDepth;
				low = (srcMask.dwGBitDepth > high) ? srcMask.dwGBitDepth - high : 0;
				g = (g << high) | (g >> low);
			}
			else if (srcMask.dwGBitDepth > dstMask.dwGBitDepth)
				g >>= srcMask.dwGBitDepth - dstMask.dwGBitDepth;


			if (srcMask.dwBBitDepth < dstMask.dwBBitDepth)
			{
				high = dstMask.dwBBitDepth - srcMask.dwBBitDepth;
				low = (srcMask.dwBBitDepth > high) ? srcMask.dwBBitDepth - high : 0;
				b = (b << high) | (b >> low);
			}
			else if (srcMask.dwBBitDepth > dstMask.dwBBitDepth)
				b >>= srcMask.dwBBitDepth - dstMask.dwBBitDepth;

			RGBM_Mono((uchar*)&r, (uchar*)&g, (uchar*)&b);
			color = dst->AMask; // destination is opaque
			color |= r << dstMask.dwRBitOffset;
			color |= g << dstMask.dwGBitOffset;
			color |= b << dstMask.dwBBitOffset;
			memcpy(dstPtr, &color, dstBpp);
			srcPtr += srcBpp;
			dstPtr += dstBpp;
		}

		srcLine += src->Pitch;
		dstLine += dst->Pitch;
	}
}

void ConvertSurfaceToTextures(LPDIRECT3DSURFACE9 surface)
{
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT srcRect, dstRect;
	BLTSURF src, dst;
	RECT r;
	LPDIRECT3DTEXTURE9 tex9;

	surface->GetDesc(&desc);
	surface->LockRect(&srcRect, 0, 0);
	tex9 = CreateTexturePage(desc.Width, desc.Height, 0, NULL, RGBM_Mono, -1);

	if (!tex9)
	{
		surface->UnlockRect();
		return;
	}

	tex9->LockRect(0, &dstRect, 0, 0);

	FormatToMasks(desc.Format, &src);
	src.pBits = srcRect.pBits;
	src.Pitch = srcRect.Pitch;

	FormatToMasks(D3DFMT_A8R8G8B8, &dst);
	dst.pBits = dstRect.pBits;
	dst.Pitch = dstRect.Pitch;

	r.left = 0;
	r.top = 0;
	r.right = desc.Width;
	r.bottom = desc.Height;
	CustomBlt(&dst, 0, 0, &src, &r);

	tex9->UnlockRect(0);
	surface->UnlockRect();

	if (MonoScreen.tex)
		MonoScreen.tex->Release();

	MonoScreen.tex = tex9;
	MonoScreenHD = 0;	// whatever came in here fills the window, as before
}

void FreeMonoScreen()
{
	if (MonoScreenOn == 1)
	{
		if (MonoScreen.tex)
		{
			Log("Released %s @ %x - RefCnt = %d", "Mono Screen Texture", MonoScreen.tex, MonoScreen.tex->Release());
			MonoScreen.tex = 0;
		}
		else
			Log("%s Attempt To Release NULL Ptr", "Mono Screen Texture");
	}

	MonoScreenOn = 0;
}

// filter 0 keeps the original point sampling, which is what a 640x480 picture
// blown up to the window wants. filter 1 asks for bilinear and is used by the
// HD pictures, where the scale factor is near 1 and point sampling would show
// as uneven pixel doubling or as aliasing, depending on the window size.
void S_DrawTile(long x, long y, long w, long h, LPDIRECT3DTEXTURE9 t, long tU, long tV, long tW, long tH, long c0, long c1, long c2, long c3, long filter)
{
	D3DTLBUMPVERTEX v[4];
	D3DTLBUMPVERTEX tri[3];
	float u1, v1, u2, v2;

	u1 = float(tU * (1.0F / 256.0F));
	v1 = float(tV * (1.0F / 256.0F));
	u2 = float((tW + tU) * (1.0F / 256.0F));
	v2 = float((tH + tV) * (1.0F / 256.0F));

	v[0].sx = (float)x;
	v[0].sy = (float)y;
	v[0].sz = 0.995F;
	v[0].tu = u1;
	v[0].tv = v1;
	v[0].rhw = 1;
	v[0].color = c0;
	v[0].specular = 0xFF000000;

	v[1].sx = float(w + x);
	v[1].sy = (float)y;
	v[1].sz = 0.995F;
	v[1].tu = u2;
	v[1].tv = v1;
	v[1].rhw = 1;
	v[1].color = c1;
	v[1].specular = 0xFF000000;

	v[2].sx = float(w + x);
	v[2].sy = float(h + y);
	v[2].sz = 0.995F;
	v[2].tu = u2;
	v[2].tv = v2;
	v[2].rhw = 1;
	v[2].color = c3;
	v[2].specular = 0xFF000000;

	v[3].sx = (float)x;
	v[3].sy = float(h + y);
	v[3].sz = 0.995F;
	v[3].tu = u1;
	v[3].tv = v2;
	v[3].rhw = 1;
	v[3].color = c2;
	v[3].specular = 0xFF000000;

	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, filter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MINFILTER, filter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	DXAttempt(App.dx.lpD3DDevice->SetTexture(0, t));
	tri[0] = v[0];
	tri[1] = v[2];
	tri[2] = v[3];
	HWDrawPrimitive(D3DPT_TRIANGLELIST, v, 3);
	HWDrawPrimitive(D3DPT_TRIANGLELIST, tri, 3);

	// Put the sampler back the way the rest of the frame expects it. The
	// original only did this when App.Filtering was on, which was the same
	// thing as long as the tile itself was always drawn with POINT.
	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, App.Filtering ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MINFILTER, App.Filtering ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

void S_DisplayMonoScreen()
{
	ulong col;
	long x, y, w, h;

	if (MonoScreenOn == 1 || MonoScreenOn == 2)
	{
		if (MonoScreenOn == 2)	//pictures always the same!!
			col = 0xFFFFFFFF;
		else
		{
			if (tomb5.inv_bg_mode == 2 || tomb5.inv_bg_mode == 3)
				col = 0xFFFFFFFF;
			else
				col = 0xFFFFFF80;
		}

		if (MonoScreenHD)
		{
			// The 640x480 pictures are stretched onto the window and always
			// were, distortion included. The HD ones are 16:9 and are fitted
			// instead: on the 16:9 window they end up pixel for pixel, and on
			// anything else they get bars rather than a squeeze. The bars
			// have to be painted, since nothing clears the frame before the
			// loading screen goes down.
			w = phd_winxmax;
			h = phd_winymax;

			if ((long)phd_winxmax * MonoScreenPicH > (long)phd_winymax * MonoScreenPicW)
				w = (long)phd_winymax * MonoScreenPicW / MonoScreenPicH;
			else
				h = (long)phd_winxmax * MonoScreenPicH / MonoScreenPicW;

			x = (phd_winxmax - w) >> 1;
			y = (phd_winymax - h) >> 1;

			if (x || y)
				App.dx.lpD3DDevice->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0F, 0);

			S_DrawTile(x, y, w, h, MonoScreen.tex, 0, 0,
				256 * MonoScreenPicW / MonoScreenPicSide,
				256 * MonoScreenPicH / MonoScreenPicSide, col, col, col, col, 1);
		}
		else
			S_DrawTile(0, 0, phd_winxmax, phd_winymax, MonoScreen.tex, 0, 0, 256, 256, col, col, col, col, 0);
	}
}

long S_LoadSave(long load_or_save, long mono, long inv_active)
{
	long fade, ret;
	uchar deselect_debounce;

	fade = 0;
	deselect_debounce = 0;

	if (!mono)
		CreateMonoScreen();

	GetSaveLoadFiles();

	if (!inv_active)
		InventoryActive = 1;

	while (1)
	{
		S_InitialisePolyList();

		if (fade)
			dbinput = 0;
		else
			S_UpdateInput();

		SetDebounce = 1;
		S_DisplayMonoScreen();
		ret = DoLoadSave(load_or_save);
		UpdatePulseColour();
		S_OutputPolyList();
		S_DumpScreen();

		if (ret >= 0)
		{
			if (load_or_save & IN_SAVE)
			{
				sgSaveGame();
				S_SaveGame(ret);
				GetSaveLoadFiles();
				break;
			}

			fade = ret + 1;
			S_LoadGame(ret);
			SetFade(0, 255);
			ret = -1;
		}

		if (fade && DoFade == 2)
		{
			ret = fade - 1;
			break;
		}

		// Circle/B backs out of here too (#133), the convention every other
		// menu screen follows. Escape keeps its own path below: it carries
		// IN_OPTION as well, and that is read raw exactly as before.
		//
		// Acted on the RELEASE, not the press, for two reasons:
		//
		//  - the press that OPENED this screen must not close it again. A
		//    button already down when the loop starts never sets the
		//    debounce, so its release does nothing. That is the trap #115
		//    walked into twice.
		//  - this screen usually sits INSIDE the inventory ring, and the
		//    ring also closes on the release of Circle
		//    (newinv2.cpp:873ff). Leaving on the press would hand the ring
		//    the release a frame later and shut it behind us - one press,
		//    two consumers.
		//
		// Not while fading: a load is already under way then, and a release
		// arriving mid-fade would cancel it.
		if (!fade)
		{
			if (input & IN_DESELECT)
				deselect_debounce = 1;
			else if (deselect_debounce)
			{
				deselect_debounce = 0;
				ret = -1;
				break;
			}
		}

		if (input & IN_OPTION)
		{
			ret = -1;
			break;
		}

		if (MainThread.ended)
			break;
	}

	TIME_Init();

	if (!mono)
		FreeMonoScreen();

	if (!inv_active)
		InventoryActive = 0;

	return ret;
}

void LoadScreen(long screen, long pathNum)
{
	FILE* file;
	D3DLOCKED_RECT rect;
	void* pic;
	ushort* pSrc;
	ushort* pDst;
	ushort col, r, g, b;

	file = FileOpen(screen_paths[pathNum]);

	if (SUCCEEDED(App.dx.lpD3DDevice->CreateOffscreenPlainSurface(640, 480, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &screen_surface, 0)) && file)
	{
		pic = malloc(0x96000);
		fseek(file, 0x96000 * screen, SEEK_SET);
		fread(pic, 0x96000, 1, file);
		fclose(file);
		screen_surface->LockRect(&rect, 0, 0);
		pDst = (ushort*)rect.pBits;
		pSrc = (ushort*)pic;

		for (int i = 0; i < 0x4B000; i++, pSrc++, pDst++)
		{
			col = *pSrc;
			r = (col >> 10) & 0x1F;
			g = (col >> 5) & 0x1F;
			b = col & 0x1F;
			*pDst = (r << 11) | (g << 6) | b;
		}

		screen_surface->UnlockRect();
		free(pic);
		MonoScreenOn = 2;
		ConvertSurfaceToTextures(screen_surface);
	}
	else
		Log("WHORE!");
}

// Is the HD path usable at all? The decoder checks its own tables once; a
// player whose build somehow got them wrong keeps Core's pictures rather than
// a screen full of noise.
long LoadScreenHDReady()
{
	const char* why;

	if (BC7SelfTest(&why))
		return 1;

	Log("HD screen: BC7 tables inconsistent (%s) - using the original pictures", why);
	return 0;
}

// Puts already decoded pixels into the mono screen texture. Does not free
// them - the caller owns the buffer, because LoadScreenHDLogo() below composes
// two pictures into one before it gets here.
//
// Direct3D 9 has no BC7, so bc7.cpp unpacks the blocks and the pixels go into
// a plain A8R8G8B8 texture. That texture is square and a power of two: 1920 is
// neither, and while every card this port runs on takes a 1920x1080 texture,
// the game already asks nothing of the device that a 2001 card could not do,
// and there is no reason to start here. The picture sits in the top left
// corner and S_DisplayMonoScreen() addresses it with texture coordinates - the
// engine counts those in 256ths, and 1920 and 1080 out of 2048 come out as
// exactly 240 and 135, so nothing is lost to rounding.
static long LoadScreenHDPixels(ulong* px, long w, long h)
{
	D3DCAPS9 caps;
	D3DLOCKED_RECT rect;
	LPDIRECT3DTEXTURE9 tex9;
	ulong* src;
	ulong* dst;
	long side, a;

	if (!px || w <= 0 || h <= 0)
		return 0;

	for (side = 32; side < w || side < h; side <<= 1);

	if (FAILED(App.dx.lpD3DDevice->GetDeviceCaps(&caps)) ||
		(long)caps.MaxTextureWidth < side || (long)caps.MaxTextureHeight < side)
	{
		Log("HD screen: device cannot hold a %d x %d texture - using the original picture", side, side);
		return 0;
	}

	if (FAILED(App.dx.lpD3DDevice->CreateTexture(side, side, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex9, 0)))
	{
		Log("HD screen: CreateTexture(%d) failed - using the original picture", side);
		return 0;
	}

	if (FAILED(tex9->LockRect(0, &rect, 0, 0)))
	{
		Log("HD screen: LockRect failed - using the original picture");
		tex9->Release();
		return 0;
	}

	// Everything outside the picture stays opaque black, so a stray texel at
	// the seam cannot drag anything bright into the border.
	for (long y = 0; y < side; y++)
	{
		dst = (ulong*)((uchar*)rect.pBits + y * rect.Pitch);

		if (y < h)
		{
			src = px + y * w;

			// The logo pictures carry an alpha channel; the level pictures do
			// not. Compose over black rather than passing alpha on - the tile
			// is drawn opaque either way, and this makes the two kinds behave
			// the same.
			for (long x = 0; x < w; x++)
			{
				a = (long)(src[x] >> 24);

				if (a == 255)
					dst[x] = src[x] | 0xFF000000;
				else
					dst[x] = 0xFF000000 |
						((((src[x] >> 16) & 0xFF) * a / 255) << 16) |
						((((src[x] >> 8) & 0xFF) * a / 255) << 8) |
						(((src[x] & 0xFF) * a / 255));
			}

			for (long x = w; x < side; x++)
				dst[x] = 0xFF000000;
		}
		else
			for (long x = 0; x < side; x++)
				dst[x] = 0xFF000000;
	}

	tex9->UnlockRect(0);

	if (MonoScreen.tex)
		MonoScreen.tex->Release();

	MonoScreen.tex = tex9;
	MonoScreenPicW = w;
	MonoScreenPicH = h;
	MonoScreenPicSide = side;
	MonoScreenHD = 1;
	MonoScreenOn = 2;
	return 1;
}

// The HD counterpart of LoadScreen(): screens\HD\<name>.DDS instead of a
// picture out of SCREENS.STR. Returns 0 when there is no usable file, and the
// caller then loads the original picture - so a player without the remaster
// notices nothing at all.
long LoadScreenHD(const char* name)
{
	ulong* px;
	long w, h, ok;

	if (!name || !*name || !LoadScreenHDReady())
		return 0;

	px = HDScreenRead(name, 2048, &w, &h);

	if (!px)
		return 0;

	ok = LoadScreenHDPixels(px, w, h);
	free(px);
	return ok;
}

// The title screen, picture 0: the title picture with the localised wordmark
// blended over it, centred near the top edge.
//
// The remaster keeps the two apart - TITLE.DDS is Lara in front of the
// lightning with no lettering at all, and the wordmark sits alone on a
// transparent frame in LOGO_DE/EU/FR/US.DDS - so before this the logo screen
// showed the wordmark on black. Putting them together is what the user did by
// hand in an image editor; his file cannot be shipped, because it is remaster
// material, so the engine does the same job at load time from the player's own
// two files. Whatever is missing simply drops out of the result.
long LoadScreenHDLogo()
{
	const char* logoName;
	ulong* title;
	ulong* logo;
	long tw, th, lw, lh, ok;

	if (!LoadScreenHDReady())
		return 0;

	logoName = HDScreenLogo();
	title = HDScreenRead("TITLE", 2048, &tw, &th);
	logo = HDScreenRead(logoName, 2048, &lw, &lh);

	if (title && logo)
	{
		if (HDScreenComposeLogo(title, tw, th, logo, lw, lh, !strcmp(logoName, "LOGO_US")))
			Log("HD screen: title picture with the %s wordmark blended over it", logoName);
		else
			Log("HD screen: %s draws nothing - showing the title picture on its own", logoName);
	}
	else if (title)
		Log("HD screen: no %s - showing the title picture on its own", logoName);
	else if (logo)
		Log("HD screen: no TITLE - showing %s on black", logoName);

	if (title)
		ok = LoadScreenHDPixels(title, tw, th);
	else if (logo)
		ok = LoadScreenHDPixels(logo, lw, lh);
	else
		ok = 0;

	free(title);
	free(logo);
	return ok;
}

void ReleaseScreen()
{
	MonoScreenOn = 0;

	// An HD picture is a 2048x2048 texture, sixteen megabytes; the original
	// path leaves its 640x480 one standing until the next picture replaces it,
	// which would be a lot to leave lying around here.
	if (MonoScreenHD)
	{
		MonoScreenHD = 0;

		if (MonoScreen.tex)
		{
			Log("Released %s @ %x - RefCnt = %d", "HD Picture Texture", MonoScreen.tex, MonoScreen.tex->Release());
			MonoScreen.tex = 0;
		}
	}

	if (screen_surface)
	{
		Log("Released %s @ %x - RefCnt = %d", "Picture Surface", screen_surface, screen_surface->Release());
		screen_surface = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "Picture Surface");

	FreeMonoScreen();
}

void DrawLoadingScreen()
{
	S_DisplayMonoScreen();
}

long GetSaveLoadFiles()
{
	FILE* file;
	SAVEFILE_INFO* pSave;
	SAVEGAME_INFO save_info;
	static long nSaves;
	char name[75];

	SaveCounter = 0;

	for (int i = 0; i < 15; i++)
	{
		pSave = &SaveGames[i];
		SaveGamePath(name, i);
		file = fopen(name, "rb");
		Log("Attempting to open %s", name);

		if (!file)
		{
			pSave->valid = 0;
			strcpy(pSave->name, SCRIPT_TEXT(TXT_Empty_Slot));
			continue;
		}

		Log("Opened OK");
		fread(&pSave->name, sizeof(char), 75, file);
		fread(&pSave->num, sizeof(long), 1, file);
		fread(&pSave->days, sizeof(short), 1, file);
		fread(&pSave->hours, sizeof(short), 1, file);
		fread(&pSave->minutes, sizeof(short), 1, file);
		fread(&pSave->seconds, sizeof(short), 1, file);
		fread(&save_info, 1, sizeof(SAVEGAME_INFO), file);
		fclose(file);

		if (pSave->num > SaveCounter)
			SaveCounter = pSave->num;

		pSave->valid = 1;
		nSaves++;
		Log("Validated savegame");
	}

	SaveCounter++;
	return nSaves;
}

long S_DisplayPauseMenu(long reset)
{
	static long menu, selection = 1;
	long y;

	if (!menu)
	{
		if (reset)
		{
			selection = reset;
			menu = 0;
		}
		else
		{
			y = phd_centery - font_height;
			PrintString(phd_centerx, y - ((3 * font_height) >> 1), 6, SCRIPT_TEXT(TXT_Paused), FF_CENTER);
			PrintString(phd_centerx, y, selection & 1 ? 1 : 2, SCRIPT_TEXT(TXT_Statistics), FF_CENTER);
			PrintString(phd_centerx, y + font_height, selection & 2 ? 1 : 2, SCRIPT_TEXT(TXT_Options), FF_CENTER);
			PrintString(phd_centerx, y + 2 * font_height, selection & 4 ? 1 : 2, SCRIPT_TEXT(TXT_Exit_to_Title), FF_CENTER);

			if (dbinput & IN_FORWARD)
			{
				if (selection > 1)
					selection >>= 1;

				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
			}

			if (dbinput & IN_BACK)
			{
				if (selection < 4)
					selection <<= 1;

				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
			}

			if (dbinput & (IN_DESELECT | IN_PAUSE))
			{
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
				return 1;
			}

			if (dbinput & IN_SELECT && !keymap[DIK_LALT])
			{
				SoundEffect(SFX_MENU_SELECT, 0, SFX_DEFAULT);

				if (selection & 1)
					menu = 2;
				else if (selection & 2)
					menu = 1;
				else if (selection & 4)
					return 8;
			}
		}
	}
	else if (menu == 1)
	{
		DoOptions();

		if (dbinput & (IN_DESELECT | IN_PAUSE))
		{
			menu = 0;
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		}
	}
	else if (menu == 2)
	{
		DisplayStatsUCunt();

		if (dbinput & (IN_DESELECT | IN_PAUSE))
		{
			menu = 0;
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		}
	}

	return 0;
}

long S_PauseMenu()
{
	long fade, ret;

	fade = 0;
	CreateMonoScreen();
	S_DisplayPauseMenu(1);
	InventoryActive = 1;

	do
	{
		S_InitialisePolyList();
		SetDebounce = 1;

		if (fade)
			dbinput = 0;
		else
			S_UpdateInput();

		S_DisplayMonoScreen();
		ret = S_DisplayPauseMenu(0);
		UpdatePulseColour();
		S_OutputPolyList();
		S_DumpScreen();

		if (ret == 1)
			break;

		if (ret == 8)
		{
			fade = 8;
			ret = 0;
			SetFade(0, 255);
		}

		if (fade && DoFade == 2)
		{
			ret = fade;
			break;
		}

	} while (!MainThread.ended);

	TIME_Init();
	FreeMonoScreen();
	InventoryActive = 0;
	return ret;
}

void DoFrontEndOneShotStuff()
{
	static long done;

	if (!done)
	{
		PlayFmvNow(0, 0);
		PlayFmvNow(1, 0);
		done = 1;
	}
}

long FindSFCursor(long in, long selection)
{
	long num, bak;

	num = 0;

	while (selection != 1)
	{
		selection >>= 1;
		num++;
	}

	bak = num;

	if (in & IN_FORWARD && num)
		do num--; while (num && !SpecialFeaturesPage[num]);

	if (in & IN_BACK && num < 4)
		do num++; while (num < 4 && !SpecialFeaturesPage[num]);

	if (!SpecialFeaturesPage[num])
		num = bak;

	return 1 << num;
}

void CalculateNumSpecialFeatures()
{
	SpecialFeaturesPage[0] = 0;
	SpecialFeaturesPage[1] = 0;
	SpecialFeaturesPage[2] = 0;
	SpecialFeaturesPage[3] = 0;
	SpecialFeaturesPage[4] = 0;
	NumSpecialFeatures = 0;

	for (int i = 0; i < 4; i++)
	{
		if (savegame.CampaignSecrets[i] >= 9)
		{
			SpecialFeaturesPage[i] = 1;
			NumSpecialFeatures++;
		}
	}
}

static const char* GetStupidText(long type)	//0: next, 1: both, 2: previous
{
	static char buf[32];

	switch (Gameflow->Language)
	{
	case FRENCH:

		if (!type)
			strcpy(buf, "Ensuite \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Pr�c�dent / Ensuite \x1b");
		else
			strcpy(buf, "\x19 Pr�c�dent");

		break;

	case GERMAN:

		if (!type)
			strcpy(buf, "Weiter \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Zur�ck / Weiter \x1b");
		else
			strcpy(buf, "\x19 Zur�ck");

		break;

	case ITALIAN:

		if (!type)
			strcpy(buf, "Prossimo \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Precedente / Prossimo \x1b");
		else
			strcpy(buf, "\x19 Precedente");

		break;

	case SPANISH:

		if (!type)
			strcpy(buf, "Siguiente \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Anterior / Siguiente \x1b");
		else
			strcpy(buf, "\x19 Anterior");

		break;

	default:

		if (!type)
			strcpy(buf, "Next \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Previous / Next \x1b");
		else
			strcpy(buf, "\x19 Previous");

		break;
	}

	return buf;
}

void SpecialFeaturesDisplayScreens(long num)
{
	static long start[4] = { 0, 0, 0, 0 };
	static long nPics[4] = { 12, 15, 12, 23 };
	long first, max, pos, count;

	first = start[num];
	max = nPics[num];
	pos = 0;
	count = 0;
	LoadScreen(first, num);

	while (!MainThread.ended && !(dbinput & IN_DESELECT))
	{
		_BeginScene();
		InitBuckets();
		InitialiseSortList();
		S_UpdateInput();
		SetDebounce = 1;

		if (count < 2)
		{
			count++;
			DrawLoadingScreen();
		}
		else if (count == 2)
		{
			count = 3;
			ReleaseScreen();
		}

		if (!pos)
			PrintString(font_height, phd_winymax - font_height, 6, GetStupidText(0), 0);
		else if (pos < max)
			PrintString(font_height, phd_winymax - font_height, 6, GetStupidText(1), 0);
		else
			PrintString(font_height, phd_winymax - font_height, 6, GetStupidText(2), 0);

		UpdatePulseColour();
		S_OutputPolyList();
		S_DumpScreen();

		if (dbinput & IN_LEFT && pos)
		{
			pos--;
			LoadScreen(pos + first, num);
			count = 0;
		}

		if (dbinput & IN_RIGHT && pos < max)
		{
			pos++;
			LoadScreen(pos + first, num);
			count = 0;
		}
	}

	dbinput &= ~IN_DESELECT;
	ReleaseScreen();
}

void DoSpecialFeaturesServer()
{
	switch (SpecialFeaturesNum)
	{
	case 0:
		SpecialFeaturesDisplayScreens(0);
		break;

	case 1:
		SpecialFeaturesDisplayScreens(1);
		break;

	case 2:
		SpecialFeaturesDisplayScreens(2);
		break;

	case 3:
		SpecialFeaturesDisplayScreens(3);
		break;
	}

	SpecialFeaturesNum = -1;
}

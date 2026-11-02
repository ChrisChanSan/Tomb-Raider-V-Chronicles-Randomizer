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
#include "gameflow.h"
#include "../specific/file.h"
#include "sound.h"
#include "spotcam.h"
#include "health.h"
#include "camera.h"
#include "deltapak.h"
#include "tomb4fx.h"
#include "control.h"
#include "items.h"
#include "lot.h"
#include "../specific/output.h"
#include "draw.h"
#include "door.h"				// DOORLOG at the level boundary
#include "../specific/LoadSave.h"
#include "text.h"
#include "../specific/alexstuff.h"
#include "../specific/specificfx.h"
#include "../specific/gamemain.h"
#include "../specific/dxsound.h"
#include "../specific/function_table.h"
#include "../specific/function_stubs.h"
#include "../specific/profile.h"		// PROFLOG's logic phase
#include "interpolate.h"			// the interpolator's level boundary
#include "modern_state.h"			// the modern control's level boundary
#include "../specific/audio.h"
#include "../specific/fmv.h"
#include "../specific/polyinsert.h"
#include "savegame.h"
#include "newinv2.h"
#include "../specific/3dmath.h"
#include "../specific/input.h"
#include "../specific/dxshell.h"
#include "lara.h"
#include "cutseq.h"
#include "../tomb5/tomb5.h"
#include "../specific/loadbar_math.h"	// LOADBAR_OFF
#include "../specific/replay.h"	// ReplayLevelStart - -record and -replay

GAMEFLOW* Gameflow;
PHD_VECTOR gfLensFlare;
CVECTOR gfLayer1Col;
CVECTOR gfLayer2Col;
CVECTOR gfLensFlareColour;
CVECTOR gfFog = { 0, 0, 0, 0 };
ushort* gfStringOffset;
ushort* gfFilenameOffset;
uchar* gfScriptFile;
uchar* gfLanguageFile;
char* gfStringWad;
char* gfFilenameWad;
long gfStatus = 0;
long gfMirrorZPlane;
short gfLevelFlags;
uchar gfCurrentLevel;
uchar gfLevelId;
uchar gfSlotFromId[18];
uchar gfIdFromSlot[18];
uchar gfLastSlot = LVL5_RED_ALERT;

// The videos hang on the sections, not on slots: intro before a section's
// first level, outro after its last. In vanilla the pairs (outro, next
// intro) lay as CMD_FMV in the block of the section end, and the Rome intro
// sat hard-wired in the title code - after a permutation the Russia intro
// played after Rome no matter what followed. FMV 8 is the finale and comes
// after the last campaign slot (gfLastSlot), not after "Red Alert".
static uchar IntroFmv(uchar id)
{
	switch (id)
	{
	case LVL5_STREETS_OF_ROME: return 2;
	case LVL5_BASE:            return 4;
	case LVL5_GALLOWS_TREE:    return 6;
	}

	return 0;
}

static uchar OutroFmv(uchar id)
{
	switch (id)
	{
	case LVL5_COLOSSEUM:          return 3;
	case LVL5_SINKING_SUBMARINE:  return 5;
	case LVL5_OLD_MILL:           return 7;
	}

	return 0;
}

#define FINALE_FMV	8
uchar gfLevelComplete;

// Identity from the level's file name (file index from CMD_LEVEL). The
// names are those of the vanilla shipment ("DATA\JOBY3"); unknown names
// fall back to the slot, so a foreign level does not count as the title.
uchar LevelIdFromFile(uchar fileIndex)
{
	static const char* names[18] =
	{
		"TITLE", "ANDREA1", "ANDREA2", "ANDREA3", "JOBY2", "JOBY3", "JOBY4", "JOBY5",
		"ANDY1", "ANDY2", "ANDY3", "RICH1", "RICH2", "RICHCUT2", "RICH3", "GIBBY", "DEL", "TOM"
	};
	const char* s;
	const char* shortName;

	if (!gfFilenameWad || fileIndex >= Gameflow->nFileNames)
		return gfCurrentLevel;

	s = &gfFilenameWad[gfFilenameOffset[fileIndex]];
	shortName = strrchr(s, '\\');
	shortName = shortName ? shortName + 1 : s;

	for (int i = 0; i < 18; i++)
		if (!_stricmp(shortName, names[i]))
			return (uchar)i;

	return gfCurrentLevel;
}
uchar gfGameMode = 1;
uchar gfRequiredStartPos;
uchar gfMirrorRoom;
uchar gfInitialiseGame;
uchar gfNumPickups;
uchar gfNumTakeaways;
uchar gfNumMips = 0;
uchar gfPickups[128];	// was 16 - the randomizer's inventory reset sets ~70 TAKEOBJ per level
uchar gfTakeaways[128];
uchar gfLateGives[128];	// GIVEOBJ standing in the block BEHIND a TAKEOBJ - see DoGameflow
uchar gfNumLateGives;
uchar gfMips[8];
uchar gfResidentCut[4];
uchar gfLevelNames[40];
char gfUVRotate;
char gfLayer1Vel;
char gfLayer2Vel;

static ushort* gfScriptOffset;
static uchar* gfScriptWad;
static char* gfExtensions;
static uchar gfLegend;
static uchar gfLegendTime;
static uchar gfResetHubDest;
static uchar gfCutNumber;

static ushort dels_cutseq_selector_flag = 0;
static long nframes = 1;

ulong GameTimer;
long GlobalSoftReset;
uchar bDoCredits = 0;
char JustLoaded;

// Marks which level slots are actually selectable (not red/blocked) in the select-
// level screen - only ever used by that screen (see the two usages below). The
// trailing dev/test level slots (and a few others) are deliberately left at 0: their
// .trc data was never shipped in the retail data folder, so selecting them crashes
// on load (FileOpen failure) rather than just being an artificial UI restriction.
static char available_levels[40] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static char fmv_to_play[2] = { 0, 0 };
static char num_fmvs = 0;
static char DEL_playingamefmv = 0;

static CUTSEQ_SELECTOR cutsel[] =
{
	{0, 0, 0},
	{TXT_cut19, LVL5_GALLOWS_TREE, 9},
	{TXT_cut36, LVL5_OLD_MILL, 44},
	{TXT_cut35, LVL5_OLD_MILL, 43},
	{TXT_cut34, LVL5_LABYRINTH, 42},
	{TXT_cut33, LVL5_COLOSSEUM, 41},
	{TXT_cut32, LVL5_DEEPSEA_DIVE, 40},
	{TXT_cut31, LVL5_OLD_MILL, 39},
	{TXT_cut30, LVL5_OLD_MILL, 38},
	{TXT_cut29, LVL5_OLD_MILL, 37},
	{TXT_cut28, LVL5_OLD_MILL, 36},
	{TXT_cut3, LVL5_BASE, 25},
	{TXT_cut1, LVL5_BASE, 27},
	{TXT_cut2, LVL5_SECURITY_BREACH, 26},
	{TXT_cut4, LVL5_THIRTEENTH_FLOOR, 24},
	{TXT_cut5, LVL5_ESCAPE_WITH_THE_IRIS, 23},
	{TXT_cut6, LVL5_BASE, 22},
	{TXT_cut7, LVL5_GALLOWS_TREE, 21},
	{TXT_cut8, LVL5_THIRTEENTH_FLOOR, 20},
	{TXT_cut9, LVL5_GALLOWS_TREE, 19},
	{TXT_cut10, LVL5_SUBMARINE, 18},
	{TXT_cut11, LVL5_STREETS_OF_ROME, 17},
	{TXT_cut12, LVL5_STREETS_OF_ROME, 16},
	{TXT_cut13, LVL5_SUBMARINE, 15},
	{TXT_cut14, LVL5_GALLOWS_TREE, 14},
	{TXT_cut15, LVL5_SINKING_SUBMARINE, 13},
	{TXT_cut16, LVL5_SINKING_SUBMARINE, 12},
	{TXT_cut17, LVL5_THIRTEENTH_FLOOR, 11},
	{TXT_cut18, LVL5_GALLOWS_TREE, 10},
	{TXT_cut20, LVL5_TRAJAN_MARKETS, 8},
	{TXT_cut21, LVL5_TRAJAN_MARKETS, 7},
	{TXT_cut22, LVL5_LABYRINTH, 6},
	{TXT_cut23, LVL5_SUBMARINE, 5},
	{TXT_cut24, LVL5_SINKING_SUBMARINE, 32},
	{TXT_cut25, LVL5_LABYRINTH, 33},
	{TXT_cut26, LVL5_LABYRINTH, 34},
	{TXT_cut27, LVL5_OLD_MILL, 35},
};

void DoGameflow()
{
	uchar* gf;
	uchar n;

	do_boot_screen(Gameflow->Language);
	num_fmvs = 0;
	fmv_to_play[0] = 0;
	fmv_to_play[1] = 0;
	gfCurrentLevel = Gameflow->TitleEnabled ? 0 : 1;
	gf = &gfScriptWad[gfScriptOffset[gfCurrentLevel]];

	while (1)
	{
		switch (n = *gf++)
		{
		case CMD_FMV:
			fmv_to_play[num_fmvs++] = *gf++;
			break;

		case CMD_LEVEL:
			gfLevelFlags = gf[1] | (gf[2] << 8);

			if (!(gfLevelFlags & GF_NOLEVEL))
				DoLevel(gf[3], gf[4]);
			else
			{
				gfStatus = 999;
				gfCurrentLevel++;
			}

			gfLegendTime = 0;
			LaserSight = 0;
			BinocularRange = 0;
			*(long*)gfResidentCut = 0;
			gfUVRotate = 0;
			gfNumMips = 0;
			gfNumPickups = 0;
			gfNumLateGives = 0;
			gfMirrorRoom = -1;
			gfFog.r = 0;
			gfFog.g = 0;
			gfFog.b = 0;
			gfFog.a = 0;

			switch (gfStatus)
			{
			case 1:
				gfInitialiseGame = 1;
				gfCurrentLevel = Gameflow->TitleEnabled == 0;
				break;

			case 2:
				gfGameMode = 4;
				gfCurrentLevel = savegame.CurrentLevel;
				break;

			case 3:
				if (Gameflow->DemoDisc || Gameflow->nLevels == 2)
					gfCurrentLevel = LVL5_TITLE;
				else
				{
					if (gfLevelComplete > Gameflow->nLevels)
						gfCurrentLevel = LVL5_TITLE;
					else
						gfCurrentLevel = gfLevelComplete;
				}

				break;

			case 4:
				return;
			}

			gf = &gfScriptWad[gfScriptOffset[gfCurrentLevel]];
			break;

		case CMD_TITLE:
			gfLevelFlags = gf[0] | (gf[1] << 8);
			DoTitle(gf[2], gf[3]);
			gfMirrorRoom = -1;
			*(long*)gfResidentCut = 0;
			gfUVRotate = 0;
			gfNumMips = 0;
			gfNumPickups = 0;
			gfNumLateGives = 0;

			switch (gfStatus)
			{
			case 2:
				gfGameMode = 4;
				gfCurrentLevel = savegame.CurrentLevel;
				break;

			case 3:
				gfGameMode = 0;
				gfCurrentLevel = gfLevelComplete;
				gfInitialiseGame = 1;
				break;

			case 4:
				return;
			}

			gf = &gfScriptWad[gfScriptOffset[gfCurrentLevel]];

			break;

		case CMD_ENDSEQ:
			break;

		case CMD_PLAYCUT:
			gfCutNumber = *gf++;
			break;

		case CMD_CUT1:
			gfResidentCut[0] = *gf++;
			break;

		case CMD_CUT2:
			gfResidentCut[1] = *gf++;
			break;

		case CMD_CUT3:
			gfResidentCut[2] = *gf++;
			break;

		case CMD_CUT4:
			gfResidentCut[3] = *gf++;
			break;

		case CMD_LAYER1:
			LightningRGB[0] = *gf;
			LightningRGBs[0] = *gf;
			gfLayer1Col.r = *gf++;
			LightningRGB[1] = *gf;
			LightningRGBs[1] = *gf;
			gfLayer1Col.g = *gf++;
			LightningRGB[2] = *gf;
			LightningRGBs[2] = *gf;
			gfLayer1Col.b = *gf++;
			gfLayer1Vel = *gf++;
			break;

		case CMD_LAYER2:
			LightningRGB[0] = *gf;
			LightningRGBs[0] = *gf;
			gfLayer2Col.r = *gf++;
			LightningRGB[1] = *gf;
			LightningRGBs[1] = *gf;
			gfLayer2Col.g = *gf++;
			LightningRGB[2] = *gf;
			LightningRGBs[2] = *gf;
			gfLayer2Col.b = *gf++;
			gfLayer2Vel = *gf++;
			break;

		case CMD_UVROT:
			gfUVRotate = *gf++;
			break;

		case CMD_LEGEND:
			gfLegend = *gf++;

			if (gfGameMode != 4)
				gfLegendTime = 150;

			break;

		case CMD_LENSFLARE:
			gfLensFlare.x = ((gf[1] << 8) | gf[0]) << 8;
			gfLensFlare.y = ((gf[3] << 8) | gf[2]) << 8;
			gfLensFlare.z = ((gf[5] << 8) | gf[4]) << 8;
			gfLensFlareColour.r = gf[6];
			gfLensFlareColour.g = gf[7];
			gfLensFlareColour.b = gf[8];
			gf += 9;
			break;

		case CMD_MIRROR:
			gfMirrorRoom = *gf;
			gfMirrorZPlane = (gf[4] << 24) | (gf[3] << 16) | (gf[2] << 8) | gf[1];
			gf += 5;
			break;

		case CMD_FOG:
			gfFog.r = *gf++;
			gfFog.g = *gf++;
			gfFog.b = *gf++;
			break;

		case CMD_ANIMATINGMIP:
			gfMips[gfNumMips++] = *gf++;
			break;

		case CMD_RESETHUB:
			gfResetHubDest = *gf++;
			break;

		case CMD_GIVEOBJ:
			// A GIVEOBJ standing in the block behind a TAKEOBJ is also
			// applied after the takeaways (third phase in setup.cpp): the
			// takeaways deliberately run last there, because among other
			// things they must remove the pistols that setup.cpp presses
			// back into the hands of every level with a loaded pistol
			// model. In vanilla all gifts stand before all takeaways
			// (Andrea1: gives Uzi, takes Uzi) - nothing changes there.
			if (gfNumTakeaways)
			{
				if (gfNumLateGives < 128) gfLateGives[gfNumLateGives++] = *gf;
			}
			else if (gfNumPickups < 128) gfPickups[gfNumPickups++] = *gf;	// guard: previously unchecked into a 16-entry array
			gf += 2;
			break;

		case CMD_TAKEOBJ:
			if (gfNumTakeaways < 128) gfTakeaways[gfNumTakeaways++] = *gf;
			gf += 2;
			break;

		default:

			if (n >= CMD_KEY1 && n <= CMD_KEY8)
				n -= 93;
			else if (n >= CMD_PUZZLE1 && n <= CMD_PUZZLE8)
				n += 127;
			else if (n >= CMD_PICKUP1 && n <= CMD_PICKUP4)
				n -= 93;
			else if (n >= CMD_EXAMINE1 && n <= CMD_EXAMINE3)
				n -= 83;
			else if (n >= CMD_KEYCOMBO1_1 && n <= CMD_KEYCOMBO8_2)
				n -= 116;
			else if (n >= CMD_PUZZLECOMBO1_1 && n <= CMD_PUZZLECOMBO8_2)
				n += 100;
			else if (n >= CMD_PICKUPCOMBO1_1 && n <= CMD_PICKUPCOMBO4_2)
				n -= 128;

			inventry_objects_list[n].objname = gf[0] | (gf[1] << 8);
			inventry_objects_list[n].yoff = gf[2] | (gf[3] << 8);
			inventry_objects_list[n].scale1 = gf[4] | (gf[5] << 8);
			inventry_objects_list[n].yrot = gf[6] | (gf[7] << 8);
			inventry_objects_list[n].xrot = gf[8] | (gf[9] << 8);
			inventry_objects_list[n].zrot = gf[10] | (gf[11] << 8);
			inventry_objects_list[n].flags = gf[12] | (gf[13] << 8);
			gf += 14;
			break;
		}
	}
}

long TitleOptions()
{
	static __int64 selection = 1;
	static __int64 selection_bak = 0;
	__int64 flag, sel;
	long nLevels, nFirst, lp;
	long ret, n, load, cheat_jump, y, rows;
	static long load_or_new;
	static long gfLevelComplete_bak;
	static long menu = 0;
	static long selected_level = 0;

	ret = 0;

	if (load_or_new)
	{
		if (DoFade == 2)
		{
			ret = load_or_new;
			gfLevelComplete = (uchar)gfLevelComplete_bak;
			gfLevelComplete_bak = 0;
			load_or_new = 0;
			return ret;
		}

		input = 0;
		dbinput = 0;
	}

	if (bDoCredits)
	{
		// Credits can be aborted: confirm, back or escape jumps to the
		// title (before, only Alt+F4 helped). ResetCredits resets the roll,
		// so the next credits start from the bottom again.
		if (dbinput & (IN_SELECT | IN_DESELECT | IN_OPTION))
		{
			ResetCredits();
			bDoCredits = 0;
			DestFadeScreenHeight = 0;
			dbinput = 0;
			return 0;
		}

		if (DoCredits())
			return 0;

		bDoCredits = 0;
		DestFadeScreenHeight = 0;
	}

	if (dels_cutseq_selector_flag)
	{
		menu = 0;
		selection = 1;
		return do_dels_cutseq_selector();
	}

	switch (menu)
	{
	case 1://select level menu

		font_height = GetFixedScale(36);
		y = font_height;
		PrintString(phd_centerx, font_height, 6, SCRIPT_TEXT(TXT_Select_Level), FF_CENTER);
		y += font_height;

		if (Gameflow->nLevels >= 10)
		{
			sel = selection;
			n = 0;
			nLevels = 10;
			
			while (sel)
			{
				sel >>= 1;
				n++;
			}

			nFirst = n - 9;

			if (nFirst >= 1)
			{
				if (nFirst > 1)
				{
					PrintString(32, y + font_height, 6, "\x18", 0);
					PrintString(phd_winxmax - 48, y + font_height, 6, "\x18", 0);
				}
			}
			else
				nFirst = 1;

			if (n != Gameflow->nLevels - 1)
			{
				PrintString(32, y + (nLevels * font_height), 6, "\x1a", 0);
				PrintString(phd_winxmax - 48, y + (nLevels * font_height), 6, "\x1a", 0);
			}
		}
		else
		{
			nFirst = 1;
			nLevels = Gameflow->nLevels - 1;
		}

		y = 2 * font_height;

		for (lp = nFirst; lp < nFirst + nLevels; lp++)
		{
			y += font_height;

			if (selection & (1i64 << (lp - 1)))
				PrintString(phd_centerx, y, 1, SCRIPT_TEXT(gfLevelNames[lp]), FF_CENTER);
			else
				PrintString(phd_centerx, y, available_levels[lp - 1] ? 2 : 3, SCRIPT_TEXT(gfLevelNames[lp]), FF_CENTER);

			if (selection & (1i64 << (lp - 1)))
				selected_level = lp - 1;
		}

		flag = 1i64 << (Gameflow->nLevels - 2);
		font_height = default_font_height;
		break;

	case 2://loading menu

		if (Gameflow->LoadSaveEnabled)
		{
			load = DoLoadSave(IN_LOAD);

			if (load >= 0)
			{
				S_LoadGame(load);
				ret = 2;
			}

			break;
		}

		SoundEffect(SFX_LARA_NO, 0, SFX_ALWAYS);
		menu = 0;

	case 0://main menu
		SuperShowLogo();
		Chris_Menu = 0;

		font_height = GetFixedScale(36);

		// The level select is a development shortcut - it lets anyone jump
		// straight to the finale. It stays in the build, but the player has to
		// switch it on first ("Level select" in the Tomb5 options, off by
		// default). Hidden, the row does not exist at all: the remaining four
		// move down and Exit takes over the bit that would have been the level
		// select's, so the keyboard walk keeps working without a gap.
		rows = tomb5.level_select ? 5 : 4;

		PrintString(phd_centerx, phd_winymax - rows-- * font_height, (selection & 1) ? 1 : 2, SCRIPT_TEXT(TXT_New_Game), FF_CENTER);
		PrintString(phd_centerx, phd_winymax - rows-- * font_height, (selection & 2) ? 1 : 2, SCRIPT_TEXT(TXT_Load_Game), FF_CENTER);
		PrintString(phd_centerx, phd_winymax - rows-- * font_height, (selection & 4) ? 1 : 2, SCRIPT_TEXT(TXT_Options), FF_CENTER);

		// "Level Select" has no baked-in localized string id (would need editing the
		// SCRIPT.DAT/ENGLISH.DAT string wad), so it's printed as a plain literal
		// instead of going through SCRIPT_TEXT() - PrintString already accepts raw
		// C strings directly elsewhere (e.g. the "\x18"/"\x1a" arrow glyphs below).
		if (tomb5.level_select)
			PrintString(phd_centerx, phd_winymax - rows-- * font_height, (selection & 8) ? 1 : 2, (char*)CustomText("Level Select"), FF_CENTER);

		flag = tomb5.level_select ? 16 : 8;
		PrintString(phd_centerx, phd_winymax - rows * font_height, (selection & flag) ? 1 : 2, SCRIPT_TEXT(TXT_Exit), FF_CENTER);
		font_height = default_font_height;

		// Switching the option off while sitting on a row that no longer exists
		// would leave the cursor past the end - the walk clamps against flag,
		// but only on the next keypress.
		if (selection > flag)
			selection = flag;

		break;

	case 3://options menu
		DoOptions();
		break;
	}

	if (menu < 2)
	{
		if (dbinput & IN_FORWARD)
		{
			if (selection > 1)
				selection >>= 1;

			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		}

		if (dbinput & IN_BACK)
		{
			if (selection < flag)
				selection <<= 1;

			SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		}
	}

	if (dbinput & IN_DESELECT && menu > 0)
	{
		menu = 0;
		selection = selection_bak;
		S_SoundStopAllSamples();
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	if (!menu)
	{
		cheat_jump = GetCampaignCheatValue();

		if (cheat_jump)
		{
			gfLevelComplete = gfSlotFromId[cheat_jump & 15];	// the cheat names an IDENTITY - translate it into the slot
			ret = 3;
		}

		if (keymap[DIK_C] && keymap[DIK_U] && keymap[DIK_T] && keymap[DIK_S])
			dels_cutseq_selector_flag = 1;
	}

	if (dbinput & IN_SELECT && !keymap[DIK_LALT] && menu < 2)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);

		if (!menu)
		{
			switch (selection)
			{
			case 1:

				if (Gameflow->PlayAnyLevel)
				{
					selection_bak = selection;
					menu = 1;
				}
				else
				{
					gfLevelComplete = 1;
					ret = 3;
				}

				break;

			case 2:
				GetSaveLoadFiles();
				selection_bak = selection;
				menu = 2;
				break;

			case 4:
				selection_bak = selection;
				menu = 3;
				break;

			case 8:

				// With the level select hidden, this bit belongs to Exit -
				// it is the fourth and last row then.
				if (!tomb5.level_select)
				{
					ret = 4;
					break;
				}

				// Level Select - reuses the same "select level" screen (menu 1)
				// New Game already uses when Gameflow->PlayAnyLevel is set, just
				// always reachable here instead of gated behind that gameflow flag.
				selection_bak = selection;
				selection = 1;
				menu = 1;
				break;

			case 16:
				ret = 4;
				break;
			}
		}
		else if (menu == 1 && available_levels[selected_level])
		{
			gfLevelComplete = 0;

			n = 0;
			sel = selection;

			while (sel)
			{
				sel >>= 1;
				gfLevelComplete++;
			}

			ret = 3;
		}
	}

	if (MainThread.ended)
		return 4;

	if (ret)
	{
		load_or_new = ret;
		gfLevelComplete_bak = gfLevelComplete;
		gfLevelComplete = 0;
		ret = 0;
		SetFade(0, 255);
	}

	return ret;
}

/* Shadows the title menu's debounced input on a present whose ControlPhase
 * call advanced no logic tick.
 *
 * DoTitle's loop calls TitleOptions once per present, but the debounced
 * input it reads (dbinput) is written once per logic tick, inside
 * ControlPhase's tick loop (specific/input.cpp) and nowhere else. At two
 * ticks per present the rates match and every edge is read once. At one
 * tick per present, every second ControlPhase call advances no tick,
 * dbinput is left standing, and the next TitleOptions reads the same edge
 * again - one key tap then moves the cursor by two rows.
 *
 * The fix asks the engine's tick counter (g_tickSerial, stepped once per
 * logic tick at any frame rate by InterpCapturePrev in interpolate.cpp)
 * whether a tick ran since input was last read; if not, input and dbinput
 * are zeroed for that one TitleOptions call and restored right after.
 *
 * A shadow, not a clear: the debounce logic in specific/input.cpp discards
 * dbinput's incoming value before computing the new edge, so zeroing it
 * cannot turn an already-held key into a spurious press. The value is
 * restored afterward because SetDebounce clears only on a ControlPhase
 * call's first tick (control.cpp); a call spanning several ticks writes
 * dbinput once, and an unrestored zero would swallow the edge found later
 * in the same hitch. `input` is restored for the same reason: nothing else
 * rewrites it on a tick-less frame.
 *
 * At a frame rate where one tick always matches one present, the tick
 * serial always advances, this always returns false, and the title
 * behaves exactly as before. */
static bool TitleInputShadow()
{
	static unsigned long seen = 0;

	const unsigned long now = InterpTickSerial();
	const bool repeat = now == seen;

	seen = now;

	return repeat && ActiveFpsMode() != 0;
}

/* Whether the TitleOptions call now running was handed the zeroed input, for
 * the MENULOG slider line (game\menu_frame.cpp); 0 outside that call, so the
 * pause menu's options page reads 0. */
static bool title_input_shadowed;

bool TitleInputShadowed()
{
	return title_input_shadowed;
}

/* An FMV's last picture stands on the screen after it: the movie clears
 * and draws every frame itself, so a fade to black before it does not
 * cover what it leaves. */
static long fmv_on_screen;

/* What the screen shows as a level load begins, for the load without a
 * loading screen (file.cpp, S_LoadLevelFile): black when the last screen
 * fade ran to black - a game chosen in the title or the load menu, a
 * death, the pause menu's exit - and no movie played after it. Asked
 * before the level's own fade-in is set, which resets the fade. */
static void LoadEntryNoteBlack()
{
	LoadEntryBlack = !fmv_on_screen && ScreenFadedToBlack();
	fmv_on_screen = 0;
}

void DoTitle(uchar name, uchar audio)
{
	gfLevelId = LVL5_TITLE;
	DoFrontEndOneShotStuff();
	LoadEntryNoteBlack();
	SetFade(255, 0);
	savegame.Level.Timer = 0;
	savegame.Game.Timer = 0;
	savegame.Level.Distance = 0;
	savegame.Game.Distance = 0;
	savegame.Level.AmmoUsed = 0;
	savegame.Game.AmmoUsed = 0;
	savegame.Level.AmmoHits = 0;
	savegame.Game.AmmoHits = 0;
	savegame.Level.Kills = 0;
	savegame.Game.Kills = 0;
	num_fmvs = 0;
	fmv_to_play[1] = 0;
	fmv_to_play[0] = 0;
	S_LoadLevelFile(name);
	GLOBAL_lastinvitem = NO_ITEM;
	dels_cutseq_player = 0;
	InitSpotCamSequences();
	title_controls_locked_out = 0;
	InitialiseFXArray(1);
	InitialiseLOTarray(1);
	InitialisePickUpDisplay();
	SOUND_Stop();
	IsAtmospherePlaying = 0;
//	S_SetReverbType(1);
	InitialiseCamera();
	sound_cut_flag = 1;

	if (bDoCredits)
	{
		cutseq_num = 28;
		SetFadeClip(32, 1);
		ScreenFadedOut = 1;
		ScreenFade = 255;
		dScreenFade = 255;
		S_CDPlay(111, 1);
	}
	else
	{
		trigger_title_spotcam(1);
		ScreenFadedOut = 0;
		ScreenFade = 0;
		dScreenFade = 0;
		ScreenFadeBack = 0;
		ScreenFadeSpeed = 8;
		ScreenFading = 0;
	}
	
	bUseSpotCam = 1;
	lara_item->mesh_bits = 0;
	gfGameMode = 1;
	gfLevelComplete = 0;
	nframes = 2;
	/* The accumulator starts at level start, exactly as DoLevel starts it.
	 * framecount survives a level change and the sixty-hertz game loop
	 * leaves it at 0 or -1 by turns; a title entered on -1 then stayed on
	 * -1 for every frame it drew, because at two ticks a present
	 * `framecount += 2; while (> 0) tick, -= 2` returns to where it
	 * started. Every title frame was then read as an in-between frame, the
	 * fade never stepped, DoFade never reached 2, a black quad stood over
	 * the menu and TitleOptions swallowed the input. The rule that makes
	 * this line unnecessary is in InterpAlphaNow (interpolate.cpp); the
	 * line is here anyway, because a picture must not hang on the parity
	 * of an accumulator. */
	framecount = 0;
	InterpInvalidate("title start");
	ReplayLevelStart();	// -record and -replay: the seeds, the frame counter
	gfStatus = ControlPhase(2, 0);
	JustLoaded = 0;

	while (!gfStatus)
	{
		S_InitialisePolyList();
		SkyDrawPhase();

		/* The menu is walked on every present, as it always was, but the
		 * call that would read an edge for the second time is handed a
		 * zeroed input and gets its own back the moment it returns. See
		 * TitleInputShadow above. */
		{
			const bool shadow = TitleInputShadow();
			const long savedInput = input;
			const long savedDbInput = dbinput;

			if (shadow)
			{
				input = 0;
				dbinput = 0;
			}

			title_input_shadowed = shadow;
			gfStatus = TitleOptions();
			title_input_shadowed = 0;

			if (shadow)
			{
				input = savedInput;
				dbinput = savedDbInput;
			}
		}

		if (gfStatus)
			break;

		handle_cutseq_triggering(name);
		nframes = DrawPhaseGame();
		ProfBegin(PROF_LOGIC);
		gfStatus = ControlPhase(nframes, 0);
		ProfEnd(PROF_LOGIC);
		DoSpecialFeaturesServer();
	}

	LaraJointsLevelEnd();	// JOINTLOG, with -log
	ClosedDoorsLevelEnd();	// DOORLOG, with -log
	S_SoundStopAllSamples();
	S_CDStop();
	sound_cut_flag = 0;
	bUseSpotCam = 0;
	bDisableLaraControl = 0;

	// New game: the intro of the section lying on slot 1 (vanilla Rome =
	// FMV 2, formerly hard-wired here).
	if (gfLevelComplete == 1 && gfStatus != 2 && IntroFmv(gfIdFromSlot[1]))
	{
		PlayFmv(IntroFmv(gfIdFromSlot[1]));
		fmv_on_screen = 1;
	}

	input = 0;
}

long do_dels_cutseq_selector()
{
	long nList, num, ret, y, lp;
	static uchar selection = 0;

	nList = 10;
	ret = 0;
	PrintString(phd_centerx, font_height, 6, SCRIPT_TEXT(TXT_cut0), FF_CENTER);
	num = selection - (nList - 1);

	if (num < 0)
		num = 0;

	if (dbinput & IN_FORWARD && selection)
	{
		selection--;
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
	}

	if (dbinput & IN_BACK && selection < 35)
	{
		selection++;
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
	}

	y = font_height * 3;

	for (lp = 0; num < 36 && lp < nList; lp++, num++)
	{
		PrintString(phd_centerx, y, (selection == num) ? 1 : 5, SCRIPT_TEXT(cutsel[num + 1].string), FF_CENTER);
		y += font_height;
	}

	if (dbinput & IN_SELECT)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		dels_cutseq_selector_flag = 0;
		cutrot = 0;
		gfLevelComplete = gfSlotFromId[cutsel[selection + 1].lvl & 15];	// the cutscene selection names an IDENTITY
		dels_cutseq_player = cutsel[selection + 1].num;
		selection = 0;
		ret = 3;
	}

	if (dbinput & (IN_JUMP | IN_DESELECT))
		dels_cutseq_selector_flag = 0;

	return ret;
}

void DoLevel(uchar Name, uchar Audio)
{
	long gamestatus;

	gamestatus = 0;
	gfLevelId = LevelIdFromFile(Name);	// identity BEFORE loading - everything level-specific asks gfLevelId from here on
	LoadEntryNoteBlack();
	SetFade(255, 0);

	if (gfGameMode != 4)
	{
		savegame.Level.Timer = 0;
		savegame.Level.Distance = 0;
		savegame.Level.AmmoUsed = 0;
		savegame.Level.AmmoHits = 0;
		savegame.Level.Kills = 0;
		savegame.Level.Secrets = 0;
	}

	S_LoadLevelFile(Name);
	Log("DoLevel: level data loaded");
	InitialiseFXArray(1);
	InitialiseLOTarray(1);
	Log("DoLevel: FX/LOT ready");
	GlobalSoftReset = 0;
	InitSpotCamSequences();
	InitialisePickUpDisplay();
	SOUND_Stop();
	bDisableLaraControl = 0;

	if (gfGameMode == 4)
	{
		sgRestoreGame();
		gfRequiredStartPos = 0;
		gfInitialiseGame = 0;
	}
	else
	{
		gfRequiredStartPos = 0;

		if (gfInitialiseGame)
		{
			GameTimer = 0;
			gfRequiredStartPos = 0;
			gfInitialiseGame = 0;
			FmvSceneTriggered = 0;
			InitCutPlayed();
		}

		CurrentAtmosphere = Audio;
		savegame.Level.Timer = 0;

		if (gfLevelId == LVL5_STREETS_OF_ROME)
			savegame.TLCount = 0;
	}

	S_CDPlay(CurrentAtmosphere, 1);
	IsAtmospherePlaying = 1;
	ScreenFadedOut = 0;
	ScreenFading = 0;
	ScreenFadeBack = 0;
	dScreenFade = 255;
	ScreenFade = 255;

	if (dels_cutseq_player)
	{
		gfCutNumber = 0;
		cutseq_num = dels_cutseq_player;
		ScreenFadedOut = 1;
	}
	else if (gfCutNumber && !CheckCutPlayed(gfCutNumber))
	{
		ScreenFadedOut = 1;
		cutseq_num = gfCutNumber;
		gfCutNumber = 0;
	}
	else
	{
		cutseq_num = 0;
		gfCutNumber = 0;
		SetScreenFadeIn(16);
	}

	InitialiseCamera();
	Log("DoLevel: camera ready, starting first logic phase");
	bUseSpotCam = 0;
	gfGameMode = 0;
	gfLevelComplete = 0;
	nframes = 2;
	framecount = 0;
	InterpInvalidate("level start");	// nothing before this tick belongs to this level

	/* The modern control's held course is not a course in this world
	 * either.
	 *
	 * What it holds is a world heading, kept across ticks so that camera
	 * swinging does not bend her path; the world that heading was
	 * measured in has just been thrown away. Its own guard cannot notice
	 * that, for the same reason the line above exists: the only clock the
	 * guard has is GlobalCounter, and GlobalCounter stops with the game.
	 * Between two levels not one tick runs - the loop below leaves at
	 * `gfLevelComplete`, and the priming ControlPhase two lines down is
	 * the next tick there is - so a level boundary reaches the guard as
	 * an ordinary step of one and reads as "game time flowed". So it is
	 * reset here, where the level really starts, rather than inferred
	 * from a counter.
	 *
	 * This one line also takes every savegame restore with it:
	 * sgRestoreGame has exactly one caller, in this same function,
	 * whether the player loaded from the title or from the ring. It sits
	 * under the option because at Tank nothing has written that state and
	 * nothing will read it - and the Controls page resets it itself the
	 * moment the scheme is switched. */
	if (tomb5.control_modern)
		ModernResetControls();

	/* The two mode latches let go here too, for the same reason and the
	 * same reach - a level start and, through sgRestoreGame's one caller,
	 * every savegame restore. Not under tomb5.control_modern like the call
	 * above: Sprint mode and Duck mode are read by both control types, so
	 * a latch left standing at Tank would be a key nobody is holding in
	 * the next level. Inert at the defaults, where the two statics it
	 * clears are already 0. */
	InputModeLatchReset();

	ReplayLevelStart();	// -record and -replay: the seeds, the frame counter
	gfStatus = ControlPhase(2, 0);
	Log("DoLevel: first logic phase done");
	dbinput = 0;
	JustLoaded = 0;

	long diagFrames = 0;

	while (!gfStatus)	//game loooooooooooopppppppppppp
	{
		S_InitialisePolyList();

		if (gfLegendTime && !DestFadeScreenHeight && !FadeScreenHeight && !cutseq_num)
		{
			PrintString(phd_winwidth >> 1, phd_winymax - font_height, 2, SCRIPT_TEXT(gfLegend), FF_CENTER);

			/* The level's name at the bottom of the screen stands for
			 * gfLegendTime frames and the count is per drawn frame, so at
			 * a higher frame rate the caption would be gone in half the
			 * time. It is printed every frame either way - only the clock
			 * is a tick's.
			 *
			 * This is the one guarded counter that sits before
			 * DrawPhaseGame in the loop, so it reads the latch the last
			 * frame set rather than this one's. The two alternate all the
			 * same, which is all that matters. The tick that ran since
			 * lowered the live latch; the drawn frame's is the one that
			 * steps once per tick here. */
			if (!InterpDrawnFrameWasTween())
			{
				gfLegendTime--;
				InterpNoteStep(INTERP_STEP_LEGEND);
			}
		}

		nframes = DrawPhaseGame();

		if (diagFrames < 3)
		{
			Log("DoLevel: frame %d drawn", diagFrames);
			diagFrames++;
		}
		handle_cutseq_triggering(Name);

		if (DEL_playingamefmv)
			DEL_playingamefmv = 0;

		if (gfLevelComplete)
		{
			gfStatus = 3;
			break;
		}

		ProfBegin(PROF_LOGIC);
		gfStatus = ControlPhase(nframes, 0);
		ProfEnd(PROF_LOGIC);

		if (GlobalSoftReset)
		{
			GlobalSoftReset = 0;
			gfStatus = 1;
		}

		if (gfStatus && !gamestatus)
		{
			if (lara_item->hit_points < 0)
			{
				gamestatus = gfStatus;
				SetFade(0, 255);
				gfStatus = 0;
			}
			else
				break;
		}

		if (gamestatus)
		{
			gfStatus = 0;

			if (DoFade == 2)
				gfStatus = gamestatus;
		}
	}

	LaraJointsLevelEnd();	// JOINTLOG, with -log
	ClosedDoorsLevelEnd();	// DOORLOG, with -log
	S_SoundStopAllSamples();
	S_CDStop();

	// Videos by identity instead of from the block (see IntroFmv/OutroFmv):
	// first the outro of the section ending here - at the last campaign
	// slot the finale instead -, then the intro of the section starting on
	// the next slot. In vanilla this yields exactly the old pairs (3+4
	// after Andrea3, 5+6 after joby5, 7 after andy3, 8 after rich3); the
	// blocks' CMD_FMV (fmv_to_play) are no longer played for this, or they
	// would come twice.
	if (gfStatus == 3)
	{
		const uchar outro = (gfCurrentLevel == gfLastSlot) ? FINALE_FMV : OutroFmv(gfLevelId);

		if (outro)
		{
			PlayFmv(outro);
			fmv_on_screen = 1;
		}

		if (gamestatus != 2 && gfCurrentLevel != gfLastSlot && gfCurrentLevel + 1 < 18)
		{
			const uchar intro = IntroFmv(gfIdFromSlot[gfCurrentLevel + 1]);

			if (intro)
			{
				PlayFmv(intro);
				fmv_on_screen = 1;
			}
		}
	}

	num_fmvs = 0;
	fmv_to_play[0] = 0;
	fmv_to_play[1] = 0;
	lara.examine1 = 0;
	lara.examine2 = 0;
	lara.examine3 = 0;

	// Credits after the last campaign slot - in vanilla that is Red Alert,
	// after a permutation whatever level the randomizer put at the end.
	// Hang this on the identity, and the credits would come mid-game.
	if (gfStatus == 3 && gfCurrentLevel == gfLastSlot)
	{
		gfStatus = 1;
		bDoCredits = 1;
	}

	input = 0;
	reset_flag = 0;
}

//----------------------------------------------------------------------------
// Language switching at runtime: SCRIPT.DAT carries a list of language file
// names at its end (NUL-separated up to the end of the file). The original
// stubbornly takes the first file that loads - regional versions differed
// only in the files they shipped. This keeps the whole list and can switch
// to any other present file at any time; SCRIPT_TEXT resolves every text
// live via gfStringOffset/gfStringWad, so the switch takes effect
// immediately and without a restart.
char* gfLanguageList;	// first name in the list (points into gfScriptFile)
long gfLanguageCount;
long LanguageChoice = -1;	// registry "Language": -1 = automatic (first loadable file)

static char* LanguageFileName(long idx)
{
	char* s = gfLanguageList;

	if (idx < 0 || idx >= gfLanguageCount)
		return 0;

	for (long i = 0; i < idx; i++)
		s += strlen(s) + 1;

	return s;
}

// Display name for the options menu: each language by its own name, an
// accented letter as a Latin-1 escape that the font composes (AccentToCode).
const char* LanguageName(long idx)
{
	static const char* names[8] = { "English", "Fran\xE7" "ais", "Deutsch",
		"Italiano", "Espa\xF1ol", "English US", "Japanese", "Nederlands" };
	const char* file = LanguageFileName(idx);

	if (!file)
		return "---";

	if (idx < 8 && !_strnicmp(file, "ENGLISH", 7)) return names[0];
	if (!_strnicmp(file, "FRENCH", 6))  return names[1];
	if (!_strnicmp(file, "GERMAN", 6))  return names[2];
	if (!_strnicmp(file, "ITALIAN", 7)) return names[3];
	if (!_strnicmp(file, "SPANISH", 7)) return names[4];
	if (!_strnicmp(file, "US.", 3))     return names[5];
	if (!_strnicmp(file, "JAPAN", 5))   return names[6];
	if (!_strnicmp(file, "DUTCH", 5))   return names[7];
	return file;
}

/* extrastrings.txt: the project's words in a text file beside the game's
 * executable, so that a player or a translator can change a word without a
 * build. The table below is the embedded copy - an executable copied alone
 * keeps its words - and a word of the file goes before the table's word of
 * the same key in French, German, Italian and Spanish (CustomText).
 *
 * The file is plain text, UTF-8 with or without a byte order mark, its lines
 * ended by CRLF or LF. A line starting with '#' is a comment and a blank one
 * is skipped. "[French]", "[German]", "[Italian]" and "[Spanish]" open a
 * section, in any case; any other section is skipped with its lines. Inside
 * a section "English literal=word" maps the exact literal the code hands to
 * CustomText to that language's word: spaces and tabs around both are
 * trimmed, both may hold spaces, and a key repeated in a section takes the
 * word of its last line. A line without '=', with an empty key or word, a
 * word of more than EXTRA_WORD_MAX bytes, a key past EXTRA_KEYS_MAX or a
 * line outside any section is skipped and counted. The UTF-8 is decoded to
 * the Latin-1 byte the table's escapes write, so a word takes the same
 * AccentToCode path as the table's; a code point above 255, a sign of
 * Latin-1 (ExtraLatin1) and a byte that is not UTF-8 become '?'. A file
 * of more than EXTRA_FILE_MAX bytes, one that starts with a UTF-16 byte
 * order mark or holds a NUL byte, is skipped as a whole and counted. */
#define EXTRA_FILE_MAX	(64 * 1024)
#define EXTRA_WORD_MAX	63	// the ammo caption's buffer, specific\input.cpp
#define EXTRA_KEYS_MAX	512

struct EXTRA_STRING
{
	const char* key;
	const char* word[4];	// French, German, Italian, Spanish; 0: none
};

// Spaces and tabs off both ends of `s`, in place; the start of what is left.
static char* ExtraTrim(char* s)
{
	char* e = s + strlen(s);

	while (*s == ' ' || *s == '\t')
		s++;

	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;

	return s;
}

// UTF-8 to Latin-1 in place: a code point up to 255 is its byte, anything
// else - a code point above 255, a byte that starts no UTF-8 sequence, a
// sequence cut short - is '?'. So is a sign from 0x80 to 0xBF but the
// inverted ! and ?: as a byte it would read as one of the font's own accent
// codes 128..173 (game\text.cpp, PrintString), some of which have no glyph.
static void ExtraLatin1(char* s)
{
	const uchar* r = (const uchar*)s;
	uchar* w = (uchar*)s;

	while (*r)
	{
		const ulong c = *r;
		const long n = c < 0x80 ? 0 : c >= 0xC2 && c <= 0xDF ? 1 :
			c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
		ulong cp = n > 0 ? c & (0x3F >> n) : c;
		long i = 1;

		for (; n > 0 && i <= n; i++)
		{
			if ((r[i] & 0xC0) != 0x80)
				break;

			cp = (cp << 6) | (r[i] & 0x3F);
		}

		if (n < 0 || i <= n)
		{
			*w++ = '?';
			r++;
			continue;
		}

		*w++ = cp > 0xFF || (cp >= 0x80 && cp <= 0xBF && cp != 0xA1 &&
			cp != 0xBF) ? '?' : (uchar)cp;
		r += n + 1;
	}

	*w = 0;
}

/* The words of the file's text, `size` bytes at `text` with a NUL behind
 * them, cut and decoded in place, into `out` (at most `max` keys). Returns
 * the number of keys; `words` gets the words per section and `skipped` the
 * lines skipped and counted. */
static long ExtraStringsParse(char* text, long size, EXTRA_STRING* out,
	long max, long* words, long* skipped)
{
	static const char* const names[4] = { "French", "German", "Italian",
		"Spanish" };
	char* p = text;
	char* end = text + size;
	long count = 0;
	long section = -1;	// -1 before any section, -2 in one skipped

	if (size >= 3 && (uchar)p[0] == 0xEF && (uchar)p[1] == 0xBB &&
		(uchar)p[2] == 0xBF)
		p += 3;

	while (p < end)
	{
		char* line = p;
		char* nl = (char*)memchr(p, '\n', end - p);

		if (nl)
		{
			*nl = 0;
			p = nl + 1;
		}
		else
			p = end;

		const long len = (long)strlen(line);

		if (len && line[len - 1] == '\r')
			line[len - 1] = 0;

		line = ExtraTrim(line);

		if (!*line || *line == '#')
			continue;

		const long l = (long)strlen(line);

		if (*line == '[' && line[l - 1] == ']')
		{
			line[l - 1] = 0;

			const char* name = ExtraTrim(line + 1);

			section = -2;

			for (long i = 0; i < 4; i++)
				if (!_stricmp(name, names[i]))
					section = i;

			continue;
		}

		if (section == -2)
			continue;

		char* eq = strchr(line, '=');

		if (section < 0 || !eq)
		{
			(*skipped)++;
			continue;
		}

		*eq = 0;

		char* key = ExtraTrim(line);
		char* word = ExtraTrim(eq + 1);

		ExtraLatin1(key);
		ExtraLatin1(word);

		if (!*key || !*word || strlen(word) > EXTRA_WORD_MAX)
		{
			(*skipped)++;
			continue;
		}

		long i = 0;

		while (i < count && strcmp(out[i].key, key))
			i++;

		if (i == count)
		{
			if (count == max)
			{
				(*skipped)++;
				continue;
			}

			memset(&out[count], 0, sizeof(out[count]));
			out[count++].key = key;
		}

		if (!out[i].word[section])
			words[section]++;

		out[i].word[section] = word;
	}

	return count;
}

static char* extra_text;	// the file's text, kept for the run
static EXTRA_STRING extra_strings[EXTRA_KEYS_MAX];
static long extra_count;

/* Read extrastrings.txt once, at start (LoadGameflow): it is opened by its
 * bare name, as LoadFile opens SCRIPT.DAT, so the game folder's copy is the
 * one read. Its words stay for the run; the Language row picks among them
 * through CustomText as it picks the table's columns. One line in the -log
 * file says what was read; found=0 when there is no file. */
static void LoadExtraStrings()
{
	FILE* f = fopen("extrastrings.txt", "rb");
	const long found = f ? 1 : 0;
	long words[4] = { 0, 0, 0, 0 };
	long skipped = 0;
	long rejected = 0;

	extra_count = 0;

	if (f)
	{
		long size = -1;

		if (!fseek(f, 0, SEEK_END))
			size = ftell(f);

		fseek(f, 0, SEEK_SET);

		if (size < 0 || size > EXTRA_FILE_MAX)
			rejected = 1;
		else
		{
			extra_text = (char*)malloc(size + 1);

			if (!extra_text || (long)fread(extra_text, 1, size, f) != size ||
				memchr(extra_text, 0, size) || (size >= 2 &&
				(((uchar)extra_text[0] == 0xFF && (uchar)extra_text[1] == 0xFE)
				|| ((uchar)extra_text[0] == 0xFE &&
				(uchar)extra_text[1] == 0xFF))))
			{
				free(extra_text);
				extra_text = 0;
				rejected = 1;
			}
			else
			{
				extra_text[size] = 0;
				extra_count = ExtraStringsParse(extra_text, size,
					extra_strings, EXTRA_KEYS_MAX, words, &skipped);
			}
		}

		fclose(f);
	}

	Log("STRINGS file=extrastrings.txt found=%ld keys=%ld french=%ld "
		"german=%ld italian=%ld spanish=%ld skipped=%ld rejected=%ld",
		found, extra_count, words[0], words[1], words[2], words[3],
		skipped, rejected);
}

// The file's word for `engl` in language column `column` (0 French, 1
// German, 2 Italian, 3 Spanish), or 0 when it has none.
static const char* ExtraStringFor(const char* engl, long column)
{
	for (long i = 0; i < extra_count; i++)
		if (!strcmp(extra_strings[i].key, engl))
			return extra_strings[i].word[column];

	return 0;
}

// The project's own words - menu rows and values, the F4 outfit window, the
// Credits screen's headings, the pad's names, the F12 notice - have no
// string ID in the language wad, so each gets its own row in the table
// below: the English literal the code passes, then the French, German,
// Italian and Spanish word. Every row carries all four words. Where a
// language uses the English word itself ("Audio", "HUD", "VSync", French
// "Pause", Spanish "Normal"), its cell repeats it: the function returns
// table[i][column] once engl has matched a row, so an empty cell would
// print blank, not fall back to English the way an absent row does. A
// literal that is the same in every language ("PSX", "TR4", "TR5",
// "Gamma", "HD / SD", a file's or a button's name) has no row, and the
// function returns it as it is when nothing matches. A word of
// extrastrings.txt goes before the table's (LoadExtraStrings above).
// English, US, Japanese and Dutch read the English literal.
//
// Only characters the game font can draw without escape codes (ASCII) are
// written directly. Accented letters are Latin-1 hex escapes (\xE4 for
// a-umlaut, \xE9 for e-acute, ...), keeping this file itself ASCII:
// AccentToCode (text.cpp) maps those bytes to the font's codes 128..173,
// which PrintString composes from CharDef plus AccentTable's rows
// (accent.inc) - the same path the language files' own accented letters
// take. It composes 33 of them: a, e, i, o and u each with the acute, the
// grave, the circumflex and the diaeresis, c-cedilla, n-tilde, y-diaeresis,
// the capitals A-umlaut, A-circumflex, A-acute, E-acute, O-umlaut, O-acute,
// U-umlaut and N-tilde, and the inverted ? and !; every word below keeps to
// those. Every escape below is followed by a letter that is not a
// hex digit, or by the end of the string, so it ends where it should; where
// a hex digit follows, the literal is split there ("d\xE9" "faut") - keep
// it that way when editing. The sharp s has no accent row, so no string
// here needs one.
//
// The Steam edition's ITALIAN.DAT is just an English copy - whoever still
// has it sees five Italian rows in an English menu; with a real
// translation everything lines up.
const char* CustomText(const char* engl)
{
	static const char* table[][5] =
	{	// English, French, German, Italian, Spanish
		{ "Language", "Langue", "Sprache", "Lingua", "Idioma" },
		{ "Level Select", "Choix du niveau", "Levelauswahl",
			"Selezione livello", "Selecci\xF3n de nivel" },
		{ "Pause", "Pause", "Pause", "Pausa", "Pausa" },
		{ "Gallery", "Galerie", "Galerie", "Galleria", "Galer\xED" "a" },
		{ "tomb5 options", "Options tomb5", "tomb5-Optionen", "Opzioni tomb5",
			"Opciones tomb5" },
		{ "Credits", "Cr\xE9" "dits", "Credits", "Crediti", "Cr\xE9" "ditos" },
		{ "Controls", "Commandes", "Steuerung", "Controlli", "Controles" },
		{ "Control type", "Type de commandes", "Steuerungsart",
			"Tipo di controllo", "Tipo de control" },
		{ "Tank", "Char", "Panzer", "Carro armato", "Tanque" },
		{ "Modern", "Moderne", "Modern", "Moderno", "Moderno" },
		{ "Combat mode", "Mode de combat", "Kampfmodus", "Modo combattimento",
			"Modo de combate" },
		{ "Toggle", "Basculer", "Umschalten", "Commuta", "Alternar" },
		{ "Hold", "Maintenir", "Halten", "Mantieni", "Mantener" },
		{ "Action", "Action", "Aktion", "Azione", "Acci\xF3n" },
		{ "One key", "Une touche", "Eine Taste", "Un tasto", "Una tecla" },
		{ "Separate", "S\xE9par\xE9" "e", "Getrennt", "Separata", "Separada" },
		// The Secondary action's bind row: the Key bindings page offers it
		// only while "Action" says Separate.
		{ "Secondary", "Secondaire", "Sekund\xE4r", "Secondaria",
			"Secundaria" },
		// The two mode rows under Combat mode; only the labels are new,
		// their values Hold and Toggle are already in this table above.
		{ "Sprint mode", "Mode sprint", "Sprintmodus", "Modo scatto",
			"Modo sprint" },
		{ "Duck mode", "Mode accroupi", "Duckmodus", "Modo accovacciato",
			"Modo agachado" },
		// "Modi" is both the page title for these five mode rows and the
		// tab's own label: one literal, one row, exactly as "Key bindings"
		// and "Credits" are above.
		{ "Grab mode", "Mode saisie", "Greifmodus", "Modo presa",
			"Modo agarre" },
		// The fifth mode row; its values are "on"/"off", already in this
		// table below.
		{ "Ledge hang", "Prise de rebord", "Kantengriff", "Presa sul bordo",
			"Agarre de cornisa" },
		{ "Modes", "Modes", "Modi", "Modalit\xE0", "Modos" },
		// Bind rows for hotkeys that used to be hard-wired keys. Only six
		// of the sixteen new rows are here: the other ten take the string
		// wad's own names through SCRIPT_TEXT (the medipacks, save, load
		// and four of the weapons), exactly as the pad's list already does.
		//
		// "Level \xFC" "berspringen" is one string split into two pieces
		// on purpose: the letter after the umlaut is 'b', which is a hex
		// digit, so "\xFCb" would be read as one oversized escape. Every
		// other escape in this table that a hex digit follows is split the
		// same way ("Cr\xE9" "dits", "d\xE9" "faut").
		{ "Crossbow", "Arbal\xE8te", "Armbrust", "Balestra", "Ballesta" },
		{ "Fly mode", "Mode vol", "Flugmodus", "Modo volo", "Modo vuelo" },
		{ "All weapons", "Toutes les armes", "Alle Waffen", "Tutte le armi",
			"Todas las armas" },
		{ "All items", "Tous les objets", "Alle Gegenst\xE4nde",
			"Tutti gli oggetti", "Todos los objetos" },
		{ "Heal", "Soigner", "Heilen", "Cura", "Curar" },
		{ "Skip level", "Passer le niveau", "Level \xFC" "berspringen",
			"Salta livello", "Saltar nivel" },
		// The pad's Modifier row, which the keyboard list has no twin for.
		// "Modifikator" reads as what it is on a German keyboard page; the
		// value column beside it prints the button's own name, so the word
		// never has to carry which button it is.
		{ "Modifier", "Modificateur", "Modifikator", "Modificatore",
			"Modificador" },
		{ "Camera speed", "Vitesse cam\xE9ra", "Kamerageschwindigkeit",
			"Velocit\xE0 telecamera", "Velocidad de c\xE1mara" },
		// "Standard wiederherstellen" is longer than the value column
		// allows for a label, but it is a centred action row where the
		// whole width of the screen is its own, so the width limit that
		// "Kamerageschwindigkeit" sets for labels does not reach it.
		{ "Audio", "Audio", "Audio", "Audio", "Audio" },
		{ "Graphics", "Graphismes", "Grafik", "Grafica", "Gr\xE1" "ficos" },
		{ "Display", "\xC9" "cran", "Display", "Schermo", "Pantalla" },
		{ "Display (60)", "\xC9" "cran (60)", "Display (60)", "Schermo (60)",
			"Pantalla (60)" },
		{ "Key bindings", "Configuration des touches", "Tastenbelegung",
			"Assegnazione tasti", "Asignaci\xF3n de teclas" },
		{ "Reset to defaults", "R\xE9tablir les valeurs par d\xE9" "faut",
			"Standard wiederherstellen", "Ripristina predefiniti",
			"Restablecer valores predeterminados" },
		{ "Control layout", "Sch\xE9ma de commandes", "Steuerungslayout",
			"Schema di controllo", "Esquema de control" },
		// Its two pad values (game\menu_frame.cpp, MenuValControlLayout); the
		// keyboard's and a plain joystick's are the script's.
		{ "PS5 Controller", "Manette PS5", "PS5-Controller", "Controller PS5",
			"Mando PS5" },
		{ "Xbox Controller", "Manette Xbox", "Xbox-Controller",
			"Controller Xbox", "Mando Xbox" },
		// German keeps the borrowed loanword "Deadzone", which is also the
		// shorter of the two.
		{ "Dead zone", "Zone morte", "Deadzone", "Zona morta", "Zona muerta" },
		// The two mouse rows under it; their values are "on"/"off" and the
		// slider, already here. "Mausempfindlichkeit" is two letters shorter
		// than "Kamerageschwindigkeit", the longest label the column holds.
		{ "Mouse sensitivity", "Sensibilit\xE9 souris", "Mausempfindlichkeit",
			"Sensibilit\xE0 mouse", "Sensibilidad del rat\xF3n" },
		{ "Invert mouse", "Inverser souris", "Maus invertieren",
			"Inverti mouse", "Invertir rat\xF3n" },
		{ "HUD", "HUD", "HUD", "HUD", "HUD" },
		{ "View", "Vue", "Ansicht", "Visuale", "Vista" },
		{ "First person view", "Vue subjective", "Ego-Sicht",
			"Visuale soggettiva", "Vista subjetiva" },
		{ "Field of view", "Champ de vision", "Sichtfeld", "Campo visivo",
			"Campo de visi\xF3n" },
		{ "Death camera", "Cam\xE9ra de mort", "Todeskamera",
			"Telecamera di morte", "C\xE1mara de muerte" },
		{ "Follow head bone", "Suivre la t\xEAte", "Kopfknochen folgen",
			"Segui la testa", "Seguir la cabeza" },
		{ "Camera damping", "Lissage cam\xE9ra", "Kamerad\xE4mpfung",
			"Fluidit\xE0 telecamera", "Suavizado de c\xE1mara" },
		{ "Body", "Corps", "K\xF6rper", "Corpo", "Cuerpo" },
		{ "Frame rate", "Fr\xE9quence d'images", "Bildrate",
			"Frequenza fotogrammi", "Tasa de fotogramas" },
		{ "VSync", "VSync", "VSync", "VSync", "VSync" },
		{ "Ammotype hotkeys", "Raccourcis munitions", "Munitions-Hotkeys",
			"Tasti rapidi munizioni", "Atajos de munici\xF3n" },
		{ "Look transparency", "Scruter transparent", "Umsehen transparent",
			"Guarda trasparente", "Mirar transparente" },
		{ "Static lighting", "\xC9" "clairage statique", "Statisches Licht",
			"Illuminazione statica", "Iluminaci\xF3n est\xE1tica" },
		{ "Footprints", "Empreintes de pas", "Trittspuren", "Impronte",
			"Huellas" },
		{ "Shadow mode", "Ombres", "Schatten", "Ombre", "Sombras" },
		{ "Fix climb up delay", "Escalade sans pause", "Klettern ohne Pause",
			"Scalata senza pausa", "Trepar sin pausa" },
		{ "Flexible crawling", "Reptation flexible", "Flexibles Kriechen",
			"Strisciata flessibile", "Arrastre flexible" },
		{ "Cutscene skipper", "Passer cin\xE9matiques", "Cutscene-Skipper",
			"Salta filmati", "Saltar escenas" },
		{ "Cheats", "Codes de triche", "Cheats", "Trucchi", "Trucos" },
		{ "Bar mode", "Style des barres", "Balkenstil", "Stile barre",
			"Estilo de barras" },
		{ "Bar positions", "Position des barres", "Balkenposition",
			"Posizione barre", "Posici\xF3n de barras" },
		{ "Enemy bars", "Barres ennemies", "Gegnerbalken", "Barre nemici",
			"Barras de enemigos" },
		{ "Ammo counter", "Compteur munitions", "Munitionsanzeige",
			"Contatore munizioni", "Contador de munici\xF3n" },
		{ "Gameover menu", "Menu Game Over", "Game-Over-Men\xFC",
			"Menu Game Over", "Men\xFA de fin de partida" },
		{ "Fog", "Brouillard", "Nebel", "Nebbia", "Niebla" },
		{ "HD textures", "Textures HD", "HD-Texturen", "Texture HD",
			"Texturas HD" },
		// The row under it and two of its values; "off" and "Strong" are
		// already in this table below. "Normal" is the same word in French,
		// German and Spanish and stands here so that every value of the row
		// has its own entry.
		{ "Body physics", "Physique du corps", "K\xF6rperphysik",
			"Fisica del corpo", "F\xEDsica corporal" },
		{ "Subtle", "Subtil", "Dezent", "Lieve", "Sutil" },
		{ "Normal", "Normal", "Normal", "Normale", "Normal" },
		{ "Crawl Tilting", "Inclinaison reptation", "Kriechneigung",
			"Inclinazione strisciata", "Inclinaci\xF3n en arrastre" },
		{ "PSX skies", "Ciels PSX", "PSX-Himmel", "Cieli PSX", "Cielos PSX" },
		{ "Loadbar style", "Barre de chargement", "Ladebalken",
			"Barra caricamento", "Barra de carga" },
		{ "Mono screen style", "Style d'\xE9" "cran mono", "Mono-Screen-Stil",
			"Stile schermo mono", "Estilo de pantalla mono" },
		{ "Loading text", "Texte de chargement", "Ladetext",
			"Testo caricamento", "Texto de carga" },
		{ "Shimmer", "Miroitement", "Schimmern", "Tremolio", "Ondulaci\xF3n" },
		{ "Distance fog", "Brouillard lointain", "Distanznebel",
			"Nebbia distanza", "Niebla de distancia" },
		{ "UW Effect", "Effet sous-marin", "Unterwasser-Effekt",
			"Effetto subacqueo", "Efecto submarino" },
		// The build gate above "HD textures" and the polished floors'
		// mirror under "Body physics"; their values are "on"/"off" of this
		// table. "HD-Installation" names what the gate switches: whether a
		// level is built with the HD data of the installation at all.
		{ "HD build", "Installation HD", "HD-Installation", "Installazione HD",
			"Instalaci\xF3n HD" },
		{ "Reflections", "Reflets", "Spiegelungen", "Riflessi", "Reflejos" },
		// Values, not row labels - "on/off", "Circle" etc. reused across many
		// rows. "PSX"/"TR4"/"TR5" are not here: they are the same in every
		// language and fall back to themselves. "Original" has its row for
		// the French "D'origine" and the Italian "Originale", as
		// "Display (60)" above has its own for the word "Display".
		{ "on", "activ\xE9", "an", "attivo", "activo" },
		{ "off", "d\xE9sactiv\xE9", "aus", "disattivo", "inactivo" },
		{ "Circle", "Rond", "Kreis", "Cerchio", "C\xEDrculo" },
		{ "Faded circle", "Rond flou", "Weicher Kreis", "Cerchio sfumato",
			"C\xEDrculo difuso" },
		// Shadow mode's fifth value, offered while HD is on.
		{ "Projected", "Projet\xE9" "e", "Projiziert", "Proiettata",
			"Proyectada" },
		{ "Improved", "Am\xE9lior\xE9" "e", "Verbessert", "Migliorata",
			"Mejorada" },
		{ "Clear", "Transparent", "Klar", "Trasparente", "Transparente" },
		{ "Light", "L\xE9ger", "Leicht", "Leggero", "Ligero" },
		{ "Medium", "Moyen", "Mittel", "Medio", "Medio" },
		{ "Strong", "Fort", "Stark", "Forte", "Fuerte" },
		{ "First person", "Premi\xE8re personne", "Ego-Sicht",
			"Prima persona", "Primera persona" },
		{ "Third person", "Troisi\xE8me personne", "Dritte Person",
			"Terza persona", "Tercera persona" },
		{ "still", "immobile", "ruhig", "fermo", "quieto" },
		{ "animated", "anim\xE9", "animiert", "animato", "animado" },
		{ "on (restart)", "activ\xE9 (relancer)", "an (Neustart)",
			"attivo (riavvio)", "activo (reinicio)" },
		{ "off (restart)", "d\xE9sactiv\xE9 (relancer)", "aus (Neustart)",
			"disattivo (riavvio)", "inactivo (reinicio)" },
		{ "Original", "D'origine", "Original", "Originale", "Original" },
		// The F4 outfit window (specific\hd\hd_outfitmenu.cpp): its title,
		// the first row's name and value, the four labels under the list,
		// their values, and the line under them. It draws in the options
		// page's two columns, so a label is held to "Kamerageschwindigkeit"
		// and a value to the value column, 0.375 of the screen's width
		// (game\menu_frame.cpp, MenuValBinding). A glyph is w * W / 512
		// pixels wide at most and a space W / 80 (game\text.cpp, InitFont
		// and GetStringLength), so a value whose glyph units plus 6.4 per
		// space stay at or under 192 fits at every screen width W: every
		// value below does, in all five languages. "Outfit" is the German
		// word as well and stands here so that every word of the window has
		// its row. "Barfuss" is written with "ss" because the font has no
		// sharp s.
		{ "Outfit", "Tenue", "Outfit", "Costume", "Atuendo" },
		{ "Level facts", "Propre au niveau", "Leveleigen", "Del livello",
			"Del nivel" },
		{ "level and run decide", "par niveau et partie", "nach Level und Lauf",
			"da livello e partita", "por nivel y partida" },
		{ "Footsteps", "Pas", "Schritte", "Passi", "Pasos" },
		{ "Head", "T\xEAte", "Kopf", "Testa", "Cabeza" },
		{ "Shine", "Brillance", "Glanz", "Lucentezza", "Brillo" },
		{ "Skin glow", "\xC9" "clat de la peau", "Hautglanz",
			"Lucentezza pelle", "Brillo de piel" },
		{ "as the outfit says", "selon la tenue", "laut Outfit",
			"come da costume", "seg\xFAn atuendo" },
		{ "Bare", "Pieds nus", "Barfuss", "Scalza", "Descalza" },
		{ "Boots", "Bottes", "Stiefel", "Stivali", "Botas" },
		{ "Bare - no 2nd set", "Pieds nus - pas ici", "Barfuss - fehlt hier",
			"Scalza - manca qui", "Descalza - falta aqu\xED" },
		{ "Boots - no 2nd set", "Bottes - pas ici", "Stiefel - fehlt hier",
			"Stivali - manca qui", "Botas - falta aqu\xED" },
		// "n\xE4" "chsten" is split for the same reason as "berspringen"
		// above: 'c' is a hex digit.
		{ "applies at the next level", "s'applique au niveau suivant",
			"gilt ab dem n\xE4" "chsten Level", "vale dal livello successivo",
			"se aplica en el siguiente nivel" },
		// The head row's and the two shine rows' words: the window's own
		// head words (specific\hd\hd_outfitmenu.cpp, head_word) and the
		// one-word tiers it asks HDWardrobeFromTag for
		// (specific\hd\hd_wardrobe.cpp), passed through here. The row's
		// label already says "Head", so the outfit's own head is "own"
		// ("eigener"), TR5's head "TR5" and its own head under TR5's braid
		// "TR5 hair" ("mit Zopf"), short enough that each, with
		// "(outfit.txt)" behind it, stays within the 192 units of the value
		// column at every screen width, in every language, and so does the
		// widest shine value, "1.00/128" before a tier. "TR5" is the same
		// in every language, and "outfit.txt" and "origin" are a file's name
		// and the key of a mod.txt line: all three fall back to themselves,
		// as "PSX" does.
		{ "TR5 hair", "\xE0 tresse", "mit Zopf", "treccia", "trenza" },
		{ "own", "propre", "eigener", "propria", "propia" },
		{ "measured", "mesur\xE9", "gemessen", "misurato", "medido" },
		{ "table", "table", "Tabelle", "tabella", "tabla" },
		{ "list", "liste", "Liste", "lista", "lista" },
		{ "default", "par d\xE9" "faut", "Standard", "predefinito",
			"est\xE1ndar" },
		// The notice at the top of the screen when the HD/SD key has
		// nothing to switch (specific\hd\hd_switch.cpp). It borrows the
		// ammo hotkeys' caption, whose buffer holds 63 letters
		// (specific\input.cpp).
		{ "HD is off", "HD d\xE9sactiv\xE9", "HD ist aus", "HD disattivo",
			"HD desactivado" },
		// The Credits screen's headings (tomb5\troyestuff.cpp), each drawn
		// centred across the screen; its title is "Credits" above. The
		// names under them are names and have no rows.
		{ "TOMB5 Engine", "Moteur TOMB5", "TOMB5-Engine", "Motore TOMB5",
			"Motor TOMB5" },
		{ "TR3 Enemies from tomb3", "Ennemis de TR3 issus de tomb3",
			"TR3-Gegner aus tomb3", "Nemici di TR3 da tomb3",
			"Enemigos de TR3 de tomb3" },
		{ "Randomizer and Windows 11 Port", "Randomizer et portage Windows 11",
			"Randomizer und Windows-11-Port",
			"Randomizer e porting per Windows 11",
			"Randomizer y adaptaci\xF3n a Windows 11" },
		{ "Contributors", "Contributeurs", "Mitwirkende", "Collaboratori",
			"Colaboradores" },
		{ "Tools and Libraries", "Outils et biblioth\xE8ques",
			"Werkzeuge und Bibliotheken", "Strumenti e librerie",
			"Herramientas y bibliotecas" },
		// The pad's button names in the Key bindings' value column
		// (specific\input.cpp, JoyButtonName, through game\menu_frame.cpp,
		// MenuValBinding). The PS5 pad's shapes are the German shapes
		// ("Circle" is above, a value of its own as well); the stick's
		// directions are "Stick R" with "oben", "unten", "links",
		// "rechts", the d-pad's the same four behind "Steuerkreuz"; the
		// Xbox pad's View and Menu buttons are "Ansicht" ("View" is above,
		// the page's caption, the same word) and "Men\xFC". A letter, a
		// number and a name printed on the pad ("Share", "Options",
		// "Touchpad", "Joy 13") fall back to themselves. Every name fits
		// the value column at every screen width on its own, and behind
		// "Mod+", as a chord prints it, in every language but for the four
		// German "Steuerkreuz" names, which are wider than the column there.
		// French, Italian and Spanish name the stick "Stick R" as German
		// does; French and Italian name the d-pad after its arrows, a word
		// apart from the Cross button's, as German and Spanish keep theirs
		// apart; Italian and Spanish shorten left and right ("sx"/"dx",
		// "izq."/"der.") so that a chord fits.
		{ "Square", "Carr\xE9", "Quadrat", "Quadrato", "Cuadrado" },
		{ "Cross", "Croix", "Kreuz", "Croce", "Cruz" },
		{ "Triangle", "Triangle", "Dreieck", "Triangolo", "Tri\xE1ngulo" },
		{ "PS Button", "Touche PS", "PS-Taste", "Tasto PS", "Bot\xF3n PS" },
		{ "Menu", "Menu", "Men\xFC", "Menu", "Men\xFA" },
		{ "Stick R Up", "Stick R haut", "Stick R oben", "Stick R su",
			"Stick R arriba" },
		{ "Stick R Down", "Stick R bas", "Stick R unten", "Stick R gi\xF9",
			"Stick R abajo" },
		{ "Stick R Left", "Stick R gauche", "Stick R links", "Stick R sx",
			"Stick R izq." },
		{ "Stick R Right", "Stick R droite", "Stick R rechts", "Stick R dx",
			"Stick R der." },
		{ "D-Pad Up", "Fl\xE8" "che haut", "Steuerkreuz oben",
			"Freccia su", "Cruceta arriba" },
		{ "D-Pad Down", "Fl\xE8" "che bas", "Steuerkreuz unten",
			"Freccia gi\xF9", "Cruceta abajo" },
		{ "D-Pad Left", "Fl\xE8" "che gauche", "Steuerkreuz links",
			"Freccia sx", "Cruceta izq." },
		{ "D-Pad Right", "Fl\xE8" "che droite", "Steuerkreuz rechts",
			"Freccia dx", "Cruceta der." },
	};

	long column;

	switch (Gameflow->Language)
	{
	case FRENCH:
		column = 1;
		break;
	case GERMAN:
		column = 2;
		break;
	case ITALIAN:
		column = 3;
		break;
	case SPANISH:
		column = 4;
		break;
	default:
		return engl;	// English, US, Japanese and Dutch
	}

	// extrastrings.txt first, then the table, then the literal.
	const char* word = ExtraStringFor(engl, column - 1);

	if (word)
		return word;

	for (long i = 0; i < (long)(sizeof(table) / sizeof(table[0])); i++)
		if (!strcmp(engl, table[i][0]))
			return table[i][column];

	return engl;
}

// Build the pointer table and un-XOR the strings (0xA5).
//
// The original compacted the buffer using TXT_NUM_STRINGS (309) as the
// fixed size of the offset table, which is only right for ENGLISH/US/
// FRENCH/ITALIAN/SPANISH (195 main + 56 PSX + 58 PC = 309). GERMAN carries
// 58 PSX entries (311 total), JAPAN/DUTCH even 204+96+58 (358) - there the
// text wad began in the middle of the offset table, every string read
// shifted, and the garbage glyphs crashed the renderer on switching the
// language to German. Vanilla never noticed because each regional version
// only shipped files with a matching count.
//
// Block boundaries are read from the file header instead, and the enum
// indices are assigned block-wise (the enum follows the English split
// 195/56/58). Every file carries at least that many entries per block -
// surplus ones hang at the block's end and simply stay unused.
static void PrepareLanguageBuffer(char* d)
{
	STRINGHEADER sh;
	static ushort table[TXT_NUM_STRINGS];
	char* s;
	char* e;
	long l, q;

	memcpy(&sh, d, sizeof(STRINGHEADER));

	const long nFile = sh.nStrings + sh.nPSXStrings + sh.nPCStrings;
	ushort* off = (ushort*)(d + sizeof(STRINGHEADER));
	char* wad = d + sizeof(STRINGHEADER) + nFile * sizeof(ushort);

	const long enumMain = 195;
	const long enumPSX = 56;

	for (long i = 0; i < TXT_NUM_STRINGS; i++)
	{
		if (i < enumMain)
			q = i;
		else if (i < enumMain + enumPSX)
			q = sh.nStrings + (i - enumMain);
		else
			q = sh.nStrings + sh.nPSXStrings + (i - enumMain - enumPSX);

		table[i] = q < nFile ? off[q] : 0;
	}

	gfLanguageFile = (uchar*)d;
	gfStringOffset = table;
	gfStringWad = wad;

	// Decoding runs over the file offsets (all nFile strings), so the
	// entries beyond the enum stay consistent too. As in the original, each
	// string's last byte (the unencrypted NUL terminator) stays untouched,
	// and the file's very last string stays encoded - it lies outside any
	// enum assignment.
	for (long i = 0; i + 1 < nFile; i++)
	{
		s = &wad[off[i]];
		e = &wad[off[i + 1]];
		l = e - s - 1;

		for (long j = 0; j < l; j++)
			s[j] ^= 0xA5;
	}
}

long SwitchLanguage(long idx)
{
	STRINGHEADER sh;
	char* d;
	char* name = LanguageFileName(idx);

	if (!name || !*name || idx > 7)	// Gameflow->Language is a 3-bit field
		return 0;

	/* The Japanese entry is the Japanese audio with English text. No text
	 * file of that entry can be drawn - the Steam edition's JAPAN.DAT is a
	 * TR4 file the header check below rejects, and a Japanese file in the
	 * TR5 layout passes it but holds glyph numbers of the PlayStation's
	 * kana font, which this game's font does not have - so it is decided
	 * by the entry, not by its file: when the HQ JP set lies in
	 * audio\HQ JP beside the executable (specific\audio.cpp,
	 * ACMEmulateCDPlay, plays it for JAPAN), the English entry's file is
	 * loaded under the same check and the language becomes JAPAN; without
	 * the folder the entry is not offered, and a saved choice of it leaves
	 * the language as it was. CustomText gives JAPAN the English literal,
	 * so every text stays English. */
	if (idx == LNG_JAPAN)
	{
		const DWORD at = GetFileAttributesA("audio\\HQ JP");

		if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
			return 0;

		name = LanguageFileName(LNG_ENGLISH);

		if (!name || !*name)
			return 0;
	}

	d = 0;

	if (!LoadFile(name, &d))
		return 0;

	// Only files in the TR5 PC layout (195 main / 58 PC strings) can be
	// mapped to the enum. JAPAN.DAT and DUTCH.DAT ship in every TR5
	// edition (Steam and the Japanese PC CD alike) but are in truth
	// Tomb Raider 4 files: layout 204/96 like TR4's English.dat, TR4 item
	// names ("Hathor Effigy") and TR4 PSX serial numbers (SLES-02238/
	// -02243) in the middle of the text. A Japanese or Dutch TR5 PC text
	// package never existed - even the Japanese CD fell back to
	// ENGLISH.DAT at startup. The header is checked, not the file name, so
	// a real edition in the TR5 layout still works under any name.
	memcpy(&sh, d, sizeof(STRINGHEADER));

	if (sh.nStrings != 195 || sh.nPCStrings != 58)
	{
		free(d);
		return 0;
	}

	free(gfLanguageFile);
	PrepareLanguageBuffer(d);
	Gameflow->Language = idx;	// also drives the German keyboard table (LoadSave.cpp)
	return 1;
}

void LoadGameflow()
{
	uchar* n;
	char* s;
	char* d;
	long l, end;

	s = 0;
	l = LoadFile("SCRIPT.DAT", &s);

	if (!l || !s)
		FatalFileMissing("SCRIPT.DAT");
	const long scriptSize = l;

	gfScriptFile = (uchar*)s;

	Gameflow = (GAMEFLOW*)s;
	s += sizeof(GAMEFLOW);

	gfExtensions = s;	//"[PCExtensions]"
	s += 40;

	gfFilenameOffset = (ushort*)s;
	s += sizeof(ushort) * Gameflow->nFileNames;

	gfFilenameWad = s;
	s += Gameflow->FileNameLen;

	gfScriptOffset = (ushort*)s;
	s += sizeof(ushort) * Gameflow->nLevels;

	gfScriptWad = (uchar*)s;
	s += Gameflow->ScriptLen;

	// Measure the language list: names lie NUL-separated to the file's end.
	gfLanguageList = s;
	gfLanguageCount = 0;

	for (char* p = s; p < (char*)gfScriptFile + scriptSize && *p; p += strlen(p) + 1)
		gfLanguageCount++;

	// Like the original: take the first file that loads. The player's
	// saved choice (LanguageChoice) is applied by WinMain after the
	// LoadSettings call - the registry has not been read here yet.
	for (l = 0;; l++)
	{
		d = 0;

		if (LoadFile(s, &d))
			break;

		s += strlen(s) + 1;
	}

	PrepareLanguageBuffer(d);
	Gameflow->Language = l;

	// The project's words of extrastrings.txt, once for the run, beside the
	// language setup and before the title.
	LoadExtraStrings();

	// Vanilla has slot == identity; the loop below overwrites whatever the
	// SCRIPT.DAT distributed differently.
	for (int i = 0; i < 18; i++)
	{
		gfSlotFromId[i] = (uchar)i;
		gfIdFromSlot[i] = (uchar)i;
	}
	gfLastSlot = 0;

	for (int i = 0; i < Gameflow->nLevels; i++)
	{
		end = 0;
		n = &gfScriptWad[gfScriptOffset[i]];

		while (!end)
		{
			switch (*n++)
			{
			case CMD_FMV:
			case CMD_PLAYCUT:
			case CMD_CUT1:
			case CMD_CUT2:
			case CMD_CUT3:
			case CMD_CUT4:
			case CMD_UVROT:
			case CMD_LEGEND:
			case CMD_ANIMATINGMIP:
			case CMD_RESETHUB:
				n++;
				break;

			case CMD_LEVEL:
				gfLevelNames[i] = *n;
				// Remember slot -> identity (inverse table) and determine
				// the last campaign slot: the credits come there, no matter
				// which level the randomizer put at the end.
				{
					const uchar id = LevelIdFromFile(n[3]);
					if (id < 18)
					{
						gfSlotFromId[id] = (uchar)i;
						if (i < 18)
							gfIdFromSlot[i] = id;
					}
					if (id >= LVL5_STREETS_OF_ROME && id <= LVL5_RED_ALERT && i > gfLastSlot)
						gfLastSlot = (uchar)i;
				}
				n += 5;
				break;

			case CMD_TITLE:
			case CMD_LAYER1:
			case CMD_LAYER2:
				n += 4;
				break;

			case CMD_ENDSEQ:
				end = 1;
				break;

			case CMD_LENSFLARE:
				n += 9;
				break;

			case CMD_MIRROR:
				n += 5;
				break;

			case CMD_FOG:
				n += 3;
				break;

			default:
				n += 2;
				break;
			}
		}
	}
}

// Is the campaign still in vanilla order? The randomizer permutes the
// SCRIPT.DAT blocks; gfIdFromSlot maps slot -> identity and in vanilla is
// the identity itself. Needed by setup.cpp to apply the four built-in
// chapter inventory wipes only while they are still chapter starts.
long OrderShuffled()
{
	for (int s = 1; s < 18; s++)
		if (gfIdFromSlot[s] != s)
			return 1;

	return 0;
}

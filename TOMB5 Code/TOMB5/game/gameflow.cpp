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
#include "../specific/LoadSave.h"
#include "text.h"
#include "../specific/alexstuff.h"
#include "../specific/specificfx.h"
#include "../specific/gamemain.h"
#include "../specific/dxsound.h"
#include "../specific/function_table.h"
#include "../specific/function_stubs.h"
#include "../specific/profile.h"		// PROFLOG's logic phase (#148)
#include "interpolate.h"			// the interpolator's level boundary (#148, order 2)
#include "modern_state.h"			// the modern control's level boundary (#161)
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

// The videos hang on the SECTIONS, not on slots: intro before a section's
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
			// A GIVEOBJ standing in the block BEHIND a TAKEOBJ is also
			// applied AFTER the takeaways (third phase in setup.cpp) -
			// otherwise there would be no "take everything first, then give
			// deliberately": the takeaways deliberately run last there,
			// because among other things they must remove the pistols that
			// setup.cpp presses back into the hands of every level with a
			// loaded pistol model. In vanilla all gifts stand BEFORE all
			// takeaways (Andrea1: gives Uzi, takes Uzi) - nothing changes
			// there.
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

		// #32: the level select is a development shortcut - it lets anyone jump
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

				// With the level select hidden (#32) this bit belongs to Exit -
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

/* THE TITLE MENU'S INPUT, ON A FRAME WHOSE CONTROLPHASE RAN NO TICK (#154).
 *
 * WHAT ORDER 1b FOUND AND WHAT IT COST. DoTitle's loop is `TitleOptions();
 * DrawPhaseGame(); ControlPhase(nframes);` - the menu is walked once per
 * PRESENT while the debounced input it walks with, dbinput, is written once
 * per TICK, inside ControlPhase's `for (framecount += nframes; framecount > 0;
 * framecount -= 2)` loop and nowhere else (specific\input.cpp). At two ticks a
 * present the two rates are the same and every edge is read exactly once. At
 * one, every second ControlPhase runs no tick, dbinput keeps standing, and the
 * next TitleOptions reads the same press again: one tap of Down moved the
 * cursor two rows. Order 1b's answer was to hold the title at thirty
 * (GameFrameMinTicks); this round's is to tell the menu which of its calls is
 * reading an edge for the second time, and to shadow its input for the length
 * of that one call.
 *
 * THE QUESTION IS ASKED OF THE ENGINE'S OWN TICK COUNTER and not of a clock of
 * its own. g_tickSerial steps once per logic tick at every frame rate
 * (game\interpolate.cpp, InterpCapturePrev), so "has a tick run since I last
 * read dbinput" is one comparison and it is right at fps_mode 1, at fps_mode 2
 * where four or five frames fall inside one tick, on the first iteration after
 * DoTitle's own priming ControlPhase, and through a hitch that runs several
 * ticks in one call.
 *
 * AND IT IS A SHADOW, NOT A CLEARING - for a reason that had to be MEASURED
 * rather than assumed. The debounce is two statements in specific\input.cpp
 * with the frame's own read between them: `if (debounce) dbinput = inputBusy;`
 * (:821, the PREVIOUS frame's), `inputBusy = linput;` (:869), then `if
 * (debounce) dbinput = inputBusy & (dbinput ^ inputBusy);` (:881). So what
 * comes out is the rising edge against the previous frame's input, and
 * dbinput's own incoming value is thrown away by the first line - a dbinput
 * left at zero would NOT turn a held key into a press, which is what this
 * round was briefed to expect.
 *
 * What the restore really buys is the hitch. SetDebounce is cleared inside the
 * FIRST tick of a ControlPhase call (game\control.cpp:314), so a call that
 * runs several ticks writes dbinput on that one tick only; a zero left behind
 * by an unrestored shadow would then stand through the rest of the hitch and
 * swallow the edge that tick found. InterpTest runs both builds side by side.
 * `input` is restored for a plainer reason: nothing rewrites it on a frame
 * with no tick at all.
 *
 * At fps_mode 0 it returns false on every frame - one tick per present, the
 * serial always moved - and the title runs the sequence it always ran. */
static bool TitleInputShadow()
{
	static unsigned long seen = 0;

	const unsigned long now = InterpTickSerial();
	const bool repeat = now == seen;

	seen = now;

	return repeat && ActiveFpsMode() != 0;
}

void DoTitle(uchar name, uchar audio)
{
	gfLevelId = LVL5_TITLE;
	DoFrontEndOneShotStuff();
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
	/* AND THE ACCUMULATOR STARTS LEVEL, exactly as DoLevel starts it (#148,
	 * fix round R). framecount survives a level change and the sixty hertz
	 * game loop leaves it at 0 or -1 by turns; a title entered on -1 then
	 * stayed on -1 for every frame it drew, because at two ticks a present
	 * `framecount += 2; while (> 0) tick, -= 2` returns to where it started.
	 * Every title frame was then read as an in-between frame, the fade never
	 * stepped, DoFade never reached 2, a black quad stood over the menu and
	 * TitleOptions swallowed the input. The rule that makes this line
	 * unnecessary is in InterpAlphaNow (game\interpolate.cpp); the line is
	 * here anyway, because a picture must not hang on the parity of an
	 * accumulator. */
	framecount = 0;
	InterpInvalidate("title start");	// #148, order 2
	gfStatus = ControlPhase(2, 0);
	JustLoaded = 0;

	while (!gfStatus)
	{
		S_InitialisePolyList();
		SkyDrawPhase();

		/* #154: the menu is walked on every present, as it always was, but
		 * the call that would read an edge for the second time is handed a
		 * zeroed input and gets its own back the moment it returns. The note
		 * is over TitleInputShadow above. */
		{
			const bool shadow = TitleInputShadow();
			const long savedInput = input;
			const long savedDbInput = dbinput;

			if (shadow)
			{
				input = 0;
				dbinput = 0;
			}

			gfStatus = TitleOptions();

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

	S_SoundStopAllSamples();
	S_CDStop();
	sound_cut_flag = 0;
	bUseSpotCam = 0;
	bDisableLaraControl = 0;

	// New game: the intro of the section lying on slot 1 (vanilla Rome =
	// FMV 2, formerly hard-wired here).
	if (gfLevelComplete == 1 && gfStatus != 2 && IntroFmv(gfIdFromSlot[1]))
		PlayFmv(IntroFmv(gfIdFromSlot[1]));

	if (gfStatus != 4 && tomb5.tr4_loadscreens)
		RenderLoadPic(0);

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
	InterpInvalidate("level start");	// nothing before this tick belongs to this level (#148, order 2)

	/* AND THE MODERN CONTROL'S HELD COURSE IS NOT A COURSE IN THIS WORLD
	 * EITHER (#161, round 161a, second review pass).
	 *
	 * What it holds is a WORLD heading, kept across ticks so that the camera
	 * swinging does not bend her path; the world that heading was measured in
	 * has just been thrown away. Its own guard cannot notice that, and the
	 * reason is worth writing down because it is the same reason the line
	 * above exists: the only clock the guard has is GlobalCounter, and
	 * GlobalCounter stops with the game. Between two levels not one tick runs
	 * - the loop below leaves at `gfLevelComplete`, and the priming
	 * ControlPhase two lines down is the next tick there is - so a level
	 * boundary reaches the guard as an ordinary step of one and reads as
	 * "game time flowed". So it is hung here, where the level really starts,
	 * rather than inferred from a counter.
	 *
	 * This one line also takes every savegame restore with it: sgRestoreGame
	 * has exactly one caller and it is sixty lines above this one, in this
	 * same function, whether the player loaded from the title or out of the
	 * ring. Under the option because at Tank nothing has written that state
	 * and nothing will read it - and the Controls page resets it itself the
	 * moment the scheme is switched. */
	if (tomb5.control_modern)
		ModernResetControls();

	/* AND THE TWO MODE LATCHES LET GO HERE TOO (#161, round 161b2c, item 4).
	 * The same line, the same reason and the same reach - a level start and,
	 * through sgRestoreGame's one caller sixty lines above, every savegame
	 * restore. NOT under tomb5.control_modern, unlike the call above it: Sprint
	 * mode and Duck mode are read in both control types, so a latch left
	 * standing at Tank would be a key nobody is holding in the next level.
	 * Inert at the defaults, where the two statics it clears are already 0. */
	InputModeLatchReset();

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

			/* R16 (#148, order 3): the level's name at the bottom of the
			 * screen stands for gfLegendTime frames and the count is per
			 * DRAWN frame, so at sixty the caption would be gone in half
			 * the time. It is printed every frame either way - only the
			 * clock is a tick's.
			 *
			 * This is the one guarded counter that sits BEFORE
			 * DrawPhaseGame in the loop, so it reads the latch the last
			 * frame set rather than this one's. The two alternate all the
			 * same, which is all the rule needs. */
			if (!InterpIsTweenFrame())
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

	S_SoundStopAllSamples();
	S_CDStop();

	// Videos by IDENTITY instead of from the block (see IntroFmv/OutroFmv):
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
			PlayFmv(outro);

		if (gamestatus != 2 && gfCurrentLevel != gfLastSlot && gfCurrentLevel + 1 < 18)
		{
			const uchar intro = IntroFmv(gfIdFromSlot[gfCurrentLevel + 1]);

			if (intro)
				PlayFmv(intro);
		}
	}

	num_fmvs = 0;
	fmv_to_play[0] = 0;
	fmv_to_play[1] = 0;
	lara.examine1 = 0;
	lara.examine2 = 0;
	lara.examine3 = 0;

	if (tomb5.tr4_loadscreens)
		RenderLoadPic(0);

	// Credits after the LAST campaign slot - in vanilla that is Red Alert,
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
// stubbornly takes the FIRST file that loads - regional versions differed
// only in the files they shipped. We remember the list and can switch to any
// other present file at any time; SCRIPT_TEXT resolves every text live via
// gfStringOffset/gfStringWad, so the switch takes effect immediately and
// without a restart.
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

// Display name for the options menu - only characters the game font knows.
const char* LanguageName(long idx)
{
	static const char* names[8] = { "English", "Francais", "Deutsch", "Italiano", "Espanol", "English US", "Japanese", "Nederlands" };
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

// Self-added menu rows ("Language", "Level Select", ...) have no string ID
// in the language wad - a small own table per loaded language. Only
// characters the game font can draw without escape codes (ASCII). Note: the
// Steam edition's ITALIAN.DAT is just an English copy - whoever still has
// it sees five Italian rows in an English menu; with a real translation
// (the user owns one) everything lines up.
const char* CustomText(const char* engl)
{
	static const char* table[][5] =
	{	// English           French             German            Italian               Spanish
		{ "Language",        "Langue",          "Sprache",        "Lingua",             "Idioma" },
		{ "Level Select",    "Choix du niveau", "Levelauswahl",   "Selezione livello",  "Seleccion de nivel" },
		{ "Pause",           "Pause",           "Pause",          "Pausa",              "Pausa" },
		{ "Gallery",         "Galerie",         "Galerie",        "Galleria",           "Galeria" },
		{ "tomb5 options",   "Options tomb5",   "tomb5-Optionen", "Opzioni tomb5",      "Opciones tomb5" },
		{ "Credits",         "Credits",         "Credits",        "Crediti",            "Creditos" },
		// #161, round 161a step 7 - the Controls page (decision 1). Only the
		// German column carries a real translation (the brief's own words);
		// French/Italian/Spanish repeat the English literal on purpose -
		// the function only returns table[i][column] once engl has matched
		// a row at all, so an empty string here would print blank, not
		// fall back to English the way an absent row does.
		{ "Controls",        "Controls",        "Steuerung",      "Controls",           "Controls" },
		{ "Control type",    "Control type",    "Steuerungsart",  "Control type",       "Control type" },
		{ "Tank",            "Tank",            "Panzer",         "Tank",               "Tank" },
		{ "Modern",          "Modern",          "Modern",         "Modern",             "Modern" },
		{ "Combat mode",     "Combat mode",     "Kampfmodus",     "Combat mode",        "Combat mode" },
		{ "Toggle",          "Toggle",          "Umschalten",     "Toggle",             "Toggle" },
		{ "Hold",            "Hold",            "Halten",         "Hold",               "Hold" },
		{ "Action",          "Action",          "Aktion",         "Action",             "Action" },
		{ "One key",         "One key",         "Eine Taste",     "One key",            "One key" },
		{ "Separate",        "Separate",        "Getrennt",       "Separate",           "Separate" },
		// #161, round 161b2c step 1 - the Secondary action's bind row, which
		// the Key bindings page offers only while "Action" says Separate. The
		// umlaut is the Latin-1 byte 0xE4 as a hex escape, the way every
		// German string in this table carries one; the 'r' after it is no hex
		// digit, so the escape ends where it should.
		{ "Secondary",       "Secondary",       "Sekund\xE4r",    "Secondary",          "Secondary" },
		// #161, round 161b2c step 3 - the two mode rows under Combat mode. Only
		// the labels are new: their two values, Hold and Toggle, are the rows
		// above's own words and are already in this table.
		{ "Sprint mode",     "Sprint mode",     "Sprintmodus",    "Sprint mode",        "Sprint mode" },
		{ "Duck mode",       "Duck mode",       "Duckmodus",      "Duck mode",          "Duck mode" },
		// #161, round 161b2d step 1 - the bind rows of the hotkeys that used to
		// be hard-wired keys. Only six of the sixteen new rows are here: the
		// other ten take the string wad's own names through SCRIPT_TEXT (the
		// medipacks, save, load and four of the weapons), exactly as the pad's
		// list already does. Same rule as every block above - German is the only
		// real translation, the other three repeat the English literal, because
		// an empty column would print blank rather than fall back.
		//
		// "Level \xFC" "berspringen" is ONE string in two pieces on purpose: the
		// letter after the umlaut is 'b', which IS a hex digit, so "\xFCb" would
		// be read as one enormous escape. Every other umlaut in this table is
		// followed by a letter that is not, which is why none of them needs the
		// split (the comment at the big block below says to keep it that way).
		{ "Crossbow",        "Crossbow",        "Armbrust",       "Crossbow",           "Crossbow" },
		{ "Fly mode",        "Fly mode",        "Flugmodus",      "Fly mode",           "Fly mode" },
		{ "All weapons",     "All weapons",     "Alle Waffen",    "All weapons",        "All weapons" },
		{ "All items",       "All items",       "Alle Gegenst\xE4nde", "All items",     "All items" },
		{ "Heal",            "Heal",            "Heilen",         "Heal",               "Heal" },
		{ "Skip level",      "Skip level",      "Level \xFC" "berspringen", "Skip level", "Skip level" },
		// #161, round 161a2b step 5 - the Camera speed row (item 5). Same
		// rule as the block above: only German is a real translation, the
		// other three repeat the English literal on purpose.
		{ "Camera speed",    "Camera speed",    "Kamerageschwindigkeit", "Camera speed", "Camera speed" },
		// #161, round 161b1 step 1 - the pages of the new tree that step 1
		// builds, and the two rows it renames. Same rule again: German is the
		// only real translation, the other three repeat the English literal.
		// The rest of the words arrive with step 2.
		//
		// "Standard wiederherstellen" is longer than the value column allows
		// for a LABEL - it is a centred action row, where the whole width of
		// the screen is its own, so the rule about "Kamerageschwindigkeit"
		// being the widest label does not reach it.
		{ "Audio",           "Audio",           "Audio",          "Audio",              "Audio" },
		{ "Graphics",        "Graphics",        "Grafik",         "Graphics",           "Graphics" },
		{ "Display",         "Display",         "Display",        "Display",            "Display" },
		{ "Key bindings",    "Key bindings",    "Tastenbelegung", "Key bindings",       "Key bindings" },
		{ "Reset to defaults", "Reset to defaults", "Standard wiederherstellen", "Reset to defaults", "Reset to defaults" },
		{ "Control layout",  "Control layout",  "Steuerungslayout", "Control layout",   "Control layout" },
		// #161, round 161b1 step 3 - the Dead zone row (item 5). German keeps
		// the borrowed word the user himself used ("Deadzone"), which is also
		// the shorter label of the two.
		{ "Dead zone",       "Dead zone",       "Deadzone",       "Dead zone",          "Dead zone" },
		// #161, round 161b1 step 2 - the right half of the tree (item 4):
		// Graphics, HUD, View and tomb5 options, plus Ammotype hotkeys moving
		// into Controls. Same rule as every block above: only German is a
		// real translation, French/Italian/Spanish repeat the English
		// literal on purpose (an empty string would print blank, not fall
		// back to English). Umlauts are the Latin-1 bytes 0xE4 / 0xF6 / 0xFC,
		// written as hex escapes so this file stays ASCII: AccentToCode
		// (game\text.cpp:187) maps them to codes 128+4 / 128+20 / 128+1 and
		// PrintString composes those from CharDef plus AccentTable's rows
		// (game\accent.inc) - the path the German language file's own
		// umlauts take. The round first wrote "~u"/"~o"/"~a", the way
		// GermanKeyboard[] (specific\input.cpp) labels its keys, because the
		// round's brief prescribed it; PrintString has no escape for '~', so
		// the menu would have shown a tilde and a plain vowel. The brief was
		// wrong there, the review round measured the real path, and the
		// project manager corrected the three strings after acceptance. Only
		// the sharp s has no accent row, so none of these strings has one.
		// Every escape below is followed by a letter that is no hex digit, or
		// by the end of the string - keep it that way when editing.
		//
		// Rows with no German entry (PSX/TR4/TR5, "Display"/"Display (60)",
		// "Original") are not listed here - CustomText already falls back to
		// the English literal on its own when nothing matches, which is
		// exactly what the brief's word table means by "stay".
		{ "HUD",             "HUD",             "HUD",            "HUD",                "HUD" },
		{ "View",            "View",            "Ansicht",        "View",               "View" },
		{ "First person view", "First person view", "Ego-Sicht",  "First person view",  "First person view" },
		{ "Field of view",   "Field of view",   "Sichtfeld",      "Field of view",      "Field of view" },
		{ "Death camera",    "Death camera",    "Todeskamera",    "Death camera",       "Death camera" },
		{ "Follow head bone", "Follow head bone", "Kopfknochen folgen", "Follow head bone", "Follow head bone" },
		{ "Camera damping",  "Camera damping",  "Kamerad\xE4mpfung", "Camera damping",    "Camera damping" },
		{ "Body",            "Body",            "K\xF6rper",        "Body",               "Body" },
		{ "Frame rate",      "Frame rate",      "Bildrate",       "Frame rate",         "Frame rate" },
		{ "VSync",           "VSync",           "VSync",          "VSync",              "VSync" },
		{ "Ammotype hotkeys", "Ammotype hotkeys", "Munitions-Hotkeys", "Ammotype hotkeys", "Ammotype hotkeys" },
		{ "Look transparency", "Look transparency", "Umsehen transparent", "Look transparency", "Look transparency" },
		{ "Static lighting", "Static lighting", "Statisches Licht", "Static lighting",  "Static lighting" },
		{ "Footprints",      "Footprints",      "Trittspuren",    "Footprints",         "Footprints" },
		{ "Shadow mode",     "Shadow mode",     "Schatten",       "Shadow mode",        "Shadow mode" },
		{ "Fix climb up delay", "Fix climb up delay", "Klettern ohne Pause", "Fix climb up delay", "Fix climb up delay" },
		{ "Flexible crawling", "Flexible crawling", "Flexibles Kriechen", "Flexible crawling", "Flexible crawling" },
		{ "Cutscene skipper", "Cutscene skipper", "Cutscene-Skipper", "Cutscene skipper", "Cutscene skipper" },
		{ "Cheats",          "Cheats",          "Cheats",         "Cheats",             "Cheats" },
		{ "Bar mode",        "Bar mode",        "Balkenstil",     "Bar mode",           "Bar mode" },
		{ "Bar positions",   "Bar positions",   "Balkenposition", "Bar positions",      "Bar positions" },
		{ "Enemy bars",      "Enemy bars",      "Gegnerbalken",   "Enemy bars",         "Enemy bars" },
		{ "Ammo counter",    "Ammo counter",    "Munitionsanzeige", "Ammo counter",     "Ammo counter" },
		{ "Gameover menu",   "Gameover menu",   "Game-Over-Men\xFC", "Gameover menu",     "Gameover menu" },
		{ "Fog",             "Fog",             "Nebel",          "Fog",                "Fog" },
		{ "HD textures",     "HD textures",     "HD-Texturen",    "HD textures",        "HD textures" },
		{ "Crawl Tilting",   "Crawl Tilting",   "Kriechneigung",  "Crawl Tilting",      "Crawl Tilting" },
		{ "PSX skies",       "PSX skies",       "PSX-Himmel",     "PSX skies",          "PSX skies" },
		{ "TR4 loadscreens", "TR4 loadscreens", "TR4-Ladebilder", "TR4 loadscreens",    "TR4 loadscreens" },
		{ "Loadbar style",   "Loadbar style",   "Ladebalken",     "Loadbar style",      "Loadbar style" },
		{ "Mono screen style", "Mono screen style", "Mono-Screen-Stil", "Mono screen style", "Mono screen style" },
		{ "Loading text",    "Loading text",    "Ladetext",       "Loading text",       "Loading text" },
		{ "Shimmer",         "Shimmer",         "Schimmern",      "Shimmer",            "Shimmer" },
		{ "Distance fog",    "Distance fog",    "Distanznebel",   "Distance fog",       "Distance fog" },
		{ "UW Effect",       "UW Effect",       "Unterwasser-Effekt", "UW Effect",      "UW Effect" },
		// Values, not row labels - "on/off", "Circle" etc. reused across many
		// rows (Values line of the brief's table). "PSX"/"TR4"/"TR5" and
		// "Original" are not here for the same reason as above: they stay.
		{ "on",              "on",              "an",             "on",                 "on" },
		{ "off",             "off",             "aus",            "off",                "off" },
		{ "Circle",          "Circle",          "Kreis",          "Circle",             "Circle" },
		{ "Faded circle",    "Faded circle",    "Weicher Kreis",  "Faded circle",       "Faded circle" },
		{ "Improved",        "Improved",        "Verbessert",     "Improved",           "Improved" },
		{ "Clear",           "Clear",           "Klar",           "Clear",              "Clear" },
		{ "Light",           "Light",           "Leicht",         "Light",              "Light" },
		{ "Medium",          "Medium",          "Mittel",         "Medium",             "Medium" },
		{ "Strong",          "Strong",          "Stark",          "Strong",             "Strong" },
		{ "First person",    "First person",    "Ego-Sicht",      "First person",       "First person" },
		{ "Third person",    "Third person",    "Dritte Person",  "Third person",       "Third person" },
		{ "still",           "still",           "ruhig",          "still",              "still" },
		{ "animated",        "animated",        "animiert",       "animated",           "animated" },
		{ "on (restart)",    "on (restart)",    "an (Neustart)",  "on (restart)",       "on (restart)" },
		{ "off (restart)",   "off (restart)",   "aus (Neustart)", "off (restart)",      "off (restart)" },
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
		return engl;	// English and US
	}

	for (long i = 0; i < (long)(sizeof(table) / sizeof(table[0])); i++)
		if (!strcmp(engl, table[i][0]))
			return table[i][column];

	return engl;
}

// Build the pointer table and un-XOR the strings (0xA5).
//
// CAUTION, legacy: the original compacted the buffer with TXT_NUM_STRINGS
// (309) as the FIXED size of the offset table. But that is only right for
// ENGLISH/US/FRENCH/ITALIAN/SPANISH (195 main + 56 PSX + 58 PC = 309).
// GERMAN carries 58 PSX entries (311 total), JAPAN/DUTCH even 204+96+58
// (358) - there the text wad began in the middle of the offset table, every
// string read shifted, and the garbage glyphs tore the renderer down (hard
// crash when switching the language to German). Vanilla never noticed
// because each regional version only shipped files with a matching count.
//
// Hence now: block boundaries from the FILE HEADER, and the enum indices
// are assigned block-wise (the enum follows the English split 195/56/58).
// Every file carries at least that many entries per block - surplus ones
// hang at the block's end and simply stay unused.
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

	// Decoding runs over the FILE offsets (all nFile strings), so the
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

	d = 0;

	if (!LoadFile(name, &d))
		return 0;

	// Only files in the TR5 PC layout (195 main / 58 PC strings) can be
	// mapped to the enum. JAPAN.DAT and DUTCH.DAT are in EVERY TR5 edition
	// (Steam and the Japanese PC CD alike) in truth TOMB RAIDER 4 files:
	// layout 204/96 like TR4's English.dat, TR4 item names ("Hathor
	// Effigy") and TR4 PSX serial numbers (SLES-02238/-02243) in the middle
	// of the text. A Japanese or Dutch TR5 PC text package never existed -
	// even the Japanese CD fell back to ENGLISH.DAT at startup. The HEADER
	// is checked, not the name: a real edition in the TR5 layout (like the
	// ITALIAN.DAT the user tracked down) still works.
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

	// Like the original: take the first file that loads. The user's saved
	// choice (LanguageChoice) is applied by WinMain after the LoadSettings
	// call - the registry has not been read here yet.
	for (l = 0;; l++)
	{
		d = 0;

		if (LoadFile(s, &d))
			break;

		s += strlen(s) + 1;
	}

	PrepareLanguageBuffer(d);
	Gameflow->Language = l;

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

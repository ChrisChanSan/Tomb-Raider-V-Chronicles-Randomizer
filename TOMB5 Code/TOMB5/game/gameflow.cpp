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
uchar gfSlotVonId[18];
uchar gfIdVonSlot[18];
uchar gfLetzterSlot = LVL5_RED_ALERT;

// Die Videos haengen an den ABSCHNITTEN, nicht an Slots: Intro vor dem
// ersten Level eines Abschnitts, Outro nach dem letzten. Vanilla lagen die
// Paare (Outro, naechstes Intro) als CMD_FMV im Block des Abschnittsendes
// und das Rom-Intro hart im Titel-Code - nach einer Permutation spielte das
// Russland-Intro nach Rom, egal was folgte. FMV 8 ist das Finale und kommt
// nach dem letzten Kampagnen-Slot (gfLetzterSlot), nicht nach "Red Alert".
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

// Identitaet aus dem Dateinamen des Levels (Dateiindex aus CMD_LEVEL). Die
// Namen sind die der Vanilla-Auslieferung ("DATA\JOBY3"); unbekannte Namen
// fallen auf den Slot zurueck, damit ein Fremdlevel nicht als Titel gilt.
uchar LevelIdVonDatei(uchar fileIndex)
{
	static const char* namen[18] =
	{
		"TITLE", "ANDREA1", "ANDREA2", "ANDREA3", "JOBY2", "JOBY3", "JOBY4", "JOBY5",
		"ANDY1", "ANDY2", "ANDY3", "RICH1", "RICH2", "RICHCUT2", "RICH3", "GIBBY", "DEL", "TOM"
	};
	const char* s;
	const char* kurz;

	if (!gfFilenameWad || fileIndex >= Gameflow->nFileNames)
		return gfCurrentLevel;

	s = &gfFilenameWad[gfFilenameOffset[fileIndex]];
	kurz = strrchr(s, '\\');
	kurz = kurz ? kurz + 1 : s;

	for (int i = 0; i < 18; i++)
		if (!_stricmp(kurz, namen[i]))
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
uchar gfPickups[128];	// war 16 - der Inventar-Reset des Randomizers setzt ~70 TAKEOBJ je Level
uchar gfTakeaways[128];
uchar gfNachgaben[128];	// GIVEOBJ, die im Block HINTER einem TAKEOBJ stehen - siehe DoGameflow
uchar gfNumNachgaben;
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
			gfNumNachgaben = 0;
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
			gfNumNachgaben = 0;

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
			// Ein GIVEOBJ, das im Block HINTER einem TAKEOBJ steht, wird auch
			// NACH den Wegnahmen angewandt (dritte Phase in setup.cpp) - sonst
			// gaebe es kein "erst alles wegnehmen, dann gezielt geben": die
			// Wegnahmen laufen dort bewusst zuletzt, weil sie u.a. die
			// Pistolen entfernen muessen, die setup.cpp jedem Level mit
			// geladenem Pistolenmodell vorher wieder in die Hand drueckt.
			// Vanilla stehen alle Gaben VOR allen Wegnahmen (Andrea1: gibt
			// Uzi, nimmt Uzi) - dort aendert sich also nichts.
			if (gfNumTakeaways)
			{
				if (gfNumNachgaben < 128) gfNachgaben[gfNumNachgaben++] = *gf;
			}
			else if (gfNumPickups < 128) gfPickups[gfNumPickups++] = *gf;	// Riegel: vorher ungeprueft in ein 16er-Feld
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
	long ret, n, load, cheat_jump, y;
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
		// Abspann abbrechbar: Bestaetigen, Zurueck oder Escape springt in den
		// Titel (vorher half nur Alt+F4). ResetCredits setzt den Lauf zurueck,
		// damit der naechste Abspann wieder von unten beginnt.
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
		PrintString(phd_centerx, phd_winymax - 5 * font_height, (selection & 1) ? 1 : 2, SCRIPT_TEXT(TXT_New_Game), FF_CENTER);
		PrintString(phd_centerx, phd_winymax - 4 * font_height, (selection & 2) ? 1 : 2, SCRIPT_TEXT(TXT_Load_Game), FF_CENTER);
		PrintString(phd_centerx, phd_winymax - 3 * font_height, (selection & 4) ? 1 : 2, SCRIPT_TEXT(TXT_Options), FF_CENTER);
		// "Level Select" has no baked-in localized string id (would need editing the
		// SCRIPT.DAT/ENGLISH.DAT string wad), so it's printed as a plain literal
		// instead of going through SCRIPT_TEXT() - PrintString already accepts raw
		// C strings directly elsewhere (e.g. the "\x18"/"\x1a" arrow glyphs below).
		PrintString(phd_centerx, phd_winymax - 2 * font_height, (selection & 8) ? 1 : 2, (char*)EigenText("Level Select"), FF_CENTER);
		PrintString(phd_centerx, phd_winymax - 1 * font_height, (selection & 16) ? 1 : 2, SCRIPT_TEXT(TXT_Exit), FF_CENTER);
		font_height = default_font_height;

		flag = 16;
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
			gfLevelComplete = gfSlotVonId[cheat_jump & 15];	// Cheat nennt eine IDENTITAET - in den Slot uebersetzen
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
	gfStatus = ControlPhase(2, 0);
	JustLoaded = 0;

	while (!gfStatus)
	{
		S_InitialisePolyList();
		SkyDrawPhase();
		gfStatus = TitleOptions();

		if (gfStatus)
			break;

		handle_cutseq_triggering(name);
		nframes = DrawPhaseGame();
		gfStatus = ControlPhase(nframes, 0);
		DoSpecialFeaturesServer();
	}

	S_SoundStopAllSamples();
	S_CDStop();
	sound_cut_flag = 0;
	bUseSpotCam = 0;
	bDisableLaraControl = 0;

	// Neues Spiel: das Intro des Abschnitts, der auf Slot 1 liegt (vanilla
	// Rom = FMV 2, hier frueher hart verdrahtet).
	if (gfLevelComplete == 1 && gfStatus != 2 && IntroFmv(gfIdVonSlot[1]))
		PlayFmv(IntroFmv(gfIdVonSlot[1]));

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
		gfLevelComplete = gfSlotVonId[cutsel[selection + 1].lvl & 15];	// Cutscene-Auswahl nennt eine IDENTITAET
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
	gfLevelId = LevelIdVonDatei(Name);	// Identitaet VOR dem Laden - alles Levelspezifische fragt ab jetzt gfLevelId
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
	Log("DoLevel: Leveldaten geladen");
	InitialiseFXArray(1);
	InitialiseLOTarray(1);
	Log("DoLevel: FX/LOT bereit");
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
	Log("DoLevel: Kamera bereit, starte erste Logikphase");
	bUseSpotCam = 0;
	gfGameMode = 0;
	gfLevelComplete = 0;
	nframes = 2;
	framecount = 0;
	gfStatus = ControlPhase(2, 0);
	Log("DoLevel: erste Logikphase fertig");
	dbinput = 0;
	JustLoaded = 0;

	long diagFrames = 0;

	while (!gfStatus)	//game loooooooooooopppppppppppp
	{
		S_InitialisePolyList();

		if (gfLegendTime && !DestFadeScreenHeight && !FadeScreenHeight && !cutseq_num)
		{
			PrintString(phd_winwidth >> 1, phd_winymax - font_height, 2, SCRIPT_TEXT(gfLegend), FF_CENTER);
			gfLegendTime--;
		}

		nframes = DrawPhaseGame();

		if (diagFrames < 3)
		{
			Log("DoLevel: Frame %d gezeichnet", diagFrames);
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

		gfStatus = ControlPhase(nframes, 0);

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

	// Videos nach IDENTITAET statt aus dem Block (siehe IntroFmv/OutroFmv):
	// erst das Outro des Abschnitts, der hier endet - am letzten Kampagnen-
	// Slot stattdessen das Finale -, dann das Intro des Abschnitts, der
	// auf dem naechsten Slot beginnt. Vanilla ergibt das exakt die alten
	// Paare (3+4 nach Andrea3, 5+6 nach joby5, 7 nach andy3, 8 nach rich3);
	// die CMD_FMV der Bloecke (fmv_to_play) werden dafuer nicht mehr
	// abgespielt, sonst kaemen sie doppelt.
	if (gfStatus == 3)
	{
		const uchar outro = (gfCurrentLevel == gfLetzterSlot) ? FINALE_FMV : OutroFmv(gfLevelId);

		if (outro)
			PlayFmv(outro);

		if (gamestatus != 2 && gfCurrentLevel != gfLetzterSlot && gfCurrentLevel + 1 < 18)
		{
			const uchar intro = IntroFmv(gfIdVonSlot[gfCurrentLevel + 1]);

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

	// Abspann nach dem LETZTEN Kampagnen-Slot - vanilla ist das Red Alert,
	// nach einer Permutation das Level, das der Randomizer ans Ende gelegt
	// hat. Haenge das an die Identitaet, kaeme der Abspann mitten im Spiel.
	if (gfStatus == 3 && gfCurrentLevel == gfLetzterSlot)
	{
		gfStatus = 1;
		bDoCredits = 1;
	}

	input = 0;
	reset_flag = 0;
}

//----------------------------------------------------------------------------
// Sprachwechsel im laufenden Spiel: die SCRIPT.DAT traegt am Ende eine Liste
// von Sprachdateinamen (NUL-getrennt bis zum Dateiende). Das Original nimmt
// stur die ERSTE Datei, die sich laden laesst - Regionalversionen unterschieden
// sich nur durch die mitgelieferten Dateien. Wir merken uns die Liste und
// koennen jederzeit auf eine andere vorhandene Datei umschalten; SCRIPT_TEXT
// loest jeden Text live ueber gfStringOffset/gfStringWad auf, der Wechsel
// wirkt also sofort und ohne Neustart.
char* gfLanguageList;	// erster Name in der Liste (zeigt in gfScriptFile)
long gfLanguageCount;
long SprachWahl = -1;	// Registry "Language": -1 = Automatik (erste ladbare Datei)

static char* SprachDateiName(long idx)
{
	char* s = gfLanguageList;

	if (idx < 0 || idx >= gfLanguageCount)
		return 0;

	for (long i = 0; i < idx; i++)
		s += strlen(s) + 1;

	return s;
}

// Anzeigename fuers Optionsmenue - nur Zeichen, die die Spielschrift kann.
const char* SprachAnzeige(long idx)
{
	static const char* namen[8] = { "English", "Francais", "Deutsch", "Italiano", "Espanol", "English US", "Japanese", "Nederlands" };
	const char* datei = SprachDateiName(idx);

	if (!datei)
		return "---";

	if (idx < 8 && !_strnicmp(datei, "ENGLISH", 7)) return namen[0];
	if (!_strnicmp(datei, "FRENCH", 6))  return namen[1];
	if (!_strnicmp(datei, "GERMAN", 6))  return namen[2];
	if (!_strnicmp(datei, "ITALIAN", 7)) return namen[3];
	if (!_strnicmp(datei, "SPANISH", 7)) return namen[4];
	if (!_strnicmp(datei, "US.", 3))     return namen[5];
	if (!_strnicmp(datei, "JAPAN", 5))   return namen[6];
	if (!_strnicmp(datei, "DUTCH", 5))   return namen[7];
	return datei;
}

// Selbst ergaenzte Menuezeilen ("Language", "Level Select", ...) haben keine
// String-ID im Sprach-Wad - kleine eigene Tabelle je geladener Sprache. Nur
// Zeichen, die die Spielschrift ohne Escape-Codes kann (ASCII). Hinweis:
// die ITALIAN.DAT der Steam-Fassung ist nur eine Englisch-Kopie - wer sie
// noch hat, sieht fuenf italienische Zeilen im englischen Menue; mit einer
// echten Uebersetzung (der Nutzer hat eine) passt alles zusammen.
const char* EigenText(const char* engl)
{
	static const char* tabelle[][5] =
	{	// Englisch          Franzoesisch       Deutsch           Italienisch           Spanisch
		{ "Language",        "Langue",          "Sprache",        "Lingua",             "Idioma" },
		{ "Level Select",    "Choix du niveau", "Levelauswahl",   "Selezione livello",  "Seleccion de nivel" },
		{ "Pause",           "Pause",           "Pause",          "Pausa",              "Pausa" },
		{ "Gallery",         "Galerie",         "Galerie",        "Galleria",           "Galeria" },
		{ "tomb5 options",   "Options tomb5",   "tomb5-Optionen", "Opzioni tomb5",      "Opciones tomb5" },
		{ "Credits",         "Credits",         "Credits",        "Crediti",            "Creditos" },
	};

	long spalte;

	switch (Gameflow->Language)
	{
	case FRENCH:
		spalte = 1;
		break;
	case GERMAN:
		spalte = 2;
		break;
	case ITALIAN:
		spalte = 3;
		break;
	case SPANISH:
		spalte = 4;
		break;
	default:
		return engl;	// Englisch und US
	}

	for (long i = 0; i < (long)(sizeof(tabelle) / sizeof(tabelle[0])); i++)
		if (!strcmp(engl, tabelle[i][0]))
			return tabelle[i][spalte];

	return engl;
}

// Zeigertabelle aufbauen und Strings ent-XOR-en (0xA5).
//
// VORSICHT, Altlast: das Original verdichtete den Puffer mit TXT_NUM_STRINGS
// (309) als FESTER Groesse der Offsettabelle. Die stimmt aber nur fuer
// ENGLISH/US/FRENCH/ITALIAN/SPANISH (195 Haupt + 56 PSX + 58 PC = 309).
// GERMAN traegt 58 PSX-Eintraege (311 gesamt), JAPAN/DUTCH sogar 204+96+58
// (358) - dort begann der Text-Wad mitten in der Offsettabelle, jeder String
// las verschoben, und die Muell-Glyphen rissen den Zeichner um (Hard-Crash
// beim Sprachwechsel auf Deutsch). Vanilla fiel das nie auf, weil jede
// Regionalversion nur Dateien mit passender Zaehlung dabei hatte.
//
// Darum jetzt: Blockgrenzen aus dem DATEIKOPF, und die Enum-Indizes werden
// blockweise zugeordnet (das Enum folgt der englischen Aufteilung 195/56/58).
// Jede Datei traegt mindestens so viele Eintraege je Block - Ueberzaehlige
// haengen hinten am Block und bleiben einfach unbenutzt.
static void SprachPufferAufbereiten(char* d)
{
	STRINGHEADER sh;
	static ushort tabelle[TXT_NUM_STRINGS];
	char* s;
	char* e;
	long l, q;

	memcpy(&sh, d, sizeof(STRINGHEADER));

	const long nDatei = sh.nStrings + sh.nPSXStrings + sh.nPCStrings;
	ushort* off = (ushort*)(d + sizeof(STRINGHEADER));
	char* wad = d + sizeof(STRINGHEADER) + nDatei * sizeof(ushort);

	const long enumHaupt = 195;
	const long enumPSX = 56;

	for (long i = 0; i < TXT_NUM_STRINGS; i++)
	{
		if (i < enumHaupt)
			q = i;
		else if (i < enumHaupt + enumPSX)
			q = sh.nStrings + (i - enumHaupt);
		else
			q = sh.nStrings + sh.nPSXStrings + (i - enumHaupt - enumPSX);

		tabelle[i] = q < nDatei ? off[q] : 0;
	}

	gfLanguageFile = (uchar*)d;
	gfStringOffset = tabelle;
	gfStringWad = wad;

	// Entschluesselt wird ueber die DATEI-Offsets (alle nDatei Strings), damit
	// auch die Eintraege jenseits des Enums konsistent bleiben. Wie im
	// Original bleibt je String das letzte Byte (der unverschluesselte
	// NUL-Abschluss) unangetastet, und der allerletzte String der Datei
	// bleibt kodiert - der liegt ausserhalb jeder Enum-Zuordnung.
	for (long i = 0; i + 1 < nDatei; i++)
	{
		s = &wad[off[i]];
		e = &wad[off[i + 1]];
		l = e - s - 1;

		for (long j = 0; j < l; j++)
			s[j] ^= 0xA5;
	}
}

long WechsleSprache(long idx)
{
	STRINGHEADER sh;
	char* d;
	char* name = SprachDateiName(idx);

	if (!name || !*name || idx > 7)	// Gameflow->Language ist ein 3-Bit-Feld
		return 0;

	d = 0;

	if (!LoadFile(name, &d))
		return 0;

	// Nur Dateien im TR5-PC-Layout (195 Haupt- / 58 PC-Strings) sind dem
	// Enum zuordenbar. JAPAN.DAT und DUTCH.DAT sind in JEDER TR5-Fassung
	// (Steam wie die japanische PC-CD) in Wahrheit TOMB-RAIDER-4-Dateien:
	// Layout 204/96 wie TR4s English.dat, TR4-Itemnamen ("Hathor Effigy")
	// und TR4-PSX-Seriennummern (SLES-02238/-02243) mitten im Text. Ein
	// japanisches oder hollaendisches TR5-PC-Textpaket gab es nie - selbst
	// die japanische CD fiel beim Start auf ENGLISH.DAT zurueck. Geprueft
	// wird der KOPF, nicht der Name: eine echte Fassung im TR5-Layout
	// (so wie die vom Nutzer aufgetriebene ITALIAN.DAT) geht weiterhin.
	memcpy(&sh, d, sizeof(STRINGHEADER));

	if (sh.nStrings != 195 || sh.nPCStrings != 58)
	{
		free(d);
		return 0;
	}

	free(gfLanguageFile);
	SprachPufferAufbereiten(d);
	Gameflow->Language = idx;	// steuert auch die deutsche Tastaturtabelle (LoadSave.cpp)
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
	const long scriptGroesse = l;

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

	// Sprachliste vermessen: Namen liegen NUL-getrennt bis zum Dateiende.
	gfLanguageList = s;
	gfLanguageCount = 0;

	for (char* p = s; p < (char*)gfScriptFile + scriptGroesse && *p; p += strlen(p) + 1)
		gfLanguageCount++;

	// Wie das Original: die erste Datei nehmen, die sich laden laesst. Die
	// gespeicherte Wahl des Nutzers (SprachWahl) zieht WinMain nach dem
	// LoadSettings-Aufruf nach - die Registry ist hier noch nicht gelesen.
	for (l = 0;; l++)
	{
		d = 0;

		if (LoadFile(s, &d))
			break;

		s += strlen(s) + 1;
	}

	SprachPufferAufbereiten(d);
	Gameflow->Language = l;

	// Vanilla ist Slot == Identitaet; die Schleife unten ueberschreibt, was
	// die SCRIPT.DAT anders verteilt hat.
	for (int i = 0; i < 18; i++)
	{
		gfSlotVonId[i] = (uchar)i;
		gfIdVonSlot[i] = (uchar)i;
	}
	gfLetzterSlot = 0;

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
				// Slot -> Identitaet merken (Umkehrtabelle) und den letzten
				// Kampagnen-Slot bestimmen: dort kommt der Abspann, egal
				// welches Level der Randomizer ans Ende gelegt hat.
				{
					const uchar id = LevelIdVonDatei(n[3]);
					if (id < 18)
					{
						gfSlotVonId[id] = (uchar)i;
						if (i < 18)
							gfIdVonSlot[i] = id;
					}
					if (id >= LVL5_STREETS_OF_ROME && id <= LVL5_RED_ALERT && i > gfLetzterSlot)
						gfLetzterSlot = (uchar)i;
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

// Steht die Kampagne noch in Vanilla-Reihenfolge? Der Randomizer permutiert
// die Bloecke der SCRIPT.DAT; gfIdVonSlot bildet Slot -> Identitaet ab und
// ist vanilla die Identitaet selbst. Gebraucht von setup.cpp, um die vier
// eingebauten Kapitel-Inventarloeschungen nur dann anzuwenden, wenn sie
// noch Kapitelanfaenge sind.
long ReihenfolgeGewuerfelt()
{
	for (int s = 1; s < 18; s++)
		if (gfIdVonSlot[s] != s)
			return 1;

	return 0;
}

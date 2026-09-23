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
#include "../game/menu_frame.h"	// the options menu, one table-driven frame for every page
#include "../game/key_scheme.h"	// KeySchemeConflicts - the conflict rule, as arithmetic
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

/* The conflict-marking double loop lives in game\key_scheme.h now, so
 * MenuTest can call it too; it compares against the ACTIVE scheme's default
 * row rather than layout[0] - which for Tank IS layout[0], so the result is
 * unchanged there.
 *
 * The third argument lets the Secondary slot claim a key only while that role
 * is live. The fourth does the same for the five cheat roles: with Cheats off
 * they are not offered and nothing reads them. See KeySchemeConflicts. */
void CheckKeyConflicts()
{
	KeySchemeConflicts(layout[1], KeyDefaultRow(), conflict, tomb5.action_split ? 1 : 0, tomb5.enable_cheats ? 1 : 0);
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

/* The options menu is a table now, driven from game\menu_frame.cpp. Each page
 * is a row table walked and drawn by one shared piece of code, in place of a
 * `menu` state of 0/1/100/200/201, a one-hot cursor bit mask, a separate
 * hand-written cursor walk per page, and a Controls page that shrank the
 * global font_height to fit its rows. This function stays the entry point,
 * called from the title screen (game\gameflow.cpp) and from the pause menu
 * (S_DisplayPauseMenu, below).
 */
void DoOptions()
{
	MenuFrameRun();
}

/* The special features page stays as it was: it reads three file-statics of
 * this file (SpecialFeaturesPage, NumSpecialFeatures, SpecialFeaturesNum),
 * unchanged. It is a page of the table-driven menu like any other, only its
 * inside logic remains its own. Returns 1 once the player has left it, and
 * the frame then walks back to the Options page and puts the cursor on the
 * row that opened this one.
 */
long SpecialFeaturesPageRun()
{
	static ulong sel = 1;
	long f;

	f = font_height - 4;

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
		sel = 1;
		dbinput &= ~IN_DESELECT;
		return 1;
	}

	return 0;
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

		// Circle/B backs out of here too, the convention every other menu
		// screen follows. Escape keeps its own path below: it carries
		// IN_OPTION as well, and that is read raw exactly as before.
		//
		// Acted on the RELEASE, not the press, for two reasons:
		//
		//  - the press that opened this screen must not close it again. A
		//    button already down when the loop starts never sets the
		//    debounce, so its release does nothing.
		//  - this screen usually sits inside the inventory ring, and the
		//    ring also closes on the release of Circle (newinv2.cpp).
		//    Leaving on the press would hand the ring the release a frame
		//    later and shut it behind us - one press, two consumers.
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
// transparent frame in LOGO_DE/EU/FR/US.DDS - so without this the logo screen
// would show the wordmark on black. A manually composited file cannot be
// shipped, since it is remaster material, so the engine performs the same
// composition at load time from the player's own two files. Whatever is
// missing simply drops out of the result.
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

/* The accented letters of the French and German rows are hex escapes on purpose:
 * the engine's own font takes the single CP-1252 bytes E9 and FC (AccentToCode,
 * game\text.cpp), and an editor that saves this file as UTF-8 turns them into the
 * replacement character. The literal is split where a hex digit follows the escape ("Zur\xFC" "ck"),
 * because "\xFCc" would be read as a single escape. */
static const char* GetStupidText(long type)	//0: next, 1: both, 2: previous
{
	static char buf[32];

	switch (Gameflow->Language)
	{
	case FRENCH:

		if (!type)
			strcpy(buf, "Ensuite \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Pr\xE9" "c\xE9" "dent / Ensuite \x1b");
		else
			strcpy(buf, "\x19 Pr\xE9" "c\xE9" "dent");

		break;

	case GERMAN:

		if (!type)
			strcpy(buf, "Weiter \x1B");
		else if (type == 1)
			strcpy(buf, "\x19 Zur\xFC" "ck / Weiter \x1b");
		else
			strcpy(buf, "\x19 Zur\xFC" "ck");

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

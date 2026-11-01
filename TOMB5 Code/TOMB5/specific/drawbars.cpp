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
#include "drawbars.h"
#include "output.h"
#include "3dmath.h"
#include "function_table.h"
#include "gamemain.h"
#include "../tomb5/tomb5.h"
#include "../game/gameflow.h"
#include "../game/lara.h"
#include "../game/camera.h"
#include "drawroom.h"
#include "polyinsert.h"
#include "winmain.h"
#include "texture.h"
#include "LoadSave.h"	// DrawLoadingScreen - the picture under the load bar
#include "loadbar_math.h"	// LOADBAR_*, the brake, the fade, the name
#include "time.h"		// TIME_SubTick - a tick passed, without waiting
#include "profile.h"		// ProfPresent - the held present
#include "../game/text.h"	// GnFrameCounter
#include "../game/interpolate.h"	// InterpNoteClassicPresent
#include "hd/hd_level.h"	// HDFramePresented - the frame clock of -log
#include "dxshell.h"		// DXGammaStart - the held present's gamma pass
#include "function_stubs.h"	// Log, gLogEnabled - the LOADFRAME line

static GouraudBarColourSet healthBarColourSet =
{
	{ 64, 96, 128, 96, 64 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 128, 192, 255, 192, 128 },
	{ 0, 0, 0, 0, 0 }
};

static GouraudBarColourSet poisonBarColourSet =
{
	{ 64, 96, 128, 96, 64 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 64, 96, 128, 96, 64 },
	{ 0, 0, 0, 0, 0 },
	{ 128, 192, 255, 192, 128 }
};

static GouraudBarColourSet airBarColourSet =
{
	{ 0, 0, 0, 0, 0 },
	{ 113, 146, 113, 93, 74 },
	{ 123, 154, 123, 107, 91 },
	{ 0, 0, 0, 0, 0 },
	{ 113, 146, 113, 93, 74 },
	{ 0, 0, 0, 0, 0 }
};

static GouraudBarColourSet dashBarColourSet =
{
	{ 144, 192, 240, 192, 144 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 144, 192, 240, 192, 144 },
	{ 144, 192, 240, 192, 144 },
	{ 0, 0, 0, 0, 0 }
};

static GouraudBarColourSet loadBarColourSet =
{
	{ 48, 96, 127, 80, 32 },
	{ 0, 0, 0, 0, 0 },
	{ 48, 96, 127, 80, 32 },
	{ 0, 0, 0, 0, 0 },
	{ 48, 96, 127, 80, 32 },
	{ 48, 96, 127, 80, 32 }
};

static GouraudBarColourSet enemyBarColourSet =
{
	{ 128, 192, 255, 192, 128 },
	{ 64, 96, 128, 96, 64 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 123, 154, 123, 107, 91 },
	{ 0, 0, 0, 0, 0 }
};

long loadbar_on;
static float loadbar_steps;
static float loadbar_pos;
static long loadbar_maxpos;

/* The loading screen of the load under way (S_LoadScreenBegin): its state
 * (the Loadbar style row as the load began, TR5 for a load that builds
 * with the row off), for a short load without a screen the fade of the
 * last frame to black - whether it runs, since when and over how long -
 * and the frames drawn. */
static long loadbar_mode;
static long loadbar_fading;
static double loadbar_fade_at;
static double loadbar_fade_ms;
static long loadbar_frames;

/* The brake's inputs (loadbar_math.h): while a level load's screen is up,
 * its first frame's time, the least time it stands (loadbar_min_ms), the
 * loading thread's share of the work and the HD build's part done; and
 * for the LOADLOG lines the frames the build's laps drew, their time, and
 * the time from the work's end to the last frame. */
static long loadbar_loading;
static double loadbar_start;
static double loadbar_min_ms;
static float loadbar_share;
static float loadbar_build;
static long loadbar_build_frames;
static double loadbar_build_ms;
static double loadbar_wait_ms;

/* The main thread's reports (S_LoadBarBuild, S_LoadScreenStep): how many
 * steps the HD build has of its own and how many points are done. */
static long loadbar_build_total;
static long loadbar_build_done;

/* From the loading thread's join to the tail (S_LoadScreenJoin,
 * S_LoadScreenEnd) the frames present held: a lost device is noted
 * (loadbar_lost), and no further frame is drawn until the tail, whose
 * present reports it to the window as every other frame's does. */
static long loadbar_held;
static long loadbar_lost;

/* The part of the load the frame being drawn belongs to, for its LOADFRAME
 * line (S_LoadFrameSeam): the first frame - the picture, drawn before the
 * loading thread starts - the thread's frames, the frame at its join, the
 * main thread's work after it, and the brake's tail. */
enum
{
	LOADPHASE_FIRST,
	LOADPHASE_THREAD,
	LOADPHASE_JOIN,
	LOADPHASE_BUILD,
	LOADPHASE_TAIL
};

static long loadbar_phase;

/* The clock of the loading screen, in ms: QueryPerformanceCounter, the
 * counter Sync() paces the frames by and the LOADLOG lines read. */
static double LoadScreenClockMs()
{
	LARGE_INTEGER now, rate;

	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&rate);
	return rate.QuadPart ?
		(double)now.QuadPart * 1000.0 / (double)rate.QuadPart : 0.0;
}

/* The same clock on Sync()'s grid: the last 1/60 s tick boundary now
 * stands past, in ms. The brake's time is the span between two of them. */
static double LoadScreenTickMs()
{
	const double sub = TIME_SubTick();

	return LoadScreenClockMs() - (sub - floor(sub)) * kLoadBarTickMs;
}

/* A frame's present between the join and the tail: what S_DumpScreenFrame
 * and DXShowFrame do, without the screenshot key, and without the report
 * of a lost device to the window. The new device the window makes for it
 * releases the HD path's objects (HDLevelDeviceLost), and the HD build,
 * which these frames interleave, still has steps to run on them; a lost
 * device stays lost, so the tail's first present finds it again after the
 * build and reports it there, and the window waits for the end of the
 * load. The tick was waited out by S_DrawLoadBar; Sync() hands it out
 * here, as in S_DumpScreenFrame. The Gamma row's pass and its copy back go
 * around the present as in DXShowFrame, so that these frames carry the
 * curve as every other frame does. */
static void LoadBarPresentHeld()
{
	InterpNoteClassicPresent();
	Sync();
	GnFrameCounter++;
	_EndScene();

	const bool curved = DXGammaStart();
	const HRESULT hr = App.dx.lpD3DDevice->Present(0, 0, 0, 0);

	HDFramePresented();
	DXGammaFinish(curved, hr);
	ProfPresent();
	App.dx.DoneBlit = 1;

	if (hr == D3DERR_DEVICELOST)
		loadbar_lost = 1;
}

/* Black over the whole window at `alpha`, into the sort list: the same
 * quad and draw type as the screen fade's (DoScreenFade), so the third
 * state's fade looks as the engine's own fades do. */
static void LoadFadeQuad(long alpha)
{
	D3DTLVERTEX v[4];
	TEXTURESTRUCT tex;
	const ulong c = (ulong)alpha << 24;

	v[0].sx = 0;
	v[0].sy = 0;
	v[1].sx = float(phd_winxmax + 1);
	v[1].sy = (float)phd_winymin;
	v[2].sx = float(phd_winxmax + 1);
	v[2].sy = float(phd_winymax + 1);
	v[3].sx = (float)phd_winxmin;
	v[3].sy = float(phd_winymax + 1);

	for (int i = 0; i < 4; i++)
	{
		v[i].sz = 0;
		v[i].rhw = f_moneoznear;
		v[i].color = c;
		v[i].specular = 0xFF000000;
	}

	tex.drawtype = 3;
	tex.flag = 0;
	tex.tpage = 0;
	clipflags[0] = 0;
	clipflags[1] = 0;
	clipflags[2] = 0;
	clipflags[3] = 0;
	AddQuadSorted(v, 0, 1, 2, 3, &tex, 0);
}

static void DrawColoredRect(float x0, float y0, float x1, float y1, float z, ulong c0, ulong c1, ulong c2, ulong c3, TEXTURESTRUCT* tex)
{
	D3DTLVERTEX* v;

	v = aVertexBuffer;

	v[0].sx = x0;
	v[0].sy = y0;
	v[0].color = RGBA_SETALPHA(c0, 0xFF);

	v[1].sx = x1;
	v[1].sy = y0;
	v[1].color = RGBA_SETALPHA(c1, 0xFF);

	v[2].sx = x1;
	v[2].sy = y1;
	v[2].color = RGBA_SETALPHA(c2, 0xFF);

	v[3].sx = x0;
	v[3].sy = y1;
	v[3].color = RGBA_SETALPHA(c3, 0xFF);

	for (int i = 0; i < 4; i++)
	{
		v[i].sz = z;
		v[i].rhw = f_mpersp / z * f_moneopersp;
		v[i].specular = 0xFF000000;
	}

	AddQuadSorted(v, 0, 1, 2, 3, tex, 0);
}

static void S_DrawGouraudBar(long x, long y, long width, long height, long pos, GouraudBarColourSet* colour)
{
	TEXTURESTRUCT tex;
	float bar, max, h, x0, y0, x1, y1;
	long p, r, g, b, c0, c1, c2, c3;

	clipflags[0] = 0;
	clipflags[1] = 0;
	clipflags[2] = 0;
	clipflags[3] = 0;
	tex.drawtype = 0;
	tex.tpage = 0;

	h = (float)height / 3.0F;
	max = (float)pos / 100.0F;
	bar = (float)width * max;

	x0 = (float)x;
	y0 = (float)y;
	x1 = x + bar;
	y1 = y + h;

	r = colour->abLeftRed[0];
	g = colour->abLeftGreen[0];
	b = colour->abLeftBlue[0];
	r -= r >> 2;
	g -= g >> 2;
	b -= b >> 2;
	c2 = RGBONLY(r, g, b);

	r = (long)((1 - max) * colour->abLeftRed[0] + max * colour->abRightRed[0]);
	g = (long)((1 - max) * colour->abLeftGreen[0] + max * colour->abRightGreen[0]);
	b = (long)((1 - max) * colour->abLeftBlue[0] + max * colour->abRightBlue[0]);
	r -= r >> 2;
	g -= g >> 2;
	b -= b >> 2;
	c3 = RGBONLY(r, g, b);

	DrawColoredRect(x0, y0, x1, y1, f_mznear, 0, 0, c3, c2, &tex);

	for (int i = 0; i < 4; i++)
	{
		c0 = RGBONLY(colour->abLeftRed[i], colour->abLeftGreen[i], colour->abLeftBlue[i]);
		r = (long)((1 - max) * colour->abLeftRed[i] + max * colour->abRightRed[i]);
		g = (long)((1 - max) * colour->abLeftGreen[i] + max * colour->abRightGreen[i]);
		b = (long)((1 - max) * colour->abLeftBlue[i] + max * colour->abRightBlue[i]);
		c1 = RGBONLY(r, g, b);
		c2 = RGBONLY(colour->abLeftRed[i + 1], colour->abLeftGreen[i + 1], colour->abLeftBlue[i + 1]);
		r = (long)((1 - max) * colour->abLeftRed[i + 1] + max * colour->abRightRed[i + 1]);
		g = (long)((1 - max) * colour->abLeftGreen[i + 1] + max * colour->abRightGreen[i + 1]);
		b = (long)((1 - max) * colour->abLeftBlue[i + 1] + max * colour->abRightBlue[i + 1]);
		c3 = RGBONLY(r, g, b);

		y0 += h;
		y1 += h;
		DrawColoredRect(x0, y0, x1, y1, f_mznear, c0, c1, c3, c2, &tex);
	}

	r = colour->abLeftRed[4];
	g = colour->abLeftGreen[4];
	b = colour->abLeftBlue[4];
	r -= r >> 2;
	g -= g >> 2;
	b -= b >> 2;
	c0 = RGBONLY(r, g, b);

	r = (long)((1 - max) * colour->abLeftRed[4] + max * colour->abRightRed[4]);
	g = (long)((1 - max) * colour->abLeftGreen[4] + max * colour->abRightGreen[4]);
	b = (long)((1 - max) * colour->abLeftBlue[4] + max * colour->abRightBlue[4]);
	r -= r >> 2;
	g -= g >> 2;
	b -= b >> 2;
	c1 = RGBONLY(r, g, b);

	y0 += h;
	y1 += h;
	DrawColoredRect(x0, y0, x1, y1, f_mznear, c0, c1, 0, 0, &tex);

	x0 = (float)x;
	y0 = (float)y;
	x1 = float(x + width);
	y1 = y + (h * 6);
	p = GetFixedScale(1);

	DrawColoredRect(x0 - p, y0, x1 + p, y1, f_mznear + 1, 0, 0, 0, 0, &tex);
	DrawColoredRect(x0 - (2 * p), y0 - p, x1 + (2 * p), y1 + p, f_mznear + 2, 0xFF508282, 0xFFA0A0A0, 0xFF508282, 0xFFA0A0A0, &tex);
	DrawColoredRect(x0 - (3 * p), y0 + p, x1 + (3 * p), y1 - p, f_mznear + 3, 0xFF284141, 0xFF505050, 0xFF284141, 0xFF505050, &tex);
}

static void S_DoTR4Bar(long x, long y, long width, long height, long pos, long c1, long c2)
{
	TEXTURESTRUCT tex;
	long p, xw, y2, bar;

	clipflags[0] = 0;
	clipflags[1] = 0;
	clipflags[2] = 0;
	clipflags[3] = 0;
	tex.drawtype = 0;
	tex.flag = 0;
	tex.tpage = 0;

	p = GetFixedScale(1);
	xw = x + width;
	y2 = y + height;
	bar = width * pos / 100;

	DrawColoredRect((float)x, (float)y, float(x + bar), float(y2), f_mznear, c1, c1, c2, c2, &tex);
	DrawColoredRect((float)x, float(y2), float(x + bar), float(y2 + height), f_mznear, c2, c2, c1, c1, &tex);

	DrawColoredRect((float)x, (float)y, (float)xw, float(y2 + height), f_mznear + 1, 0, 0, 0, 0, &tex);
	DrawColoredRect(float(x - p), float(y - p), float(xw + p), float(y2 + height + p), f_mznear + 2, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, &tex);
}

static void DoBar(long x, long y, long width, long height, long pos, long clr1, long clr2)
{
	TEXTURESTRUCT tex;
	float r1, g1, b1, r2, g2, b2, r, g, b, mul;
	long bar, y2, p, lr, lg, lb, c0, c1, c2, c3;

	clipflags[0] = 0;
	clipflags[1] = 0;
	clipflags[2] = 0;
	clipflags[3] = 0;
	tex.drawtype = 0;
	tex.tpage = 0;

	p = GetFixedScale(1);
	y2 = y + height;
	bar = width * pos / 100;

	r1 = (float)CLRR(clr1);
	g1 = (float)CLRG(clr1);
	b1 = (float)CLRB(clr1);
	r2 = (float)CLRR(clr2);
	g2 = (float)CLRG(clr2);
	b2 = (float)CLRB(clr2);

	mul = (float)bar / (float)width;
	r = r1 + ((r2 - r1) * mul);
	g = g1 + ((g2 - g1) * mul);
	b = b1 + ((b2 - b1) * mul);

	lr = (long)r1;
	lg = (long)g1;
	lb = (long)b1;
	c0 = RGBONLY(lr >> 1, lg >> 1, lb >> 1);
	c2 = RGBONLY(lr, lg, lb);

	lr = (long)r;
	lg = (long)g;
	lb = (long)b;
	c1 = RGBONLY(lr >> 1, lg >> 1, lb >> 1);
	c3 = RGBONLY(lr, lg, lb);

	DrawColoredRect((float)x, (float)y, float(x + bar), (float)y2, f_mznear, c0, c1, c3, c2, &tex);
	DrawColoredRect((float)x, (float)y2, float(x + bar), float(y2 + height), f_mznear, c2, c3, c1, c0, &tex);

	DrawColoredRect((float)x, (float)y, float(x + width), float(y2 + height), f_mznear + 1, 0, 0, 0, 0, &tex);
	DrawColoredRect(float(x - p), float(y - p), float(x + width + p), float(y2 + height + p), f_mznear + 2, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, &tex);
}

static void S_DrawHealthBar2(long pos)
{
	long x, y, w, h;

	w = GetFixedScale(150);
	h = GetFixedScale(6);
	x = phd_centerx - GetFixedScale(75);
	y = GetFixedScale(100);

	if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, (lara.poisoned || lara.Gassed) ? &poisonBarColourSet : &healthBarColourSet);
	else if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, (lara.poisoned || lara.Gassed) ? 0xFFFFFF00 : 0xFFFF0000);
	else
		DoBar(x, y, w, h, pos, 0xA00000, (lara.poisoned || lara.Gassed) ? 0xA0A000 : 0x00A000);
}

void S_DrawHealthBar(long pos)
{
	long x, y, w, h;

	if (gfLevelId == LVL5_TITLE)
		return;

	if (BinocularRange || SniperOverlay)
	{
		S_DrawHealthBar2(pos);
		return;
	}

	w = GetFixedScale(150);
	h = GetFixedScale(6);

	if (tomb5.bars_pos == 1 || tomb5.bars_pos == 2)//original or improved
	{
		x = GetFixedScale(8);
		y = GetFixedScale(8);
	}
	else
	{
		x = GetFixedScale(36);
		x = phd_winwidth - w - x;
		y = GetFixedScale(18);
	}

	if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, (lara.poisoned || lara.Gassed) ? 0xFFFFFF00 : 0xFFFF0000);
	else if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, (lara.poisoned || lara.Gassed) ? &poisonBarColourSet : &healthBarColourSet);
	else
		DoBar(x, y, w, h, pos, 0xA00000, (lara.poisoned || lara.Gassed) ? 0xA0A000 : 0x00A000);
}

void S_DrawAirBar(long pos)
{
	long x, y, w, h;

	if (gfLevelId == LVL5_TITLE)
		return;

	w = GetFixedScale(150);
	h = GetFixedScale(6);

	if (tomb5.bars_pos == 1)//original
	{
		x = phd_winwidth - w - GetFixedScale(8);
		y = GetFixedScale(25);
	}
	else if (tomb5.bars_pos == 2)//improved
	{
		x = phd_winwidth - w - GetFixedScale(8);
		y = GetFixedScale(8);
	}
	else
	{
		x = GetFixedScale(36);
		x = phd_winwidth - w - x;
		y = GetFixedScale(43);
	}

	if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, 0xFF0000FF);
	else if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, &airBarColourSet);
	else
		DoBar(x, y, w, h, pos, 0x0000A0, 0x0050A0);
}

void S_DrawDashBar(long pos)
{
	long x, y, w, h;

	if (gfLevelId == LVL5_TITLE)
		return;

	w = GetFixedScale(150);
	h = GetFixedScale(6);

	if (tomb5.bars_pos == 1)//original
	{
		x = phd_winwidth - w - GetFixedScale(8);
		y = GetFixedScale(8);
	}
	else if (tomb5.bars_pos == 2)//improved
	{
		x = phd_winwidth - w - GetFixedScale(8);
		y = GetFixedScale(25);
	}
	else
	{
		x = GetFixedScale(36);
		x = phd_winwidth - w - x;
		y = GetFixedScale(68);
	}

	if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, 0xFF00FF00);
	else if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, &dashBarColourSet);
	else
		DoBar(x, y, w, h, pos, 0xA0A000, 0x00A000);
}

static void S_DrawEnemyBar2(long pos)
{
	long x, y, w, h;

	w = GetFixedScale(150);
	h = GetFixedScale(6);
	x = phd_centerx - GetFixedScale(75);
	y = GetFixedScale(117);

	if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, &enemyBarColourSet);
	else if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, 0xFFFFA000);
	else
		DoBar(x, y, w, h, pos, 0xA00000, 0xA0A000);
}

void S_DrawEnemyBar(long pos)
{
	long x, y, w, h;

	if (BinocularRange)
	{
		S_DrawEnemyBar2(pos);
		return;
	}

	w = GetFixedScale(150);
	h = GetFixedScale(6);

	if (tomb5.bars_pos == 1 || tomb5.bars_pos == 2)//original or improved
	{
		x = GetFixedScale(8);
		y = GetFixedScale(25);
	}
	else
	{
		x = GetFixedScale(36);
		x = phd_winwidth - w - x;
		y = GetFixedScale(93);
	}

	if (tomb5.bar_mode == 3)
		S_DrawGouraudBar(x, y, w, h, pos, &enemyBarColourSet);
	else if (tomb5.bar_mode == 2)
		S_DoTR4Bar(x, y, w, h, pos, 0xFF000000, 0xFFFFA000);
	else
		DoBar(x, y, w, h, pos, 0xA00000, 0xA0A000);
}

/* A level load's loading screen begins, right after its first frame (the
 * picture, in the TR5 and TR4 states): `mode` is the screen's state
 * (LOADBAR_*, file.cpp's LoadScreenModeOf), the row's own except TR5 for
 * a load that builds with the row off; with LOADBAR_OFF, a short load
 * without a screen, `fade` says whether the last frame is faded to black
 * over `fadeMs` (0 a cut), from now on. `minMs` is the
 * brake, the least time the screen stands; `hd` says whether an HD build
 * follows the loading thread, which then has its share of the bar
 * (kLoadBarThreadShare) and otherwise all of it. The brake's clock starts
 * on the tick of the first frame. */
void S_LoadScreenBegin(long mode, long fade, double fadeMs, double minMs,
	long hd)
{
	loadbar_mode = mode;
	loadbar_fading = mode == LOADBAR_OFF && fade;
	loadbar_fade_at = LoadScreenClockMs();
	loadbar_fade_ms = fadeMs;
	loadbar_frames = mode != LOADBAR_OFF;
	loadbar_loading = 1;
	loadbar_phase = LOADPHASE_THREAD;
	loadbar_start = LoadScreenTickMs();
	loadbar_min_ms = minMs;
	loadbar_share = hd ? kLoadBarThreadShare : 1.0F;
	loadbar_build = 0;
	loadbar_build_total = kLoadBarBuildLaps;
	loadbar_build_done = 0;
	loadbar_build_frames = 0;
	loadbar_build_ms = 0;
	loadbar_wait_ms = 0;
	loadbar_held = 0;
	loadbar_lost = 0;
}

/* A frame of the main thread's work after the join, only when a whole
 * 1/60 s tick has passed since the last one Sync() handed out, so that it
 * never waits: the work goes on at once. `build` counts it as the HD
 * build's for the LOADLOG line. */
static void LoadScreenFrameDue(long build)
{
	if (!(App.dx.Flags & DXF_HWR) || !loadbar_on || !S_LoadScreenDraws() ||
		loadbar_lost || TIME_SubTick() < 1.0)
		return;

	const double at = LoadScreenClockMs();

	S_DrawLoadBar();

	if (build)
	{
		loadbar_build_frames++;
		loadbar_build_ms += LoadScreenClockMs() - at;
	}
}

/* The HD build's progress, from its laps or the resident build's steps
 * (hd_level.cpp): `done` of its `total` steps, and a frame if one is due.
 * Nothing outside a level load's screen (the rebuild after a new device
 * runs the same build). */
void S_LoadBarBuild(long done, long total)
{
	if (!loadbar_loading)
		return;

	loadbar_build_total = total;
	loadbar_build_done = done;
	loadbar_build = LoadBarBuildShare(done, total);
	LoadScreenFrameDue(1);
}

/* A step of the main thread's work after the join has ended (file.cpp,
 * S_LoadLevelFile; LOADSTEP_*), and a frame if one is due. The build's
 * end, the font and the cutscenes are points of the work's table
 * (loadbar_math.h) and move the bar; aInit and the warmup only give a
 * frame its chance. */
void S_LoadScreenStep(long step)
{
	if (!loadbar_loading)
		return;

	if (step == LOADSTEP_BUILD || step == LOADSTEP_FONT ||
		step == LOADSTEP_CUTSCENES)
	{
		const long done = loadbar_build_total + 1 + step - LOADSTEP_BUILD;

		if (done > loadbar_build_done)
		{
			loadbar_build_done = done;
			loadbar_build = LoadBarBuildShare(done, loadbar_build_total);
		}
	}

	LoadScreenFrameDue(step == LOADSTEP_BUILD);
}

/* The loading thread has joined; from here to the tail the frames present
 * held (LoadBarPresentHeld). Without a screen (the row off, a short
 * load), a fade still running - a thread shorter than loadfade_ms - ends
 * here in one black frame: the HD build after it draws a frame only at
 * the ends of its steps, the longest of which take most of a second, and
 * the half faded frame would stand that long. That frame waits for its
 * tick, at most 1/60 s. In the TR5 and TR4 states a frame is drawn if
 * one is due. */
void S_LoadScreenJoin()
{
	loadbar_held = loadbar_loading;
	loadbar_phase = LOADPHASE_JOIN;

	if (!loadbar_fading || !(App.dx.Flags & DXF_HWR))
	{
		S_LoadScreenStep(LOADSTEP_JOIN);
		loadbar_phase = LOADPHASE_BUILD;
		return;
	}

	loadbar_fade_ms = 0;
	S_DrawLoadBar();
	loadbar_phase = LOADPHASE_BUILD;
}

/* The work is done: the brake's tail and the last frame. The bar runs on
 * with the clock, one frame a tick, until the screen has stood minMs; the
 * last frame is the full bar - at minMs, or at the next tick when the work
 * took longer. Then the screen is over and the build's frames stop. The
 * tail presents as every frame does again: a device lost in the work is
 * reset by its first present. */
void S_LoadScreenEnd()
{
	const double from = LoadScreenClockMs();

	loadbar_held = 0;
	loadbar_phase = LOADPHASE_TAIL;
	S_FinishLoadBar();

	if (App.dx.Flags & DXF_HWR && loadbar_mode != LOADBAR_OFF)
		while (!S_DrawLoadBar());

	loadbar_wait_ms = LoadScreenClockMs() - from;
	loadbar_loading = 0;
	loadbar_phase = LOADPHASE_FIRST;	// the next load's first frame
}

// What this load's screen drew, for the LOADLOG lines.
void S_LoadScreenStatsGet(LoadScreenStats* out)
{
	out->frames = loadbar_frames;
	out->build_frames = loadbar_build_frames;
	out->build_ms = loadbar_build_ms;
	out->wait_ms = loadbar_wait_ms;
}

/* Whether the load draws a frame now: the bar in the TR5 and TR4 states,
 * and without a screen (the row off, a short load) only while the fade
 * runs. */
long S_LoadScreenDraws()
{
	return loadbar_mode != LOADBAR_OFF || loadbar_fading;
}

// game\text.cpp: the accents PrintString composes over a letter.
extern char AccentTable[46][2];

/* The name of the level in `slot` at the top centre of the frame being
 * drawn, in the game's font (PrintString: the HD atlas when the HD path
 * draws), its highest ink kLoadNameMargin under the top edge at this
 * screen size (loadbar_math.h). Only where the level's texture pages
 * stand - the font's classic page is one of them. */
void S_LoadScreenName(long slot)
{
	const long reach = LoadNameReach(CharDef, 106, AccentTable, 46);

	PrintString(phd_centerx, LoadNameY(GetFixedScale(kLoadNameMargin),
		reach), (uchar)kLoadNameStyle, SCRIPT_TEXT(gfLevelNames[slot]),
		FF_CENTER);
}

/* The load's first frame, drawn with the name, read back as the loading
 * picture before it is presented: every later frame draws that picture
 * and no text of its own, since the loading thread frees the level's
 * texture pages - the classic font's among them (DrawChar's nTextures - 2)
 * - while those frames draw. The scene is ended for the read and begun
 * again for the present. 1 when the picture was made; otherwise the one
 * before stays up, without the name. */
long S_LoadScreenKeepName()
{
	const char was = MonoScreenOn;
	long made;

	_EndScene();
	made = CaptureLoadingScreen();
	_BeginScene();

	if (!made)
		MonoScreenOn = was;

	return made;
}


void S_InitLoadBar(long max)
{
	loadbar_steps = 0;
	loadbar_maxpos = max;
	loadbar_pos = 0;
	loadbar_on = 1;
}

void S_UpdateLoadBar()
{
	loadbar_steps = 100.0F / loadbar_maxpos + loadbar_steps;
}

/* LOADFRAME, with -log: one line per frame of a level load's screen, after
 * its present - the device as the frame found it, before the picture's
 * bind, and what the present's gamma pass drew with:
 *   LOADFRAME n=<frame> phase=<first|thread|join|build|tail>
 *     pic=<hd|classic|view|none> tex0=<entry|none|other> ps=<set|none>
 *     vs=<set|none> stage1=<set|none> stage2=<set|none> nTextures=<n>
 *     name_page=<n-2> bar_page=<n-1> gamma=<off|copy|entry|none|other>
 * n counts the load's frames from its first, 0. pic: the HD picture, a
 * classic one, the first frame read back with the level's name (view), or
 * none. tex0: the entry of Textures[] bound at stage 0, none, or another
 * texture. ps and vs: a shader set; stage1 and stage2: a texture there.
 * name_page and bar_page: the entries a classic name (DrawChar) and a
 * slider (DoSlider) would take at that moment. gamma: what stage 0 held as
 * the gamma pass drew - its own copy of the frame, an entry, none, another
 * texture - or off without the pass. Without -log nothing is read. */
static struct
{
	char tex0[16];
	long ps, vs, stage1, stage2;
	long textures;
	long pic;
} loadframe_look;

/* A texture of stage 0 by name: its entry of Textures[], none or other.
 * Every entry is asked, not only those below nTextures, which the loading
 * thread moves while the frames draw. */
static void LoadFrameTexName(const void* t, char* out, size_t size)
{
	snprintf(out, size, "%s", t ? "other" : "none");

	for (long i = 0; t && i < long(sizeof(Textures) / sizeof(Textures[0]));
		i++)
	{
		if ((const void*)Textures[i].tex == t)
		{
			snprintf(out, size, "%ld", i);
			break;
		}
	}
}

// The device as the frame finds it, for the LOADFRAME line.
static void LoadFrameLook()
{
	LPDIRECT3DBASETEXTURE9 t[3] = { 0, 0, 0 };
	LPDIRECT3DPIXELSHADER9 ps = 0;
	LPDIRECT3DVERTEXSHADER9 vs = 0;

	for (int i = 0; i < 3; i++)
		App.dx.lpD3DDevice->GetTexture(i, &t[i]);

	App.dx.lpD3DDevice->GetPixelShader(&ps);
	App.dx.lpD3DDevice->GetVertexShader(&vs);
	LoadFrameTexName(t[0], loadframe_look.tex0, sizeof(loadframe_look.tex0));
	loadframe_look.ps = ps != 0;
	loadframe_look.vs = vs != 0;
	loadframe_look.stage1 = t[1] != 0;
	loadframe_look.stage2 = t[2] != 0;
	loadframe_look.textures = nTextures;
	loadframe_look.pic = LoadingPictureKind();

	for (int i = 0; i < 3; i++)
	{
		if (t[i])
			t[i]->Release();
	}

	if (ps)
		ps->Release();

	if (vs)
		vs->Release();
}

/* The start of a frame of the loading screen, before the picture's bind:
 * S_DrawLoadBar's under the stage lock, the first frame's in
 * S_LoadLevelFile. The frame draws with the fixed function whatever the
 * frame or the build step before it left: no shader, the classic vertex
 * format, and no texture on stages 1 and 2, where the HD room bracket
 * binds its light field. Stage 0 is set by each draw of the frame. */
void S_LoadFrameBegin()
{
	const LPDIRECT3DDEVICE9 dev = App.dx.lpD3DDevice;

	if (gLogEnabled)
		LoadFrameLook();

	dev->SetVertexShader(0);
	dev->SetPixelShader(0);
	dev->SetFVF(FVF);
	dev->SetTexture(1, 0);
	dev->SetTexture(2, 0);
}

// The LOADFRAME line of the frame just presented (above).
void S_LoadFrameSeam()
{
	static const char* const phase[] = { "first", "thread", "join", "build",
		"tail" };
	static const char* const pic[] = { "none", "classic", "hd", "view" };
	const void* seen = 0;
	char gamma[16];

	if (!gLogEnabled)
		return;

	const long drew = DXGammaSeen(&seen);
	const long kind = loadframe_look.pic >= 0 && loadframe_look.pic < 4 ?
		loadframe_look.pic : 0;

	if (drew == 2)
		LoadFrameTexName(seen, gamma, sizeof(gamma));
	else
		snprintf(gamma, sizeof(gamma), "%s", drew ? "copy" : "off");

	Log("LOADFRAME n=%ld phase=%s pic=%s tex0=%s ps=%s vs=%s stage1=%s "
		"stage2=%s nTextures=%ld name_page=%ld bar_page=%ld gamma=%s",
		loadbar_phase == LOADPHASE_FIRST ? 0 : loadbar_frames,
		phase[loadbar_phase], pic[kind], loadframe_look.tex0,
		loadframe_look.ps ? "set" : "none", loadframe_look.vs ? "set" : "none",
		loadframe_look.stage1 ? "set" : "none",
		loadframe_look.stage2 ? "set" : "none", loadframe_look.textures,
		loadframe_look.textures - 2, loadframe_look.textures - 1, gamma);
}

/* One frame of the loading screen, at the next 1/60 s tick: the picture -
 * with the level's name in it, read back with the first frame - and over
 * it the bar or, without a screen, the fade. The tick is waited
 * out first, so that the bar's position is the one at the Present:
 * S_DumpScreenFrame then finds its tick and does not wait again. The
 * position is the brake's (loadbar_math.h) out of the work done - the
 * loading thread's S_LoadBar calls and the main thread's points - and the
 * ticks since the screen's first frame. Returns 1 when the work is done
 * and the screen has stood its time: this frame showed the full bar. */
long S_DrawLoadBar()
{
	long x, y, w, h;

	while (TIME_SubTick() < 1.0);

	const float work = LoadBarWork(loadbar_steps / 100.0F, loadbar_build,
		loadbar_share);
	const double elapsed = LoadBarTicksMs(LoadScreenTickMs() -
		loadbar_start);

	loadbar_pos = LoadBarPosition(work, elapsed, loadbar_min_ms);

	_BeginScene();
	InitBuckets();
	InitialiseSortList();
	App.dx.lpD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);

	/* The loading picture first, in every frame of the bar: nothing clears
	 * the back buffer between frames, but a swap chain with
	 * D3DSWAPEFFECT_DISCARD promises nothing of its contents after a
	 * Present, so the picture is drawn again rather than expected to be
	 * there. Nothing when no picture is up (MonoScreenOn 0).
	 * The loading thread sets stage 0 meanwhile as the level's pages go:
	 * the picture's and the bar's binds and draws are held together up to
	 * DrawSortList (texture.cpp), from the device S_LoadFrameBegin set. */
	TextureStageLock();
	S_LoadFrameBegin();
	DrawLoadingScreen();

	/* Without a screen there is no bar: the frame is the last frame of the
	 * game (the picture above, captured by S_LoadLevelFile) under black
	 * that thickens with the time since the fade began; the frame at full
	 * black ends the fade, and nothing is drawn after it. */
	if (loadbar_mode == LOADBAR_OFF)
	{
		const long alpha = LoadFadeAlpha(LoadScreenClockMs() -
			loadbar_fade_at, loadbar_fade_ms);

		LoadFadeQuad(alpha);

		if (alpha >= 255)
			loadbar_fading = 0;
	}
	else if (tomb5.tr4_loadbar)
	{
		x = GetFixedScale(20);
		w = phd_winwidth - (x << 1);
		h = GetFixedScale(7);
		y = phd_winheight - h - GetFixedScale(20);

		if (tomb5.bar_mode == 3)
			S_DrawGouraudBar(x, y, w, h, (long)loadbar_pos, &loadBarColourSet);
		else if (tomb5.bar_mode == 2)
			S_DoTR4Bar(x, y, w, h, (long)loadbar_pos, 0xFF000000, 0xFF9F1F80);
		else
			DoBar(x, y, w, h, (long)loadbar_pos, 0xFF7F007F, 0xFF007F7F);
	}
	else
	{
		x = GetFixedScale(170);
		w = phd_winwidth - (x << 1);
		h = GetFixedScale(5);
		y = phd_winheight - h - GetFixedScale(20);

		if (tomb5.bar_mode == 3)
			S_DrawGouraudBar(x, y, w, h, (long)loadbar_pos, &loadBarColourSet);
		else if (tomb5.bar_mode == 2)
			S_DoTR4Bar(x, y, w, h, (long)loadbar_pos, 0xFF000000, 0xFF9F1F80);
		else
			DoBar(x, y, w, h, (long)loadbar_pos, 0x0000A0, 0x0000F0);
	}

	SortPolyList(SortCount, SortList);
	DrawSortList();
	TextureStageUnlock();

	if (loadbar_held)
		LoadBarPresentHeld();
	else
		S_DumpScreenFrame();

	S_LoadFrameSeam();
	loadbar_frames++;
	return LoadBarDone(work, elapsed, loadbar_min_ms);
}

void S_LoadBar()
{
	S_UpdateLoadBar();
}

// Puts the work at its end, the loading thread's count and the HD build's
// part both: the caller knows better than the counting - once the load is
// done, loading is done. The count alone can fall short of 100: it grows by
// 100/maxpos per call and adds up in floats, while maxpos is pages + 20
// (file.cpp LoadLevel). The bar's position follows from the brake.
void S_FinishLoadBar()
{
	loadbar_steps = 100.0F;
	loadbar_build = 1.0F;

	if (loadbar_maxpos > 100)
		loadbar_maxpos = 100;
}

void DoSlider(long x, long y, long width, long height, long pos, long c1, long c2, long c3)
{
	TEXTURESTRUCT tex;
	float sx, sy, w, h;
	static float V;

	V += 0.01F;

	if (V > 0.99F)
		V = 0;

	clipflags[0] = 0;
	clipflags[1] = 0;
	clipflags[2] = 0;
	clipflags[3] = 0;

	sx = (float)x * (float)phd_winxmax / 640.0F;
	sy = (float)y;
	w = (float)GetFixedScale(width);
	h = (float)GetFixedScale(height >> 1);

	tex.tpage = ushort(nTextures - 1);
	tex.drawtype = 0;
	tex.flag = 0;
	tex.u1 = 0;
	tex.v1 = V;
	tex.u2 = 1;
	tex.v2 = V;
	tex.u3 = 1;
	tex.v3 = V + 0.01F;
	tex.u4 = 0;
	tex.v4 = V + 0.01F;
	DrawColoredRect(sx, sy, sx + w, sy + h, f_mznear + 2, c1, c1, c2, c2, &tex);
	DrawColoredRect(sx, sy + h, sx + w, sy + (h * 2), f_mznear + 2, c2, c2, c1, c1, &tex);

	tex.tpage = 0;
	DrawColoredRect(sx - 1, sy - 1, sx + w + 1, sy + (h * 2) + 1, f_mznear + 4, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, &tex);

	w = pos * w / 100;
	tex.drawtype = 2;
	DrawColoredRect(sx, sy, sx + w + 1, sy + (h * 2), f_mznear + 1, c3, c3, c3, c3, &tex);
}

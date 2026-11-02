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
#include "alexstuff.h"
#include "function_stubs.h"
#include "drawroom.h"
#include "winmain.h"
#include "file.h"
#include "../game/gameflow.h"
#include "../game/text.h"
#include "specificfx.h"
#include "function_table.h"
#include "lighting.h"
#include "d3dmatrix.h"
#include "../game/tomb4fx.h"
#include "../game/deltapak.h"
#include "texture.h"
#include "../game/control.h"
#include "3dmath.h"
#include "gamemain.h"
#include "../game/interpolate.h"		// the tween latch
#include "../global/disk_layout.h"	// the file's room header, converted

const char* CreditNames[] =
{
	"nobody",
	"Derek Leigh-Gilchrist",
	"Martin Gibbins",
	"Tom Scutt",
	"Chris Coupe",
	"Alex Davis",
	"Richard Flower",
	"Martin Jensen",
	"Phil Chapman",
	"Jerr O'Carroll",
	"Joby Wood",
	"Andrea Cordella",
	"Richard Morton",
	"Andy Sandham",
	"Peter Connelly",
	"Martin Iveson",
	"Andy Watt",
	"Nick Connolly",
	"Hayos Fatunmbi",
	"Paul Field",
	"Steve Wakeman",
	"Dave Ward",
	"Jason Churchman",
	"Jeremy H. Smith",
	"Adrian Smith",
	"Benjamin Twose",
	"Ex Machina",
	"Ray Tran",
	"Stuart Abrahart",
	"Richard Apperley"
};

#pragma warning(push)
#pragma warning(disable : 4838)
#pragma warning(disable : 4309)
short CreditGroups[15] =
{
	0,
	TXT_Programmers,
	TXT_AI_Programming,
	TXT_Additional_Programmers,
	TXT_Animators,
	TXT_Level_Designers,
	TXT_FMV_Sequences,
	TXT_Music_Sound_FX,
	TXT_Additional_Sound_FX,
	TXT_Original_Story,
	TXT_Script,
	TXT_Producer,
	TXT_QA,
	TXT_Executive_Producers,
	0
};

const char* CreditsTable[] =
{
	"%01",
	"!05", "!02", "!01", "0",

	"%02",
	"!03", "0",

	"%03",
	"!04", "!06", "!07", "0",

	"%04",
	"!09", "!08", "0",

	"%05",
	"!11", "!12", "!13", "!10", "0",

	"%06",
	"!26", "0",

	"%07",
	"!14", "0",

	"%08",
	"!15", "0",

	"%09",
	"!12", "!13", "0",

	"%10",
	"!13", "0",

	"%11",
	"!16", "0",

	"%12",
	"!28", "!29", "!17", "!18", "!19", "!20", "!21", "!22", "!25", "0",

	"%13",
	"!23", "!24", "0",

	"Tomb Raider V Community Edition",
	"Troye", "ChocolateFan"
};
#pragma warning(pop)

long aWibble;

void aFixUpRoom(ROOM_INFO* r, char* s)
{
	ROOM_INFO_DISK disk;
	ROOMLET_DISK* pDisk;
	ROOMLET* pRm;
	float* pVtx;
	long* pPre;
	short* pFac;

	// The block starts with the file's room header, ROOM_INFO as a 32-bit
	// program laid it out; its offsets count from the header's end. It is
	// converted into r rather than copied (global\disk_layout.h).
	memcpy(&disk, s, sizeof(disk));
	RoomFromDisk(r, &disk, s + sizeof(ROOM_INFO_DISK));

	// An odd address in a short* is impossible, so this catches a door list
	// that came out of the file wrong: bit 0 of the address, whatever its
	// width.
	if ((uintptr_t)r->door & 1)
	{
		Log("%X", LogAddr(r->door));
		r->door = 0;
	}

	pFac = r->pRmFace;
	pPre = r->pRmPrelight;
	pVtx = r->pRmVtx;
	pDisk = (ROOMLET_DISK*)r->pRoomlets;
	pRm = (ROOMLET*)pDisk;

	// Where a ROOMLET is wider than the file's (72 bytes to 56 on a 64-bit
	// build) the roomlets get an array of their own in the pool; where the
	// two are one, they are converted where they lie, as before, and the
	// pool stays as it was.
	if (sizeof(ROOMLET) != sizeof(ROOMLET_DISK) && r->nRoomlets > 0)
		pRm = (ROOMLET*)game_malloc(r->nRoomlets * (long)sizeof(ROOMLET));

	RoomletsFromDisk(pRm, pDisk, r->nRoomlets, pFac, pPre, pVtx);
	r->pRoomlets = pRm;
}

void aUpdate()
{
	aWibble++;
}

void aTransformClip_D3DV(D3DVECTOR* vec, D3DTLVERTEX* v, long nVtx, long nClip)
{
	D3DMATRIX mx;
	short* clip;
	float x, y, z, zv;
	short c;

	mx = D3DMView;
	clip = &clipflags[nClip];

	for (int i = 0; i < nVtx; i++)
	{
		c = 0;
		x = mx._11 * vec->x + mx._21 * vec->y + mx._31 * vec->z + mx._41;
		y = mx._12 * vec->x + mx._22 * vec->y + mx._32 * vec->z + mx._42;
		z = mx._13 * vec->x + mx._23 * vec->y + mx._33 * vec->z + mx._43;

		if (z < f_mznear)
			c = -128;
		else
		{
			zv = f_mpersp / z;

			if (v->sz > f_mzfar)
			{
				v->sz = f_zfar;
				c = 256;
			}

			v->sx = zv * x + f_centerx;
			v->sy = zv * y + f_centery;
			v->sz = z;
			v->rhw = zv * f_moneopersp;

			if (v->sx < phd_winxmin)
				c++;
			else if (v->sx > phd_winxmax)
				c += 2;

			if (v->sy < phd_winymin)
				c += 4;
			else if (v->sy > phd_winymax)
				c += 8;
		}

		*clip++ = c;
		vec++;
		v++;
	}
}

void aTransform_D3DV(D3DVECTOR* vec, D3DTLVERTEX* v, long nVtx)
{
	D3DMATRIX mx;
	float x, y, z, zv;

	mx = D3DMView;

	for (int i = 0; i < nVtx; i++)
	{
		x = mx._11 * vec->x + mx._21 * vec->y + mx._31 * vec->z + mx._41;
		y = mx._12 * vec->x + mx._22 * vec->y + mx._32 * vec->z + mx._42;
		z = mx._13 * vec->x + mx._23 * vec->y + mx._33 * vec->z + mx._43;
		zv = f_mpersp / z;
		v->sx = zv * x + f_centerx;
		v->sy = zv * y + f_centery;
		v->sz = z;
		v->rhw = zv * f_moneopersp;
		vec++;
		v++;
	}
}

void aInit()
{
	aRoomInit();
}

char* aReadCutData(long n, FILE* file)
{
	char* data;
	long offset, size;

	if (!n)
		return 0;

	//cutseq file header is put in tsv_buffer beforehand!
	offset = *(long*)&tsv_buffer[n * 2 * sizeof(long)];
	size = *(long*)&tsv_buffer[n * 2 * sizeof(long) + 4];
	fseek(file, offset, SEEK_SET);
	data = (char*)game_malloc(size);
	fread(data, size, 1, file);
	return data;
}

long aCalcDepackBufferSz(char* data)
{
	long size;

	GLOBAL_cutme = (NEW_CUTSCENE*)data;
	size = 0;

	for (int i = 0; i < GLOBAL_cutme->numactors; i++)
		size += sizeof(PACKNODE) * (GLOBAL_cutme->actor_data[i].nodes + 1);

	return size + (sizeof(PACKNODE) * 2);
}

void aMakeCutsceneResident(long n1, long n2, long n3, long n4)
{
	FILE* file;
	char* d1;
	char* d2;
	char* d3;
	char* d4;

	if (!n1 && !n2 && !n3 && !n4)
		return;

	file = FileOpen("DATA\\CUTSEQ.BIN");

	if (!file)
		return;

	fread(tsv_buffer, 1, 2048, file);	//whole header in tsv_buffer
	memset(cutseq_resident_addresses, 0, sizeof(cutseq_resident_addresses));
	lastcamnum = -1;
	GLOBAL_playing_cutseq = 0;
	cutseq_trig = 0;
	d1 = aReadCutData(n1, file);
	d2 = aReadCutData(n2, file);
	d3 = aReadCutData(n3, file);
	d4 = aReadCutData(n4, file);
	fclose(file);

	if (d1)
		aCalcDepackBufferSz(d1);

	if (d2)
		aCalcDepackBufferSz(d2);

	if (d3)
		aCalcDepackBufferSz(d3);

	if (d4)
		aCalcDepackBufferSz(d4);

	if (n1)
		cutseq_resident_addresses[n1] = d1;

	if (n2)
		cutseq_resident_addresses[n2] = d2;

	if (n3)
		cutseq_resident_addresses[n3] = d3;

	if (n4)
		cutseq_resident_addresses[n4] = d4;
}

char* aFetchCutData(long n)
{
	FILE* file;
	char* data;

	data = cutseq_resident_addresses[n];

	if (!data)
	{
		file = FileOpen("DATA\\CUTSEQ.BIN");

		if (file)
		{
			fread(tsv_buffer, 1, 2048, file);
			data = aReadCutData(n, file);
			fclose(file);
		}
	}

	return data;
}

// The credits' running state - outside the function, so an abort
// (ResetCredits, from TitleOptions) lets the next roll start at the bottom
// again; before, the state was stuck as a function static.
static ulong CreditsStartPos = 0;
static long CreditsInit = 0;

void ResetCredits()
{
	CreditsInit = 0;
}

long DoCredits()
{
	const char* s;
	ulong& StartPos = CreditsStartPos;
	long& init = CreditsInit;
	long y, num_drawn;

	num_drawn = 0;

	if (!init)
	{
		StartPos = font_height + phd_winheight;
		init = 1;
	}

	y = StartPos;

	for (int i = 0; i < sizeof(CreditsTable) / 4; i++)
	{
		s = CreditsTable[i];

		if (y < font_height + phd_winheight + 1 && y > -font_height)
		{
			if (*s == '%')
				PrintString(phd_winwidth >> 1, y, 6, SCRIPT_TEXT(CreditGroups[atoi(s + 1)]), FF_CENTER);
			else if (*s != '0')
			{
				if (i >= 57)
					PrintString(phd_winwidth >> 1, y, 2 + (i == 57 ? 4 : 0), s, FF_CENTER);
				else
					PrintString(phd_winwidth >> 1, y, 2, CreditNames[atoi(s + 1)], FF_CENTER);
			}

			num_drawn++;
		}

		y += font_height;
	}

	/* The roll is a per-present counter, and the title screen is no longer
	 * fixed at thirty presents a second: one pixel a call was one pixel a
	 * thirtieth of a second only while DoTitle presented at that rate. Now
	 * that it presents at the display's frame rate, unguarded the credits
	 * would scroll at twice the speed at fps_mode 1 and five times over on a
	 * 165 Hz panel. At fps_mode 0 the guard is false on every frame, and this
	 * is vanilla's own line. DoTitle walks TitleOptions before its
	 * DrawPhaseGame, after the tick that lowered the live latch, so the roll
	 * reads the latch of the frame drawn last. */
	if (!InterpDrawnFrameWasTween())
		StartPos--;

	if (!num_drawn)
		init = 0;

	return num_drawn;
}

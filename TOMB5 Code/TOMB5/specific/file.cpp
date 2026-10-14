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
#include "../tomb5/libs/zlib/zlib.h"
#include "hqsfx.h"
#include "hdscreen.h"
#include "hd/hd_level.h"
#include "hd/hd_levelkey.h"		// HDLevelKeyNext - the HD data kept
#include "hd/hd_font.h"			// the HD font
#include "hd/hd_pagecache.h"		// the level's HD pages, for the warm-up
#include "hd/hd_model.h"		// HDModelPreload
#include "hd/hd_feet.h"		// the two footstep sets
#include "file.h"
#include "function_stubs.h"
#include "profile.h"			// ProfileLevel - PROFLOG counts from the load
#include "../game/interpolate.h"		// InterpInvalidate - the level boundary
#include "../game/gameflow.h"
#include "../game/setup.h"
#include "alexstuff.h"
#include "../game/init.h"
#include "../game/items.h"
#include "../game/objects.h"
#include "../game/laraskin.h"
#include "drawroom.h"
#include "dxsound.h"
#include "LoadSave.h"
#include "function_table.h"
#include "polyinsert.h"
#include "winmain.h"
#include "output.h"
#include "texture.h"
#include "dxshell.h"
#include "lighting.h"
#include "../game/tomb4fx.h"
#include "../game/deltapak.h"
#include "3dmath.h"
#include "specificfx.h"
#include "../game/box.h"
#include "../game/camera.h"
#include "../game/sound.h"
#include "../game/spotcam.h"
#include "../game/control.h"
#include "../game/draw.h"
#include "audio.h"
#include "../game/effects.h"
#include "../game/effect2.h"
#include "../game/lara.h"
#include "drawbars.h"
#include "../game/lot.h"
#include "../tomb5/tomb5.h"

TEXTURESTRUCT* textinfo;
SPRITESTRUCT* spriteinfo;
CHANGE_STRUCT* changes;
RANGE_STRUCT* ranges;
AIOBJECT* AIObjects;
THREAD LevelLoadingThread;
short* aranges;
short* frames;
short* commands;
short* floor_data;
short* mesh_base;
long number_cameras;
long nAnimUVRanges;
short nAIObjects;

TEXTURESTRUCT* AnimatingWaterfalls[6];
float AnimatingWaterfallsV[6];

static TEXTURESTRUCT* MonitorScreenTex;
static FILE* level_fp;
static char* FileData;
static char* CompressedData;
static float MonitorScreenU;
static long FileCompressed = 1;

static char ReadChar()
{
	char read;

	read = *(char*)FileData;
	FileData += sizeof(char);
	return read;
}

static short ReadShort()
{
	short read;

	read = *(short*)FileData;
	FileData += sizeof(short);
	return read;
}

static long ReadLong()
{
	long read;

	read = *(long*)FileData;
	FileData += sizeof(long);
	return read;
}

static void ReadBlock(void* dest, long size)
{
	memcpy(dest, FileData, size);
	FileData += size;
}

FILE* FileOpen(const char* Filename)
{
	FILE* fp;
	char cdFilename[80];

	memset(cdFilename, 0, 80);
	strcat(cdFilename, Filename);
	Log("FileOpen - %s", cdFilename);
	fp = fopen(cdFilename, "rb");

	if (!fp)
		Log("Unable To Open %s", cdFilename);

	return fp;
}

void FileClose(FILE* fp)
{
	Log(__FUNCTION__);
	fclose(fp);
}

long FileSize(FILE* fp)
{
	long size;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	return size;
}

long LoadFile(const char* name, char** dest)
{
	FILE* file;
	long size, count;

	Log(__FUNCTION__);
	Log("File - %s", name);
	file = FileOpen(name);

	if (!file)
		return 0;

	size = FileSize(file);

	if (!*dest)
		*dest = (char*)malloc(size);

	count = fread(*dest, 1, size, file);
	Log("Read - %d FileSize - %d", count, size);

	if (count != size)
	{
		Log("Error Reading File");
		FileClose(file);
		free(*dest);
		return 0;
	}

	FileClose(file);
	return size;
}

bool Decompress(char* pDest, char* pCompressed, long compressedSize, long size)
{
	z_stream stream;

	Log(__FUNCTION__);
	memset(&stream, 0, sizeof(z_stream));
	stream.avail_in = compressedSize;
	stream.avail_out = size;
	stream.next_out = (Bytef*)pDest;
	stream.next_in = (Bytef*)pCompressed;
	inflateInit(&stream);
	inflate(&stream, Z_FINISH);

	if (stream.total_out != size)
	{
		Log("Error Decompressing Data");
		return 0;
	}

	inflateEnd(&stream);
	Log("Decompression OK");
	return 1;
}

void AdjustUV(long num)
{
	TEXTURESTRUCT* tex;
	float u, v;
	ushort type;

	Log(__FUNCTION__);

	for (int i = 0; i < num; i++)
	{
		tex = &textinfo[i];
		Textures[tex->tpage].tpage++;
		tex->tpage++;
		u = 1.0F / float(Textures[tex->tpage].width << 1);
		v = 1.0F / float(Textures[tex->tpage].height << 1);
		type = tex->flag & 7;

		if (tex->flag & 0x8000)
		{
			switch (type)
			{
			case 0:
				tex->u1 += u;
				tex->v1 += v;
				tex->u2 -= u;
				tex->v2 += v;
				tex->u3 += u;
				tex->v3 -= v;
				break;

			case 1:
				tex->u1 -= u;
				tex->v1 += v;
				tex->u2 -= u;
				tex->v2 -= v;
				tex->u3 += u;
				tex->v3 += v;
				break;

			case 2:
				tex->u1 -= u;
				tex->v1 -= v;
				tex->u2 += u;
				tex->v2 -= v;
				tex->u3 -= u;
				tex->v3 += v;
				break;

			case 3:
				tex->u1 += u;
				tex->v1 -= v;
				tex->u2 += u;
				tex->v2 += v;
				tex->u3 -= u;
				tex->v3 -= v;
				break;

			case 4:
				tex->u1 -= u;
				tex->v1 += v;
				tex->u2 += u;
				tex->v2 += v;
				tex->u3 -= u;
				tex->v3 -= v;
				break;

			case 5:
				tex->u1 += u;
				tex->v1 += v;
				tex->u2 += u;
				tex->v2 -= v;
				tex->u3 -= u;
				tex->v3 += v;
				break;

			case 6:
				tex->u1 += u;
				tex->v1 -= v;
				tex->u2 -= u;
				tex->v2 -= v;
				tex->u3 += u;
				tex->v3 += v;
				break;

			case 7:
				tex->u1 -= u;
				tex->v1 -= v;
				tex->u2 -= u;
				tex->v2 += v;
				tex->u3 += u;
				tex->v3 -= v;
				break;

			default:
				Log("TextureInfo Type %d Not Found", type);
				break;
			}
		}
		else
		{
			switch (type)
			{
			case 0:
				tex->u1 += u;
				tex->v1 += v;
				tex->u2 -= u;
				tex->v2 += v;
				tex->u3 -= u;
				tex->v3 -= v;
				tex->u4 += u;
				tex->v4 -= v;
				break;

			case 1:
				tex->u1 -= u;
				tex->v1 += v;
				tex->u2 += u;
				tex->v2 += v;
				tex->u3 += u;
				tex->v3 -= v;
				tex->u4 -= u;
				tex->v4 -= v;
				break;

			default:
				Log("TextureInfo Type %d Not Found", type);
				break;
			}
		}
	}
}

void DoMonitorScreen()
{
	float s;
	static long pos, reset;

	if (!MonitorScreenTex)
		return;

	s = (fSin(pos) * 47.0F + 47.0F);
	MonitorScreenTex->u1 = MonitorScreenU + s * (1.0F / 256.0F);
	MonitorScreenTex->u2 = MonitorScreenU + s * (1.0F / 256.0F) + 0.125F;
	MonitorScreenTex->u4 = MonitorScreenU + s * (1.0F / 256.0F);
	MonitorScreenTex->u3 = MonitorScreenU + s * (1.0F / 256.0F) + 0.125F;

	/* The monitor screens scroll on the 30 Hz game clock. `pos` walks the
	 * scan band across the surveillance screens and `reset` holds it still at
	 * each end; this is called from the top of DrawRooms, once per drawn
	 * frame, so at 60 fps the band would sweep twice as fast and pause half
	 * as long. The UV write above runs on every frame from the current
	 * `pos`, so the picture updates at 60 while the clock stays at 30. */
	if (InterpIsTweenFrame())
		return;

	if (reset)
		reset--;
	else
	{
		if (s >= 94 || s <= 0)
			reset = 65;

		pos += 128;
	}
}

void S_GetUVRotateTextures()
{
	TEXTURESTRUCT* tex;
	short* pRange;

	// AnimatingTexturesV is a fixed [16][8][3] array, and both dimensions
	// used here come unchecked from the level file: nAnimUVRanges (outer, i)
	// is a raw byte read in LoadAnimatedTextures(); the per-range count
	// (inner, j's starting value) is read from aranges[] below. Without
	// these checks a larger level writes past the array (the same overflow
	// class as SAVEGAME_INFO in game\savegame.cpp).
	if (nAnimUVRanges > 16)
	{
		Log("FATAL: level has %d animated-UV texture ranges, only 16 fit in AnimatingTexturesV[]", nAnimUVRanges);

		MessageBox(0,
			"This level has more animated (UV-rotating) texture ranges than "
			"the engine can handle.\r\n\r\nThe game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	pRange = aranges + 1;

	for (int i = 0; i < nAnimUVRanges; i++, pRange++)
	{
		int count = (int)*pRange;

		if (count > 7)
		{
			Log("FATAL: animated-UV texture range %d has %d entries, only 8 fit in AnimatingTexturesV[][]", i, count + 1);

			MessageBox(0,
				"This level has an animated (UV-rotating) texture range with "
				"more entries than the engine can handle.\r\n\r\n"
				"The game cannot continue and must close.",
				"Tomb Raider V - Fatal Error", MB_ICONSTOP);

			ExitProcess(1);
		}

		for (int j = (int)*(pRange++); j >= 0; j--, pRange++)
		{
			tex = &textinfo[*pRange];
			AnimatingTexturesV[i][j][0] = tex->v1;
		}

		pRange--;
	}
}

void FreeLevel()
{
	Log(__FUNCTION__);

	FreeTextures();
	DXFreeSounds();
	HDLevelUnload();	// the other half of HDLevelLoad

	/* The interpolator's snapshot goes too. It holds item indices, not
	 * pointers, so nothing can dangle - but its positions belong to a level
	 * that no longer exists, and the next one fills the same slots with
	 * different things. */
	InterpInvalidate("level freed");

	/* The footstep sets in hd_feet.cpp point into this level's sample_lut and
	 * sample_infos, both game_malloc'd, which the next two lines invalidate.
	 * HDFeetLevelBuild clears them too, but it is not always reached:
	 * LoadSamples returns early in seven places (no sample infos, no samples,
	 * a failed decode) and is skipped when sound is off. Without this the
	 * next level would find a bare set answering "available" through dead
	 * pointers - the same stale-pool class as baddie_slots below. */
	HDFeetLevelEnd();
	malloc_ptr = malloc_buffer;
	malloc_free = malloc_size;

	// Resetting the bump allocator makes every pointer into the game_malloc
	// pool stale: an address from the previous level now points into the
	// middle of the level about to be loaded. baddie_slots matters because
	// it is read before it is reallocated: InitialiseLOTarray runs after
	// S_LoadLevelFile (gameflow.cpp), while ShutThatDoor - called from
	// InitialiseDoor during loading - walks all MAX_LOT slots and writes to
	// them. Through a stale pointer those writes (2047 for a closed door)
	// land in the new level's data, e.g. an item's room_number, and the
	// level then crashes while loading on room[2047].
	baddie_slots = 0;
}

bool LoadTextures(long RTPages, long OTPages, long BTPages)
{
	DXTEXTUREINFO* dxtex;
	LPDIRECT3DTEXTURE9 tSurf;
	uchar* TextureData;
	long* d;
	char* pData;
	char* pComp;
	char* s;
	long format, skip, size, compressedSize, nTex, c;
	uchar r, g, b, a;

	Log(__FUNCTION__);

	// RTPages/OTPages/BTPages come unchecked from the level file, and the
	// fill loops below write Textures[nTextures++] for each page with no
	// bounds check of their own (the same overflow class as SAVEGAME_INFO in
	// game\savegame.cpp). Index 0 is reserved (set by the caller before
	// LoadTextures runs), so 1..(RTPages+OTPages+BTPages) must fit within
	// Textures[].
	if (1 + RTPages + OTPages + BTPages > 256)
	{
		Log("FATAL: level has %d texture pages (RTPages=%d OTPages=%d BTPages=%d), only 256 fit in Textures[]",
			1 + RTPages + OTPages + BTPages, RTPages, OTPages, BTPages);

		MessageBox(0,
			"This level has more texture pages than the engine can handle "
			"(too many combined room/object/bump-mapped textures).\r\n\r\n"
			"The game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	nTextures = 1;
	format = 0;
	skip = 4;
	dxtex = &G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].TextureInfos[G_dxinfo->nTexture];

	if (dxtex->rbpp == 8 && dxtex->gbpp == 8 && dxtex->bbpp == 8 && dxtex->abpp == 8)
		format = 1;
	else if (dxtex->rbpp == 5 && dxtex->gbpp == 5 && dxtex->bbpp == 5 && dxtex->abpp == 1)
	{
		format = 2;
		skip = 2;
	}

	if (format <= 1)
	{
		fread(&size, 1, 4, level_fp);
		fread(&compressedSize, 1, 4, level_fp);
		CompressedData = (char*)malloc(compressedSize);
		FileData = (char*)malloc(size);

		if (FileCompressed)
		{
			fread(CompressedData, compressedSize, 1, level_fp);
			Decompress(FileData, CompressedData, compressedSize, size);
			fread(&size, 1, 4, level_fp);
			fread(&compressedSize, 1, 4, level_fp);
			fseek(level_fp, compressedSize, SEEK_CUR);
		}
		else
		{
			fread(FileData, size, 1, level_fp);
			fread(&size, 1, 4, level_fp);
			fread(&compressedSize, 1, 4, level_fp);
			fseek(level_fp, size, SEEK_CUR);
		}

		free(CompressedData);
	}
	else
	{
		fread(&size, 1, 4, level_fp);
		fread(&compressedSize, 1, 4, level_fp);

		if (FileCompressed)
			fseek(level_fp, compressedSize, SEEK_CUR);
		else
			fseek(level_fp, size, SEEK_CUR);

		fread(&size, 1, 4, level_fp);
		fread(&compressedSize, 1, 4, level_fp);
		CompressedData = (char*)malloc(compressedSize);
		FileData = (char*)malloc(size);

		if (FileCompressed)
		{
			fread(CompressedData, compressedSize, 1, level_fp);
			Decompress(FileData, CompressedData, compressedSize, size);
		}
		else
			fread(FileData, size, 1, level_fp);

		free(CompressedData);
	}

	pData = FileData;

	Log("RTPages %d", RTPages);
	size = RTPages * skip * 0x10000;
	TextureData = (uchar*)malloc(size);
	ReadBlock(TextureData, size);
	S_LoadBar();

	for (int i = 0; i < RTPages; i++)
	{
		nTex = nTextures++;
		tSurf = CreateTexturePage(App.TextureSize, App.TextureSize, 0, (long*)(TextureData + (i * skip * 0x10000)), 0, format);
		Textures[nTex].tex = tSurf;
		Textures[nTex].width = App.TextureSize;
		Textures[nTex].height = App.TextureSize;
		Textures[nTex].bump = 0;
		S_LoadBar();
	}

	free(TextureData);

	Log("OTPages %d", OTPages);
	size = OTPages * skip * 0x10000;
	TextureData = (uchar*)malloc(size);
	ReadBlock(TextureData, size);
	S_LoadBar();

	for (int i = 0; i < OTPages; i++)
	{
		nTex = nTextures++;
		tSurf = CreateTexturePage(App.TextureSize, App.TextureSize, 0, (long*)(TextureData + (i * skip * 0x10000)), 0, format);
		Textures[nTex].tex = tSurf;
		Textures[nTex].width = App.TextureSize;
		Textures[nTex].height = App.TextureSize;
		Textures[nTex].bump = 0;
		App.dx.lpD3DDevice->SetTexture(0, tSurf);
		S_LoadBar();
	}

	free(TextureData);
	S_LoadBar();

	Log("BTPages %d", BTPages);

	if (BTPages)
	{
		size = BTPages * skip * 0x10000;
		TextureData = (uchar*)malloc(size);
		ReadBlock(TextureData, size);

		for (int i = 0; i < BTPages; i++)
		{
			if (i < (BTPages >> 1))
				tSurf = CreateTexturePage(App.TextureSize, App.TextureSize, 0, (long*)(TextureData + (i * skip * 0x10000)), 0, format);
			else
			{
				if (!App.BumpMapping)
					break;

				tSurf = CreateTexturePage(App.BumpMapSize, App.BumpMapSize, 0, (long*)(TextureData + (i * skip * 0x10000)), 0, format);
			}

			nTex = nTextures++;
			Textures[nTex].tex = tSurf;

			if (i < (BTPages >> 1))
			{
				Textures[nTex].width = App.TextureSize;
				Textures[nTex].height = App.TextureSize;
			}
			else
			{
				Textures[nTex].width = App.BumpMapSize;
				Textures[nTex].height = App.BumpMapSize;
			}

			Textures[nTex].bump = 1;
			Textures[nTex].bumptpage = nTex + (BTPages >> 1);
			S_LoadBar();
		}

		free(TextureData);
	}

	free(pData);

	fread(&size, 1, 4, level_fp);
	fread(&compressedSize, 1, 4, level_fp);
	CompressedData = (char*)malloc(compressedSize);
	FileData = (char*)malloc(size);

	if (FileCompressed)
	{
		fread(CompressedData, compressedSize, 1, level_fp);
		Decompress(FileData, CompressedData, compressedSize, size);
	}
	else
		fread(FileData, size, 1, level_fp);

	free(CompressedData);

	pData = FileData;
	TextureData = (uchar*)malloc(0x40000);

	//main menu logo
	if (gfLevelId == LVL5_TITLE)
	{
		pComp = 0;
		CompressedData = 0;

		// The Steam data's uslogo.pak (and the unused uklogo.pak) hold Tomb
		// Raider IV's "The Last Revelation" logo on black - leftovers, like
		// JAPAN.DAT. The colour key is magenta, so the black would show as an
		// opaque box behind the wrong game's name. US takes logo512.pak like
		// every other language without a logo of its own.
		if (Gameflow->Language == GERMAN)
			size = LoadFile("data\\grlogo.pak", &CompressedData);
		else if (Gameflow->Language == FRENCH)
			size = LoadFile("data\\frlogo.pak", &CompressedData);
		else
			size = LoadFile("data\\logo512.pak", &CompressedData);

		// Language logos (grlogo/frlogo) only existed on the respective
		// CDs - the Steam edition ships only logo512/uslogo/uklogo. If the
		// file is missing, the title falls back to the English logo
		// instead of decompressing a null page (hard crash on returning to
		// the title menu with German or French active).
		if (!size || !CompressedData)
		{
			CompressedData = 0;
			size = LoadFile("data\\logo512.pak", &CompressedData);
		}

		pComp = (char*)malloc(*(long*)CompressedData);
		Decompress(pComp, CompressedData + 4, size - 4, *(long*)CompressedData);
		free(CompressedData);

		for (int i = 0; i < 2; i++)
		{
			s = pComp + (i * 768);
			d = (long*)TextureData;

			for (int y = 0; y < 256; y++)
			{
				for (int x = 0; x < 256; x++)
				{
					r = *(s + (x * 3) + (y * 1536));
					g = *(s + (x * 3) + (y * 1536) + 1);
					b = *(s + (x * 3) + (y * 1536) + 2);
					a = 0xFF;

					if (r == 0xFF && b == 0xFF && !g)	//magenta
					{
						r = 0;
						b = 0;
						a = 0;
					}

					c = RGBA(r, g, b, a);
					*d++ = c;
				}
			}

			nTex = nTextures++;
			tSurf = CreateTexturePage(256, 256, 0, (long*)TextureData, 0, 0);
			Textures[nTex].tex = tSurf;
			Textures[nTex].width = 256;
			Textures[nTex].height = 256;
			Textures[nTex].bump = 0;
		}

		free(pComp);
	}

	//shine
	ReadBlock(TextureData, 0x40000);
	nTex = nTextures++;
	tSurf = CreateTexturePage(256, 256, 0, (long*)TextureData, 0, 0);
	Textures[nTex].tex = tSurf;
	Textures[nTex].width = 256;
	Textures[nTex].height = 256;
	Textures[nTex].bump = 0;

	//font
	ReadBlock(TextureData, 0x40000);

	/* The same page goes to the HD font. This is the only place where the
	 * classic font page is in hand as pixels, and the HD path needs it as the
	 * template it looks for in the remaster's glyph atlas, not as a picture
	 * to draw. This runs on the load thread, so no Direct3D here; the texture
	 * is made later on the main thread by HDFontBuild. With hd_enable off the
	 * call returns at once and copies nothing. */
	HDFontLevelPage(TextureData);

	nTex = nTextures++;
	tSurf = CreateTexturePage(256, 256, 0, (long*)TextureData, 0, 0);
	Textures[nTex].tex = tSurf;
	Textures[nTex].width = 256;
	Textures[nTex].height = 256;
	Textures[nTex].bump = 0;

	//sky
	ReadBlock(TextureData, 0x40000);
	nTex = nTextures++;
	tSurf = CreateTexturePage(256, 256, 0, (long*)TextureData, 0, 0);
	Textures[nTex].tex = tSurf;
	Textures[nTex].width = 256;
	Textures[nTex].height = 256;
	Textures[nTex].bump = 0;

	// The title menu's wordmark in HD (LoadTitleLogoHD, specificfx.cpp). If
	// the player has the remaster's screens\HD\LOGO_<language>.DDS, the mark
	// out of that file takes over the page the pak logo's left half went into
	// - the page SuperShowLogo() addresses as nTextures-5 - and the menu
	// draws one quad from it instead of two from the pak. The pak is decoded
	// first and stays the fallback, so a missing or unreadable HD file costs
	// nothing at all. Every other level passes -1, which only clears what the
	// last title screen left behind.
	LoadTitleLogoHD(gfLevelId == LVL5_TITLE ? nTextures - 5 : -1);

	Log("Created %d Texture Pages", nTextures - 1);

	free(TextureData);
	free(pData);
	return 1;
}

static void aLoadRoomStream()
{
	ROOM_INFO* r;
	long num, size;
	char* data;

	ReadLong();	//skip unused space
	num = ReadLong();
	room = (ROOM_INFO*)game_malloc(num * sizeof(ROOM_INFO));

	for (int i = 0; i < num; i++)
	{
		r = &room[i];

		ReadLong();		//X E L A

		size = ReadLong();
		data = (char*)game_malloc(size);
		ReadBlock(data, size);
		aFixUpRoom(r, data);
	}

	number_rooms = (short)num;
}

bool LoadRooms()
{
	long size;

	Log(__FUNCTION__);
	wibble = 0;

	aLoadRoomStream();
	BuildOutsideTable();

	size = ReadLong();
	floor_data = (short*)game_malloc(size * sizeof(short));
	ReadBlock(floor_data, sizeof(short) * size);

	Log("Floor Data Size %d @ %x", size, floor_data);
	return 1;
}

bool LoadObjects()
{
	OBJECT_INFO* obj;
	STATIC_INFO* stat;
	short** mesh;
	short** mesh_size;
	long num, num_anims, num_meshes, slot, lp;

	Log(__FUNCTION__);
	memset(objects, 0, NUMBER_OBJECTS * sizeof(OBJECT_INFO));
	memset(static_objects, 0, 70 * sizeof(STATIC_INFO));

	//meshes
	num = ReadLong();
	mesh_base = (short*)game_malloc(num * 2);
	ReadBlock(mesh_base, num * sizeof(short));

	Log("OBJLOG mesh data %d words", num);

	num = ReadLong();
	meshes = (short**)game_malloc(num * sizeof(long) * 2);	//*2 for meshswaps
	ReadBlock(meshes, num * sizeof(long));

	for (lp = 0; lp < num; lp++)
		meshes[lp] = &mesh_base[(long)meshes[lp] / 2];

	num_meshes = num;
	Log("OBJLOG %d mesh pointers", num_meshes);

	//anims
	num_anims = ReadLong();
	anims = (ANIM_STRUCT*)game_malloc(num_anims * sizeof(ANIM_STRUCT));
	ReadBlock(anims, num_anims * sizeof(ANIM_STRUCT));

	Log("OBJLOG %d anims", num_anims);

	//changes
	num = ReadLong();
	changes = (CHANGE_STRUCT*)game_malloc(num * sizeof(CHANGE_STRUCT));
	ReadBlock(changes, num * sizeof(CHANGE_STRUCT));

	//ranges
	num = ReadLong();
	ranges = (RANGE_STRUCT*)game_malloc(num * sizeof(RANGE_STRUCT));
	ReadBlock(ranges, num * sizeof(RANGE_STRUCT));

	//anim commands
	num = ReadLong();
	commands = (short*)game_malloc(num * sizeof(short));
	ReadBlock(commands, num * sizeof(short));

	//bones
	num = ReadLong();
	bones = (long*)game_malloc(num * sizeof(long));
	ReadBlock(bones, num * sizeof(long));

	//frames
	num = ReadLong();
	frames = (short*)game_malloc(num * sizeof(short));
	ReadBlock(frames, num * sizeof(short));

	for (lp = 0; lp < num_anims; lp++)
		anims[lp].frame_ptr = (short*)((long)anims[lp].frame_ptr + (long)frames);

	num = ReadLong();

	for (lp = 0; lp < num; lp++)
	{
		slot = ReadLong();

		// The slot comes straight from the level file with no upstream
		// validation (same bug class as num_items in LoadItems). A file
		// written for a newer build of this engine may carry slots this
		// build's enum does not know yet - skip the entry instead of
		// writing past the end of objects[].
		if (slot < 0 || slot >= NUMBER_OBJECTS)
		{
			Log("LoadObjects: unknown slot %d skipped (NUMBER_OBJECTS=%d)", slot, NUMBER_OBJECTS);
			ReadShort();
			ReadShort();
			ReadLong();
			ReadLong();
			ReadShort();
			ReadShort();	//Skip padding
			continue;
		}

		obj = &objects[slot];

		obj->nmeshes = ReadShort();
		obj->mesh_index = ReadShort();
		obj->bone_index = ReadLong();
		obj->frame_base = (short*)ReadLong();
		obj->anim_index = ReadShort();
		obj->loaded = 1;

		ReadShort();	//Skip padding
	}

	Log("OBJLOG moveables read, skinning next (LaraDrawType %d)", LaraDrawType);

	if (LaraDrawType != LARA_DIVESUIT)
		CreateSkinningData();

	Log("OBJLOG skinning done");

	for (int i = 0; i < NUMBER_OBJECTS; i++)
	{
		obj = &objects[i];
		obj->mesh_index *= 2;
	}

	mesh = meshes;
	mesh_size = &meshes[num_meshes];
	memcpy(mesh_size, mesh, num_meshes * 4);

	for (int i = 0; i < num_meshes; i++)
	{
		*mesh++ = *mesh_size;
		*mesh++ = *mesh_size++;
	}

	Log("OBJLOG mesh pointers doubled, objects next");
	InitialiseObjects();
	InitialiseClosedDoors();
	Log("OBJLOG objects initialised, statics next");

	//statics
	num = ReadLong();

	for (int i = 0; i < num; i++)
	{
		slot = ReadLong();
		stat = &static_objects[slot];

		stat->mesh_number = ReadShort();
		ReadBlock(&stat->x_minp, 6 * sizeof(short));
		ReadBlock(&stat->x_minc, 6 * sizeof(short));
		stat->flags = ReadShort();
	}

	for (int i = 0; i < 70; i++)
	{
		stat = &static_objects[i];
		stat->mesh_number *= 2;
	}

	ProcessMeshData(num_meshes * 2);
	return 1;
}

bool LoadSprites()
{
	STATIC_INFO* stat;
	OBJECT_INFO* obj;
	PHDSPRITESTRUCT sprite;
	long num_sprites, num_slots, slot;

	Log(__FUNCTION__);
	ReadLong();			//SPR 0 marker

	num_sprites = ReadLong();
	spriteinfo = (SPRITESTRUCT*)game_malloc(sizeof(SPRITESTRUCT) * num_sprites);

	for (int i = 0; i < num_sprites; i++)
	{
		ReadBlock(&sprite, sizeof(PHDSPRITESTRUCT));
		spriteinfo[i].height = sprite.height;
		spriteinfo[i].offset = sprite.offset;
		spriteinfo[i].tpage = sprite.tpage;
		spriteinfo[i].width = sprite.width;
		spriteinfo[i].x1 = (float)sprite.x1 * (1.0F / 256.0F);
		spriteinfo[i].y1 = (float)sprite.y1 * (1.0F / 256.0F);
		spriteinfo[i].x2 = (float)sprite.x2 * (1.0F / 256.0F);
		spriteinfo[i].y2 = (float)sprite.y2 * (1.0F / 256.0F);
		spriteinfo[i].x1 += (1.0F / 256.0F);
		spriteinfo[i].y1 += (1.0F / 256.0F);
		spriteinfo[i].x2 -= (1.0F / 256.0F);
		spriteinfo[i].y2 -= (1.0F / 256.0F);
		spriteinfo[i].tpage++;
	}

	num_slots = ReadLong();

	if (num_slots <= 0)
		return 1;

	for (int i = 0; i < num_slots; i++)
	{
		slot = ReadLong();

		// SLOT_FILE_BOUNDARY, not NUMBER_OBJECTS: every existing level file
		// encodes static-object sprite slots as 460 + static number, because
		// 460 was the value of NUMBER_OBJECTS when those files were written.
		// NUMBER_OBJECTS may grow as new moveable slots are appended to the
		// enum (randomizer project); the on-disk encoding must stay pinned at
		// 460 or these entries would be misread as moveables in every level.
		if (slot >= SLOT_FILE_BOUNDARY)
		{
			slot -= SLOT_FILE_BOUNDARY;
			stat = &static_objects[slot];
			stat->mesh_number = ReadShort();
			stat->mesh_number = ReadShort();
		}
		else
		{
			obj = &objects[slot];
			obj->nmeshes = ReadShort();
			obj->mesh_index = ReadShort();
			obj->loaded = 1;
		}
	}

	return 1;
}

bool LoadCameras()
{
	Log(__FUNCTION__);
	number_cameras = ReadLong();

	if (number_cameras)
	{
		camera.fixed = (OBJECT_VECTOR*)game_malloc(number_cameras * sizeof(OBJECT_VECTOR));
		ReadBlock(camera.fixed, number_cameras * sizeof(OBJECT_VECTOR));
	}

	number_spotcams = (short)ReadLong();

	// number_spotcams comes unchecked from the level file, SpotCam is a
	// fixed 256-entry global array, and the ReadBlock below is a raw memcpy
	// with no bounds check of its own (the same overflow class as
	// SAVEGAME_INFO in game\savegame.cpp).
	if (number_spotcams > 256)
	{
		Log("FATAL: level has %d spotcams, only 256 fit in SpotCam[]", number_spotcams);

		MessageBox(0,
			"This level has more spot cameras than the engine can handle.\r\n\r\n"
			"The game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	if (number_spotcams)
		ReadBlock(SpotCam, number_spotcams * sizeof(SPOTCAM));

	return 1;
}

bool LoadSoundEffects()
{
	Log(__FUNCTION__);
	number_sound_effects = ReadLong();
	Log("Number of SFX %d", number_sound_effects);

	if (number_sound_effects)
	{
		sound_effects = (OBJECT_VECTOR*)game_malloc(number_sound_effects * sizeof(OBJECT_VECTOR));
		ReadBlock(sound_effects, number_sound_effects * sizeof(OBJECT_VECTOR));
	}

	return 1;
}

bool LoadBoxes()
{
	BOX_INFO* box;
	long size;

	Log(__FUNCTION__);
	num_boxes = ReadLong();

	boxes = (BOX_INFO*)game_malloc(num_boxes * sizeof(BOX_INFO));
	ReadBlock(boxes, num_boxes * sizeof(BOX_INFO));

	size = ReadLong();
	overlap = (ushort*)game_malloc(size * sizeof(ushort));
	ReadBlock(overlap, size * sizeof(ushort));

	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			ground_zone[j][i] = (short*)game_malloc(num_boxes * sizeof(short));
			ReadBlock(ground_zone[j][i], num_boxes * sizeof(short));
		}

		ground_zone[4][i] = (short*)game_malloc(num_boxes * sizeof(short));
		ReadBlock(ground_zone[4][i], num_boxes * sizeof(short));
	}

	for (int i = 0; i < num_boxes; i++)
	{
		box = &boxes[i];

		if (box->overlap_index & 0x8000)
			box->overlap_index |= 0x4000;
	}

	return 1;
}

bool LoadAnimatedTextures()
{
	long num_anim_ranges;

	Log(__FUNCTION__);
	num_anim_ranges = ReadLong();

	aranges = (short*)game_malloc(num_anim_ranges * 2);
	ReadBlock(aranges, num_anim_ranges * sizeof(short));

	if (gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS)
		aranges[2] = 2076;

	nAnimUVRanges = ReadChar();
	return 1;
}

static bool LarasNoseTexture(long i)
{
	if (gfLevelId == LVL5_STREETS_OF_ROME && (i == 200 || i == 204))
		return 1;

	if (gfLevelId == LVL5_TRAJAN_MARKETS && (i == 225 || i == 229))
		return 1;

	if (gfLevelId == LVL5_COLOSSEUM && (i == 240 || i == 244))
		return 1;

	if (gfLevelId == LVL5_BASE && (i == 210 || i == 213))
		return 1;

	if (gfLevelId == LVL5_SUBMARINE && (i == 205 || i == 208))
		return 1;

	if (gfLevelId == LVL5_DEEPSEA_DIVE && (i == 201 || i == 205))
		return 1;

	if (gfLevelId == LVL5_SINKING_SUBMARINE && (i == 235 || i == 238))
		return 1;

	if ((gfLevelId >= LVL5_GALLOWS_TREE && gfLevelId <= LVL5_OLD_MILL) && (i == 144 || i == 148))
		return 1;

	if (gfLevelId == LVL5_THIRTEENTH_FLOOR && (i == 99 || i == 103))
		return 1;

	if (gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS && (i == 101 || i == 105))
		return 1;

	if (gfLevelId == LVL5_RED_ALERT && (i == 133 || i == 137))
		return 1;

	return 0;
}

bool LoadTextureInfos()
{
	PHDTEXTURESTRUCT tex;
	long nTInfos;

	Log(__FUNCTION__);

	ReadLong();			//TEX 0 marker
	nTInfos = ReadLong();
	Log("Texture Infos : %d", nTInfos);
	textinfo = (TEXTURESTRUCT*)game_malloc(nTInfos * sizeof(TEXTURESTRUCT));

	for (int i = 0; i < nTInfos; i++)
	{
		ReadBlock(&tex, sizeof(PHDTEXTURESTRUCT));
		textinfo[i].drawtype = tex.drawtype;
		textinfo[i].tpage = tex.tpage & 0x7FFF;
		textinfo[i].flag = tex.tpage ^ (tex.tpage ^ tex.flag) & 0x7FFF;
		
		textinfo[i].u1 = (tex.u1 >> 8) * (1.0F / 256.0F);
		textinfo[i].v1 = (tex.v1 >> 8) * (1.0F / 256.0F);

		if (LarasNoseTexture(i))
		{
			textinfo[i].u2 = (tex.u2 >> 8) * 0.00393690F;
			textinfo[i].v2 = (tex.v2 >> 8) * 0.00393690F;
			textinfo[i].u3 = (tex.u3 >> 8) * 0.00393690F;
			textinfo[i].v3 = (tex.v3 >> 8) * 0.00393690F;
		}
		else
		{
			textinfo[i].u2 = (tex.u2 >> 8) * (1.0F / 256.0F);
			textinfo[i].v2 = (tex.v2 >> 8) * (1.0F / 256.0F);
			textinfo[i].u3 = (tex.u3 >> 8) * (1.0F / 256.0F);
			textinfo[i].v3 = (tex.v3 >> 8) * (1.0F / 256.0F);
		}

		textinfo[i].u4 = (tex.u4 >> 8) * (1.0F / 256.0F);
		textinfo[i].v4 = (tex.v4 >> 8) * (1.0F / 256.0F);
	}

	AdjustUV(nTInfos);
	return 1;
}

// Small printf for the message below. One buffer is enough - the call ends
// the process, so nobody comes back for a second string.
static const char* aFmt(const char* fmt, ...)
{
	static char buf[256];
	va_list list;

	va_start(list, fmt);
	vsprintf(buf, fmt, list);
	va_end(list);
	return buf;
}

// One exit for level data the engine cannot use, in the same manner as the
// MAX_ITEMS check below: say what is wrong, say it where the player sees it,
// then stop. Deliberately not a silent clamp - a level carrying impossible
// values is either damaged or was written for a different build, and both
// are worth knowing rather than playing around. The hint at -log is there
// because the diagnostic log lines that name the culprit only write with it.
static void FatalLevelData(const char* what)
{
	char msg[512];

	Log("FATAL: %s", what);
	sprintf(msg,
		"This level file carries data the engine cannot use:\r\n\r\n"
		"    %s\r\n\r\n"
		"Starting the game once with -log writes a log.txt that records how "
		"far loading got.\r\n\r\n"
		"The game cannot continue and must close.", what);

	MessageBox(0, msg, "Tomb Raider V - Fatal Error", MB_ICONSTOP);
	ExitProcess(1);
}

bool LoadItems()
{
	ITEM_INFO* item;
	ROOM_INFO* r;
	FLOOR_INFO* floor;
	STATIC_INFO* stat;
	long x, y, z, num_items;

	bool watchDone = false;

	Log(__FUNCTION__);
	num_items = ReadLong();
	Log("Number of items %d, rooms %d", num_items, number_rooms);

	if (!num_items)
		return 1;

	// num_items comes unchecked from the level file, and the fill loop below
	// writes items[i] for every one of them with no bounds check of its own
	// (the same overflow class as SAVEGAME_INFO in game\savegame.cpp).
	// MAX_ITEMS (global\types.h) is deliberately not raised - see items[]
	// itself for why.
	if (num_items > MAX_ITEMS)
	{
		Log("FATAL: level has %d items, only MAX_ITEMS=%d fit in items[]", num_items, MAX_ITEMS);

		MessageBox(0,
			"This level has more items/moveables than the engine can handle.\r\n\r\n"
			"The game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	items = (ITEM_INFO*)game_malloc(MAX_ITEMS * sizeof(ITEM_INFO));
	level_items = num_items;
	InitialiseClosedDoors();
	InitialiseItemArray(MAX_ITEMS);

	for (int i = 0; i < num_items; i++)
	{
		item = &items[i];
		item->object_number = ReadShort();
		item->room_number = ReadShort();
		item->pos.x_pos = ReadLong();
		item->pos.y_pos = ReadLong();
		item->pos.z_pos = ReadLong();
		item->pos.y_rot = ReadShort();
		item->shade = ReadShort();
		item->trigger_flags = ReadShort();
		item->flags = ReadShort();

		// Both fields index an array further down with no check of their
		// own: object_number goes into objects[] in InitialiseItem, and
		// room_number into room[] two lines later. A file written by a tool
		// - ours included - can carry a slot this build does not know or a
		// room that does not exist. Caught here, that is one clear message;
		// uncaught, it is a wild read and a hard crash.
		if (item->object_number < 0 || item->object_number >= NUMBER_OBJECTS)
			FatalLevelData(aFmt("item %d carries slot %d - valid is 0..%d",
								i, item->object_number, NUMBER_OBJECTS - 1));

		if (item->room_number < 0 || item->room_number >= number_rooms)
			FatalLevelData(aFmt("item %d sits in room %d - the level has %d",
								i, item->room_number, number_rooms));
	}

	for (int i = 0; i < num_items; i++)
	{
		// ITEMLOG (-log only): index, slot (with its loaded flag and anim
		// index), room, OCB, flags and position of each item before it is
		// initialised. InitialiseItem is where the level's data meets the
		// engine's slot logic - anim offsets off the object's anchor, OCB read
		// as a behaviour, neighbours looked up by room - and a randomized
		// level produces slot/OCB pairings no vanilla level has. Written
		// before the call, the last ITEMLOG line in log.txt names the item a
		// crash happened on.
		Log("ITEMLOG %d: slot %d (loaded %d anim %d) room %d ocb %d flags 0x%X pos %d,%d,%d",
			i, items[i].object_number,
			objects[items[i].object_number].loaded,
			objects[items[i].object_number].anim_index,
			items[i].room_number,
			items[i].trigger_flags & 0x3F, items[i].flags,
			items[i].pos.x_pos, items[i].pos.y_pos, items[i].pos.z_pos);

		InitialiseItem(i);

		// ITEMWATCH: room numbers were checked while reading, so an item in
		// an impossible room afterwards was hit by a stray write from one of
		// the initialise calls. ITEMLOG would only name the item that
		// crashed; this line names the damaged item and the one being
		// initialised when it happened.
		//
		// The scan is O(n^2), so it is gated at the call site and not only
		// inside Log(): without -log not a single comparison runs. It stops
		// after the first hit - the first one is the cause, the rest are its
		// consequences.
		if (gLogEnabled && !watchDone)
		{
			for (int j = 0; j < num_items; j++)
			{
				if (items[j].room_number >= 0 && items[j].room_number < number_rooms)
					continue;

				Log("ITEMWATCH: item %d (slot %d) now sits in room %d - written while "
					"item %d (slot %d, ocb %d, room %d) was initialised",
					j, items[j].object_number, items[j].room_number,
					i, items[i].object_number, items[i].trigger_flags & 0x3F,
					items[i].room_number);
				watchDone = true;
				break;
			}
		}
	}

	for (int i = 0; i < number_rooms; i++)
	{
		r = &room[i];

		for (int j = 0; j < r->num_meshes; j++)
		{
			x = (r->mesh[j].x - r->x) >> 10;
			z = (r->mesh[j].z - r->z) >> 10;

			floor = &(r->floor[x * r->x_size + z]);

			if (boxes[floor->box].overlap_index & 0x4000)
				continue;

			stat = &static_objects[r->mesh[j].static_number];
			y = floor->floor << 8;

			if (y <= (r->mesh[j].y - stat->y_maxc + 512) && y < r->mesh[j].y - stat->y_minc)
			{
				if (!stat->x_maxc || !stat->x_minc || !stat->z_maxc || !stat->z_minc ||
					(stat->x_maxc ^ stat->x_minc) & 0x8000 && (stat->z_maxc ^ stat->z_minc) & 0x8000)
				{
					x = (r->mesh[j].x - r->x) >> 10;
					z = (r->mesh[j].z - r->z) >> 10;
					r->floor[x * r->x_size + z].stopper = 1;
				}
			}
		}
	}

	return 1;
}

bool LoadAIInfo()
{
	long num;

	Log(__FUNCTION__);
	num = ReadLong();

	if (!num)
		return 1;

	nAIObjects = (short)num;
	AIObjects = (AIOBJECT*)game_malloc(num * sizeof(AIOBJECT));
	ReadBlock(AIObjects, num * sizeof(AIOBJECT));
	return 1;
}

bool LoadCinematic()
{
	Log(__FUNCTION__);
	ReadShort();
	return 1;
}

/* Whether a level sample belongs to the footsteps (SFX_LARA_FEET or
 * SFX_LARA_LAND). The two ids own a handful of samples between them - four
 * steps and one landing in each of the sixteen classic levels, measured -
 * and those take their payload from the unmodded bank. The answer comes
 * from the level's own sound map, so a level that numbers them differently
 * is still answered right. */
static bool feet_sample(long sample)
{
	static const int kFeetIds[2] = { SFX_LARA_FEET, SFX_LARA_LAND };

	/* Only with hd_enable on. Without it there is no wardrobe, no outfit and
	 * no second set, so the two ids take the ordinary chain and a sound mod
	 * plays unchanged. */
	if (!HDEnabled() || !sample_lut || !sample_infos)
		return false;

	for (int k = 0; k < 2; k++)
	{
		const int info = sample_lut[kFeetIds[k]];

		if (info < 0)
			continue;

		int takes = (sample_infos[info].flags >> 2) & 0xF;

		if (takes < 1)
			takes = 1;

		if (sample >= sample_infos[info].number &&
			sample < sample_infos[info].number + takes)
			return true;
	}

	return false;
}

bool LoadSamples()
{
	long num_infos, num_samples, uncomp_size, comp_size;

	Log(__FUNCTION__);
	sample_lut = (short*)game_malloc(MAX_SAMPLES * sizeof(short));
	ReadBlock(sample_lut, MAX_SAMPLES * sizeof(short));

	num_infos = ReadLong();
	Log("Number Of Sample Infos %d", num_infos);

	if (!num_infos)
	{
		Log("No Sample Infos");
		return 0;
	}

	/* Two spare SAMPLE_INFO entries after the level's own. The barefoot
	 * recordings are a second set for sound ids 0 and 4, and a set needs a
	 * SAMPLE_INFO of its own - the number of its first sample and how many
	 * takes follow it. The level's table is exactly num_infos long, so the
	 * two are allocated here with it. The level file fills only its own
	 * entries; hd_feet.cpp fills these two, and leaves them zero in a level
	 * without a bare set.
	 *
	 * Nothing reads past num_infos on its own: sample_lut names an info, and
	 * only HDFeetBare ever writes an index above it.
	 *
	 * Only with hd_enable on: sixteen bytes shift every later allocation in
	 * the bump allocator, and with HD off the memory layout stays exactly
	 * that of the classic engine. */
	const long spare = HDEnabled() ? HD_FEET_SPARE_INFOS : 0;

	sample_infos = (SAMPLE_INFO*)game_malloc((num_infos + spare) * sizeof(SAMPLE_INFO));
	ReadBlock(sample_infos, num_infos * sizeof(SAMPLE_INFO));

	if (spare)
		memset(sample_infos + num_infos, 0, spare * sizeof(SAMPLE_INFO));

	// Optional: higher quality versions of these same sounds, out of a mod's
	// bank, out of audio\MAIN.SFX or out of the installed remaster itself
	// (the chain is in hqsfx.cpp). The bridge between this
	// level's sample numbers and that file's global ones runs over the sound
	// map, so it can only be built now that the map has been read. Without a
	// bank both calls do nothing and the level plays its own samples.
	HQSfxOpen();
	HQSfxBuildMap(sample_lut, MAX_SAMPLES, sample_infos, num_infos);

	num_samples = ReadLong();

	if (!num_samples)
	{
		Log("No Samples");
		return 0;
	}

	Log("Number Of Samples %d", num_samples);
	fread(&num_samples, 1, 4, level_fp);

	if (feof(level_fp))
		Log("END OF FILE");

	InitSampleDecompress();

	if (num_samples <= 0)
	{
		FreeSampleDecompress();
		return 1;
	}

	// num_samples comes unchecked from the level file, and
	// DXCreateSampleADPCM() (specific\dxsound.cpp) writes DS_Buffers[num]
	// for every one of them (the same overflow class as SAVEGAME_INFO in
	// game\savegame.cpp).
	//
	// DS_Buffers[] holds MAX_SAMPLES=450, the same size as sample_lut above,
	// because every imported enemy brings its own sounds along. Both
	// DXCreateSample* range-check as well, so this is a clean early exit
	// rather than the only line of defence.
	if (num_samples > MAX_SAMPLES)
	{
		FreeSampleDecompress();

		Log("FATAL: level has %d sound samples, only %d fit in DS_Buffers[]",
			num_samples, MAX_SAMPLES);

		MessageBox(0,
			"This level has more sound samples than the engine can handle.\r\n\r\n"
			"The game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	for (int i = 0; i < num_samples; i++)
	{
		fread(&uncomp_size, 1, 4, level_fp);
		fread(&comp_size, 1, 4, level_fp);

		// comp_size comes unchecked from the level file; without this bound
		// an oversized sample (imported sounds make that possible) overruns
		// the fixed samples_buffer.
		if (comp_size < 0 || comp_size > SAMPLES_BUFFER_SIZE)
		{
			Log("FATAL: sample %d is %d bytes, samples_buffer holds %d",
				i, comp_size, SAMPLES_BUFFER_SIZE);
			FreeSampleDecompress();
			return 0;
		}

		fread(samples_buffer, comp_size, 1, level_fp);

		// The sample is read either way, so the file position stays right;
		// only what is handed on changes. HQSfxPayload returns null unless
		// there is a bank, this sample has a counterpart in it, and that
		// counterpart is the same length - so a sound the randomizer moved
		// or imported keeps Core's own version.
		char* riff = samples_buffer;
		long riffSize = comp_size;
		long hqSize = 0;
		SfxRiffParts hqWhere;
		/* The samples behind sound ids 0 and 4 (the booted footsteps) are
		 * loaded through the first unmodded bank of the chain, so that a
		 * sound mod which replaces the footsteps supplies the barefoot set
		 * (hd_feet.cpp) instead of making every outfit barefoot at once.
		 * Every other sample takes the ordinary chain, mod and loose .wav
		 * included. */
		char* hq = feet_sample(i)
			? HQSfxPayloadBoots(i, uncomp_size, &hqSize, &hqWhere)
			: HQSfxPayload(i, uncomp_size, &hqSize, &hqWhere);

		if (hq)
		{
			riff = hq;
			riffSize = hqSize;
		}

		// Format switch for imported samples. Core's own samples are
		// MS-ADPCM and go through the ACM decoder. The samples from Tomb
		// Raider 3 (MAIN.SFX) are plain PCM in exactly the format the engine
		// expects - mono, 22050 Hz, 16 bit - so they need no decoder, only
		// the direct way into the XAudio2 buffer that DXCreateSample offers.
		//
		// For a sample out of the level the tag stands in the fmt block at
		// +20 (RIFF 4 + size 4 + WAVE 4 + "fmt " 4 + block size 4). 1 = PCM,
		// 2 = MS-ADPCM. DXCreateSampleADPCM reads the same spot.
		//
		/* For a sample out of a mod the offsets are not assumed. +20 only
		 * holds the format tag when fmt is the first chunk, and wavs written
		 * by an editor often are not: Reaper, Pro Tools and libsndfile put a
		 * 28 byte JUNK block in front of fmt as room to grow into RF64, and a
		 * LIST block with the title is just as common. Such a file is a legal
		 * mono 16 bit PCM wav and passes the gate in hqsfx.cpp, but its bytes
		 * at +20 are 0x0000 (measured on a JUNK-first file), so a fixed
		 * offset would drop it silently while the load log counted it as
		 * supplied. The offsets therefore come through SfxRiffParts from the
		 * walk that accepted the file, and `hq` alone takes the branch:
		 * everything HQSfxPayload returns is PCM, mono, 16 bit with a data
		 * chunk inside the buffer. */
		long fmtAt = 20;
		long dataAt = 0, dataLen = 0;

		if (hq)
		{
			fmtAt = hqWhere.fmtAt;
			dataAt = hqWhere.dataAt;
			dataLen = hqWhere.dataBytes;
		}

		const WAVEFORMATEX* wf = (const WAVEFORMATEX*)(riff + fmtAt);

		if (hq || (riffSize > 20 && wf->wFormatTag == WAVE_FORMAT_PCM))
		{
			// Search for the data block instead of assuming its position:
			// further blocks can stand behind the fmt block (fact, LIST).
			// Only for the level's own sample - a mod's arrived with its
			// data chunk already found and already bounded by the gate.
			if (!hq)
			{
				long at = 12;

				while (at + 8 <= riffSize)
				{
					const long len = *(const long*)(riff + at + 4);

					if (!memcmp(riff + at, "data", 4))
					{
						dataAt = at + 8;
						dataLen = len;
						break;
					}

					at += 8 + len + (len & 1);
				}
			}

			if (dataAt <= 0 || dataAt + dataLen > riffSize)
			{
				Log("Sample %d: PCM without a usable data block", i);
				FreeSampleDecompress();
				return 0;
			}

			if (!DXCreateSample(i, (LPWAVEFORMATEX)wf, riff + dataAt, dataLen))
			{
				FreeSampleDecompress();
				return 0;
			}

			continue;
		}

		if (!DXCreateSampleADPCM(samples_buffer, comp_size, uncomp_size, i))
		{
			FreeSampleDecompress();
			return 0;
		}
	}

	FreeSampleDecompress();

	// Which bank supplied the samples - a mod's, audio\MAIN.SFX or the
	// installed remaster's - how many of them a mod's bank replaced, and how
	// many a mod's loose .wav supplied on top.
	if (HQSfxSubstituted())
		Log("HQ SFX: %d of %d samples came from %s, %d of them a recording the "
			"mod's bank replaced, %d of them overlaid by a mod's .wav",
			HQSfxSubstituted(), num_samples, HQSfxSource(), HQSfxReplaced(),
			HQSfxOverlaid());

	/* The barefoot set, into the slots after the level's own and into the
	 * two spare infos allocated above. It writes its own log line, including
	 * the reason when a level cannot have one.
	 *
	 * After the summary above, because it takes its recordings through the
	 * same HQSfxPayload the loop does and would otherwise raise that line's
	 * count by takes that are not this level's samples; before the map is
	 * dropped, because it reads them through the bridge HQSfxDropMap frees. */
	HDFeetLevelBuild(sample_lut, sample_infos, num_infos, spare, num_samples);

	HQSfxDropMap();
	return 1;
}

/* LOADLOG, with -log: the steps of a level load, one line each with the
 * milliseconds since the step before - the load thread's steps out of
 * LoadLevel, the main thread's out of S_LoadLevelFile, and the HD build's
 * own from hd_level.cpp and hd_object.cpp in the same form. The tick in
 * front of a log line moves in steps of about 16 ms, coarser than most
 * steps, so the clock is QueryPerformanceCounter's. Each thread keeps its
 * own lap; the main thread only waits while the load thread runs. Without
 * -log no clock is read and nothing is written. */
static char g_loadlog_level[32];
static double g_loadlog_thread;

static void LoadLogStep(double* at, const char* step)
{
	if (!gLogEnabled)
		return;

	LARGE_INTEGER now, rate;

	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&rate);

	const double ms = (double)now.QuadPart * 1000.0 / (double)rate.QuadPart;

	if (step)
		Log("LOADLOG level=%s step=%s ms=%.1f", g_loadlog_level, step,
			ms - *at);

	*at = ms;
}

// The level's name for those lines: the file name without folder or .TRC.
static void LoadLogLevel(const char* wad)
{
	const char* slash = strrchr(wad, '\\');
	const char* name = slash ? slash + 1 : wad;
	size_t n = 0;

	while (name[n] && name[n] != '.' && n + 1 < sizeof(g_loadlog_level))
	{
		g_loadlog_level[n] = name[n];
		n++;
	}

	g_loadlog_level[n] = 0;
}

unsigned int __stdcall LoadLevel(void* name)
{
	OBJECT_INFO* obj;
	TEXTURESTRUCT* tex;
	MESH_DATA* mesh;
	char* pData;
	char* pBefore;
	char* pAfter;
	long version, size, compressedSize;
	short RTPages, OTPages, BTPages;
	short data[16];

	Log("Begin " __FUNCTION__);
	LoadLogStep(&g_loadlog_thread, 0);
	// The level about to be read, for HDLevelUnload inside FreeLevel: the HD
	// data of the same level stay in memory (hd_levelkey.h). Nothing with
	// hd_enable off.
	HDLevelKeyNext((const char*)name);
	FreeLevel();
	LoadLogStep(&g_loadlog_thread, "free");
	nTextures = 1;
	Textures[0].tex = 0;
	Textures[0].width = 0;
	Textures[0].height = 0;
	Textures[0].bump = 0;
	CompressedData = 0;
	FileData = 0;
	level_fp = 0;
	level_fp = FileOpen((const char*)name);

	if (level_fp)
	{
		fread(&version, 1, 4, level_fp);
		fread(&RTPages, 1, 2, level_fp);
		fread(&OTPages, 1, 2, level_fp);
		fread(&BTPages, 1, 2, level_fp);
		S_InitLoadBar(OTPages + BTPages + RTPages + 20);
		S_LoadBar();

		LoadTextures(RTPages, OTPages, BTPages);
		LoadLogStep(&g_loadlog_thread, "textures");

		fread(data, 1, 32, level_fp);
		LaraDrawType = data[0] + LARA_NORMAL;
		WeatherType = (char)data[1];
		// data[2]: randomizer marker "speech heads do not match the outfit"
		// (outfit donor without LARA_SPEECH_HEAD1-4, e.g. catsuit from rich2).
		// In vanilla all 16 files carry 0 here - measured.
		GfForeignSpeechHeads = (char)data[2];
		// data[3]: randomizer marker "this level wears a Tomb Raider III
		// outfit". It carries the engine's own HD_LARA_OUTFIT_* number -
		// 5 ANTARC, 6 NEVADA, 7 COAST, 8 CATSUIT - so that neither side
		// translates and a stale byte cannot pass as a vanilla outfit;
		// hd_laradraw.cpp hands it to HDLaraOutfitFor unchanged. All 17
		// shipped files carry 0 in data[3] through data[15] - measured. Read
		// here beside data[0..2] because this is the one place the header
		// block is in hand, long before the HD path asks for it.
		GfHdOutfit = (char)data[3];

		fread(&size, 1, 4, level_fp);
		fread(&compressedSize, 1, 4, level_fp);
		FileData = (char*)malloc(size);
		fread(FileData, size, 1, level_fp);

		pData = FileData;
		LoadLogStep(&g_loadlog_thread, "read");

		LoadRooms();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "rooms");

		LoadObjects();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "moveables");

		LoadSprites();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "sprites");

		LoadCameras();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "cameras");

		LoadSoundEffects();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "sound_fx");

		LoadBoxes();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "boxes");

		LoadAnimatedTextures();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "anim_tex");

		LoadTextureInfos();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "tex_infos");

		// The HD side of the level. Here and not earlier because this is the
		// first moment the rooms, the meshes and the object slots are all in
		// memory - and in the randomized state, which is the state the models
		// have to be bound against. With the registry setting hd_enable at 0,
		// the shipped default, it reads only the level's .hdm sidecar, whose
		// sound line serves the sound bank bridge with HD off - see
		// HDEnabled in specific\hd\hd_level.cpp.
		HDLevelLoad((const char*)name);
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "hd_load");

		pBefore = FileData;
		size = ReadLong();		//nItems
		FileData += 24 * size;	//skip item data

		LoadAIInfo();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "ai");

		pAfter = FileData;
		FileData = pBefore;

		LoadItems();
		S_LoadBar();
		FileData = pAfter;
		LoadLogStep(&g_loadlog_thread, "items");

		LoadCinematic();
		S_LoadBar();
		LoadLogStep(&g_loadlog_thread, "cinematic");

		if (acm_ready && !App.SoundDisabled)
			LoadSamples();
		LoadLogStep(&g_loadlog_thread, "samples");

		free(pData);
		S_LoadBar();

		for (int i = 0; i < 6; i++)
		{
			obj = &objects[WATERFALL1 + i];

			if (obj->loaded)
			{
				tex = &textinfo[mesh_vtxbuf[obj->mesh_index]->gt4[4] & 0x7FFF];
				AnimatingWaterfalls[i] = tex;
				AnimatingWaterfallsV[i] = tex->v1;
			}
		}

		S_LoadBar();

		S_GetUVRotateTextures();
		S_LoadBar();
		SetupGame();
		S_LoadBar();
		SetFadeClip(0, 1);
		reset_cutseq_vars();

		if (gfLevelId == LVL5_STREETS_OF_ROME)
			find_a_fucking_item(ANIMATING10)->mesh_bits = 11;

		if (gfLevelId == LVL5_OLD_MILL)
			find_a_fucking_item(ANIMATING16)->mesh_bits = 1;

		MonitorScreenTex = 0;
		obj = &objects[MONITOR_SCREEN];

		if (obj->loaded)
		{
			mesh = (MESH_DATA*)meshes[objects[MONITOR_SCREEN].mesh_index];

			for (int i = 0; i < mesh->ngt4; i++)
			{
				if (mesh->gt4[i * 6 + 5] & 1)	//semitrans quad
				{
					mesh->gt4[i * 6 + 5] &= ~1;	//no more
					MonitorScreenTex = &textinfo[mesh->gt4[i * 6 + 4] & 0x7FFF];
					MonitorScreenU = MonitorScreenTex->u1;
					break;
				}
			}
		}

		FileClose(level_fp);
		LoadLogStep(&g_loadlog_thread, "game_setup");
		// aInit() and aMakeCutsceneResident() are not called here on the load
		// thread: both work on the D3D device (room vertex buffers, cutscene
		// textures) while the main thread presents the load bar, and two
		// threads on one device freeze the game in this window (bar full,
		// nothing moves). The main thread makes both calls after this thread
		// ends, see S_LoadLevelFile.
		ClearFX();
		LevelLoadingThread.ended = 1;    // success path - main thread may aInit()
	}

	Log("LoadLevel: load thread done");
	LevelLoadingThread.active = 0;
	_endthreadex(1);
	return 1;
}

/* The warm-up: one-time work taken out of a level's first drawn frames.
 * Without it a level needs about three seconds to settle at its frame rate
 * (the title screen drops to 23 fps first) on hardware that is far from its
 * limit, because work done once lands inside frames meant to draw.
 *
 * Nearly every texture in this engine is D3DPOOL_MANAGED - the classic pages
 * (specific\texture.cpp, CreateTexturePage), the HD pages when the budget
 * allows (specific\hd\hd_pagecache.cpp) and every HD model's vertex and index
 * buffer (specific\hd\hd_model.cpp). A managed resource lives in system
 * memory until something draws with it, and Direct3D uploads it at that
 * first bind - so the frame that first shows a room, a texture or an enemy
 * pays for its upload.
 *
 * IDirect3DResource9::PreLoad asks the driver to make the resource resident
 * now. It costs nothing for a resource that already is, returns void (a hint,
 * not a promise) and draws nothing - no render state, no geometry, nothing
 * on screen - which is why it is used instead of drawing a tiny triangle with
 * each page off screen. It runs where a stall is free: after the load thread
 * has joined and the HD build is done, with the loading bar still up.
 *
 * Not covered: the water mark tables, a room's first light list, the
 * driver's first use of a shader or vertex declaration, and Windows paging
 * the process in. PROFLOG's `first_frame` line measures those. */
static void WarmUpLevel()
{
	LARGE_INTEGER freq, t0, t1;
	int pages, hd_pages, hd_buffers;
	double ms;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t0);

	pages = 0;
	hd_pages = 0;
	hd_buffers = 0;

	for (int i = 0; i < nTextures; i++)
	{
		if (Textures[i].tex)
		{
			Textures[i].tex->PreLoad();
			pages++;
		}
	}

	// The HD pages of this level. HDPageCacheSlots() is 0 with hd_enable off
	// and before a level is built, so this is one call and a loop that does
	// not run. A page in D3DPOOL_DEFAULT is already on the card and PreLoad
	// is documented to be ignored for it - correct either way, no test here.
	for (int i = 0, n = HDPageCacheSlots(); i < n; i++)
	{
		IDirect3DTexture9* tex = HDPageCacheSlotTexture(i);

		if (tex)
		{
			tex->PreLoad();
			hd_pages++;
		}
	}

	hd_buffers = HDModelPreload();

	/* The CPU side of the same idea: the sort buffer, the sort list and the
	 * bucket vertices are ~5 MB of .bss that Windows only commits on the
	 * first write, and those page faults would otherwise fall in the level's
	 * first drawn frame. The note over PreTouchDrawBuffers says why zeroing
	 * them cannot change a pixel. */
	PreTouchDrawBuffers();

	QueryPerformanceCounter(&t1);
	ms = freq.QuadPart ?
		(double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart : 0;

	Log("WARMUP pages=%d hd_pages=%d hd_buffers=%d ms=%.1f", pages, hd_pages,
		hd_buffers, ms);
}

long S_LoadLevelFile(long num)
{
	static long lscreen = 0;
	static long flag = 0;
	const char* hd_name;
	long chosen_screen;
	long hd_logo;
	char name[80];
	double loadlog_at = 0.0;
	double loadlog_start = 0.0;

	Log(__FUNCTION__);
	LoadLogStep(&loadlog_at, 0);
	LoadLogStep(&loadlog_start, 0);

	if (!tomb5.tr4_loadscreens || (!num && !bDoCredits && !gfStatus))
	{
		if (!MonoScreenOn)
		{
			// SCREENS.STR holds the pictures in the campaign's own order:
			// 0 is the logo screen, 3..16 the fourteen levels in the order
			// the file name table lists them (ANDREA1..RICH3), 17..19 the
			// three title pictures, 1/2/20 unused.
			//
			// num is NOT the slot. It is the file name index out of the
			// level's CMD_LEVEL - DoLevel() passes gf[3] straight through -
			// and the randomizer permutes only the script offset table, never
			// the file name table or the indices pointing into it. So num is
			// the level's identity in vanilla and in a shuffled campaign
			// alike, and num + 2 is already the right picture (measured
			// against both SCRIPT.DATs). gfIdFromSlot[num] would be wrong
			// here: it indexes a slot table with an identity.
			chosen_screen = num;
			hd_logo = 0;
			hd_name = HDScreenLevelName(&gfFilenameWad[gfFilenameOffset[num]]);

			if (!num)
			{
				hd_name = "TITLE";

				if (flag)
				{
					chosen_screen = lscreen % 3 + 15;
					lscreen++;
				}
				else
				{
					flag = 1;
					chosen_screen = -2;

					// Picture 0 is the game's logo screen. The remaster's HD
					// files carry it per language and without the title
					// picture behind it, so this is the only place that needs
					// two HD files at once and the only one that picks by
					// language rather than by level.
					hd_logo = 1;
				}
			}

			if (!(hd_logo ? LoadScreenHDLogo() : LoadScreenHD(hd_name)))
				LoadScreen(chosen_screen + 2, 4);
		}
	}

	strcpy(name, &gfFilenameWad[gfFilenameOffset[num]]);
	LoadLogLevel(name);
	loadbar_on = 0;
	strcat(name, ".TRC");

	for (int i = 0; i < 4; i++)
	{
		_BeginScene();
		InitBuckets();
		InitialiseSortList();
		DrawLoadingScreen();
		SortPolyList(SortCount, SortList);
		DrawSortList();
		S_DumpScreenFrame();
	}

	if (!tomb5.tr4_loadscreens || (!num && !bDoCredits && !gfStatus))
	{
		if (MonoScreenOn == 2)
			ReleaseScreen();
	}

	LoadLogStep(&loadlog_at, "screen");
	LevelLoadingThread.active = 1;
	LevelLoadingThread.ended = 0;
	LevelLoadingThread.handle = _beginthreadex(0, 0, LoadLevel, name, 0, (unsigned int*)&LevelLoadingThread.address);

	while (LevelLoadingThread.active)
	{
		if (App.dx.Flags & DXF_HWR && loadbar_on)
			S_DrawLoadBar();


		// This loop only repaints the loading bar while a background thread
		// does the file I/O, so it need not call Present() (via
		// S_DrawLoadBar -> S_DumpScreenFrame -> DXShowFrame) faster than a
		// human can see. Unthrottled, it calls Present() as fast as the CPU
		// allows with no vsync (D3DPRESENT_INTERVAL_IMMEDIATE) and no message
		// pumping in between, and the game thread can hang for good inside
		// the NVIDIA driver (nvd3dum) at this call site - a present-queue/DWM
		// stall, observed under a debugger.
		Sleep(1);
	}

	Log("S_LoadLevelFile: load thread joined");
	LoadLogStep(&loadlog_at, "thread");

	// Here and not on the load thread (see the comment in LoadLevel): room
	// vertex buffers and resident cutscenes belong to the main thread,
	// which owns the device - no second thread touches D3D while the bar
	// presents.
	if (LevelLoadingThread.ended)        // load thread's success path
	{
		aInit();
		Log("S_LoadLevelFile: aInit done");
		LoadLogStep(&loadlog_at, "ainit");

		// The HD geometry and its texture pages. Here on the main thread,
		// not in HDLevelLoad on the load thread, for the same reason as
		// aInit() above. HDLevelLoad has already read the .TRG's numbers and
		// bound the models; this makes the Direct3D objects, which belong to
		// the thread that owns the device. Nothing happens while hd_enable
		// is 0.
		HDLevelBuild();
		Log("S_LoadLevelFile: HD build done");
		LoadLogStep(&loadlog_at, "hd_build");

		// The HD font's own texture, on the same thread for the same
		// reason. It is made once and kept for the whole
		// session - the atlas does not change from level to level - so after
		// the first level this is a comparison and a return.
		HDFontBuild();
		LoadLogStep(&loadlog_at, "font");
	}
	aMakeCutsceneResident(gfResidentCut[0], gfResidentCut[1], gfResidentCut[2], gfResidentCut[3]);
	Log("S_LoadLevelFile: Cutscenes resident");
	LoadLogStep(&loadlog_at, "cutscenes");

	// The level's textures and buffers onto the card before the first frame
	// asks for them - see the note over WarmUpLevel.
	WarmUpLevel();
	LoadLogStep(&loadlog_at, "warmup");

	// PROFLOG counts a level's seconds from here. The name still carries its
	// .TRC; the profile strips it.
	ProfileLevel(name);
	LoadLogStep(&loadlog_at, "profile");

	// The work is done, so the bar is shown full in one frame and the level
	// starts: the bar may take no longer than the load itself. While the
	// load thread runs, the loop above moves it by 2 per drawn frame; it
	// stands still while the main thread works after the join.
	// S_FinishLoadBar puts target and position at 100, so this one draw shows
	// the full bar (see drawbars.cpp).
	S_FinishLoadBar();

	if (App.dx.Flags & DXF_HWR)
		S_DrawLoadBar();

	Log("S_LoadLevelFile: load bar finished");
	LoadLogStep(&loadlog_at, "bar");
	LoadLogStep(&loadlog_start, "total");
	return 1;
}

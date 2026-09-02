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
#include "file.h"
#include "function_stubs.h"
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

	// AnimatingTexturesV is a fixed [16][8][3] array - same bug class as
	// SAVEGAME_INFO (see that fix's comments in game\savegame.cpp). Both
	// dimensions used here come from level file data with no upstream
	// validation: nAnimUVRanges (outer, i) is a raw byte read in
	// LoadAnimatedTextures(); the per-range count (inner, j's starting
	// value) is read from aranges[] right below, per range.
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
	malloc_ptr = malloc_buffer;
	malloc_free = malloc_size;

	// This line is the moment EVERY pointer into the game_malloc pool goes
	// stale: the bump allocator starts over at the beginning, so an address
	// from the level before now points into the middle of the level about
	// to be loaded. baddie_slots is the one that matters, because it is
	// read again before it is reallocated: InitialiseLOTarray runs AFTER
	// S_LoadLevelFile (gameflow.cpp), while ShutThatDoor - called from
	// InitialiseDoor, i.e. DURING loading - walks all MAX_LOT slots and
	// writes to them.
	//
	// Found 2026-08-25 in joby3: a closed door scattered 2047 through the
	// old array's addresses and one of them landed exactly on item 277's
	// room number. The engine then read room[2047] out of a 195-room field
	// and the level died while loading, with nothing in the log pointing at
	// a door.
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

	// RTPages/OTPages/BTPages come straight from the level file with no
	// upstream validation - the fill loops below write Textures[nTextures++]
	// for each page with no bounds check of their own (this is the exact
	// bug class already found and fixed for SAVEGAME_INFO - see that fix's
	// comments in game\savegame.cpp for the full rationale). Index 0 is
	// reserved (set explicitly by the caller before LoadTextures runs), so
	// the usable range is 1..(RTPages+OTPages+BTPages), which must fit
	// within Textures[]'s size.
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

		if (Gameflow->Language == US)
			size = LoadFile("data\\uslogo.pak", &CompressedData);
		else if (Gameflow->Language == GERMAN)
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
		// build's enum does not know yet - skip the record instead of
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

	// number_spotcams comes straight from the level file, and SpotCam is a
	// fixed-size 256-entry global array - same bug class as SAVEGAME_INFO
	// (see game\savegame.cpp's fix for the full rationale): the ReadBlock
	// below is a raw memcpy with no bounds check of its own.
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
// MAX_ITEMS check below: say what is wrong, say it where the user sees it,
// then stop. Deliberately not a silent clamp - a level carrying impossible
// values is either damaged or was written for a different build, and both
// are worth knowing rather than playing around. The hint at -log is there
// because the seams that name the culprit only write with it (#125).
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

	// num_items comes straight from the level file with no upstream
	// validation - the fill loop below writes items[i] for every one of
	// them with no bounds check of its own (same bug class as SAVEGAME_INFO,
	// see that fix's comments in game\savegame.cpp). MAX_ITEMS (defined
	// global\types.h) is left as-is here (not raised) - see items[] itself
	// for why.
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
		// room that does not exist. Caught here, that is one clear line;
		// caught nowhere, it is a wild read and a hard crash.
		if (item->object_number < 0 || item->object_number >= NUMBER_OBJECTS)
			FatalLevelData(aFmt("item %d carries slot %d - valid is 0..%d",
								i, item->object_number, NUMBER_OBJECTS - 1));

		if (item->room_number < 0 || item->room_number >= number_rooms)
			FatalLevelData(aFmt("item %d sits in room %d - the level has %d",
								i, item->room_number, number_rooms));
	}

	for (int i = 0; i < num_items; i++)
	{
		// Diagnostic seam (#125). An item's initialise is the one place in
		// loading where the LEVEL's data meets the ENGINE's slot logic -
		// anim offsets off the object's anchor, OCB read as a behaviour,
		// neighbouring items looked up by room. A randomized level produces
		// slot/OCB pairings no vanilla level ever had, so when loading dies
		// here the only question that matters is WHICH item. The line is
		// written before the call, so the last one in log.txt names it.
		Log("ITEMLOG %d: slot %d (loaded %d anim %d) room %d ocb %d flags 0x%X pos %d,%d,%d",
			i, items[i].object_number,
			objects[items[i].object_number].loaded,
			objects[items[i].object_number].anim_index,
			items[i].room_number,
			items[i].trigger_flags & 0x3F, items[i].flags,
			items[i].pos.x_pos, items[i].pos.y_pos, items[i].pos.z_pos);

		InitialiseItem(i);

		// The room number was checked while reading, so an item standing
		// somewhere impossible AFTERWARDS can only have been written by one
		// of the initialise calls above - a stray write, and the seam over
		// it would only say "it died at item 277", never who did it. This
		// scan names both sides in one line.
		//
		// It is O(n2) and therefore hangs on the call-site gate and not only
		// on Log()'s own: without -log not a single comparison runs. Once a
		// culprit is found the scan switches itself off - the first one is
		// the one that matters, the rest are its consequences.
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

	sample_infos = (SAMPLE_INFO*)game_malloc(num_infos * sizeof(SAMPLE_INFO));
	ReadBlock(sample_infos, num_infos * sizeof(SAMPLE_INFO));

	// Optional: higher quality versions of these same sounds, if the player
	// put the remaster's MAIN.SFX into audio\. The bridge between this
	// level's sample numbers and that file's global ones runs over the sound
	// map, so it can only be built now that the map has been read. Without
	// the file both calls do nothing and the level plays its own samples.
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

	// num_samples comes straight from the level file with no upstream
	// validation, and DXCreateSampleADPCM() (specific\dxsound.cpp) writes
	// DS_Buffers[num] for every one of them - same bug class as SAVEGAME_INFO
	// (see that fix's comments in game\savegame.cpp).
	//
	// The ceiling used to be 256 while sample_lut right above is allocated
	// with MAX_SAMPLES=450 - DS_Buffers[] was the tightest part of the
	// engine's own sound system. It now matches (#94), because every foreign
	// import brings its own sounds along and the TR3 enemies ate visibly into
	// the budget. Both DXCreateSample* now range-check as well, so this stays
	// a clean early exit rather than the only line of defence.
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

		// comp_size comes straight out of the level file. Reading it into a
		// fixed buffer with no bound is the same hole #94 closed in
		// DXCreateSample*, and it stopped being theoretical the moment the
		// randomizer began importing foreign sounds.
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
		char* hq = HQSfxPayload(i, uncomp_size, &hqSize);

		if (hq)
		{
			riff = hq;
			riffSize = hqSize;
		}

		// Format switch for foreign imports (#93). Core's own samples come
		// as MS-ADPCM and go through the ACM decoder. The samples from
		// Tomb Raider 3 (MAIN.SFX), by contrast, are plain PCM - and in
		// exactly the format the engine expects anyway: mono, 22050 Hz,
		// 16 bit. They need no decoder at all, only the direct way into
		// the XAudio2 buffer that DXCreateSample already offers.
		//
		// The tag stands in the fmt block at +20 (RIFF 4 + size 4 + WAVE 4
		// + "fmt " 4 + block size 4). 1 = PCM, 2 = MS-ADPCM. The same spot
		// is read by DXCreateSampleADPCM.
		const WAVEFORMATEX* wf = (const WAVEFORMATEX*)(riff + 20);

		if (riffSize > 20 && wf->wFormatTag == WAVE_FORMAT_PCM)
		{
			// Search for the data block instead of assuming its position:
			// further blocks can stand behind the fmt block (fact, LIST).
			long at = 12;
			long dataAt = 0, dataLen = 0;

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

	if (HQSfxSubstituted())
		Log("HQ SFX: %d of %d samples came from audio\\MAIN.SFX",
			HQSfxSubstituted(), num_samples);

	HQSfxDropMap();
	return 1;
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
	FreeLevel();
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

		fread(data, 1, 32, level_fp);
		LaraDrawType = data[0] + LARA_NORMAL;
		WeatherType = (char)data[1];
		// data[2]: randomizer marker "speech heads do not match the outfit"
		// (outfit donor without LARA_SPEECH_HEAD1-4, e.g. catsuit from rich2).
		// In vanilla all 16 files carry 0 here - measured.
		GfForeignSpeechHeads = (char)data[2];

		fread(&size, 1, 4, level_fp);
		fread(&compressedSize, 1, 4, level_fp);
		FileData = (char*)malloc(size);
		fread(FileData, size, 1, level_fp);

		pData = FileData;

		LoadRooms();
		S_LoadBar();

		LoadObjects();
		S_LoadBar();

		LoadSprites();
		S_LoadBar();

		LoadCameras();
		S_LoadBar();

		LoadSoundEffects();
		S_LoadBar();

		LoadBoxes();
		S_LoadBar();

		LoadAnimatedTextures();
		S_LoadBar();

		LoadTextureInfos();
		S_LoadBar();

		pBefore = FileData;
		size = ReadLong();		//nItems
		FileData += 24 * size;	//skip item data

		LoadAIInfo();
		S_LoadBar();

		pAfter = FileData;
		FileData = pBefore;

		LoadItems();
		S_LoadBar();
		FileData = pAfter;

		LoadCinematic();
		S_LoadBar();

		if (acm_ready && !App.SoundDisabled)
			LoadSamples();

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
		// aInit() and aMakeCutsceneResident() originally still ran in the
		// LOAD THREAD here - both work on the D3D device (room vertex
		// buffers, cutscene textures) while the main thread presents the
		// load bar in parallel. The same two-threads-one-device class as
		// the level-load hang of Jul 31 (then LoadTextures, fix
		// D3DCREATE_MULTITHREADED); since the randomized levels grew, the
		// game sporadically froze exactly in this window (bar full,
		// nothing moves - seen at the title too on Aug 15). Both calls are
		// now made by the main thread AFTER the thread's end, see
		// S_LoadLevelFile.
		ClearFX();
		LevelLoadingThread.ended = 1;    // success path - main thread may aInit()
	}

	Log("LoadLevel: load thread done");
	LevelLoadingThread.active = 0;
	_endthreadex(1);
	return 1;
}

long S_LoadLevelFile(long num)
{
	static long lscreen = 0;
	static long flag = 0;
	long chosen_screen;
	char name[80];

	Log(__FUNCTION__);

	if (!tomb5.tr4_loadscreens || (!num && !bDoCredits && !gfStatus))
	{
		if (!MonoScreenOn)
		{
			chosen_screen = num;

			if (!num)
			{
				if (flag)
				{
					chosen_screen = lscreen % 3 + 15;
					lscreen++;
				}
				else
				{
					flag = 1;
					chosen_screen = -2;
				}
			}

			LoadScreen(chosen_screen + 2, 4);
		}
	}

	strcpy(name, &gfFilenameWad[gfFilenameOffset[num]]);
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

	LevelLoadingThread.active = 1;
	LevelLoadingThread.ended = 0;
	LevelLoadingThread.handle = _beginthreadex(0, 0, LoadLevel, name, 0, (unsigned int*)&LevelLoadingThread.address);

	while (LevelLoadingThread.active)
	{
		if (App.dx.Flags & DXF_HWR && loadbar_on)
			S_DrawLoadBar();


		// This loop just repaints the loading bar while a background thread
		// does the actual file I/O - it doesn't need to call Present() (via
		// S_DrawLoadBar -> S_DumpScreenFrame -> DXShowFrame) any faster than a
		// human can see it update. Left unthrottled, it hammers Present() as
		// fast as the CPU allows with no vsync (D3DPRESENT_INTERVAL_IMMEDIATE)
		// and no message pumping in between - cdb caught the game thread stuck
		// for good deep inside the NVIDIA driver (nvd3dum) from exactly this
		// call site, almost certainly a present-queue/DWM stall from calling
		// Present() far harder than this code was ever tested against on
		// original hardware.
		Sleep(1);
	}

	Log("S_LoadLevelFile: load thread joined");

	// Moved here from the load thread (see the comment in LoadLevel): room
	// vertex buffers and resident cutscenes belong to the main thread,
	// which owns the device anyway - no second thread on the D3D anymore
	// while the bar presents.
	if (LevelLoadingThread.ended)        // load thread's success path
	{
		aInit();
		Log("S_LoadLevelFile: aInit done");
	}
	aMakeCutsceneResident(gfResidentCut[0], gfResidentCut[1], gfResidentCut[2], gfResidentCut[3]);
	Log("S_LoadLevelFile: Cutscenes resident");

	// The load thread has joined - loading IS done, no matter what the
	// step counting believes. S_FinishLoadBar puts the bar at 100%
	// (Core's completion criterion tipped into an endless loop from ~75
	// texture pages on, see drawbars.cpp), and the catch-up loop is
	// additionally bounded - nothing may ever hang here again.
	S_FinishLoadBar();

	if (App.dx.Flags & DXF_HWR && !S_DrawLoadBar())
		for (int i = 0; i < 200 && !S_DrawLoadBar(); ++i)
			Sleep(1);

	Log("S_LoadLevelFile: load bar finished");
	return 1;
}

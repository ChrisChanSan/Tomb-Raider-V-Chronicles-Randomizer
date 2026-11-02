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
#include "replay.h"
#include "replay_format.h"
#include "function_stubs.h"	// Log, gLogEnabled, the two generators
#include "input.h"		// the pad's readings
#include "output.h"		// ActiveFpsMode
#include "../game/control.h"	// GlobalCounter
#include "../game/text.h"	// GnFrameCounter
#include "../game/gameflow.h"	// gfLevelId
#include "../game/lara.h"		// lara_item - the input record's check

/* -record and -replay.
 *
 * What a run consumes that is not in the level file: the devices, read once
 * per call of S_UpdateInput, and the clock, read by S_DumpScreen as the
 * number of 1/60 s ticks a present took (specific\output.cpp), which
 * ControlPhase turns into logic ticks and the inventory ring into steps,
 * and at the display-bound frame rate each drawn frame's place between two
 * ticks (TIME_SubTick): the frame is blended by it, and the next tick reads
 * Lara's joints as that frame left them (GetLaraJointPos: the camera's
 * target, TestForObjectOnLedge, TriggerLaraBlood).
 * -record writes both as they were read; -replay puts them in their place,
 * so a run plays the same ticks with the same input on any build and at
 * any speed of the machine. A level's start writes the two generators'
 * seeds and the frame counter, which the loading screen advances by its own
 * clock; -replay sets them from the file. A save and a load write their
 * slot, as a check that the replayed run reached the same menu choice.
 *
 * Under -replay every record is checked against the run: its type, and the
 * tick it was written at. The first that does not fit ends the replay with
 * a REPLAYLOG desync line, and the run goes on with the devices - from
 * there the two runs are different games, and the line says where they
 * parted. The end of the file ends it the same way, with REPLAYLOG end. */

static long g_mode = REPLAY_OFF;
static FILE* g_file = 0;		// -record
static uint8_t* g_data = 0;		// -replay: the whole file
static size_t g_size = 0;
static size_t g_pos = 0;
static long g_records = 0;
static long g_inputs = 0;
static bool g_drift_said = false;

/* The game's state as an input call finds it, mixed into one number: the
 * logic generator's seed and Lara's place, turn and life. -record writes it
 * beside the devices; -replay compares, and says the first tick where the
 * replayed game is no longer the recorded one (REPLAYLOG drift) - a menu
 * reached at another moment shows only much later, as a desync. */
static int32_t ReplayCheck()
{
	uint32_t h = (uint32_t)GetRandomControlSeed();

	if (lara_item)
	{
		h = h * 31 + (uint32_t)lara_item->pos.x_pos;
		h = h * 31 + (uint32_t)lara_item->pos.y_pos;
		h = h * 31 + (uint32_t)lara_item->pos.z_pos;
		h = h * 31 + (uint32_t)(unsigned short)lara_item->pos.y_rot;
		h = h * 31 + (uint32_t)(unsigned short)lara_item->hit_points;
	}

	return (int32_t)h;
}

static void ReplayStop(const char* why)
{
	if (g_mode == REPLAY_PLAY)
	{
		Log("REPLAYLOG end mode=replay why=%s records=%ld tick=%d left=%lu",
			why, g_records, (int)GlobalCounter,
			(unsigned long)(g_size - g_pos));
		free(g_data);
		g_data = 0;
	}

	g_mode = REPLAY_OFF;
}

static void ReplayDesync(char want, const ReplayRecord* r, const char* what)
{
	Log("REPLAYLOG desync want=%c got=%c what=%s rec_tick=%d tick=%d "
		"record=%ld at=%lu", want, r->type ? r->type : '-', what,
		(int)r->tick, (int)GlobalCounter, g_records, (unsigned long)g_pos);
	ReplayStop("desync");
}

/* The next record under -replay, if it is of `type`. */
static bool ReplayNext(uint8_t type, ReplayRecord* r)
{
	if (g_mode != REPLAY_PLAY)
		return false;

	if (g_pos >= g_size)
	{
		ReplayStop("file");
		return false;
	}

	const size_t n = ReplayDecode(g_data + g_pos, g_size - g_pos, r);

	if (!n || r->type != type)
	{
		ReplayDesync((char)type, r, n ? "type" : "cut");
		return false;
	}

	g_pos += n;
	g_records++;
	return true;
}

static void ReplayPut(const ReplayRecord* r)
{
	uint8_t buf[REPLAY_RECORD_MAX];
	const size_t n = ReplayEncode(r, buf);

	if (n && fwrite(buf, 1, n, g_file) == n)
		g_records++;
}

void ReplayOpen(long mode, const char* path, const char* stamp)
{
	uint8_t head[REPLAY_HEADER_SIZE];
	char rec_stamp[REPLAY_STAMP_SIZE];
	int version, bits;

	if (mode == REPLAY_RECORD)
	{
		if (fopen_s(&g_file, path, "wb") || !g_file)
		{
			g_file = 0;
			Log("REPLAYLOG refused mode=record file=%s why=open", path);
			return;
		}

		ReplayHeaderWrite(head, (int)(sizeof(void*) * 8), stamp);
		fwrite(head, 1, sizeof(head), g_file);
		g_mode = REPLAY_RECORD;
		Log("REPLAYLOG open mode=record file=%s version=%d bits=%d", path,
			REPLAY_VERSION, (int)(sizeof(void*) * 8));
		return;
	}

	if (mode != REPLAY_PLAY)
		return;

	FILE* f = 0;

	if (fopen_s(&f, path, "rb") || !f)
	{
		Log("REPLAYLOG refused mode=replay file=%s why=open", path);
		return;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	g_data = size > 0 ? (uint8_t*)malloc((size_t)size) : 0;
	g_size = g_data ? fread(g_data, 1, (size_t)size, f) : 0;
	fclose(f);

	const int head_ok = ReplayHeaderRead(g_data, g_size, &version, &bits,
		rec_stamp);

	if (head_ok != 1)
	{
		Log("REPLAYLOG refused mode=replay file=%s why=%s version=%d", path,
			head_ok ? "version" : "header", version);
		free(g_data);
		g_data = 0;
		return;
	}

	g_pos = REPLAY_HEADER_SIZE;
	g_mode = REPLAY_PLAY;
	Log("REPLAYLOG open mode=replay file=%s version=%d bits=%d recorded=%s",
		path, version, bits, rec_stamp);
}

void ReplayClose()
{
	if (g_mode == REPLAY_RECORD && g_file)
	{
		Log("REPLAYLOG end mode=record records=%ld tick=%d", g_records,
			(int)GlobalCounter);
		fclose(g_file);
		g_file = 0;
	}

	if (g_mode == REPLAY_PLAY)
		ReplayStop("exit");

	g_mode = REPLAY_OFF;
}

void ReplayInputSeam(char* keys, long* mouse_dx, long* mouse_dy,
	long* mouse_buttons, long* captured, long fmv)
{
	ReplayRecord r;

	if (g_mode == REPLAY_OFF || fmv)
		return;

	if (g_mode == REPLAY_RECORD)
	{
		memset(&r, 0, sizeof(r));
		r.type = REPLAY_INPUT;
		r.tick = GlobalCounter;
		memcpy(r.keys, keys, REPLAY_KEYS);
		r.mouse_dx = *mouse_dx;
		r.mouse_dy = *mouse_dy;
		r.mouse_buttons = *mouse_buttons;
		r.flags = (uint8_t)((*captured ? REPLAY_CAPTURED : 0) |
			(joystick_read ? REPLAY_PAD_READ : 0) |
			(joystick_is_xinput ? REPLAY_PAD_XINPUT : 0));
		r.pad_x = joystick_read_x;
		r.pad_y = joystick_read_y;
		r.pad_fire = joystick_read_fire;
		r.raw_lx = joystick_raw_lx;
		r.raw_ly = joystick_raw_ly;
		r.raw_rx = joystick_raw_rx;
		r.raw_ry = joystick_raw_ry;
		r.check = ReplayCheck();
		ReplayPut(&r);

		// About ten seconds of input at a time reach the disk.
		if (!(++g_inputs % 300))
			fflush(g_file);

		return;
	}

	if (!ReplayNext(REPLAY_INPUT, &r))
		return;

	if (r.tick != GlobalCounter)
	{
		ReplayDesync(REPLAY_INPUT, &r, "tick");
		return;
	}

	// Said once, and the replay goes on: what follows shows how far apart.
	if (!g_drift_said && r.check != ReplayCheck())
	{
		g_drift_said = true;
		Log("REPLAYLOG drift tick=%d record=%ld rec=0x%08lX now=0x%08lX",
			(int)GlobalCounter, g_records, (unsigned long)(uint32_t)r.check,
			(unsigned long)(uint32_t)ReplayCheck());
	}

	memcpy(keys, r.keys, REPLAY_KEYS);
	*mouse_dx = r.mouse_dx;
	*mouse_dy = r.mouse_dy;
	*mouse_buttons = r.mouse_buttons;
	*captured = (r.flags & REPLAY_CAPTURED) ? 1 : 0;
	joystick_read = (r.flags & REPLAY_PAD_READ) ? 1 : 0;
	joystick_is_xinput = (r.flags & REPLAY_PAD_XINPUT) ? 1 : 0;
	joystick_read_x = r.pad_x;
	joystick_read_y = r.pad_y;
	joystick_read_fire = r.pad_fire;
	joystick_raw_lx = r.raw_lx;
	joystick_raw_ly = r.raw_ly;
	joystick_raw_rx = r.raw_rx;
	joystick_raw_ry = r.raw_ry;
}

long ReplayFrames(long frames)
{
	ReplayRecord r;

	if (g_mode == REPLAY_RECORD)
	{
		memset(&r, 0, sizeof(r));
		r.type = REPLAY_FRAMES;
		r.tick = GlobalCounter;
		r.frames = frames;
		ReplayPut(&r);
		return frames;
	}

	if (!ReplayNext(REPLAY_FRAMES, &r))
		return frames;

	if (r.tick != GlobalCounter)
	{
		ReplayDesync(REPLAY_FRAMES, &r, "tick");
		return frames;
	}

	return r.frames;
}

double ReplaySubTick(double subtick)
{
	ReplayRecord r;

	if (g_mode == REPLAY_RECORD)
	{
		memset(&r, 0, sizeof(r));
		r.type = REPLAY_SUBTICK;
		r.tick = GlobalCounter;
		r.subtick = subtick;
		ReplayPut(&r);
		return subtick;
	}

	if (!ReplayNext(REPLAY_SUBTICK, &r))
		return subtick;

	if (r.tick != GlobalCounter)
	{
		ReplayDesync(REPLAY_SUBTICK, &r, "tick");
		return subtick;
	}

	return r.subtick;
}

void ReplayLevelStart()
{
	ReplayRecord r;

	// Asked only with a switch: it can write a line of its own.
	if (g_mode == REPLAY_OFF)
		return;

	const long fps = ActiveFpsMode();

	if (g_mode == REPLAY_RECORD)
	{
		memset(&r, 0, sizeof(r));
		r.type = REPLAY_LEVEL;
		r.level = gfLevelId;
		r.ctrl_seed = GetRandomControlSeed();
		r.draw_seed = GetRandomDrawSeed();
		r.counter = GlobalCounter;
		r.frame_counter = GnFrameCounter;
		r.fps_mode = fps;
		ReplayPut(&r);
		fflush(g_file);
	}
	else if (ReplayNext(REPLAY_LEVEL, &r))
	{
		if (r.level != gfLevelId || r.counter != GlobalCounter)
		{
			r.tick = (uint16_t)r.counter;
			ReplayDesync(REPLAY_LEVEL, &r, r.level != gfLevelId ? "level" :
				"tick");
			return;
		}

		SeedRandomControl(r.ctrl_seed);
		SeedRandomDraw(r.draw_seed);
		GnFrameCounter = r.frame_counter;

		if (r.fps_mode != fps)
			Log("REPLAYLOG warn fps=%ld recorded=%ld", fps, (long)r.fps_mode);
	}
	else
		return;

	Log("REPLAYLOG level=%d tick=%d seed=0x%08lX draw=0x%08lX frames=%ld "
		"fps=%ld", (int)gfLevelId, (int)GlobalCounter,
		(unsigned long)GetRandomControlSeed(),
		(unsigned long)GetRandomDrawSeed(), GnFrameCounter, fps);
}

void ReplaySlot(char kind, long slot)
{
	ReplayRecord r;

	if (g_mode == REPLAY_RECORD)
	{
		memset(&r, 0, sizeof(r));
		r.type = REPLAY_SLOT;
		r.tick = GlobalCounter;
		r.slot_kind = (uint8_t)kind;
		r.slot = slot;
		ReplayPut(&r);
		fflush(g_file);
	}
	else if (ReplayNext(REPLAY_SLOT, &r))
	{
		if (r.slot_kind != (uint8_t)kind || r.slot != slot ||
			r.tick != GlobalCounter)
		{
			ReplayDesync(REPLAY_SLOT, &r, r.tick != GlobalCounter ? "tick" :
				"slot");
			return;
		}
	}
	else
		return;

	Log("REPLAYLOG %s slot=%ld tick=%d", kind == 's' ? "save" : "load", slot,
		(int)GlobalCounter);
}

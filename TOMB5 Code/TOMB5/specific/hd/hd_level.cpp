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
#include "../../tomb5/pch.h"
#include "hd_level.h"
#include "hd_install.h"
#include "hd_bind.h"
#include "hd_lara.h"			// HD_LARA_OUTFIT_* - the data[3] agreement
#include "hd_markers.h"
#include "hd_scan.h"
#include "hd_render.h"
#include "hd_object.h"
#include "../function_stubs.h"
#include "../d3dmatrix.h"
#include "../3dmath.h"
#include "../winmain.h"
#include "../dxshell.h"				// DXTextureMemoryAtStart (#123, S6)
#include "../texture.h"				// nTextures (#123, S6)
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// number_rooms, room
#include "../../game/camera.h"		// camera.underwater
#include "../../game/effect2.h"		// dynamics, wibble
#include "../../game/gameflow.h"		// gfLevelId
#include "../../game/lara.h"		// GfHdOutfit - data[3] (#123, S7)
#include "../drawroom.h"			// ActiveFogBulbs, aRoomVertexWaterMarks
#include "../gamemain.h"			// WaterTable, vert_wibble_table
#include "hd_light.h"

#include <vector>

// The generated slot name table in hd_bind.cpp has to be the enum, entry for
// entry. This is the line that notices when a slot was added to objects.h and
// not to that table - at build time, not in a picture.
static_assert(HD_SLOT_COUNT == NUMBER_OBJECTS,
	"hd_bind.cpp's slot name table and enum object_types have drifted apart - "
	"regenerate the table (see the comment above it) and update HD_SLOT_COUNT");
static_assert(HAIR == 30,
	"HDSlotIsLara() covers slots 0..30 because HAIR is 30 - it no longer is");

/* THE FOUR TOMB RAIDER III GUESTS (#92, #123 stage S7-2). hd_bind.cpp's
 * tr3_guests table maps these four slot NUMBERS to the .TRM names of the I-III
 * remaster, and it is built without the engine, so the numbers stand there as
 * numbers. These are the lines that notice when one of the four slots moves -
 * at build time, and not as a T-Rex wearing a soldier's model. The randomizer
 * picks the same four in TR5Randomizer\src\core\Tr3Source.cpp, dinos(). */
static_assert(SAS == 31 && CROW == 43 && SAILOR == 53 && REAPER == 75,
	"the slots the randomizer imports the Tomb Raider III guests into have "
	"moved - update tr3_guests in hd_bind.cpp with them");

/* THE OUTFIT MARKER, data[3] of the level header (#123, stage S7). The
 * randomizer writes these very numbers - Patch::hdOutfitId in
 * TR5Randomizer\src\core\ApplyPatch.cpp - so that neither side translates.
 * Renumber the enum and this line stops the build; renumber it without this
 * line and a Nevada Lara quietly becomes an Antarctica one. */
static_assert(HD_LARA_OUTFIT_TR3_ANTARC == 5 && HD_LARA_OUTFIT_TR3_NEVADA == 6 &&
	HD_LARA_OUTFIT_TR3_COAST == 7 && HD_LARA_OUTFIT_TR3_CATSUIT == 8,
	"data[3] of the level header carries HD_LARA_OUTFIT_* 5..8 by agreement "
	"with the randomizer (Preparation\\Contract.md) - the enum has moved");

// The engine never has more rooms than this, and draw_rooms (game\draw.cpp)
// never holds more than 100 of them in one frame. Both are fixed arrays here
// rather than allocations, so that nothing in the frame path can fail.
#define HD_MAX_ROOMS	1024
#define HD_MAX_FRAME	128

namespace
{
	struct FrameRoom
	{
		int room;
		int group;		// A1: the .TRG group that draws it right now
		D3DMATRIX world;	// view * world, as the classic path set it
	};

	char g_stem[64] = { 0 };

	// Set by HDLevelBuild and by nothing else. Every frame-path function
	// tests it first, so switching hd_enable while a level runs changes
	// nothing until the next level starts - which is what the option row
	// says it does.
	bool g_level_active = false;

	/* WHICH .TRG GROUP DRAWS WHICH ROOM (#123, fix round A, A1).
	 *
	 * Group i is room i - until FlipMap runs. FlipMap (game\control.cpp,
	 * 2634) does not move a room, it SWAPS THE CONTENTS of two ROOM_INFO
	 * records: after FlipMap(n) the record at index r holds the geometry, the
	 * floor data, the statics and the lights of what used to be room
	 * r->flipped_room. The engine goes on calling that room r, so InsertRoom
	 * hands us r, and stage S1-2 drew group r - the art of the room that is
	 * no longer there. That is the reported bug in its exact words: "the wall
	 * is gone, but the HD texture is still standing".
	 *
	 * So the identity is replaced by a table, and the table is swapped in the
	 * same breath as the ROOM_INFO records (HDFlipRoom, called from FlipMap
	 * itself). Everything that is decided per room then has to be read
	 * through it:
	 *
	 *   g_group_hd    is per GROUP, not per room - "this art exists and may
	 *                 be drawn" is a property of the .TRG, and it must travel
	 *                 with the art. HDRoomIsHD, which also decides whether a
	 *                 room's static meshes are suppressed, reads
	 *                 g_group_hd[g_room_group[r]].
	 *   water marks   are built once, before any flip, on the classic
	 *                 vertices of every room INCLUDING the alternates, and
	 *                 they are carried into the group of the room they were
	 *                 found in. So they already sit on the right group and
	 *                 need no second thought - they follow the art.
	 *   lighting      reads the room's own flags (they were swapped with the
	 *                 record, so they are the alternate's) and the GROUP's
	 *                 bounding box.
	 *
	 * A flip pair stands at the same place in the world - that is what makes
	 * it a flip pair - so the world matrix the engine hands in is right for
	 * either group of the pair, and nothing about the matrix changes here.
	 *
	 * The table survives a save: the engine restores a flip by calling
	 * FlipMap once per set flip (game\savegame.cpp:684), so a loaded game
	 * walks through the very same hook. A level starts with it as the
	 * identity, which is what an unflipped level is. */
	unsigned char g_group_hd[HD_MAX_ROOMS];
	short g_room_group[HD_MAX_ROOMS];
	int g_room_count = 0;
	int g_flips_done = 0;		// for the log, once per level

	// Stage S2b. The marks are built once per level out of the classic
	// roomlets and handed to the renderer; the counters below say once per
	// level whether the shader's fixed maxima ever bit.
	std::vector<HDWaterMark> g_water_marks;

	/* STAGE S5: THE GAME CLOCK, IN SECONDS.
	 *
	 * The UV scroll a .TRG part carries is a speed, and a speed needs a clock
	 * that runs forward. The engine has exactly one counter that does:
	 * GlobalCounter (control.cpp:262), incremented once per game update in
	 * the same loop that steps `wibble`, so 30 a second, and standing still
	 * whenever the game stands still - the inventory, the pause, a load.
	 *
	 * It is a ushort and turns over every 36 minutes, which would put a jump
	 * in the middle of a waterfall, so what is kept here is the DIFFERENCE
	 * since the last frame, accumulated. The subtraction is done in ushort,
	 * where the turn-over is exactly what unsigned arithmetic gives back.
	 *
	 * Not a clock of our own: no timeGetTime, no steady_clock. A frame drawn
	 * twice while the game is paused draws the same picture, which is what
	 * the classic path does too. */
	double g_clock = 0.0;
	unsigned short g_clock_last = 0;
	bool g_clock_started = false;

	void AdvanceClock()
	{
		const unsigned short now = GlobalCounter;

		if (!g_clock_started)
		{
			g_clock_started = true;
			g_clock_last = now;
			return;
		}

		g_clock += (double)(unsigned short)(now - g_clock_last) / 30.0;
		g_clock_last = now;
	}

	/* WHY THESE ARE TOTALS AND NOT A PER FRAME FLAG. The first version reset
	 * them after every frame that clipped, so the "once per level" line came
	 * out once per FRAME - 173 identical lines out of one title screen, which
	 * is how the 6 bulb cap was found in the first place. They now count for
	 * the whole level: the first frame that clips writes one line naming the
	 * room, and the level's end writes one total. Nothing else logs them. */
	int g_clipped_lights = 0;	// dynamic lights turned away, whole level
	int g_clipped_bulbs = 0;	// fog bulbs turned away, whole level
	int g_clip_frames = 0;		// frames in which either happened
	int g_clip_lights_worst = 0;	// most lights one room ever offered
	int g_clip_lights_room = -1;	// and which room that was
	int g_clip_bulbs_worst = 0;	// most bulbs one frame ever offered
	bool g_clip_reported = false;	// the one line has been written
	bool g_clip_this_frame = false;

	/* ------------------------------------------------------ the frame clock --
	 *
	 * "It felt like the frame rate dropped below 30 sometimes" is not
	 * something anybody can answer by looking at the picture, so the engine
	 * measures it: one FPSLOG line per second of wall clock, written from the
	 * one place every presented frame goes through.
	 *
	 * WHERE IT IS TAKEN. HDFramePresented() is called from DXShowFrame
	 * (specific\dxshell.cpp) directly after IDirect3DDevice9::Present returns.
	 * That is the only function in this engine that presents anything, and
	 * every path reaches it: the game frame (S_DumpScreen), the loading bar
	 * and the credits (S_DumpScreenFrame), the inventory, the title menu and
	 * an FMV (fmv.cpp calls it itself). Measuring inside S_OutputPolyList
	 * would miss the loading bar and the movies; measuring in the main loop
	 * would count message pumps that drew nothing.
	 *
	 * WHAT THE NUMBERS MEAN. The logic is a fixed 30 Hz: S_DumpScreen waits
	 * for two ticks of the engine's own 60 Hz counter (specific\time.cpp)
	 * before it presents, so a healthy second is `frames=30` and every
	 * interval sits on 33.3 ms. That quantisation is why there are TWO
	 * counters and not one:
	 *
	 *   slow   intervals over the 33.3 ms budget. Half a healthy second lands
	 *          here on jitter alone, so a number here on its own means
	 *          nothing - it is the budget line the brief asked for.
	 *   late   intervals over 41.7 ms, which is the budget plus half a 60 Hz
	 *          tick. Jitter cannot reach it; a frame that MISSED its slot
	 *          costs a whole tick and lands at 50 ms or beyond. This is the
	 *          number that says the frame rate really fell under 30.
	 *
	 * NOTHING HAPPENS WITHOUT -log. The first line of HDFramePresented is
	 * `if (!gLogEnabled) return;` - one bool test per present, no counter
	 * kept, no clock read. Nothing here allocates, ever: the accumulators are
	 * plain integers and doubles in this file.
	 */
	const double kFrameBudgetMs = 1000.0 / 30.0;		// 33.33
	const double kMissedSlotMs = kFrameBudgetMs + 1000.0 / 120.0;	// 41.67

	char g_fps_level[64] = { 0 };	// the level the frame clock is counting for
	char g_log_stem[64] = { 0 };	// what HDLevelLoad saw, whatever hd_enable says

	__int64 g_qpc_freq = 0;		// 0 = the clock has not been read yet
	__int64 g_qpc_last = 0;		// the previous present, 0 = none yet
	__int64 g_qpc_second = 0;	// when the second being filled started

	int g_sec_frames = 0;
	double g_sec_min = 0;
	double g_sec_max = 0;
	double g_sec_sum = 0;
	int g_sec_slow = 0;
	int g_sec_late = 0;
	int g_sec_rooms = 0;		// the HD counters, MAXIMUM over the second -
	int g_sec_items = 0;		// the heaviest frame is the one that explains
	int g_sec_draws = 0;		// a drop, not the last one before the flush

	int g_lvl_seconds = 0;
	int g_lvl_frames = 0;
	double g_lvl_worst = 0;
	int g_lvl_slow = 0;
	int g_lvl_late = 0;

	void FrameClockNewSecond()
	{
		g_sec_frames = 0;
		g_sec_min = 0;
		g_sec_max = 0;
		g_sec_sum = 0;
		g_sec_slow = 0;
		g_sec_late = 0;
		g_sec_rooms = 0;
		g_sec_items = 0;
		g_sec_draws = 0;
	}

	void FrameClockFlushSecond()
	{
		if (g_sec_frames <= 0)
			return;

		Log("FPSLOG level=%s frames=%d min_ms=%.1f max_ms=%.1f avg_ms=%.1f "
			"slow=%d late=%d hd_rooms=%d hd_items=%d hd_draws=%d",
			g_fps_level[0] ? g_fps_level : "?", g_sec_frames, g_sec_min,
			g_sec_max, g_sec_sum / g_sec_frames, g_sec_slow, g_sec_late,
			g_sec_rooms, g_sec_items, g_sec_draws);

		g_lvl_seconds++;
		g_lvl_frames += g_sec_frames;
		g_lvl_slow += g_sec_slow;
		g_lvl_late += g_sec_late;

		if (g_sec_max > g_lvl_worst)
			g_lvl_worst = g_sec_max;
	}

	/* The end of a level, main thread: the frame clock's summary and the one
	 * total of what the shader maxima turned away. Called from HDLevelBuild
	 * (for the level BEFORE the one being built, after the load thread has
	 * joined) and from HDLevelDeviceLost, which is where DXClose comes
	 * through at the end of the session - so the last level gets its line
	 * too. Both counters start again afterwards. */
	void LevelDiagnosticsEnd()
	{
		if (gLogEnabled && g_lvl_seconds)
			Log("FPSLOG level=%s summary seconds=%d frames=%d worst_ms=%.1f "
				"slow_frames=%d late_frames=%d",
				g_fps_level[0] ? g_fps_level : "?", g_lvl_seconds,
				g_lvl_frames, g_lvl_worst, g_lvl_slow, g_lvl_late);

		if (gLogEnabled && (g_clipped_lights || g_clipped_bulbs))
			Log("HDLOG level=%s clipped summary frames=%d lights=%d bulbs=%d "
				"worst_lights=%d(room %d) worst_bulbs=%d (maxima %d / %d)",
				g_fps_level[0] ? g_fps_level : "?", g_clip_frames,
				g_clipped_lights, g_clipped_bulbs, g_clip_lights_worst,
				g_clip_lights_room, g_clip_bulbs_worst,
				HD_MAX_LIGHTS, HD_MAX_BULBS);

		g_lvl_seconds = 0;
		g_lvl_frames = 0;
		g_lvl_worst = 0;
		g_lvl_slow = 0;
		g_lvl_late = 0;
		g_qpc_last = 0;
		FrameClockNewSecond();

		g_clipped_lights = 0;
		g_clipped_bulbs = 0;
		g_clip_frames = 0;
		g_clip_lights_worst = 0;
		g_clip_lights_room = -1;
		g_clip_bulbs_worst = 0;
		g_clip_reported = false;
		g_clip_this_frame = false;
	}

	FrameRoom g_frame[HD_MAX_FRAME];
	int g_frame_count = 0;
	int g_frame_dropped = 0;

	// The projection is taken WHEN THE ROOMS ARE COLLECTED, not when they are
	// drawn: AlterFOV moves f_persp inside a frame (the sky phase does it
	// twice while the binoculars are up), and the rooms have to be projected
	// with the same field of view the classic path transformed its vertices
	// with.
	float g_persp = 0;
	float g_centerx = 0;
	float g_centery = 0;
	float g_za = 0;
	float g_zb = 0;
	float g_zboo = 0;
	float g_znear = 0;
	float g_oneopersp = 0;

	bool g_matrix_checked = false;
	// R3a: one line per level about the sky dome, and only one.
	bool g_sky_logged = false;

	// Whether THIS frame's projection has been taken yet - by the first HD
	// room or by the first HD item, whichever the engine reaches first.
	bool g_projection_taken = false;

	const D3DMATRIX kIdentity =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	/* What HDLevelLoad measured of the .TRG, kept for the budget below: the
	 * vertex and index buffers of this level have to come out of the card's
	 * memory before a single page does, and this is the only place their size
	 * is known BEFORE they are made. */
	int g_scan_vertices = 0;
	int g_scan_indices = 0;

	/* THE AUTOMATIC TEXTURE BUDGET (#123, stage S6).
	 *
	 * hd_texture_budget_mb = 0 means "work it out", and this is the working
	 * out. It starts from what the device said it had the moment it was made
	 * (dxshell.cpp, DXTextureMemoryAtStart) and takes off everything else
	 * that has to live on the card while a level runs:
	 *
	 *   the back buffer and the depth buffer, from the present parameters -
	 *   16.6 MiB at 1920x1080, more on a bigger screen and less on a smaller
	 *   one, so it is read rather than assumed;
	 *
	 *   the classic texture pages, which are still loaded and still drawn:
	 *   nTextures of App.TextureSize squared in A8R8G8B8. They are
	 *   D3DPOOL_MANAGED and Direct3D pages them in and out as it likes, so
	 *   this is the worst case, which is the right case for a budget. A TR5
	 *   level has between 60 and 160 of them and the array holds 256 - 16 to
	 *   40 MiB, 64 at the ceiling;
	 *
	 *   this level's HD vertex and index buffers, which are D3DPOOL_DEFAULT
	 *   and are made BEFORE the pages, so they are already spent by the time
	 *   the budget bites. 61 MiB on TITLE, the largest there is;
	 *
	 *   and 64 MiB of slack, because a driver keeps command buffers, shader
	 *   constants and its own copies of things, and a budget that spends the
	 *   last byte of what a card admits to is not a budget.
	 *
	 * The floor is 16 MiB: below that the pages give up level after level
	 * until they fit, which is degradation rather than refusal - a page is
	 * never left out, whatever the number says.
	 */
	double AutoBudgetBytes()
	{
		const double have = (double)DXTextureMemoryAtStart;

		if (have <= 0)
			return 0;		// the device did not answer - no cap at all

		const double w = (double)App.dx.PresentParams.BackBufferWidth;
		const double h = (double)App.dx.PresentParams.BackBufferHeight;
		const double frame = w * h * 4.0 *
			(double)(App.dx.PresentParams.BackBufferCount + 1) +
			(App.dx.PresentParams.EnableAutoDepthStencil ? w * h * 4.0 : 0.0);
		const double classic = (double)nTextures *
			(double)App.TextureSize * (double)App.TextureSize * 4.0;
		const double buffers = (double)g_scan_vertices *
			(double)HDRenderVertexBytes() + (double)g_scan_indices * 4.0;
		const double slack = 64.0 * 1048576.0;

		double budget = have - (frame + classic + buffers + slack);

		if (budget < 16.0 * 1048576.0)
			budget = 16.0 * 1048576.0;

		return budget;
	}

	int DecodeThreads()
	{
		SYSTEM_INFO info;

		GetSystemInfo(&info);

		int threads = (int)info.dwNumberOfProcessors;

		if (threads < 1)
			threads = 1;

		if (threads > 16)
			threads = 16;

		return threads;
	}

	// "DATA\ANDREA1.TRC" -> "ANDREA1". Not HDScreenLevelName(), which maps
	// RICHCUT2 to RICH3 because the Remaster ships no loading picture for the
	// cutscene - the .TRG of RICHCUT2 exists and is its own.
	void LevelStem(const char* wadName, char* out, int size)
	{
		const char* start;
		const char* dot;
		int length;

		out[0] = 0;

		if (!wadName || !*wadName)
			return;

		start = strrchr(wadName, '\\');
		start = start ? start + 1 : wadName;

		if (const char* slash = strrchr(start, '/'))
			start = slash + 1;

		dot = strrchr(start, '.');
		length = dot ? (int)(dot - start) : (int)strlen(start);

		if (length > size - 1)
			length = size - 1;

		for (int i = 0; i < length; i++)
			out[i] = (char)toupper((uchar)start[i]);

		out[length] = 0;
	}

	/* THE PROOF THAT THE TWO PATHS PUT THE SAME POINT IN THE SAME PIXEL.
	 *
	 * The classic path transforms a room-local vertex with the matrix on the
	 * stack and divides by z itself (specific\drawroom.cpp,
	 * aRoomletTransformLight):
	 *
	 *     v  = pos * (view * world)          - a ROW vector, translation in row 3
	 *     sx = v.x * f_persp / v.z + f_centerx
	 *     sy = v.y * f_persp / v.z + f_centery
	 *     sz = f_a - f_boo * (2048 / v.z)
	 *
	 * The HD path hands the device the same matrix times the projection and
	 * lets the hardware divide. If the two disagree by so much as a pixel,
	 * every HD wall stands beside its own floor. So they are compared here,
	 * on the level's own vertices, with the level's own camera - once per
	 * level, into the log, and the numbers are the acceptance of this stage.
	 *
	 * Not a self-test of a formula: the classic side below is the three lines
	 * of drawroom.cpp, and the HD side is the matrix that really goes to the
	 * device, through the viewport rule the device really applies. */
	void MatrixCheck(int roomIndex, int group, const D3DMATRIX& world,
		const D3DMATRIX& projection)
	{
		short xyz[HD_RENDER_SAMPLES * 3];
		const int count = HDRenderGroupSamples(group, xyz, HD_RENDER_SAMPLES);

		if (count <= 0)
			return;

		const double width = 2.0 * (double)g_centerx;
		const double height = 2.0 * (double)g_centery;

		/* world * projection, the general 4x4 the way hd_render.cpp builds it
		 * and the way the vertex shader uses it - mul(float4(pos,1), wvp) with
		 * the row convention. Written out in full rather than shortened with
		 * "the projection is mostly zeroes", so that the proof would notice a
		 * projection that stops being mostly zeroes. */
		const float* w = (const float*)&world;
		const float* p = (const float*)&projection;
		double wvp[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
			{
				double sum = 0;

				for (int k = 0; k < 4; k++)
					sum += (double)w[row * 4 + k] * (double)p[k * 4 + col];

				wvp[row * 4 + col] = sum;
			}

		double worst_x = 0, worst_y = 0, worst_z = 0;
		int used = 0;
		int behind = 0;

		for (int i = 0; i < count; i++)
		{
			const float px = (float)xyz[i * 3 + 0];
			const float py = (float)xyz[i * 3 + 1];
			const float pz = (float)xyz[i * 3 + 2];

			// --- the classic path, line for line ------------------------
			const float vx = px * world._11 + py * world._21 + pz * world._31 + world._41;
			const float vy = px * world._12 + py * world._22 + pz * world._32 + world._42;
			const float vz = px * world._13 + py * world._23 + pz * world._33 + world._43;

			if (vz < g_znear)
			{
				behind++;
				continue;
			}

			const float zv = g_persp / vz;
			const double classic_x = (double)(vx * zv + g_centerx);
			const double classic_y = (double)(vy * zv + g_centery);
			const double classic_z = (double)(g_za - g_zboo * (zv * g_oneopersp));

			// --- the device ---------------------------------------------
			// The row vector through world * projection, then the w division,
			// then the viewport - which this engine never sets, so it is the
			// whole back buffer with MinZ 0 and MaxZ 1.
			const double cx = (double)px * wvp[0] + (double)py * wvp[4] +
				(double)pz * wvp[8] + wvp[12];
			const double cy = (double)px * wvp[1] + (double)py * wvp[5] +
				(double)pz * wvp[9] + wvp[13];
			const double cz = (double)px * wvp[2] + (double)py * wvp[6] +
				(double)pz * wvp[10] + wvp[14];
			const double cw = (double)px * wvp[3] + (double)py * wvp[7] +
				(double)pz * wvp[11] + wvp[15];

			if (cw <= 0.0)
			{
				behind++;
				continue;
			}

			const double device_x = (cx / cw + 1.0) * 0.5 * width;
			const double device_y = (1.0 - cy / cw) * 0.5 * height;
			const double device_z = cz / cw;

			const double dx = fabs(device_x - classic_x);
			const double dy = fabs(device_y - classic_y);
			const double dz = fabs(device_z - classic_z);

			if (dx > worst_x) worst_x = dx;
			if (dy > worst_y) worst_y = dy;
			if (dz > worst_z) worst_z = dz;

			used++;
		}

		Log("HDLOG matrixcheck level=%s room=%d group=%d samples=%d used=%d "
			"behind=%d maxdx=%.5f maxdy=%.5f maxdz=%.9f persp=%.1f "
			"center=%.0f,%.0f",
			g_stem, roomIndex, group, count, used, behind,
			worst_x, worst_y, worst_z, (double)g_persp,
			(double)g_centerx, (double)g_centery);
	}
}

/* --------------------------------------------------------- stage S2b -- */

/* THE WATER MARKS, once per level.
 *
 * Every classic room vertex is asked the three questions drawroom.cpp answers
 * per frame (aRoomVertexWaterMarks), and every vertex that answers yes to any
 * of them is written down with its room local position. hd_render.cpp then
 * finds the HD vertex at the same position while it fills the vertex buffer.
 *
 * Cheap by construction: a room with none of the water flags and no roomlet
 * water vertices produces nothing, and that is nearly every room. The
 * expensive predicates behind the two shimmer bits are guarded inside
 * aRoomVertexWaterMarks by the room's own flags.
 */
static void BuildWaterMarks()
{
	int water_rooms = 0;

	g_water_marks.clear();

	for (int i = 0; i < number_rooms && i < HD_MAX_ROOMS; i++)
	{
		ROOM_INFO* r = &room[i];

		if (!r->pRoomlets || r->nRoomlets <= 0)
			continue;

		const bool interesting = (r->flags & (ROOM_MIST | ROOM_REFLECT)) != 0;
		int here = 0;

		for (int n = 0; n < r->nRoomlets; n++)
		{
			ROOMLET* roomlet = &r->pRoomlets[n];

			if (!roomlet->pSVtx)
				continue;

			// Without a water flag only the nWVtx bit can ever be set, so
			// only those vertices need looking at.
			const long last = interesting ? roomlet->nVtx : roomlet->nWVtx;

			for (long v = 0; v < last; v++)
			{
				const uchar bits = aRoomVertexWaterMarks(r, roomlet, v);

				if (!bits)
					continue;

				const float* xyz = roomlet->pSVtx + v * 7;
				HDWaterMark mark;

				mark.room = (short)i;
				mark.x = (short)xyz[0];
				mark.y = (short)xyz[1];
				mark.z = (short)xyz[2];
				mark.bits = bits;
				g_water_marks.push_back(mark);
				here++;
			}
		}

		if (here)
			water_rooms++;
	}

	if (!g_water_marks.empty())
		HDRenderSetWaterMarks(&g_water_marks[0], (int)g_water_marks.size());
	else
		HDRenderSetWaterMarks(0, 0);

	Log("HDLOG level=%s water marks=%d rooms=%d", g_stem,
		(int)g_water_marks.size(), water_rooms);
}

/* WHAT THE WHOLE FRAME SHARES. Every one of these is the engine's own global,
 * read where the classic path reads it: the fog bulb list that draw.cpp built
 * behind `if (tomb5.fog)` - so with fog off the count is 0 and the HD rooms
 * lose their bulbs at exactly the same moment the classic ones do - the
 * distance in blocks out of the options, the wibble counter, and
 * camera.underwater for the screen wobble. */
static void FillFrameLighting(HDFrameLighting& out, HDFogBulb* bulbs)
{
	memset(&out, 0, sizeof(out));

	out.centerX = g_centerx;
	out.centerY = g_centery;
	out.wibble = (float)wibble;
	out.cameraUnderwater = camera.underwater ? 1 : 0;
	out.distanceFogStart = (float)tomb5.distance_fog * 1024.0f;
	out.wibbleTable = vert_wibble_table;
	out.bulbs = bulbs;
	out.bulbCount = 0;

	// Stage S5. The clock is stepped once per frame, here, because this is
	// the one function the frame path cannot skip.
	AdvanceClock();
	out.clock = (float)g_clock;
	out.motion = tomb5.hd_motion ? 1 : 0;

	int offered = 0;

	for (int i = 0; i < NumActiveFogBulbs && i < 64; i++)
	{
		const FOGBULB_STRUCT& in = ActiveFogBulbs[i];

		if (!in.visible)
			continue;

		// Counted to the end rather than left at the cap: "one bulb was
		// dropped" and "nine were dropped" are two different reports, and
		// the loop is at most 64 long.
		offered++;

		if (out.bulbCount >= HD_MAX_BULBS)
			continue;

		HDFogBulb& b = bulbs[out.bulbCount++];

		b.x = in.pos.x;
		b.y = in.pos.y;
		b.z = in.pos.z;
		b.rad = in.rad;
		b.r = in.r;
		b.g = in.g;
		b.b = in.b;
		b.density = in.d;
	}

	if (offered > HD_MAX_BULBS)
	{
		g_clipped_bulbs += offered - HD_MAX_BULBS;
		g_clip_this_frame = true;

		if (offered > g_clip_bulbs_worst)
			g_clip_bulbs_worst = offered;
	}
}

/* WHAT ONE ROOM ADDS. The dynamic lights are culled against the room's own
 * bounding box with the very test aBuildRoomletLights uses on a roomlet's
 * (drawroom.cpp:967-969) - x and z only, as there. A room is bigger than a
 * roomlet, so this lets a light through that the classic path would have
 * dropped for one roomlet and kept for the next; the falloff test inside the
 * shader is a real distance test and throws it out again, so the pixel is the
 * same and only the constant upload is one longer.
 *
 * The shimmer branch of drawroom.cpp:451-475 is a decision about the ROOM,
 * and it is taken here, per frame, because r->flags can change while a level
 * runs (a room that floods). What the shader gets is which of the three baked
 * marks to look at. All three selectors are zero while tomb5.shimmer is off -
 * that is the option switch, in one place, for the HD path.
 */
static void FillRoomLighting(int roomIndex, int group, HDRoomLighting& out,
	HDLightSource* lights)
{
	ROOM_INFO* r = &room[roomIndex];
	float mins[3], maxs[3];

	memset(&out, 0, sizeof(out));

	out.originX = (float)r->x;
	out.originY = (float)r->y;
	out.originZ = (float)r->z;
	out.surfaceY = (float)(r->minfloor - r->y);
	out.underwater = (r->flags & ROOM_UNDERWATER) ? 1 : 0;
	out.choppy = 1;
	out.lights = lights;
	out.lightCount = 0;

	const int effect = (int)r->MeshEffect;
	out.waterTable = (effect >= 0 && effect < 22) ? (const void*)WaterTable[effect] : 0;

	if (tomb5.shimmer)
	{
		if (r->flags & ROOM_REFLECT && r->flags & ROOM_REFLECT_CEILING)
		{
			if (gfLevelId == LVL5_RED_ALERT)
				out.redAlert = 1;
			else
				out.sel1[2] = 1.0f;	// the whole-room mark, no height test
		}
		else
		{
			out.sel1[1] = 1.0f;	// the surface mark ...
			out.surfaceGate = 1;	// ... and the height test that goes with it
		}
	}

	/* The GROUP's box, not the room's number - after a flip the two are
	 * different rooms of the same pair (A1). */
	if (!HDRenderGroupBounds(group, mins, maxs))
		return;

	int offered = 0;

	for (int i = 0; i < 32; i++)
	{
		DYNAMIC* dynamic = &dynamics[i];

		if (!dynamic->on)
			continue;

		const float dx = (float)dynamic->x - r->posx;
		const float dy = (float)dynamic->y - r->posy;
		const float dz = (float)dynamic->z - r->posz;
		const float falloff = float((dynamic->falloff >> 1) + (dynamic->falloff >> 3));

		if (!(dx - falloff <= maxs[0] && falloff + dx >= mins[0] &&
			dz - falloff <= maxs[2] && falloff + dz >= mins[2]))
			continue;

		// As with the bulbs above: counted to the end, so that the level's
		// one line can say how many were turned away and not just that some
		// were. The loop is 32 long whatever happens.
		offered++;

		if (out.lightCount >= HD_MAX_LIGHTS)
			continue;

		HDLightSource& l = lights[out.lightCount++];

		l.x = dx;
		l.y = dy;
		l.z = dz;
		l.falloff = falloff;
		l.r = (float)dynamic->r * (1.0f / 255.0f);
		l.g = (float)dynamic->g * (1.0f / 255.0f);
		l.b = (float)dynamic->b * (1.0f / 255.0f);
	}

	if (offered > HD_MAX_LIGHTS)
	{
		g_clipped_lights += offered - HD_MAX_LIGHTS;
		g_clip_this_frame = true;

		if (offered > g_clip_lights_worst)
		{
			g_clip_lights_worst = offered;
			g_clip_lights_room = roomIndex;
		}
	}
}

bool HDEnabled()
{
	// The guard. With hd_enable off nothing below this line ever runs, no
	// Remaster is looked for, no file is opened, and the frame is what it was
	// before #123 existed - byte for byte.
	if (!tomb5.hd_enable)
		return false;

	return HDInstallRoot()[0] != 0;
}

void HDLevelLoad(const char* wadName)
{
	static unsigned char loaded[NUMBER_OBJECTS];
	HDScanStats stats;
	const char* why;

	g_stem[0] = 0;
	g_scan_vertices = 0;
	g_scan_indices = 0;

	/* The name for the frame clock, taken BEFORE the HD guard and kept in its
	 * own buffer: FPSLOG has nothing to do with hd_enable and has to name the
	 * level either way. This is the load thread, so nobody reads it here -
	 * HDLevelBuild copies it across on the main thread once that thread has
	 * joined (specific\file.cpp, S_LoadLevelFile). */
	LevelStem(wadName, g_log_stem, sizeof(g_log_stem));

	if (!HDEnabled())
		return;

	LevelStem(wadName, g_stem, sizeof(g_stem));

	if (!g_stem[0])
		return;

	/* WHAT THE RANDOMIZER CHANGED (#123, stage S7-2), before anything else is
	 * measured: the sidecar names the rooms whose shape no longer matches the
	 * Remaster's art and the slots whose model came out of another file, and
	 * both decide what the rest of this load is allowed to do.
	 *
	 * The path is the .TRC's own with .hdm in its place, so the file is found
	 * wherever the level is - a folder the user moved, a data folder beside a
	 * second installation. A missing file is the normal case and not a fault;
	 * an unreadable one falls back to the same answer and says so. */
	const bool markers = HDMarkersLoad(wadName);

	Log("HDLOG level=%s markers file=%s rooms=%d donors=%d outfit=%d "
		"tr3_install=%s%s%s",
		g_stem, markers ? HDMarkersPath() : "none",
		HDMarkersRoomCount(), HDMarkersDonorCount(), HDMarkersOutfit(),
		HDInstallRootTr3()[0] ? "found" : "absent",
		markers ? "" : " reason=", markers ? "" : HDMarkersError());

	/* data[3] and the sidecar's `outfit` line come out of ONE value in the
	 * randomizer and can only disagree when one of the two halves was written
	 * by an older run. The header byte wins - it is the one the draw path
	 * reads, and it is there before a room is loaded - so this is a line in
	 * the log and nothing else. */
	if (markers && HDMarkersOutfit() != (int)GfHdOutfit)
		Log("HDLOG level=%s markers MISMATCH data[3]=%d, the sidecar says %d "
			"- data[3] wins", g_stem, (int)GfHdOutfit, HDMarkersOutfit());

	for (int i = 0; i < HDMarkersUnknownCount(); i++)
		Log("HDLOG level=%s markers line this engine has no rule for: %s",
			g_stem, HDMarkersUnknown(i));

	for (int i = 0; i < NUMBER_OBJECTS; i++)
		loaded[i] = objects[i].loaded ? 1 : 0;

	// measureBc7 is off from stage S1-2 on: HDLevelBuild decodes every page
	// for real a moment later and reports what that cost, so decoding them
	// all a second time here would only make the level load twice as slow to
	// measure the same thing.
	if (!HDScanLevel(HDInstallRoot(), g_stem, number_rooms, loaded, NUMBER_OBJECTS,
		false, 1, &stats, &why))
	{
		Log("HDLOG level=%s FAILED %s", g_stem, why);
		g_stem[0] = 0;
		return;
	}

	// What the buffers of this level will cost, for the budget - see
	// AutoBudgetBytes. This is the only moment they are known in advance.
	g_scan_vertices = stats.vertices;
	g_scan_indices = stats.indices;

	Log("HDLOG level=%s trg=v%d groups=%d rooms=%d sky=%d empty=%d pages=%d "
		"verts=%d idx=%d ranges=%d changes=%d mixed=%d map=%d slots=%d "
		"trm_bound=%d trm_missing=%d trm_dummy=%d trm_lara=%d heads=%d",
		g_stem, stats.trgVersion, stats.groups, stats.rooms, stats.skyGroups,
		stats.emptyGroups, stats.pages, stats.vertices, stats.indices,
		stats.ranges, stats.pageChanges, stats.mixedPageTriangles,
		stats.mapEntries, stats.slotsLoaded, stats.trmBound, stats.trmMissing,
		stats.trmDummy, stats.trmLara, stats.headsBound);

	for (int i = 0; i < HDScanMissingCount(); i++)
		Log("HDLOG level=%s missing slot=%s model=%s.TRM",
			g_stem, HDScanMissingSlot(i), HDScanMissingModel(i));

	if (stats.groups < stats.rooms)
		Log("HDLOG level=%s WARNING the .TRG has %d groups for %d rooms - "
			"the rooms above %d stay classic",
			g_stem, stats.groups, stats.rooms, stats.groups - 1);
}

void HDLevelBuild()
{
	HDRenderStats stats;
	const char* why = "";
	int hd_rooms = 0;
	int classic_rooms = 0;
	int suppressed_statics = 0;
	int kept_statics = 0;

	g_level_active = false;
	g_frame_count = 0;
	g_frame_dropped = 0;
	g_matrix_checked = false;
	g_sky_logged = false;
	memset(g_group_hd, 0, sizeof(g_group_hd));
	g_room_count = 0;
	g_flips_done = 0;

	for (int i = 0; i < HD_MAX_ROOMS; i++)
		g_room_group[i] = (short)i;

	/* THE LEVEL BEFORE THIS ONE, for the two diagnostic counters: its frame
	 * clock summary and its one clipped total. Here and not in HDLevelUnload,
	 * because this is the main thread and the load thread has joined - the
	 * counters are written by the thread that presents frames, and reading
	 * them from the load thread while it does would be a race for nothing.
	 * NOT behind HDEnabled(): FPSLOG counts frames whatever hd_enable says. */
	LevelDiagnosticsEnd();
	strcpy(g_fps_level, g_log_stem);

	// The level before this one, released here rather than in HDLevelUnload -
	// see the comment there. This is the main thread; that one is not.
	if (HDRenderGroupCount())
	{
		// The models first: their ranges name page slots in the cache
		// HDRenderLevelEnd is about to empty.
		HDObjectRelease();
		HDRenderLevelEnd();
		Log("HDLOG the previous level's HD objects are released");
	}

	if (!HDEnabled() || !g_stem[0])
		return;

	if (number_rooms <= 0 || number_rooms > HD_MAX_ROOMS)
	{
		Log("HDLOG level=%s build refused reason=room_count(%d)", g_stem, (int)number_rooms);
		return;
	}

	// The device is made once and lives as long as it does. A card that
	// cannot do shader model 3 says so here, once, and the game runs on
	// classic for the rest of the session - no picture changes, no error box.
	if (!HDRenderReady())
	{
		if (!HDRenderInit(App.dx.lpD3DDevice, &why))
		{
			Log("HDLOG level=%s build refused reason=%s", g_stem, why);
			return;
		}

		Log("HDLOG render init ok");
	}

	/* THE TEXTURE POLICY OF THIS LEVEL (#123, stage S6), set before anything
	 * is loaded and in force for the rooms AND for the models and Lara's
	 * wardrobe that follow them - it is one cache and one budget, not three.
	 * The device's own size limit is added inside HDRenderLevelBegin, which
	 * is the one place that has the caps in hand.
	 *
	 * Both settings are registry only and neither is in the options menu:
	 * they are what a card that cannot hold the art needs, not a taste. */
	{
		HDPageCachePolicy policy;

		policy.format = _stricmp(tomb5_hd_texture_format, "dxt5") == 0
			? HD_PAGE_FORMAT_DXT5 : HD_PAGE_FORMAT_RGBA;
		policy.budgetBytes = tomb5.hd_texture_budget_mb
			? (double)tomb5.hd_texture_budget_mb * 1048576.0
			: AutoBudgetBytes();
		policy.maxSide = 0;

		HDPageCacheConfigure(&policy);

		Log("HDLOG level=%s budget=%.0f MiB (%s) format=%s card=%.0f MiB "
			"classic_pages=%d framebuffer=%dx%d",
			g_stem, policy.budgetBytes / 1048576.0,
			tomb5.hd_texture_budget_mb ? "hd_texture_budget_mb" : "worked out",
			policy.format == HD_PAGE_FORMAT_DXT5 ? "dxt5" : "rgba",
			(double)DXTextureMemoryAtStart / 1048576.0, (int)nTextures,
			(int)App.dx.PresentParams.BackBufferWidth,
			(int)App.dx.PresentParams.BackBufferHeight);
	}

	const DWORD started = GetTickCount();

	g_clipped_lights = 0;
	g_clipped_bulbs = 0;
	g_clock = 0.0;
	g_clock_started = false;
	BuildWaterMarks();

	if (!HDRenderLevelBegin(HDInstallRoot(), g_stem, number_rooms, false,
		DecodeThreads(), &stats, &why))
	{
		Log("HDLOG level=%s build failed reason=%s", g_stem, why);

		/* The page cache's own notes BEFORE it is emptied - on the way out
		 * they are the only place that says which page the card gave up on
		 * (#123, stage S6). Without this the log names the failure and not
		 * the cause. */
		for (int i = 0; i < HDPageCacheNoteCount(); i++)
			Log("HDLOG level=%s page note: %s", g_stem, HDPageCacheNote(i));

		HDRenderLevelEnd();
		return;
	}

	const DWORD build_ms = GetTickCount() - started;

	// The marks were only borrowed for the length of that call.
	HDRenderSetWaterMarks(0, 0);
	g_water_marks.clear();

	Log("HDLOG level=%s water stamped=%d choppy=%d shimmer=%d room_shimmer=%d "
		"ps_slots=%lu",
		g_stem, stats.markedVertices, stats.markedChoppy, stats.markedShimmer,
		stats.markedRoomShimmer, HDRenderPixelSlots());

	// Stage S5. Everything that moves, counted out of the .TRG itself:
	// scroll = material 1 parts and the draw calls they cost, sway =
	// vertices with a wind weight, flipbook = material 13 parts, which are
	// counted and NOT drawn (see MODIFICATIONS.md).
	// cutout_parts (fix round A2) is the opaque parts of materials 6, 7 and
	// 8 - the ones that draw with the alpha test on. Held against
	// cutout_pages in the frame line below, the two say whether a page the
	// alpha measurement calls a cut-out sheet is really used as one.
	// add_weighted / add_min / add_swinging (fix round R3a) is the additive
	// side of the same thing: how many parts the .TRG's own material
	// parameters dimmed, how far the smallest of them was dimmed, and how
	// many of them also swing on the clock.
	Log("HDLOG level=%s motion=%d scroll_parts=%d scroll_ranges=%d "
		"scroll_tris=%d sway_verts=%d sway_parts=%d sway_dropped=%d "
		"flipbook_parts=%d cutout_parts=%d add_weighted=%d add_min=%.3f "
		"add_swinging=%d",
		g_stem, tomb5.hd_motion ? 1 : 0, stats.scrollParts, stats.scrollRanges,
		stats.scrollTris, stats.swayVertices, stats.swayParts,
		stats.swayDropped, stats.flipbookParts, stats.cutoutParts,
		stats.addWeightParts, stats.addWeightMin, stats.addSwingParts);

	g_room_count = number_rooms;

	int marked_rooms = 0;

	for (int r = 0; r < g_room_count; r++)
	{
		const int tris = HDRenderGroupTriangles(r, -1);
		const bool has_group = r < HDRenderGroupCount();

		/* THE THIRD WAY A ROOM STAYS CLASSIC (#123, stage S7-2). The first
		 * two are the .TRG's own: no group for this room, or a group with no
		 * triangle in it - which is what a Make_Room_Geometry room looks like
		 * and needs no marker at all. This one is the randomizer's: the run
		 * cut a wall open, moved a floor, added a window. The Remaster's art
		 * still shows the room as it was shipped, so drawing it would show a
		 * closed wall in front of an open doorway - the gravest of the S7
		 * cases, and the reason the sidecar exists.
		 *
		 * A marked room therefore loses its HD art even when the .TRG has a
		 * full group for it. The classic geometry IS the randomized geometry:
		 * it is read out of the level file the run wrote. */
		const bool marked = HDMarkersRoomTouched(r);

		const char* reason = !has_group ? "no_group"
			: (!tris ? "empty_group" : (marked ? "marker" : "ok"));

		if (marked && has_group && tris)
			marked_rooms++;

		/* Per GROUP, and the loop runs over rooms only because group i is
		 * room i while this runs - no flip has happened yet, and the marker
		 * that can take a group away is about the ROOM the randomizer
		 * reshaped. A flipped room's alternate is a room of its own with a
		 * group of its own, so both halves of a pair are decided here. */
		g_group_hd[r] = (unsigned char)((has_group && tris && !marked) ? 1 : 0);

		if (g_group_hd[r])
		{
			hd_rooms++;
			suppressed_statics += room[r].num_meshes;
		}
		else
		{
			classic_rooms++;
			kept_statics += room[r].num_meshes;
		}

		Log("HDLOG room=%d group=%d tris=%d/%d/%d ranges=%d statics=%d hd=%d reason=%s",
			r, r, HDRenderGroupTriangles(r, 0), HDRenderGroupTriangles(r, 1),
			HDRenderGroupTriangles(r, 2), HDRenderGroupRanges(r, -1),
			(int)room[r].num_meshes, (int)g_group_hd[r], reason);
	}

	g_level_active = hd_rooms > 0;

	/* THE FLIP PAIRS OF THIS LEVEL (#123, fix round A, A1), one line each and
	 * only with -log. A pair whose two halves disagree about HD is the one
	 * that can still show the bug in a milder form: the room turns classic in
	 * mid-level (or HD in mid-level), which is a picture change nobody asked
	 * for, so it is named rather than left to be discovered. */
	if (gLogEnabled)
	{
		int pairs = 0, mixed = 0;

		for (int r = 0; r < g_room_count; r++)
		{
			const int other = room[r].flipped_room;

			if (other < 0 || other >= g_room_count || other < r)
				continue;

			pairs++;

			if (g_group_hd[r] != g_group_hd[other])
				mixed++;

			Log("HDLOG flippair room=%d alt=%d flip=%d hd=%d/%d tris=%d/%d",
				r, other, (int)room[r].FlipNumber,
				(int)g_group_hd[r], (int)g_group_hd[other],
				HDRenderGroupTriangles(r, -1), HDRenderGroupTriangles(other, -1));
		}

		Log("HDLOG level=%s flip pairs=%d mixed_hd=%d", g_stem, pairs, mixed);
	}

	/* What the sidecar cost this level, in one line: how many rooms it named,
	 * and how many of those really had HD art to give up. The two differ
	 * whenever the run reshaped a room the .TRG had nothing for anyway. */
	if (HDMarkersPresent())
		Log("HDLOG level=%s markers rooms=%d of them classic now=%d "
			"donors=%d outfit=%d",
			g_stem, HDMarkersRoomCount(), marked_rooms,
			HDMarkersDonorCount(), HDMarkersOutfit());

	Log("HDLOG level=%s built ms=%d rooms=%d hd=%d classic=%d sky_groups=%d "
		"verts=%d idx=%d(%dbit) ranges=%d split=%d layer0_tris=%d "
		"vb_mib=%.1f ib_mib=%.1f read_ms=%d build_ms=%d",
		g_stem, (int)build_ms, g_room_count, hd_rooms, classic_rooms,
		stats.skyGroups, stats.vertices, stats.indices, stats.index32 ? 32 : 16,
		stats.ranges, stats.splitRanges, stats.fallbackPageTris,
		stats.vertexBytes / 1048576.0, stats.indexBytes / 1048576.0,
		(int)(stats.readMs + 0.5), (int)(stats.buildMs + 0.5));

	Log("HDLOG level=%s pages named=%d distinct=%d from5=%d from4=%d "
		"white=%d failed=%d mips=%d widest=%d big=%d mib=%.1f "
		"read_ms=%d decode_ms=%d(%d threads) upload_ms=%d compress_ms=%d",
		g_stem, stats.pages.pages, stats.pages.slots, stats.pages.fromGame5,
		stats.pages.fromGame4, stats.pages.fromWhite,
		stats.pages.failed, stats.pages.mipLevels, stats.pages.widest,
		stats.pages.bigPages, stats.pages.bytes / 1048576.0,
		(int)(stats.pages.readMs + 0.5), (int)(stats.pages.decodeMs + 0.5),
		stats.pages.threads, (int)(stats.pages.uploadMs + 0.5),
		(int)(stats.pages.compressMs + 0.5));

	/* THE BUDGET, in the words stage S6 promised: what was allowed, what the
	 * rooms of this level wanted, and what it cost them to fit. `dropped_mip0`
	 * is pages that gave up their largest level to the budget; `caps` is
	 * pages the DEVICE could not hold at full size, which is a different
	 * thing and not the user's to change. The models and Lara add their own
	 * line to this from hd_object.cpp - one cache, two callers. */
	Log("HDLOG level=%s rooms budget=%.0f need=%.0f have=%.0f got=%.0f MiB "
		"dropped_mip0=%d levels=%d caps=%d max_side=%d format=%s oom=%d",
		g_stem, stats.pages.budgetBytes / 1048576.0,
		stats.pages.needBytes / 1048576.0, stats.pages.haveBytes / 1048576.0,
		stats.pages.bytes / 1048576.0, stats.pages.droppedMip0,
		stats.pages.droppedLevels, stats.pages.clampedByCaps,
		stats.maxTextureSide,
		stats.pages.formatUsed == HD_PAGE_FORMAT_DXT5 ? "dxt5" : "rgba",
		stats.pages.outOfMemory);

	Log("HDLOG level=%s indices=%dbit window=%d (a 16 bit draw call reaches "
		"65536) split=%d", g_stem, stats.index32 ? 32 : 16, stats.indexWindow,
		stats.splitRanges);

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		Log("HDLOG level=%s page note: %s", g_stem, HDPageCacheNote(i));

	/* THE STATIC MESHES. A .TRG room has the level's static meshes baked into
	 * its art, so DrawStaticObjects must not draw them a second time - that is
	 * TROne's "double lantern" (game_renderer.cpp:3722-3736, a baked mesh
	 * pointer passes only while the moveable that claims it is streaming).
	 *
	 * Our seam is narrower and the rule with it: we suppress a ROOM's own
	 * MESH_INFO list, and only for a room that really draws HD. A moveable
	 * that happens to share a static's MESH_DATA* goes through
	 * DrawAnimatingItem and is never touched, and a room that stayed classic
	 * keeps every static it had. The exception TROne needs cannot arise here.
	 *
	 * What this does NOT solve is a static the game makes disappear at run
	 * time - a shattered vase is gone from the classic list and still stands
	 * in the baked art. TROne has the same hole; it belongs to the stage that
	 * draws objects, not to this one. */
	Log("HDLOG level=%s statics suppressed=%d kept=%d (in %d HD rooms, %d classic)",
		g_stem, suppressed_statics, kept_statics, hd_rooms, classic_rooms);

	if (!g_level_active)
	{
		Log("HDLOG level=%s no room draws HD - releasing again", g_stem);
		HDRenderLevelEnd();
		return;
	}

	/* THE OBJECTS (#123, stage S3-2), after the rooms and never before them:
	 * a model's pages are APPENDED to the cache the rooms built, and a page
	 * slot handed out before that cache exists points nowhere.
	 *
	 * They ride on the rooms in one more way. A level whose .TRG is missing
	 * or empty has no page cache at all, so it has no HD objects either -
	 * which is the same answer the rooms give and one line to explain. */
	HDObjectLevelBuild(HDInstallRoot(), g_stem);
}

void HDLevelUnload()
{
	/* NOT behind HDEnabled(): a level built while HD was on has to come down
	 * even when the player switched HD off in the options in the meantime.
	 * Asking the setting here is how a vertex buffer outlives its level.
	 *
	 * AND NOT A SINGLE DIRECT3D CALL HERE. FreeLevel runs at the top of
	 * LoadLevel, on the LOAD thread, while the main thread is presenting the
	 * loading bar - the two-threads-one-device window that froze this game
	 * twice (specific\file.cpp, the comment at the end of LoadLevel). The
	 * objects are released in HDLevelBuild instead, on the thread that owns
	 * the device, immediately before the next level's are made; and DXClose
	 * releases them through HDLevelDeviceLost when the device itself goes. */
	g_level_active = false;
	g_frame_count = 0;
	g_room_count = 0;
	g_stem[0] = 0;
	HDMarkersFree();	// text, no device - the same rule as the objects
	HDObjectLevelEnd();
}

void HDLevelDeviceLost()
{
	/* The device is going away - a resolution change, Alt+Enter, or DXClose
	 * at the end of the session. The last one is why the summary is flushed
	 * here as well as in HDLevelBuild: without it the level the player quit
	 * from would be the one level that never got its FPSLOG summary. Main
	 * thread in every one of those cases. */
	LevelDiagnosticsEnd();

	g_level_active = false;
	g_frame_count = 0;
	g_room_count = 0;
	HDObjectDeviceLost();

	if (!HDRenderReady() && !HDRenderGroupCount())
		return;

	// Shaders, declaration, both buffers and every page texture belong to the
	// device that is going away. HDRenderShutdown drops all of it and forgets
	// the device; the next HDLevelBuild calls HDRenderInit again on whatever
	// device exists then. Until the next level starts the rooms are classic -
	// which is a picture, not a crash, and a crash is what a dangling
	// IDirect3DVertexBuffer9 would be.
	HDRenderShutdown();
	Log("HDLOG device lost - HD released, classic until the next level");
}

/* The group that draws room `roomIndex` right now, or -1. The one place the
 * flip table is read; everything else asks this. */
static int GroupOfRoom(int roomIndex)
{
	if (!g_level_active || roomIndex < 0 || roomIndex >= g_room_count)
		return -1;

	const int group = g_room_group[roomIndex];

	if (group < 0 || group >= HD_MAX_ROOMS || !g_group_hd[group])
		return -1;

	return group;
}

bool HDRoomIsHD(int roomIndex)
{
	return GroupOfRoom(roomIndex) >= 0;
}

/* ONE HALF OF ONE FlipMap SWAP (#123, fix round A, A1).
 *
 * Called from FlipMap (game\control.cpp) at the exact line where the two
 * ROOM_INFO records change places, once per pair, so the art follows the
 * geometry in the same statement. Cheap enough to be unconditional: two
 * bounds tests and a swap, and it is called at most once per room per flip.
 *
 * With HD off, or in a level the .TRG had nothing for, g_level_active is
 * false, the table is never read, and this returns on its first test - so the
 * classic path pays one bool for the whole hook. */
void HDFlipRoom(int roomIndex, int otherIndex)
{
	if (!g_level_active)
		return;

	if (roomIndex < 0 || otherIndex < 0 ||
		roomIndex >= g_room_count || otherIndex >= g_room_count)
		return;

	const short keep = g_room_group[roomIndex];

	g_room_group[roomIndex] = g_room_group[otherIndex];
	g_room_group[otherIndex] = keep;

	/* The first ten swaps of a level, and then a count - a flip that runs on
	 * a timer would otherwise fill the log with the same two lines. */
	if (gLogEnabled && g_flips_done < 10)
		Log("HDLOG flip room=%d <-> room=%d now group=%d / group=%d hd=%d/%d",
			roomIndex, otherIndex, (int)g_room_group[roomIndex],
			(int)g_room_group[otherIndex],
			(int)g_group_hd[g_room_group[roomIndex]],
			(int)g_group_hd[g_room_group[otherIndex]]);

	g_flips_done++;
}

void HDFrameReset()
{
	g_frame_count = 0;
	g_frame_dropped = 0;
	g_projection_taken = false;
	HDObjectFrameReset();
}

/* THE PROJECTION OF THIS FRAME, taken the moment the first HD thing turns up -
 * a room out of InsertRoom or an item out of DrawAnimatingItem, whichever the
 * engine reaches first, and never twice. It has to be taken while the world is
 * COLLECTED and not when it is drawn, because AlterFOV moves f_persp and the
 * centre inside a frame (the sky phase does it twice while the binoculars are
 * up) and the HD path has to project with the field of view the classic path
 * transformed its vertices with. */
void HDFrameCaptureProjection()
{
	if (g_projection_taken)
		return;

	g_projection_taken = true;
	g_persp = f_persp;
	g_centerx = f_centerx;
	g_centery = f_centery;
	g_za = f_a;
	g_zb = f_b;
	g_zboo = f_boo;
	g_znear = f_mznear;
	g_oneopersp = f_moneopersp;
}

bool HDFrameRoom(int roomIndex, const float* viewWorld)
{
	if (!viewWorld)
		return false;

	const int group = GroupOfRoom(roomIndex);

	if (group < 0)
		return false;

	if (g_frame_count >= HD_MAX_FRAME)
	{
		// draw_rooms (game\draw.cpp) holds 100, so this cannot happen - but a
		// silent overrun of a fixed array is not a thing to leave to a
		// comment.
		g_frame_dropped++;
		return false;
	}

	HDFrameCaptureProjection();

	FrameRoom& entry = g_frame[g_frame_count++];
	entry.room = roomIndex;
	entry.group = group;
	SetD3DMatrix(&entry.world, (float*)viewWorld);

	return true;
}

/* THE HD SKY, IN THE CLASSIC HORIZON'S PLACE (#123, fix round R3a).
 *
 * Called from SkyDrawPhase (game\draw.cpp) at the line where the classic path
 * draws its HORIZON mesh, with the matrix that is on the engine's stack right
 * there - phd_TranslateAbs(camera.pos), no rotation, no scale. Returns true
 * when the .TRG's own sky was drawn, and the caller then leaves out the
 * horizon mesh and the two flat colour layers, which is exactly what the dome
 * replaces. A level whose .TRG has no sky group (JOBY3, JOBY5) answers false
 * and keeps the classic sky whole.
 *
 * WHY HERE AND NOT IN HDFrameDraw, where the rooms are drawn. The classic sky
 * writes no depth (OutputSky switches D3DRS_ZWRITEENABLE off), so the order
 * inside SkyDrawPhase is the order on the screen: the moon first, then the
 * sky, then the stars. Drawn with the rooms instead, the dome would paint
 * over the moon that was drawn a moment earlier. Here it takes the horizon
 * mesh's own place in that order and nothing else moves.
 *
 * THE PROJECTION IS TAKEN FRESH and HDFrameCaptureProjection is deliberately
 * not used: SkyDrawPhase moves f_persp itself while the binoculars are up
 * (AlterFOV at its top and again at its bottom), so the sky is drawn with a
 * different field of view from the rooms, and the frame's one captured
 * projection belongs to the rooms.
 *
 * The lighting block is filled by hand rather than through FillFrameLighting:
 * that function steps the game clock, and the frame must step it once. The
 * sky gets the clock as it stands, the hd_motion switch, no dynamic light, no
 * fog bulb and no distance fog - the classic path draws its horizon under
 * nPolyType 6, which is none of those either. */
bool HDFrameSky(const float* viewWorld)
{
	if (!viewWorld || !HDEnabled() || !g_level_active)
		return false;

	if (!HDRenderSkyGroups())
		return false;

	D3DMATRIX projection;
	D3DMATRIX world;
	HDFrameLighting frame;

	HDRenderMakeProjection((double)f_persp, (double)f_centerx, (double)f_centery,
		(double)f_a, (double)f_b, &projection);

	memset(&frame, 0, sizeof(frame));
	frame.centerX = f_centerx;
	frame.centerY = f_centery;
	frame.wibble = (float)wibble;
	frame.cameraUnderwater = 0;
	frame.distanceFogStart = 1.0e9f;	// no fog on the sky, as in the classic path
	frame.wibbleTable = vert_wibble_table;
	frame.bulbs = 0;
	frame.bulbCount = 0;
	frame.clock = (float)g_clock;
	frame.motion = tomb5.hd_motion ? 1 : 0;

	SetD3DMatrix(&world, (float*)viewWorld);

	HDRenderFrameBegin(&kIdentity, &projection, &frame);
	const bool drew = HDRenderSky(&world);
	HDRenderFrameEnd();

	if (gLogEnabled && !g_sky_logged)
	{
		g_sky_logged = true;
		Log("HDLOG level=%s sky groups=%d drew=%d calls=%d persp=%.1f",
			g_stem, HDRenderSkyGroups(), drew ? 1 : 0, HDRenderDrawCalls(),
			f_persp);
	}

	return drew;
}

void HDFrameDraw()
{
	const int objects_here = HDObjectFrameCount();

	if (!g_frame_count && !objects_here)
		return;

	D3DMATRIX projection;
	HDFrameLighting frame;
	HDFogBulb bulbs[HD_MAX_BULBS];
	HDRoomLighting lighting;
	HDLightSource lights[HD_MAX_LIGHTS];

	HDRenderMakeProjection((double)g_persp, (double)g_centerx, (double)g_centery,
		(double)g_za, (double)g_zb, &projection);

	// Cleared before the two Fill functions, which are the only two that set
	// it, and read at the bottom of this function: one frame's worth.
	g_clip_this_frame = false;

	FillFrameLighting(frame, bulbs);
	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	for (int i = 0; i < g_frame_count; i++)
	{
		FillRoomLighting(g_frame[i].room, g_frame[i].group, lighting, lights);
		HDRenderRoom(g_frame[i].group, &g_frame[i].world, &lighting);
	}

	/* THE OBJECTS, AFTER THE ROOMS AND INSIDE THE SAME BRACKET (#123, S3-2).
	 *
	 * One bracket a frame: hd_model sets its own shader, declaration and
	 * stream and finds the rest of the device state exactly as the room path
	 * left it, which is why hd_render splits the bracket into a save and a
	 * restore instead of hiding it inside a room draw. It also means the fog
	 * and the fog bulbs are already in their registers - a model reads the
	 * very ones a room was just given.
	 *
	 * AFTER THE ROOMS AND BEFORE DrawBuckets, and here is the whole argument.
	 * The depth buffer is shared and exact (hd_render.cpp, HDRenderSelfTest:
	 * both paths write a - b/z for the same point), so an opaque HD guard and
	 * an opaque classic crate sort themselves out by depth whatever order
	 * they are drawn in - which covers every opaque pixel in the frame and
	 * needs no rule at all. Only the two passes that do NOT write depth care:
	 *
	 *   - A model's ALPHA pass writes depth (it is an alpha test, not a
	 *     blend), so it behaves like the opaque one.
	 *   - A model's ADDITIVE pass does not, and it is drawn before the
	 *     classic buckets. A classic OPAQUE polygon that stands BEHIND such a
	 *     glow and in front of the wall therefore paints over it. Known hole,
	 *     and the same one the rooms have had since stage S1-2; it needs a
	 *     second bracket after DrawBuckets to close and it is worth one only
	 *     if a picture ever shows it. The classic sort list, which is where
	 *     TR5 puts its own see-through polygons, still draws last of all and
	 *     over both.
	 *
	 * Drawing the objects INSIDE the room loop would be worse for a reason
	 * that has nothing to do with depth: a model's state is not a room's, and
	 * swapping shader, declaration and stream twice per room costs more than
	 * the whole object path does. */
	HDObjectFrameDraw(&projection, frame.bulbCount);

	HDRenderFrameEnd();

	if (!g_matrix_checked && gLogEnabled && g_frame_count)
	{
		g_matrix_checked = true;
		MatrixCheck(g_frame[0].room, g_frame[0].group, g_frame[0].world, projection);
		Log("HDLOG frame rooms=%d calls=%d dropped=%d bulbs=%d fog_start=%.0f "
			"wibble=%d cam_uw=%d shimmer=%d objects=%d object_calls=%d "
			"cutout_calls=%d cutout_pages=%d",
			g_frame_count, HDRenderDrawCalls(), g_frame_dropped,
			frame.bulbCount, frame.distanceFogStart, (int)wibble,
			frame.cameraUnderwater, tomb5.shimmer ? 1 : 0,
			objects_here, HDObjectDrawCalls(),
			HDRenderCutoutCalls(), HDPageCacheCutoutPages());
	}

	/* THE FRAME'S HD COUNTERS, handed to the frame clock as MAXIMA over the
	 * second (see FrameClockFlushSecond). Draw calls are rooms plus objects,
	 * which is every call the HD path made in this frame. Costs three
	 * comparisons and only when the log is on. */
	if (gLogEnabled)
	{
		/* HDRenderDrawCalls is reset by HDRenderFrameBegin and is therefore
		 * this frame's. HDObjectDrawCalls is NOT - hd_object.cpp resets it
		 * per LEVEL - so it is only read when this frame actually had
		 * objects; otherwise it would carry the last frame that did. */
		const int draws = HDRenderDrawCalls() +
			(objects_here ? HDObjectDrawCalls() : 0);

		if (g_frame_count > g_sec_rooms)
			g_sec_rooms = g_frame_count;

		if (objects_here > g_sec_items)
			g_sec_items = objects_here;

		if (draws > g_sec_draws)
			g_sec_draws = draws;
	}

	if (g_clip_this_frame)
	{
		g_clip_frames++;

		/* ONCE PER LEVEL, at the first frame that clips - not once per frame,
		 * which is what the old reset-every-frame version did (173 identical
		 * lines out of one title screen). The total goes into the level's
		 * summary in LevelDiagnosticsEnd. */
		if (gLogEnabled && !g_clip_reported)
		{
			g_clip_reported = true;
			Log("HDLOG level=%s clipped first frame lights=%d bulbs=%d "
				"room=%d offered_lights=%d offered_bulbs=%d (maxima %d / %d)",
				g_stem, g_clipped_lights, g_clipped_bulbs, g_clip_lights_room,
				g_clip_lights_worst, g_clip_bulbs_worst,
				HD_MAX_LIGHTS, HD_MAX_BULBS);
		}
	}

	g_frame_count = 0;
}

/* ONE PRESENTED FRAME. Called from DXShowFrame (specific\dxshell.cpp) straight
 * after Present returns - the long note beside the frame clock's counters says
 * why that call site and no other. */
void HDFramePresented()
{
	// THE GUARD. Without -log this is a single bool test and a return: no
	// clock is read, no counter moves, nothing is written.
	if (!gLogEnabled)
		return;

	LARGE_INTEGER now;

	if (!g_qpc_freq)
	{
		LARGE_INTEGER f;

		if (!QueryPerformanceFrequency(&f) || !f.QuadPart)
		{
			g_qpc_freq = -1;	// no clock on this machine - stay quiet
			return;
		}

		g_qpc_freq = f.QuadPart;
	}

	if (g_qpc_freq < 0)
		return;

	QueryPerformanceCounter(&now);

	if (!g_qpc_last)
	{
		// The first present of the session or of a level: there is no
		// interval yet, only a start.
		g_qpc_last = now.QuadPart;
		g_qpc_second = now.QuadPart;
		FrameClockNewSecond();
		return;
	}

	const double ms = (double)(now.QuadPart - g_qpc_last) * 1000.0 / (double)g_qpc_freq;

	g_qpc_last = now.QuadPart;

	if (!g_sec_frames || ms < g_sec_min)
		g_sec_min = ms;

	if (ms > g_sec_max)
		g_sec_max = ms;

	g_sec_sum += ms;
	g_sec_frames++;

	if (ms > kFrameBudgetMs)
		g_sec_slow++;

	if (ms > kMissedSlotMs)
		g_sec_late++;

	if (now.QuadPart - g_qpc_second < g_qpc_freq)
		return;

	FrameClockFlushSecond();

	/* The next second starts one second after this one UNLESS the gap was
	 * bigger than two - a level load presents nothing for a while, and a
	 * catch-up loop would write a run of empty lines about a second nobody
	 * drew in. Then it simply starts again here. */
	if (now.QuadPart - g_qpc_second < 2 * g_qpc_freq)
		g_qpc_second += g_qpc_freq;
	else
		g_qpc_second = now.QuadPart;

	FrameClockNewSecond();
}

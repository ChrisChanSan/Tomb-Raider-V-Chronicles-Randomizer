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
#include "../profile.h"			// PROFLOG phases (#148)
#include "../output.h"			// gPresentMinTicks - the budget follows the present (#148)
#include "../d3dmatrix.h"
#include "../3dmath.h"
#include "../winmain.h"
#include "../dxshell.h"				// DXTextureMemoryAtStart (#123, S6)
#include "../texture.h"				// nTextures (#123, S6)
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// number_rooms, room
#include "../../game/camera.h"		// camera.underwater
#include "../../game/draw.h"
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

		/* E6: THE ROOM'S LIGHTING, WORKED OUT ONCE AND KEPT. The frame draws
		 * its rooms twice now - the solid passes in one phase, the glows in
		 * another after the classic buckets - and FillRoomLighting counts the
		 * lights it had to turn away as it goes, so running it a second time
		 * would count this room's clipped lights twice. It runs in the solid
		 * phase and the glow phase reads what it left. */
		HDRoomLighting lighting;
		HDLightSource lights[HD_MAX_LIGHTS];

		/* GROUP M: THE PORTAL WINDOW, in backbuffer pixels, as the scissor
		 * test wants it - `x0/y0` the first pixel drawn, `x1/y1` the first
		 * one that is not. Worked out once, when the room is collected,
		 * because that is when the engine's own `r->left..r->bottom` still
		 * hold this room's answer: PrintObjects resets all four at the end
		 * of the same frame.
		 *
		 * `full` is a window that covers the whole backbuffer - the camera's
		 * own room, and any room the walk could see all of. Those are drawn
		 * with the test off, which is both faster and exactly what a
		 * rectangle covering everything would do. */
		int x0, y0, x1, y1;
		bool full;
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

	/* THE WATER SURFACE THE REMASTER DOES NOT DRAW (#123, fix round H, H1).
	 *
	 * One entry per classic room face that IS a water surface and that the
	 * .TRG has no art for. See BuildWaterSurfaces for the measurement; the
	 * faces are kept sorted by roomlet so that drawroom.cpp transforms and
	 * lights a roomlet once and then picks its water faces out of it. */
	std::vector<HDRoomWaterFace> g_water_faces[HD_MAX_ROOMS];
	int g_water_face_total = 0;
	int g_water_face_rooms = 0;
	int g_water_face_kept = 0;	// faces the .TRG DOES cover - left to it

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
	 * WHAT THE NUMBERS MEAN. The LOGIC is a fixed 30 Hz in every mode; what
	 * fps_mode changes is how often the same logic state is presented.
	 * S_DumpScreen waits for gPresentMinTicks ticks of the engine's own 60 Hz
	 * counter (specific\time.cpp) before it presents - two at fps_mode 0, one
	 * at fps_mode 1 - so a healthy second is `frames=30` with every interval
	 * on 33.3 ms, or `frames=60` with every interval on 16.7 ms. The line
	 * carries both: `fps=` is the target the budget was taken from, and
	 * `logic_ticks=` is what the simulation really ran, which must stay 30
	 * whatever `frames=` says. That quantisation is why there are TWO
	 * counters and not one:
	 *
	 *   slow   intervals over the budget (33.3 ms after a two-tick present,
	 *          16.7 after a one-tick one; the line prints which as `budget=`).
	 *          Half a healthy second lands here on jitter alone, so a number
	 *          here on its own means nothing - it is the budget line the
	 *          brief asked for.
	 *   late   intervals over the budget plus half a 60 Hz tick - 41.7 ms
	 *          after two ticks, 25.0 after one. Jitter cannot reach it; a
	 *          frame that MISSED its slot costs a whole tick and lands well
	 *          beyond. This is the number that says the frame rate really
	 *          fell under the target.
	 *
	 * A SECOND SPENT IN A MENU is not a second of gameplay: `logic_ticks=0`,
	 * `hd_rooms=0` and `frames=30` together mean the inventory, the pause
	 * menu or the title was up, and those keep thirty by design at any
	 * setting. `budget=` says so on the line itself. PROFLOG goes one step
	 * further and calls such an interval a `pause` rather than a frame -
	 * FPSLOG cannot, because it never sees where a frame came from.
	 *
	 * NOTHING HAPPENS WITHOUT -log. The first line of HDFramePresented is
	 * `if (!gLogEnabled) return;` - one bool test per present, no counter
	 * kept, no clock read. Nothing here allocates, ever: the accumulators are
	 * plain integers and doubles in this file.
	 */
	/* #148, order 1b: THE BUDGET FOLLOWS THE PRESENT, NOT THE OPTION. It was
	 * a pair of constants while thirty was the only rate there was; order 1
	 * made it follow tomb5.fps_mode, and that was still wrong, because
	 * fps_mode is not what a given frame waited for. Only DrawPhaseGame's own
	 * present ever drops to one tick (and only outside the title - see
	 * GameFrameMinTicks); the inventory, the pause menu, the load/save
	 * screen, the death menu, the title and the level-change fade all keep
	 * their two whatever the option says. Judged against 16.7 ms they
	 * reported a perfectly healthy menu second as thirty slow and a dozen
	 * late frames, which is exactly what the 2026-09-07 log showed.
	 *
	 * gPresentMinTicks is the number the present that just returned waited
	 * for, and HDFramePresented is called directly after that present - so
	 * reading it here is reading this frame's own slot. The one present that
	 * does not go through it is the loading bar's (S_DumpScreenFrame waits
	 * for one tick of its own); it is measured against the default two, i.e.
	 * exactly as generously as it always was. */
	/* #148, order 6: and the third case is a frame that waited for NO tick,
	 * because the present itself waited for the display. Its slot is the
	 * panel's period - 6.9 ms on a 144 Hz monitor - and the definition now
	 * lives in specific\output.cpp so that this line and PROFLOG's cannot
	 * drift apart. */
	double FrameBudgetMs()
	{
		return PresentSlotMs();
	}

	double MissedSlotMs()
	{
		return FrameBudgetMs() + 1000.0 / 120.0;
	}

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

	/* #148, order 1: GlobalCounter when this second began. Its difference at
	 * the flush is how many LOGIC ticks the second really ran, and that is
	 * the proof the clock split did what it says: at fps_mode 1 the line has
	 * to read frames=60 with logic_ticks=30. GlobalCounter is a ushort and
	 * wraps; the subtraction is done in ushort and therefore wraps with it. */
	unsigned short g_sec_counter = 0;

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
		g_sec_counter = GlobalCounter;
	}

	void FrameClockFlushSecond()
	{
		if (g_sec_frames <= 0)
			return;

		/* #148, order 6: `fps=` is what the mode ASKS for - 30, 60, or the
		 * panel's own refresh rate - and `real_fps=` is what the presents
		 * actually came at, worked out from the mean interval rather than
		 * from `frames=`, which is a count over a second that begins and ends
		 * on a frame boundary and is therefore a frame out either way. On a
		 * display-bound run the two have to agree with the monitor: 144 and
		 * 143.8 is a healthy 144 Hz panel. */
		const double avg_ms = g_sec_sum / g_sec_frames;

		Log("FPSLOG level=%s frames=%d logic_ticks=%d fps=%d real_fps=%.1f "
			"budget=%.1f min_ms=%.1f max_ms=%.1f avg_ms=%.1f slow=%d late=%d "
			"hd_rooms=%d hd_items=%d hd_draws=%d",
			g_fps_level[0] ? g_fps_level : "?", g_sec_frames,
			(int)(unsigned short)(GlobalCounter - g_sec_counter),
			(int)TargetPresentHz(), avg_ms > 0.0 ? 1000.0 / avg_ms : 0.0,
			FrameBudgetMs(), g_sec_min,
			g_sec_max, avg_ms, g_sec_slow, g_sec_late,
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

	/* GROUP M: what the portal windows of this frame came to, for the log.
	 * `full` drew over the whole backbuffer, `scissored` inside a rectangle,
	 * `empty` were rooms the walk gave a window of no width or no height and
	 * that are therefore not drawn at all. */
	int g_clip_full = 0;
	int g_clip_scissored = 0;
	int g_clip_empty = 0;
	// Once per level, on the first frame that draws an HD room: one line per
	// room with the window it got. Reset beside g_matrix_checked.
	bool g_clip_listed = false;
	// Throttle for the per-frame line: GlobalCounter when it was last
	// written. The counter ticks 30 times a second at every fps_mode, so a
	// difference of 30 is one second of game time.
	unsigned short g_clip_line_at = 0;
	bool g_clip_line_ever = false;

	/* THE ENGINE'S INCLUSIVE WINDOW AS A SCISSOR RECTANGLE (#123, group M).
	 *
	 * `r->left..r->bottom` are pixel INDICES and both ends belong to the room
	 * (game\draw.cpp, SetRoomBounds: the projected portal corner is widened
	 * by a pixel on every side before it is written). Direct3D's rectangle is
	 * half open, so the right and bottom edges gain one to reach the pixel
	 * they name - and one more, with a matching one taken off the left and
	 * the top, as the MARGIN.
	 *
	 * The margin is not decoration. The classic path does not really clip a
	 * polygon to this window: it drops a polygon all of whose corners lie
	 * beyond one edge of it (specific\polyinsert.cpp, `c[v0] & c[v1] & ...`)
	 * and clips whatever is left against the SCREEN. So a polygon that
	 * straddles the window's edge is drawn whole by the classic path and
	 * would be cut exactly at the edge by a scissor. One pixel of slack is
	 * what makes the two agree along a portal's rim; everything wider than a
	 * pixel is geometry the near room's own wall covers, which is the
	 * invariant the portal walk is built on. */
	void ClipWindow(FrameRoom& entry, int left, int top, int right, int bottom)
	{
		const int w = (int)phd_winwidth;
		const int h = (int)phd_winheight;

		if (right <= left || bottom <= top || w <= 0 || h <= 0)
		{
			// A window of no width or no height. The walk cannot produce one
			// for a room it put on the draw list (SetRoomBounds returns
			// before it writes when tL >= tR), so this is the guard and not
			// a case - but a room drawn over the whole screen because four
			// numbers came out backwards is the bug this group exists to
			// stop.
			entry.x0 = entry.y0 = entry.x1 = entry.y1 = 0;
			entry.full = false;
			return;
		}

		int x0 = left - 1;
		int y0 = top - 1;
		int x1 = right + 2;
		int y1 = bottom + 2;

		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > w) x1 = w;
		if (y1 > h) y1 = h;

		entry.x0 = x0;
		entry.y0 = y0;
		entry.x1 = x1;
		entry.y1 = y1;
		entry.full = x0 <= 0 && y0 <= 0 && x1 >= w && y1 >= h;
	}

	// A window of no width or no height: nothing of this room can be seen and
	// nothing of it is drawn. Both phases ask the same question of the same
	// four numbers, so both skip the same rooms.
	bool ClipEmpty(const FrameRoom& entry)
	{
		return entry.x1 <= entry.x0 || entry.y1 <= entry.y0;
	}

	// The window of one collected room, applied to the device. A full window
	// and a room with no window switch the test off instead.
	void ApplyClip(const FrameRoom& entry)
	{
		if (entry.full || ClipEmpty(entry))
			HDRenderScissorOff();
		else
			HDRenderScissor(entry.x0, entry.y0, entry.x1, entry.y1);
	}

	/* E6: WHAT THE GLOW PHASE NEEDS AGAIN. The solid phase fills these and
	 * HDFrameDrawGlow, which runs after the classic buckets and therefore
	 * after HDFrameDraw has returned, reads them: the projection, the frame's
	 * lighting block and the fog bulbs it points at. Filled per frame in
	 * HDFrameDraw and nowhere else; g_frame_pending says whether there is a
	 * phase still owing, so a frame that drew nothing opens no second bracket
	 * and a HDFrameReset in between cancels it. */
	D3DMATRIX g_frame_projection;
	HDFrameLighting g_frame_light;
	HDFogBulb g_frame_bulbs[HD_MAX_BULBS];
	int g_frame_objects = 0;
	int g_frame_room_calls = 0;
	bool g_frame_pending = false;

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

/* THE WATER SURFACE THE REMASTER DOES NOT DRAW (#123, fix round H, H1).
 *
 * WHAT WAS MEASURED. The player's picture of a VCI pool shows dark washed-out
 * blotches where the remaster shows a moving surface, and his own look in
 * trview says why: "the vanilla water faces carry a wave texture, in HD the
 * assignment for the face is missing". The first reading of that - the .TRG
 * gives those faces texture layer 0 and the renderer falls back to the
 * level's first page - is WRONG, and the file says so: over all fifteen
 * levels, 0 of 12,247,000 vertices carry layer 0 (TrgProbe -layer0).
 *
 * What is missing is the GEOMETRY. The classic engine draws a water surface
 * as ordinary room faces at the room's own minfloor, and the shimmer branch
 * that makes them move opens with `pos.y == CurrentRoomPtr->minfloor` in a
 * room carrying ROOM_REFLECT or ROOM_MIST (drawroom.cpp:531-545). Held
 * against the .TRG plane by plane (TrgProbe -watergap), over 93 such rooms in
 * the fifteen files:
 *
 *     JOBY2   4 of 5 rooms have NO .TRG triangle in the classic water plane
 *     JOBY5  13 of 25          -   the whole VCI pool set
 *     ANDY3  23 of 23, ANDY1 5 of 6, ANDREA2 10 of 13, ANDREA1 3 of 3,
 *     JOBY4   1 of 1, RICH3 2 of 4
 *     ANDREA3, JOBY3, RICH2  0 of 15 - those rooms keep their surface
 *
 * JOBY2's room 66 is the pool of the player's picture: 24 classic faces of
 * ONE texinfo at y -1280, and 0 .TRG triangles in that plane. The HD room
 * therefore draws the pool FLOOR and nothing over it, which is exactly the
 * flat dark checkerboard the player photographed - there is no water there
 * at all, so no texture and no shader could have been wrong.
 *
 * WHAT IS DONE. Where the level has a water surface and the .TRG has not, the
 * classic face is drawn on top of the HD room, by the classic path, with its
 * own texture, its own animated texture range, its own light and the shimmer
 * the classic path has always given it. Nothing is invented: the surface that
 * comes back is the one the level ships.
 *
 * The two tests a face has to pass are the engine's own, not a rule of ours:
 * every corner at the room's minfloor (the classic height test), and every
 * corner marked by aRoomVertexWaterMarks as a shimmer-surface vertex (the
 * classic predicate, portal walk and all). A face the .TRG covers - asked per
 * face, in its own plane and its own box - is left to the .TRG, so a room
 * whose water the remaster did build is untouched.
 */
static void BuildWaterSurfaces()
{
	g_water_face_total = 0;
	g_water_face_rooms = 0;
	g_water_face_kept = 0;

	for (int i = 0; i < number_rooms && i < HD_MAX_ROOMS; i++)
	{
		g_water_faces[i].clear();

		ROOM_INFO* r = &room[i];

		if (!r->pRoomlets || r->nRoomlets <= 0)
			continue;

		// Only the two flags whose branch ever sets the surface bit.
		if (!(r->flags & (ROOM_MIST | ROOM_REFLECT)))
			continue;

		/* A room the .TRG has no group for is drawn classically anyway, with
		 * its water face and all. Read straight out of the table rather than
		 * through GroupOfRoom: this runs before any flip, so group i is room
		 * i, which is the same window BuildStaticTables works in. */
		if (i >= HD_MAX_ROOMS || !g_group_hd[i])
			continue;

		int covered = 0;

		for (int n = 0; n < r->nRoomlets; n++)
		{
			ROOMLET* roomlet = &r->pRoomlets[n];

			if (!roomlet->pSVtx || !roomlet->pFac)
				continue;

			short* face = roomlet->pFac;

			for (int f = 0; f < roomlet->nQuad + roomlet->nTri; f++)
			{
				const bool quad = f < roomlet->nQuad;
				const int corners = quad ? 4 : 3;
				bool water = true;
				float x0 = 0, x1 = 0, z0 = 0, z1 = 0;

				for (int c = 0; c < corners && water; c++)
				{
					const long v = face[c];

					if (v < 0 || v >= roomlet->nVtx)
					{
						water = false;
						break;
					}

					const float* xyz = roomlet->pSVtx + v * 7;

					if (xyz[1] != r->minfloor ||
						!(aRoomVertexWaterMarks(r, roomlet, v) & 2))
					{
						water = false;
						break;
					}

					if (!c)
					{
						x0 = x1 = xyz[0];
						z0 = z1 = xyz[2];
					}
					else
					{
						if (xyz[0] < x0) x0 = xyz[0];
						if (xyz[0] > x1) x1 = xyz[0];
						if (xyz[2] < z0) z0 = xyz[2];
						if (xyz[2] > z1) z1 = xyz[2];
					}
				}

				face += quad ? 6 : 5;

				if (!water)
					continue;

				// The .TRG's own answer for that piece of plane.
				if (HDRenderGroupFlatArt(i, (int)r->minfloor,
					(int)x0, (int)z0, (int)x1, (int)z1))
					covered++;

				HDRoomWaterFace entry;

				entry.roomlet = (short)n;
				entry.face = (short)(quad ? f : f - roomlet->nQuad);
				entry.quad = quad ? 1 : 0;
				g_water_faces[i].push_back(entry);
			}
		}

		/* PER ROOM AND NOT PER FACE. A pool is one surface, and half of one
		 * is worse than either whole: JOBY2's room 66 has 24 water faces and
		 * the remaster's art touches 3 of their sectors with the coping, so a
		 * face-by-face rule would leave three square holes in the middle of
		 * the pool. So the room decides, by majority - and a room whose
		 * surface the remaster really did build (ANDREA3, JOBY3, RICH2, and
		 * twelve of JOBY5's twenty-five) keeps every face out of the way. */
		if (!g_water_faces[i].empty() &&
			covered * 2 >= (int)g_water_faces[i].size())
		{
			g_water_face_kept += (int)g_water_faces[i].size();
			g_water_faces[i].clear();
		}

		if (!g_water_faces[i].empty())
		{
			g_water_face_rooms++;
			g_water_face_total += (int)g_water_faces[i].size();
			g_water_faces[i].shrink_to_fit();

			Log("HDLOG room=%d water surface restored=%d face(s) at y=%d, "
				"the .TRG covers %d of them", i,
				(int)g_water_faces[i].size(), (int)r->minfloor, covered);
		}
	}

	Log("HDLOG level=%s water surfaces restored=%d in %d room(s), "
		"left to the .TRG=%d", g_stem, g_water_face_total, g_water_face_rooms,
		g_water_face_kept);
}

/* ---------------------------------------------- the statics (R3b) ------- */

/* THE TWO STATIC MESH TABLES OF AN HD LEVEL (#123, fix round R3b).
 *
 * Since stage S1-2 an HD room's whole MESH_INFO list is suppressed, because
 * the remaster bakes the static meshes into the room's art. Fix round B found
 * the two holes in that rule and left both named:
 *
 *   - 21 statics of ANDREA1 (rooms 121 and 221) sit in NO .TRG group at all.
 *     The room seam takes them away and puts nothing in their place, so they
 *     are simply gone from the level.
 *   - a static the game destroys at run time is gone from the classic list and
 *     goes on standing in the baked art. That is the player's "Static mashes
 *     sind nicht zerstoerbar".
 *
 * Both are answered here, once per level, against the .TRG's own geometry:
 *
 *   g_static_classic  one byte per static of the level: 1 = the .TRG draws
 *                     nothing where it stands, so DrawStaticObjects draws it
 *                     the classic way even inside an HD room.
 *   g_destruct_pairs  the destructible pieces of the .TRG - a part whose
 *                     material carries bit 15 - paired with the classic
 *                     MESH_INFO standing inside their box. The engine clears
 *                     that mesh's Flags bit 0 when Lara shoots a static of
 *                     slot 50 to 57 (control.cpp:1935), and the piece goes
 *                     with it.
 *
 * The box of a static is its own visibility box (STATIC_INFO x_minp..z_maxp)
 * turned by the mesh's y_rot - the four corners of the XZ rectangle through
 * the engine's own sine table, not an approximation of it - and made room
 * local, which is the space the .TRG is in.
 */
struct HDStaticPair
{
	short room;
	short mesh;
	int piece;
};

static std::vector<unsigned char> g_static_classic;
static std::vector<int> g_static_first;		// per room, index into the above
static std::vector<HDStaticPair> g_destruct_pairs;

static void StaticBox(const MESH_INFO* mesh, const ROOM_INFO* r,
	float* mins, float* maxs)
{
	const STATIC_INFO* s = &static_objects[mesh->static_number];
	const float c = (float)phd_cos(mesh->y_rot) * (1.0f / 16384.0f);
	const float sn = (float)phd_sin(mesh->y_rot) * (1.0f / 16384.0f);
	const float x0 = (float)s->x_minp, x1 = (float)s->x_maxp;
	const float z0 = (float)s->z_minp, z1 = (float)s->z_maxp;
	const float cx = (float)(mesh->x - r->x);
	const float cz = (float)(mesh->z - r->z);

	const float xs[4] = { x0, x1, x0, x1 };
	const float zs[4] = { z0, z0, z1, z1 };

	for (int i = 0; i < 4; i++)
	{
		const float wx = cx + xs[i] * c + zs[i] * sn;
		const float wz = cz + zs[i] * c - xs[i] * sn;

		if (!i)
		{
			mins[0] = maxs[0] = wx;
			mins[2] = maxs[2] = wz;
		}
		else
		{
			if (wx < mins[0]) mins[0] = wx;
			if (wx > maxs[0]) maxs[0] = wx;
			if (wz < mins[2]) mins[2] = wz;
			if (wz > maxs[2]) maxs[2] = wz;
		}
	}

	mins[1] = (float)(mesh->y - r->y + s->y_minp);
	maxs[1] = (float)(mesh->y - r->y + s->y_maxp);
}

static void BuildStaticTables()
{
	g_static_classic.clear();
	g_static_first.assign((size_t)number_rooms + 1, 0);
	g_destruct_pairs.clear();

	int total = 0;

	for (int r = 0; r < number_rooms; r++)
	{
		g_static_first[(size_t)r] = total;
		total += room[r].num_meshes;
	}

	g_static_first[(size_t)number_rooms] = total;
	g_static_classic.assign((size_t)total, 0);

	int orphans = 0;
	int paired = 0;
	int shatterable = 0;
	const int pieces = HDRenderDestructibleCount();

	for (int r = 0; r < number_rooms; r++)
	{
		if (!HDRoomIsHD(r))
			continue;

		const ROOM_INFO* info = &room[r];

		for (int i = 0; i < info->num_meshes; i++)
		{
			const MESH_INFO* mesh = &info->mesh[i];

			if (mesh->static_number < 0 || mesh->static_number >= 70)
				continue;

			float mins[3], maxs[3];

			StaticBox(mesh, info, mins, maxs);

			/* 1. Is there any HD geometry here at all? */
			if (!HDRenderGroupHasGeometry(r, mins, maxs))
			{
				g_static_classic[(size_t)(g_static_first[(size_t)r] + i)] = 1;
				orphans++;

				if (gLogEnabled)
					Log("HDLOG static room=%d mesh=%d slot=%d classic "
						"box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f",
						r, i, (int)mesh->static_number,
						mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

				continue;
			}

			/* 2. And if the engine can destroy it, which piece of the art is
			 * it? Slots 50 to 57 are the shatterable ones - the same test
			 * control.cpp:1928 makes before it shatters one. */
			if (mesh->static_number < 50 || mesh->static_number >= 58)
				continue;

			shatterable++;

			for (int p = 0; p < pieces; p++)
			{
				int group = -1;
				float pmins[3], pmaxs[3];

				if (!HDRenderDestructibleInfo(p, &group, 0, pmins, pmaxs) ||
					group != r)
					continue;

				bool overlaps = true;

				for (int axis = 0; axis < 3; axis++)
					if (maxs[axis] < pmins[axis] || mins[axis] > pmaxs[axis])
						overlaps = false;

				if (!overlaps)
					continue;

				HDStaticPair pair;

				pair.room = (short)r;
				pair.mesh = (short)i;
				pair.piece = p;
				g_destruct_pairs.push_back(pair);
				paired++;

				if (gLogEnabled)
					Log("HDLOG static room=%d mesh=%d slot=%d destructible "
						"piece=%d", r, i, (int)mesh->static_number, p);
			}
		}
	}

	Log("HDLOG level=%s statics classic=%d of %d, destructible pieces=%d "
		"shatterable=%d paired=%d",
		g_stem, orphans, total, pieces, shatterable, paired);
}

bool HDStaticIsClassic(int roomIndex, int meshIndex)
{
	if (roomIndex < 0 || roomIndex + 1 >= (int)g_static_first.size())
		return false;

	const int at = g_static_first[(size_t)roomIndex] + meshIndex;

	if (meshIndex < 0 || at >= g_static_first[(size_t)roomIndex + 1])
		return false;

	return g_static_classic[(size_t)at] != 0;
}

/* Once per frame, before the rooms are drawn: a piece whose static the engine
 * has taken out of the classic list goes with it. At most 69 entries in the
 * whole game, so this is a walk and not a lookup. */
static void UpdateDestructibles()
{
	for (size_t i = 0; i < g_destruct_pairs.size(); i++)
	{
		const HDStaticPair& pair = g_destruct_pairs[i];
		const MESH_INFO* mesh = &room[pair.room].mesh[pair.mesh];

		HDRenderDestructibleHide(pair.piece, (mesh->Flags & 1) == 0);
	}
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
	out.reflect = tomb5.hd_reflect ? 1 : 0;		// R3b

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
	// GROUP M: the per-level window list is written again for this level.
	g_clip_listed = false;
	g_clip_line_ever = false;
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

	/* E2: `flooded` is how many of the two shimmer sets no classic corner
	 * named and the plane answered for - the number that says whether a
	 * level's water animates over its whole surface or over a sliver of it -
	 * and `planes_dropped` must stay 0 (see HDRenderStats). */
	Log("HDLOG level=%s water stamped=%d choppy=%d shimmer=%d room_shimmer=%d "
		"flooded=%d planes_dropped=%d ps_slots=%lu",
		g_stem, stats.markedVertices, stats.markedChoppy, stats.markedShimmer,
		stats.markedRoomShimmer, stats.markedFlooded,
		stats.waterPlanesDropped, HDRenderPixelSlots());

	// Stage S5. Everything that moves, counted out of the .TRG itself:
	// scroll = material 1 parts and the draw calls they cost, sway =
	// vertices with a wind weight, flipbook = material 13 parts. Of those,
	// flipbook_pages is how many run as a four page flipbook (fix round R3b -
	// their own page plus the three layers their material names, one per
	// frame time), flipbook_dropped how many kept their own page because the
	// file's numbers did not resolve, and flipbook_ranges the draw calls the
	// running ones cost.
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
		"flipbook_parts=%d flipbook_pages=%d flipbook_dropped=%d "
		"flipbook_ranges=%d cutout_parts=%d add_weighted=%d add_min=%.3f "
		"add_swinging=%d gloss_parts=%d reflect=%d",
		g_stem, tomb5.hd_motion ? 1 : 0, stats.scrollParts, stats.scrollRanges,
		stats.scrollTris, stats.swayVertices, stats.swayParts,
		stats.swayDropped, stats.flipbookParts, stats.flipbookDrawn,
		stats.flipbookDropped, stats.flipbookRanges, stats.cutoutParts,
		stats.addWeightParts, stats.addWeightMin, stats.addSwingParts,
		stats.glossParts, tomb5.hd_reflect ? 1 : 0);

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
	BuildStaticTables();

	/* H1: and the water surface the remaster left out - see the long note
	 * over BuildWaterSurfaces. It has to run after the loop above, because it
	 * asks per room whether that room draws HD at all. */
	BuildWaterSurfaces();

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
	/* R3b: the static tables point into THIS level's rooms. */
	g_static_classic.clear();
	g_static_first.clear();
	g_destruct_pairs.clear();

	/* H1: and so does the water surface table - a roomlet number and a face
	 * number in the room that is being freed. */
	for (int i = 0; i < HD_MAX_ROOMS; i++)
		g_water_faces[i].clear();

	g_water_face_total = 0;
	g_water_face_rooms = 0;
	g_water_face_kept = 0;
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

/* THE CLASSIC WATER FACES OF ONE HD ROOM (#123, fix round H, H1). Read by
 * drawroom.cpp's InsertRoom the moment the HD path has taken the room over -
 * see BuildWaterSurfaces for what is in the list and why. */
int HDRoomWaterFaceCount(int roomIndex)
{
	if (!g_level_active || roomIndex < 0 || roomIndex >= g_room_count ||
		roomIndex >= HD_MAX_ROOMS)
		return 0;

	return (int)g_water_faces[roomIndex].size();
}

const HDRoomWaterFace* HDRoomWaterFaceAt(int roomIndex, int at)
{
	if (at < 0 || at >= HDRoomWaterFaceCount(roomIndex))
		return 0;

	return &g_water_faces[roomIndex][(size_t)at];
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

	/* H1: and the restored water faces with it. They are a roomlet number and
	 * a face number INSIDE a ROOM_INFO, and FlipMap is the statement where
	 * two ROOM_INFO records change places - so the list has to travel with
	 * the mesh it names, exactly as the art above travels with the geometry.
	 * Both are empty in almost every room, so this is two empty vectors
	 * changing places. */
	if (roomIndex < HD_MAX_ROOMS && otherIndex < HD_MAX_ROOMS)
		g_water_faces[roomIndex].swap(g_water_faces[otherIndex]);

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
	// E6: whatever glow phase was still owing belongs to a frame that is
	// over. A new frame's HDFrameDraw sets it again.
	g_frame_pending = false;
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

bool HDFrameRoom(int roomIndex, const float* viewWorld,
	int left, int top, int right, int bottom)
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
	/* GROUP M: the portal window, taken HERE and not at draw time. These four
	 * numbers are the engine's own `r->left..r->bottom` and they do not
	 * survive the frame - PrintObjects (game\draw.cpp) resets all four of
	 * them at the end of each room's items, which is before S_OutputPolyList
	 * draws anything HD. */
	ClipWindow(entry, left, top, right, bottom);

	return true;
}

/* THE WINDOW OF THE ROOM AN ITEM STANDS IN (#123, group M). The frame list is
 * short - a hundred rooms at the very most, and the walk usually returns ten -
 * so this is a scan and not a table: a table indexed by room would have to be
 * cleared per frame over every room of the level, which is the more expensive
 * of the two for every level this game has.
 *
 * A room that is not on the list gets no scissor at all. That is not a
 * fallback but the right answer: an item whose room the walk never reached is
 * an item the classic path would have drawn just the same (PrintObjects hands
 * every item the WHOLE screen, phd_left = 0 .. phd_winxmax + 1, and not its
 * room's window), and cutting it against a window that does not exist would
 * invent a rule the engine has not got. */
void HDFrameScissorForRoom(int roomIndex)
{
	if (roomIndex >= 0)
	{
		for (int i = 0; i < g_frame_count; i++)
		{
			if (g_frame[i].room != roomIndex)
				continue;

			ApplyClip(g_frame[i]);
			return;
		}
	}

	HDRenderScissorOff();
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
	frame.reflect = tomb5.hd_reflect ? 1 : 0;	// R3b

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

/* THE FRAME IN TWO PHASES (#123, fix round E, E6).
 *
 * WHAT WAS WRONG. This function drew room-major: all three passes of the
 * first room, then all three of the second, then the objects, then the
 * classic buckets. The first two passes write depth and the third does not
 * (hd_render.cpp, draw_pass: pass 2 is ONE / ONE with D3DRS_ZWRITEENABLE
 * FALSE), so every later draw that DOES write depth and stands BEHIND a glow
 * in the world passes the depth test - the glow left no depth to fail
 * against - and paints over it.
 *
 * On the screen that is the player's picture: a wall lamp of RICH2 whose cone
 * hangs in a portal is cut off along the portal's edge, because the room
 * beyond the portal is drawn after it and its far wall is what fills the
 * opening. The same hole ate an object's glow behind a classic crate, and
 * hd_level.cpp's own note has named it since stage S1-2 as "worth a second
 * bracket only if a picture ever shows it". A picture shows it.
 *
 * THE ORDER NOW, which is the classic engine's own: everything that writes
 * depth first, whatever it belongs to, then everything that does not.
 *
 *   HDFrameDraw       every room's passes 0 and 1, then every HD object and
 *                     Lara's passes 0 and 1
 *   DrawBuckets       the classic opaque world (output.cpp)
 *   HDFrameDrawGlow   every room's pass 2, then every object's pass 2
 *   DrawSortList      TR5's own see-through polygons, last of all
 *
 * so an HD glow is now behind the classic sort list and in front of every
 * opaque thing it should be in front of - which is what the classic path does
 * with its own additive polygons, and it needs no rule of its own.
 *
 * THE COST is one more state bracket per frame (HDRenderFrameBegin does a
 * GetRenderState and a SetRenderState for each state it owns) and one more
 * prepare per drawn room - two matrix multiplies and the room's light block.
 * The lighting itself is worked out once: FillRoomLighting counts the lights
 * it turns away, so it runs in this phase and the glow phase reads what it
 * left in the FrameRoom. */
void HDFrameDraw()
{
	const int objects_here = HDObjectFrameCount();

	g_frame_objects = objects_here;
	g_frame_pending = false;

	if (!g_frame_count && !objects_here)
		return;

	D3DMATRIX& projection = g_frame_projection;
	HDFrameLighting& frame = g_frame_light;
	HDFogBulb* const bulbs = g_frame_bulbs;

	HDRenderMakeProjection((double)g_persp, (double)g_centerx, (double)g_centery,
		(double)g_za, (double)g_zb, &projection);

	// Cleared before the two Fill functions, which are the only two that set
	// it, and read at the bottom of this function: one frame's worth.
	g_clip_this_frame = false;

	FillFrameLighting(frame, bulbs);
	// R3b: a destructible piece follows its own static's Flags bit.
	UpdateDestructibles();
	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	// GROUP M: this frame's windows, counted for the log below.
	g_clip_full = 0;
	g_clip_scissored = 0;
	g_clip_empty = 0;

	for (int i = 0; i < g_frame_count; i++)
	{
		const FrameRoom& entry = g_frame[i];
		const bool empty = ClipEmpty(entry);

		if (empty)
			g_clip_empty++;
		else if (entry.full)
			g_clip_full++;
		else
			g_clip_scissored++;

		/* GROUP M: the room is drawn inside the window the portal walk gave
		 * it, and a room with no window is not drawn at all. Without the cap
		 * ApplyClip does nothing and every room draws whole, which is the
		 * picture before this group. */
		if (empty && HDRenderScissorAvailable())
			continue;

		ApplyClip(entry);

		// PROF_HD_LIGHT is the light block alone, inside PROF_HD_DRAW: the
		// one part of the room draw that is processor work rather than draw
		// calls, and therefore the one that a single core can stall on.
		ProfBegin(PROF_HD_LIGHT);
		FillRoomLighting(g_frame[i].room, g_frame[i].group,
			g_frame[i].lighting, g_frame[i].lights);
		ProfEnd(PROF_HD_LIGHT);
		HDRenderRoomMask(g_frame[i].group, &g_frame[i].world,
			&g_frame[i].lighting, HD_PASS_SOLID);
	}

	/* GROUP M: the rooms are done. The objects set their own window per item,
	 * so this line is only for the frame that has none: without it the last
	 * room's window would still be in force when the bracket closes, and the
	 * one thing this group must never do is hand the classic path a device it
	 * did not have. */
	HDRenderScissorOff();

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
	 * needs no rule at all. Only the pass that does NOT write depth cares:
	 *
	 *   - A model's ALPHA pass writes depth (it is an alpha test, not a
	 *     blend), so it behaves like the opaque one and travels with it.
	 *   - A model's ADDITIVE pass does not, and fix round E took it out of
	 *     this phase for that reason - it is drawn in HDFrameDrawGlow below,
	 *     after the classic buckets, where nothing opaque can follow it. The
	 *     classic sort list, which is where TR5 puts its own see-through
	 *     polygons, still draws last of all and over both.
	 *
	 * Drawing the objects INSIDE the room loop would be worse for a reason
	 * that has nothing to do with depth: a model's state is not a room's, and
	 * swapping shader, declaration and stream twice per room costs more than
	 * the whole object path does. */
	HDObjectFrameDraw(&projection, frame.bulbCount, HD_PASS_SOLID);

	HDRenderFrameEnd();

	// E6: the glow phase is owing, and this is what the rooms cost so far -
	// HDRenderFrameBegin zeroes the renderer's counter per bracket.
	g_frame_room_calls = HDRenderDrawCalls();
	g_frame_pending = true;
}

/* THE GLOW PHASE (#123, fix round E, E6). Called from S_OutputPolyList right
 * after DrawBuckets - see the long note over HDFrameDraw for the order and
 * the argument. Everything drawn here is additive, writes no depth and is
 * tested against a depth buffer that now holds the WHOLE opaque frame, HD and
 * classic alike, so no later opaque draw can take a glow away and no glow can
 * stand in front of something nearer than itself.
 *
 * A frame that drew no HD room and no HD object never sets g_frame_pending
 * and this is a test and a return. */
void HDFrameDrawGlow()
{
	if (!g_frame_pending)
		return;

	g_frame_pending = false;

	D3DMATRIX& projection = g_frame_projection;
	HDFrameLighting& frame = g_frame_light;
	const int objects_here = g_frame_objects;

	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	/* H2: the glass first and the glows after it. Both are drawn without a
	 * depth write against a depth buffer that now holds the whole opaque
	 * frame; the order between them is the classic path's own - a surface you
	 * can see through is a surface, and a glow is painted on top of whatever
	 * it shines through.
	 *
	 * GROUP M: the same window per room in this phase as in the solid one. A
	 * glow is a surface of its room like any other - the RICH2 lamp cone that
	 * fix round E moved into this phase hangs inside a portal, and it has to
	 * be cut by that portal's window and by no other. ClipEmpty asks the same
	 * question of the same four numbers HDFrameDraw asked, so the two phases
	 * skip the same rooms; a room skipped there had its light block left
	 * unfilled and must not be drawn here either. */
	for (int i = 0; i < g_frame_count; i++)
	{
		if (ClipEmpty(g_frame[i]) && HDRenderScissorAvailable())
			continue;

		ApplyClip(g_frame[i]);
		HDRenderRoomMask(g_frame[i].group, &g_frame[i].world,
			&g_frame[i].lighting, HD_PASS_BLEND);
	}

	for (int i = 0; i < g_frame_count; i++)
	{
		if (ClipEmpty(g_frame[i]) && HDRenderScissorAvailable())
			continue;

		ApplyClip(g_frame[i]);
		HDRenderRoomMask(g_frame[i].group, &g_frame[i].world,
			&g_frame[i].lighting, HD_PASS_ADDITIVE);
	}

	// GROUP M: the rooms are done - see the same line in HDFrameDraw.
	HDRenderScissorOff();
	HDObjectFrameDraw(&projection, frame.bulbCount, HD_PASS_ADDITIVE);
	HDRenderFrameEnd();

	g_frame_room_calls += HDRenderDrawCalls();
	HDObjectFrameEnd();

	if (!g_matrix_checked && gLogEnabled && g_frame_count)
	{
		g_matrix_checked = true;
		MatrixCheck(g_frame[0].room, g_frame[0].group, g_frame[0].world, projection);
		Log("HDLOG frame rooms=%d calls=%d dropped=%d bulbs=%d fog_start=%.0f "
			"wibble=%d cam_uw=%d shimmer=%d objects=%d object_calls=%d "
			"cutout_calls=%d cutout_pages=%d flipbook_calls=%d "
			"gloss_calls=%d",
			g_frame_count, g_frame_room_calls, g_frame_dropped,
			frame.bulbCount, frame.distanceFogStart, (int)wibble,
			frame.cameraUnderwater, tomb5.shimmer ? 1 : 0,
			objects_here, HDObjectDrawCalls(),
			HDRenderCutoutCalls(), HDPageCacheCutoutPages(),
			HDRenderFlipbookCalls(), HDRenderGlossCalls());
	}

	/* GROUP M, THE PORTAL WINDOWS OF THIS FRAME. Two lines, both behind -log
	 * and both throttled, because this runs once per drawn frame:
	 *
	 *   once per level, at the first frame that drew an HD room, one line per
	 *   room with the window it got - the numbers a picture of the bleed can
	 *   be held against;
	 *
	 *   once a second after that, the tally. GlobalCounter ticks thirty times
	 *   a second whatever the frame rate is (#148), so thirty of it is one
	 *   second of game time. */
	if (gLogEnabled && g_frame_count)
	{
		const unsigned short now = GlobalCounter;

		if (!g_clip_listed)
		{
			g_clip_listed = true;
			Log("HDLOG level=%s clip first frame - %s, backbuffer %dx%d",
				g_stem, HDRenderScissorAvailable()
					? "scissor test in force"
					: "NO SCISSOR TEST ON THIS DEVICE - rooms draw whole",
				(int)phd_winwidth, (int)phd_winheight);

			for (int i = 0; i < g_frame_count; i++)
				Log("HDLOG   room=%d group=%d clip=%d,%d,%d,%d %s",
					g_frame[i].room, g_frame[i].group,
					g_frame[i].x0, g_frame[i].y0, g_frame[i].x1, g_frame[i].y1,
					ClipEmpty(g_frame[i]) ? "EMPTY - not drawn"
						: g_frame[i].full ? "(the whole screen)" : "");
		}

		if (!g_clip_line_ever || (unsigned short)(now - g_clip_line_at) >= 30)
		{
			g_clip_line_ever = true;
			g_clip_line_at = now;
			Log("HDLOG clip rooms=%d full=%d scissored=%d empty=%d",
				g_frame_count, g_clip_full, g_clip_scissored, g_clip_empty);
		}
	}

	/* THE FRAME'S HD COUNTERS, handed to the frame clock as MAXIMA over the
	 * second (see FrameClockFlushSecond). Draw calls are rooms plus objects,
	 * which is every call the HD path made in this frame. Costs three
	 * comparisons and only when the log is on. */
	if (gLogEnabled)
	{
		/* g_frame_room_calls is the two brackets of this frame added up -
		 * HDRenderDrawCalls is reset by HDRenderFrameBegin and would answer
		 * for the glow phase alone (E6). HDObjectDrawCalls is not reset per
		 * frame at all - hd_object.cpp resets it per LEVEL - so it is only
		 * read when this frame actually had objects; otherwise it would carry
		 * the last frame that did. */
		const int draws = g_frame_room_calls +
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

	if (ms > FrameBudgetMs())
		g_sec_slow++;

	if (ms > MissedSlotMs())
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

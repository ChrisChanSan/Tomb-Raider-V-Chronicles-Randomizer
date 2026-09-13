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
#include "hd_outfitmenu.h"		// the footsteps follow the outfit (#153, M3)
#include "hd_install.h"
#include "hd_bind.h"
#include "hd_lara.h"			// HD_LARA_OUTFIT_* - the data[3] agreement
#include "hd_markers.h"
#include "hd_scan.h"
#include "hd_render.h"
#include "hd_object.h"
#include "hd_font.h"			// the HD font's texture goes with the device
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
#include "../file.h"				// textinfo, aranges, nAnimUVRanges (#123, N2)
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

/* The engine never has more rooms than HD_MAX_ROOMS, and one frame never
 * holds more of them than draw_rooms (game\draw.cpp) does. Both are fixed
 * arrays here rather than allocations, so that nothing in the frame path can
 * fail.
 *
 * T5: HD_MAX_FRAME follows draw_rooms' own capacity, which is now the
 * engine's room maximum of 256. It was 128 against a list of 100, which was
 * headroom; with the reach budget at 128 it would have been the exact worst
 * case and no headroom at all - the walk's own list is trimmed to 128
 * reach-wise, but a list of CLASSIC rooms is never trimmed and was measured
 * at 106 in one JOBY4 frame (RoomProbe -reachsurvey). One entry per listed
 * room reaches here, from InsertRoom (specific\drawroom.cpp:200), so 256 is
 * the true bound and this array can no longer be the thing that drops a room
 * the walk kept. The cost is static and named: sizeof(FrameRoom) is 380
 * bytes, so the array goes from 47.5 to 95 KiB of BSS. */
#define HD_MAX_ROOMS	1024
#define HD_MAX_FRAME	256

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

		/* FIX ROUND T (T1), narrowed in T2-2: whether the GROUP drawing this
		 * room carries any art that stands inside another room. A group that
		 * does not draws over the whole screen, which is what every room did
		 * before group M and what the classic path does with a polygon that
		 * straddles the window's rim. */
		bool windowed;

		/* WHICH CAMERA THIS ROOM WAS COLLECTED WITH (fix round R). ViewSerial
		 * at the InsertRoom that put it here; see the note over it in
		 * specific\3dmath.cpp. Read only by the log below. */
		long view;
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
	/* FIX ROUND T (T1), REWRITTEN IN T2-2: which GROUPS carry art that
	 * stands inside another room, and therefore have triangles to draw
	 * inside their own portal window.
	 *
	 * Group M scissored EVERY HD room to the rectangle the portal walk gave
	 * it, and the play test of 2026-09-07 says what that cost: "everything
	 * that goes over portals does not go over its edges properly any more."
	 * Round T narrowed it to the rooms of an overlapping pair - and the play
	 * test of 2026-09-08 found the half that was still wrong: it gave the
	 * window to the HOLDER as well, and to the whole of both partners' art.
	 * BuildRoomIntrusions below measures the triangles instead, once per
	 * level; every other triangle in the game draws over the whole screen,
	 * as it did before group M.
	 *
	 * PER GROUP AND NOT PER ROOM, like g_group_hd and for the same reason:
	 * HDFrameFlip swaps which group draws which room, and the property
	 * belongs to the art. */
	unsigned char g_group_windowed[HD_MAX_ROOMS];
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
	// T1: rooms with no art inside another room, drawn over the whole screen,
	// and (T2-2) the triangles the rest of them really did draw inside their
	// portal window - the only draw calls in the game the scissor is on for.
	int g_clip_open = 0;
	int g_clip_windowed_tris = 0;
	/* Fix round R: HD items whose own room's window is NOT the whole screen,
	 * i.e. the ones group M used to cut and the classic path never does.
	 * Counted per frame with -log and reported in the clip line, so that the
	 * withdrawn rule can be judged by a number. */
	int g_clip_items_spared = 0;
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

	/* ONE ROOM'S ART, IN AS MANY PIECES AS THE WINDOW NEEDS (#123, fix round
	 * T, T1; fix round T2, T2-2).
	 *
	 * A room with no intruding art, or one whose window is the whole screen,
	 * is one draw with the scissor test off - which is what every room was
	 * before group M and what every room but a handful is again.
	 *
	 * A room that HAS intruding art is drawn twice: everything of it over the
	 * whole screen first, then the triangles that stand inside another room
	 * inside its own portal window. That second call is the only place in the
	 * game the scissor test is ever on, and it covers a few dozen triangles
	 * per level - see BuildRoomIntrusions. Everything else, the overhang
	 * included, is drawn whole, which is what the classic path does with a
	 * polygon that straddles the window's rim. */
	void DrawRoomWindowed(const FrameRoom& entry, unsigned int passes)
	{
		if (!entry.windowed || entry.full || !HDRenderScissorAvailable())
		{
			HDRenderScissorOff();
			HDRenderRoomMask(entry.group, &entry.world, &entry.lighting,
				passes, HD_WINDOW_ALL);
			return;
		}

		HDRenderScissorOff();
		HDRenderRoomMask(entry.group, &entry.world, &entry.lighting, passes,
			HD_WINDOW_OPEN);

		/* AND AN EMPTY WINDOW NOW ONLY SILENCES THE INTRUDING HALF (T2-2).
		 * Group M skipped the whole room when the walk left it a rectangle of
		 * no width or height, which was safe while the window was the room -
		 * but the open half is not windowed at all and must be drawn whatever
		 * four numbers the walk came back with. */
		if (!ClipEmpty(entry))
		{
			HDRenderScissor(entry.x0, entry.y0, entry.x1, entry.y1);
			HDRenderRoomMask(entry.group, &entry.world, &entry.lighting,
				passes, HD_WINDOW_INTRUDING);
			HDRenderScissorOff();
		}

		if (gLogEnabled)
			g_clip_windowed_tris +=
				HDRenderGroupIntrudingTriangles(entry.group);
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

	/* P2: THE PICKUP DISPLAY'S OWN TWO, and they are kept apart from the
	 * frame's on purpose. The display is drawn in the middle of
	 * S_OutputPolyList, after the frame's own bracket has been opened, drawn
	 * and closed; writing its projection over g_frame_projection would take
	 * the glow phase's copy away from it if the two ever met. They never do
	 * today - the glow phase runs above the depth clear and the display below
	 * it - and that is a reason to keep them apart, not to share them. */
	D3DMATRIX g_pickup_projection;
	HDFrameLighting g_pickup_light;

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
	// N1: the most water surfaces any frame of this level has drawn, which
	// is what throttles the line about them - see HDFrameDrawWindowed.
	int g_water_calls_worst = 0;

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

	/* WHAT THE IN-LEVEL RE-DRESSES HAVE COST SO FAR (#153, round M3). The
	 * cache's own byte count when this level was built, and how many times
	 * the player has changed his outfit since. The cap is on the BYTES; the
	 * count is for the log. Both are reset by the next level build. */
	double g_redress_bytes0 = 0;
	int g_redress_count = 0;

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

/* THE ROOMS' OWN BOXES, FOR THE RENDERER (#123, fix round T, T1).
 *
 * The box TR5 itself calls the inside of a room, taken from the very lines
 * that ask whether an item fits inside one (game\draw.cpp:2248-2253):
 *
 *     x    r->x + 1024  ..  r->x + (y_size - 1) * 1024
 *     z    r->z + 1024  ..  r->z + (x_size - 1) * 1024
 *     y    r->maxceiling ..  r->minfloor
 *
 * - and note that x runs on y_size and z on x_size, which is TR5's own
 * naming and not a slip: `r->floor[z_index + x_size * x_index]` says which
 * is which.
 *
 * Handed over ROOM LOCAL, because that is the frame the .TRG's vertices are
 * in: x and z relative to the room's origin, y in world units (the file
 * stores it that way and every TR5 room has y 0 anyway - measured over the
 * fifteen files). */
static std::vector<HDRoomBox> g_room_boxes;

static void BuildRoomBoxes()
{
	g_room_boxes.clear();
	g_room_boxes.resize((size_t)number_rooms);

	for (int i = 0; i < number_rooms; i++)
	{
		const ROOM_INFO* r = &room[i];
		HDRoomBox& b = g_room_boxes[(size_t)i];

		b.mins[0] = 1024.0f;
		b.maxs[0] = (float)((r->y_size - 1) << 10);
		b.mins[1] = (float)(r->maxceiling - r->y);
		b.maxs[1] = (float)(r->minfloor - r->y);
		b.mins[2] = 1024.0f;
		b.maxs[2] = (float)((r->x_size - 1) << 10);

		for (int axis = 0; axis < 3; axis++)
			if (b.maxs[axis] < b.mins[axis])
				b.maxs[axis] = b.mins[axis];
	}

	if (!g_room_boxes.empty())
		HDRenderSetRoomBoxes(&g_room_boxes[0], (int)g_room_boxes.size());
	else
		HDRenderSetRoomBoxes(0, 0);
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

		/* Every classic water face of the room, and of those how many the
		 * .TRG has ANY art for (fix round H's majority) and how many it lays
		 * its OWN water over (N1, a material 16 sheet). See the note below;
		 * the first two numbers are counted exactly as fix round H counted
		 * them, sheeted faces and all, so that the majority it takes is the
		 * same majority. */
		int inPlane = 0;
		int covered = 0;
		int sheeted = 0;

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

				inPlane++;

				// The .TRG's own answer for that piece of plane.
				if (HDRenderGroupFlatArt(i, (int)r->minfloor,
					(int)x0, (int)z0, (int)x1, (int)z1))
					covered++;

				/* N1: and does the .TRG lay its OWN water over this piece of
				 * ground? Then the classic face is dropped here and now -
				 * that ground has a surface already, and a second one one to
				 * twenty units away from it is the one thing worse than
				 * none. It is still counted above, because the majority
				 * below is fix round H's and has to stay fix round H's. */
				if (HDRenderGroupWaterArt(i, (int)r->minfloor,
					(int)x0, (int)z0, (int)x1, (int)z1))
				{
					sheeted++;
					continue;
				}

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
		/* BUT THE REMASTER'S OWN WATER IS DROPPED PER FACE, AND ONLY THE FACE
		 * (#123, fix round N, N1, and the audit of that round).
		 *
		 * Fix round H asked one question - does the .TRG draw ANY horizontal
		 * art in this plane - and a stone coping answers it as readily as a
		 * water surface. That is why the COPING question is a majority: a
		 * coping is not water, so dropping the faces it touches one by one
		 * would leave square holes in the middle of a pool.
		 *
		 * Material 16 is a different answer: it IS the remaster's still
		 * water, so where it stands the classic face is not a hole but a
		 * DOUBLE - a second surface one to twenty units from the first.
		 * Dropping exactly those faces leaves no hole anywhere; the pool
		 * stays one continuous surface, part of it the level's own and part
		 * of it the remaster's.
		 *
		 * THE FIRST READING OF THIS ROUND handed the whole room over on one
		 * sheet anywhere in the plane, and measured that is far too much:
		 * JOBY5's room 184 has 29 classic faces in the plane and a sheet over
		 * 2 of them, room 4 has 30 and a sheet over 6 - so 27 and 24 sectors
		 * of pool would have gone bare to save a double over 2 and 6.
		 *
		 * The other end is just as wrong. Leaving the sheeted faces out of
		 * the majority as well turns rooms fix round H deliberately handed to
		 * the .TRG back into restored ones - ANDREA2's 231 (26 faces, 14 of
		 * them the .TRG's, all 14 the sheet) and eight more of JOBY5 - which
		 * is a change nobody asked for and no measurement supports. So the
		 * count above is fix round H's count, sheeted faces and all, and the
		 * ONLY thing this round changes is that a sheeted face is not drawn.
		 *
		 * Measured over the fifteen levels that is six rooms and no others:
		 * JOBY5's 4 (30 -> 24), 27 (6 -> 5), 28 (7 -> 4), 83 (3 -> 2), 115
		 * (6 -> 5) and 184 (29 -> 27). Every other room in the game restores
		 * exactly what it restored before. */
		if (inPlane && covered * 2 >= inPlane)
		{
			g_water_face_kept += inPlane;
			g_water_faces[i].clear();
		}
		else
			// A sheeted face is left to the .TRG as surely as a covered one.
			g_water_face_kept += sheeted;

		if (!g_water_faces[i].empty())
		{
			g_water_face_rooms++;
			g_water_face_total += (int)g_water_faces[i].size();
			g_water_faces[i].shrink_to_fit();

			Log("HDLOG room=%d water surface restored=%d of %d face(s) at "
				"y=%d, the .TRG covers %d of them and lays its own water over "
				"%d", i, (int)g_water_faces[i].size(), inPlane,
				(int)r->minfloor, covered, sheeted);
		}
		else if (sheeted)
			Log("HDLOG room=%d water surface left to the .TRG, %d of its %d "
				"face(s) under the .TRG's own sheet", i, sheeted, inPlane);
	}

	Log("HDLOG level=%s water surfaces restored=%d in %d room(s), "
		"left to the .TRG=%d", g_stem, g_water_face_total, g_water_face_rooms,
		g_water_face_kept);
}


/* ------------------------------------- the HD water frames (N2) --------- */

/* THE REMASTER'S OWN PICTURE ON THE CLASSIC WATER SURFACE (#123, fix round
 * N, N2).
 *
 * WHAT FIX ROUND H LEFT. Where the .TRG builds no water, the level's own face
 * is drawn on top of the HD room by the classic path, with its own texinfo -
 * and therefore its own animated texture range, its own light and the classic
 * shimmer. That is right about the geometry and about the motion, and it puts
 * a 256-pixel classic tile in the middle of a room drawn from 512-pixel HD
 * pages. The player found the missing half himself: the remaster ships HD
 * pictures of those very animation frames in its TEX folder.
 *
 * MEASURED, over the fifteen levels (RoomProbe -watersurvey, whose
 * `WaterFrames` is the offline twin of this file):
 *
 *     ANDREA1   1 range,  8 frames -> 171..177 and 236
 *     ANDREA2   1 range,  8 frames -> 171..177 and 281
 *     ANDY1     1 range,  8 frames -> 764, 765, 785..787, 790..792
 *     ANDY3     5 ranges, 33 frames -> 897, 898, 901, 903..905, 915, 916, 935
 *     JOBY2     1 range,  8 frames -> 503, 515..521
 *     JOBY4     1 range,  8 frames -> the same eight
 *     JOBY5     1 range,  8 frames -> the same eight
 *     the other seven levels: no restored water face is in an animated range
 *
 * and every one of those pages is in the TEX folder - `missing=0` in all
 * seven. The player's own list of what he found there is the same list, minus
 * the three eighth frames (236, 281, 935) and plus ANDREA's 149..153 and 170,
 * which are on tiles no water face names - ANDREA1's .TEX puts 149..153 on
 * five tiles and 170 on three, and not one of the eight is in the range the
 * fountain's own texinfo belongs to (its frames are tiles 2417..2423 and
 * 2665, which carry 171..177 and 236).
 *
 * HOW THE CLASSIC ANIMATION WORKS, because the substitution has to follow it:
 * S_AnimateTextures (specific\output.cpp) does not change which texinfo a
 * face names. Every six game steps it ROTATES THE CONTENTS of the texinfos in
 * a range - textinfo[range[0]] = textinfo[range[1]] all the way along and the
 * first one round to the end. So the face keeps its number and the TILE it
 * shows walks along the range. What identifies the tile that is on screen
 * right now is therefore the CONTENT of its texinfo - its page and its own uv
 * corner - and that is what is matched below. The values are copied whole by
 * that rotation, so the comparison is on the same bits that were written at
 * load time; nothing is recomputed and nothing rounds.
 *
 * WHAT AN HD PAGE IS: not a repaint of the tile but of the whole 64 by 64
 * CELL the tile sits in, at eight times the size (hd_scan.h has the
 * measurement). So the classic u of 0..1 over a 256 page becomes u * 4 minus
 * the cell's own column, and the tile lands where it belongs inside the 512
 * page. A tile that reaches ACROSS a cell edge would land outside it, so
 * those are dropped and keep the classic picture - 441 tiles in TR4 and TR5
 * are of that kind and none of them turned up in a water range here.
 *
 * HOW IT IS DRAWN: through the classic path exactly as before. The face keeps
 * its light, its shimmer, its sort and its blend; the only thing that changes
 * is which texture the bucket binds and the four coordinates on it. The HD
 * page is borrowed into the classic Textures[] array ABOVE nTextures - never
 * by raising that count, because the font, the sky and the bar sprites are
 * addressed as nTextures minus a small number and every one of them would
 * move (specific\specificfx.cpp says so in the same words) - and the
 * reference is given back at the next level build, by POINTER and not by slot
 * number, because the array has been refilled for another level by then
 * (ReleaseWaterFramePages says how, and why the difference matters).
 *
 * Nothing here runs with hd_enable off: the water face list is empty then,
 * and this function returns at its first line. */
struct HDWaterFrame
{
	unsigned short tile;		// the classic texinfo the frame lives in
	unsigned short tpage;		// its page, as textinfo carries it
	float u, v;			// its own uv corner - how the tile is known
	short page;			// the borrowed entry in Textures[], or -1
	unsigned char cx, cy;		// the 64 x 64 cell it sits in
};

// The biggest .TEX table in the game is 3,007 entries (JOBY5); this is the
// ceiling the reader is given and it is not a limit anything reaches.
#define HD_TEX_MAX_TILES 8192

/* ONE BORROWED PAGE: the slot in Textures[] and the reference that was put
 * into it. Both, and not the slot alone - see ReleaseWaterFramePages. */
struct HDBorrowedPage
{
	int at;				// the entry in Textures[]
	IDirect3DTexture9* tex;		// the reference this code AddRef'd
};

static std::vector<HDWaterFrame> g_water_frames;
static std::vector<HDBorrowedPage> g_water_borrowed;

/* Gives the borrowed pages back. On the main thread only: it releases
 * Direct3D objects, so it belongs beside HDObjectRelease and never in
 * HDLevelUnload - see the note there.
 *
 * AND THE SLOT NUMBER ALONE IS NOT ENOUGH TO GIVE ONE BACK (#123, fix round
 * N, the audit of that round). The pages are borrowed at the CURRENT level's
 * nTextures, and by the time this runs the level has changed under them:
 * FreeLevel calls FreeTextures, which walks 0..nTextures-1 of the OLD level
 * (specific\texture.cpp) and so does not touch a slot above it; then
 * nTextures goes back to 1 and LoadTextures fills Textures[1..] for the NEW
 * level (specific\file.cpp). Where the new level has more pages than the old
 * one, the slots this list names are by then the NEW level's own classic
 * pages - measured, JOBY4 has 13 and borrows 13..20 while JOBY5 has 19 and
 * puts its own pages in 1..18, so six of them would be released and NULLed
 * the moment the next level is built; and with the randomizer permuting the
 * order, every such pairing is reachable.
 *
 * So the reference is given back by POINTER, which is always ours, and the
 * slot is cleared only while it still holds that same pointer. A slot the
 * next level has taken over is left exactly as that level wrote it. */
static void ReleaseWaterFramePages()
{
	int given = 0;
	int taken = 0;

	for (size_t i = 0; i < g_water_borrowed.size(); i++)
	{
		const int at = g_water_borrowed[i].at;
		IDirect3DTexture9* tex = g_water_borrowed[i].tex;

		// The slot first, and only while it is still the one we wrote - the
		// comparison happens before the Release so that it is never made
		// against a pointer whose object has just gone.
		if (at > 0 && at < 256)
		{
			if (Textures[at].tex == tex)
			{
				Textures[at].tex = 0;
				Textures[at].width = 0;
				Textures[at].height = 0;
				Textures[at].bump = 0;
				Textures[at].bumptpage = 0;
			}
			else
				taken++;
		}

		// The reference then, and unconditionally: it is ours whatever has
		// happened to the array in the meantime.
		if (tex)
		{
			tex->Release();
			given++;
		}
	}

	if (!g_water_borrowed.empty())
		Log("HDLOG water frame pages given back=%d of %d, %d slot(s) had "
			"already been taken over by the next level and were left alone "
			"(nTextures=%d)", given, (int)g_water_borrowed.size(), taken,
			(int)nTextures);

	g_water_borrowed.clear();
	g_water_frames.clear();
}

static void BuildWaterFrames()
{
	g_water_frames.clear();

	if (!g_water_face_total || !aranges || !textinfo)
		return;

	/* 1. THE TEXINFOS THE RESTORED FACES NAME. The same walk InsertRoomWater
	 * makes, over the same faces, so a face that is not drawn cannot ask for
	 * a page. */
	std::vector<int> want;

	for (int r = 0; r < number_rooms && r < HD_MAX_ROOMS; r++)
	{
		ROOM_INFO* room_ptr = &room[r];

		for (size_t f = 0; f < g_water_faces[r].size(); f++)
		{
			const HDRoomWaterFace& entry = g_water_faces[r][f];

			if (entry.roomlet < 0 || entry.roomlet >= room_ptr->nRoomlets)
				continue;

			ROOMLET* roomlet = &room_ptr->pRoomlets[entry.roomlet];

			if (!roomlet->pFac)
				continue;

			const short* ptr = entry.quad
				? roomlet->pFac + entry.face * 6
				: roomlet->pFac + roomlet->nQuad * 6 + entry.face * 5;
			const int tile = (entry.quad ? ptr[4] : ptr[3]) & 0x3FFF;
			bool seen = false;

			for (size_t i = 0; i < want.size(); i++)
				if (want[i] == tile)
					seen = true;

			if (!seen)
				want.push_back(tile);
		}
	}

	if (want.empty())
		return;

	/* 2. THE .TEX TABLE. A randomized level carries more texinfos than the
	 * table has entries - the randomizer only ever APPENDS them, which is
	 * measured: JOBY2 vanilla has 2,820 texinfos and its table 2,820, and
	 * the same level out of a run has 4,998 with the first 2,820 unmoved. So
	 * a tile past the end of the table simply has no HD picture. */
	std::vector<unsigned short> hd((size_t)HD_TEX_MAX_TILES, (unsigned short)0xFFFF);
	const int got = HDScanLevelTextures(HDInstallRoot(), g_stem, &hd[0],
		HD_TEX_MAX_TILES);

	if (!got)
	{
		Log("HDLOG level=%s water ranges=0 frames=0 hd pages=none missing=0 "
			"(no .TEX table)", g_stem);
		return;
	}

	/* 3. THE RANGES THAT CARRY ONE OF THOSE TEXINFOS. The walk is
	 * S_AnimateTextures' own: one short with the range count, then per range
	 * one short with the number of frames MINUS ONE and that many plus one
	 * texinfo numbers.
	 *
	 * The UV-ROTATE ranges are left out. Those are the level's waterfalls,
	 * and they do not rotate their contents at all - the engine scrolls
	 * their v instead, once per frame (S_AnimateTextures again), so the tile
	 * in such a texinfo stays put while its v walks out of the cell the HD
	 * page paints. The test is the engine's own expression, gfUVRotate and
	 * the range's own place in the list. */
	const int ranges = (int)*aranges;
	const short* range = aranges + 1;
	int water_ranges = 0, frames = 0, missing = 0, crossing = 0, uvskipped = 0;
	std::vector<int> pages;

	for (int i = 0; i < ranges; i++)
	{
		const int count = (int)*range++ + 1;	// the file counts n - 1
		const short* tiles = range;

		range += count;

		if (i < nAnimUVRanges && gfUVRotate)
		{
			uvskipped++;
			continue;
		}

		bool wanted = false;

		for (int f = 0; f < count && !wanted; f++)
			for (size_t w = 0; w < want.size(); w++)
				if (want[w] == (int)tiles[f])
				{
					wanted = true;
					break;
				}

		if (!wanted)
			continue;

		water_ranges++;

		for (int f = 0; f < count; f++)
		{
			const int tile = (int)tiles[f];

			if (tile < 0 || tile >= got)
				continue;

			frames++;

			const unsigned short page = hd[(size_t)tile];

			if (page == 0xFFFF)
			{
				missing++;
				continue;
			}

			const TEXTURESTRUCT* tex = &textinfo[tile];
			/* The cell of the tile, and the guard that it lies wholly
			 * inside it: u is 0..1 over the classic page, which is four
			 * cells across, and the corner that reaches furthest decides.
			 *
			 * THREE CORNERS FOR A TRIANGLE. Bit 15 of the flag word says
			 * triangle, and then the fourth pair is not a corner at all -
			 * AdjustUV adjusts three of them and the draw reads three
			 * (specific\file.cpp). Measured with the offline twin: counting
			 * the fourth turns 127 of JOBY2's 382 tiles into cell crossers
			 * that are nothing of the kind, and 27 remain, which is the
			 * order TRForge measured over the two games. */
			const int corners = (tex->flag & 0x8000) ? 2 : 3;
			float u0 = tex->u1, u1 = tex->u1, v0 = tex->v1, v1 = tex->v1;
			const float us[3] = { tex->u2, tex->u3, tex->u4 };
			const float vs[3] = { tex->v2, tex->v3, tex->v4 };

			for (int c = 0; c < corners; c++)
			{
				if (us[c] < u0) u0 = us[c];
				if (us[c] > u1) u1 = us[c];
				if (vs[c] < v0) v0 = vs[c];
				if (vs[c] > v1) v1 = vs[c];
			}

			const int cx = (int)(u0 * 4.0f);
			const int cy = (int)(v0 * 4.0f);

			if (cx < 0 || cx > 3 || cy < 0 || cy > 3 ||
				(int)(u1 * 4.0f - 0.0001f) != cx ||
				(int)(v1 * 4.0f - 0.0001f) != cy)
			{
				crossing++;
				continue;
			}

			HDWaterFrame one;

			one.tile = (unsigned short)tile;
			one.tpage = tex->tpage;
			one.u = tex->u1;
			one.v = tex->v1;
			one.page = -1;
			one.cx = (unsigned char)cx;
			one.cy = (unsigned char)cy;
			g_water_frames.push_back(one);

			bool seen = false;

			for (size_t at = 0; at < pages.size(); at++)
				if (pages[at] == (int)page)
					seen = true;

			if (!seen)
				pages.push_back((int)page);
		}
	}

	// The page list as one string for the log. A fixed buffer: the busiest
	// level in the game names nine pages.
	char list[256];
	int wrote = 0;

	list[0] = 0;

	for (size_t i = 0; i < pages.size() && wrote < (int)sizeof(list) - 12; i++)
		wrote += sprintf(list + wrote, "%s%d", i ? "," : "", pages[i]);

	Log("HDLOG level=%s water ranges=%d frames=%d hd pages=%s missing=%d "
		"crossing=%d uv_ranges_skipped=%d", g_stem, water_ranges, frames,
		list[0] ? list : "none", missing, crossing, uvskipped);

	if (g_water_frames.empty() || pages.empty())
	{
		g_water_frames.clear();
		return;
	}

	/* 4. THE PAGES THEMSELVES, appended to the cache the rooms filled - one
	 * texture per page for the whole level, exactly as the models are added
	 * (stage S3-1). */
	HDPageCacheStats stats;
	const char* why = "";

	if (!HDPageCacheAppend(App.dx.lpD3DDevice, HDInstallRoot(), &pages[0],
		(int)pages.size(), false, DecodeThreads(), &stats, &why))
	{
		Log("HDLOG level=%s water frames refused reason=%s", g_stem, why);
		g_water_frames.clear();
		return;
	}

	/* 5. AND BORROWED INTO THE CLASSIC TEXTURE ARRAY, above nTextures. The
	 * count is NOT raised: game\text.cpp reads the font as nTextures - 2,
	 * drawbars.cpp the sky as nTextures - 1 and specificfx.cpp the menu
	 * pages as nTextures - 4 and - 5, and every one of them would move.
	 * Textures[] holds 256 entries; measured from the .TRC headers, a VANILLA
	 * level uses 10 to 22 of them and a RANDOMIZED one - the randomizer
	 * appends pages - 10 to 102, the worst being ANDY1 out of a run. So the
	 * eight or nine pages of a water animation fit with room to spare either
	 * way, and where they would not, the water keeps its classic tile. */
	std::vector<int> slot_of_page(pages.size(), -1);
	int at = nTextures;

	for (size_t i = 0; i < pages.size(); i++)
	{
		const int cache = HDPageCacheSlotOf(pages[i]);
		IDirect3DTexture9* tex = cache >= 0 ? HDPageCacheSlotTexture(cache) : 0;

		if (!tex || at >= 256)
		{
			Log("HDLOG level=%s water frame page %d has no texture (or no "
				"room in Textures[]) - those frames stay classic",
				g_stem, pages[i]);
			continue;
		}

		D3DSURFACE_DESC desc;
		const bool described = SUCCEEDED(tex->GetLevelDesc(0, &desc));

		tex->AddRef();
		Textures[at].tex = tex;
		Textures[at].width = described ? desc.Width : 512;
		Textures[at].height = described ? desc.Height : 512;
		Textures[at].bump = 0;
		Textures[at].bumptpage = 0;
		Textures[at].xoff = 0;
		Textures[at].yoff = 0;
		Textures[at].tpage = at;

		HDBorrowedPage lent;

		lent.at = at;
		lent.tex = tex;
		g_water_borrowed.push_back(lent);
		slot_of_page[i] = at;
		at++;
	}

	// 6. every frame gets the entry its page landed in.
	for (size_t f = 0; f < g_water_frames.size(); f++)
	{
		const int tile = (int)g_water_frames[f].tile;
		const unsigned short page = hd[(size_t)tile];

		for (size_t i = 0; i < pages.size(); i++)
			if (pages[i] == (int)page)
			{
				g_water_frames[f].page = (short)slot_of_page[i];
				break;
			}
	}

	int drawn = 0;

	for (size_t f = 0; f < g_water_frames.size(); f++)
		if (g_water_frames[f].page >= 0)
			drawn++;

	Log("HDLOG level=%s water frames in HD=%d of %d, %d page(s) borrowed at "
		"Textures[%d..%d] (nTextures=%d)", g_stem, drawn,
		(int)g_water_frames.size(), (int)g_water_borrowed.size(),
		g_water_borrowed.empty() ? 0 : g_water_borrowed.front().at,
		g_water_borrowed.empty() ? 0 : g_water_borrowed.back().at,
		(int)nTextures);
}

/* THE HD FRAME OF ONE WATER FACE, or false for "draw it classically".
 *
 * `tex` is the texinfo the face names, with whatever content the classic
 * rotation has left in it this moment; `out` comes back as the same record
 * with the HD page and the coordinates on it. Called once per drawn water
 * face from InsertRoomWater, inside the HD branch and nowhere else - with
 * hd_enable off there are no water faces and this list is empty. */
bool HDWaterFrameFor(const TEXTURESTRUCT* tex, TEXTURESTRUCT* out)
{
	if (!tex || !out || g_water_frames.empty())
		return false;

	for (size_t i = 0; i < g_water_frames.size(); i++)
	{
		const HDWaterFrame& f = g_water_frames[i];

		if (f.page < 0 || f.tpage != tex->tpage ||
			f.u != tex->u1 || f.v != tex->v1)
			continue;

		const float cx = (float)f.cx;
		const float cy = (float)f.cy;

		*out = *tex;
		out->tpage = (unsigned short)f.page;
		out->u1 = tex->u1 * 4.0f - cx;
		out->v1 = tex->v1 * 4.0f - cy;
		out->u2 = tex->u2 * 4.0f - cx;
		out->v2 = tex->v2 * 4.0f - cy;
		out->u3 = tex->u3 * 4.0f - cx;
		out->v3 = tex->v3 * 4.0f - cy;
		out->u4 = tex->u4 * 4.0f - cx;
		out->v4 = tex->v4 * 4.0f - cy;
		return true;
	}

	return false;
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

/* WHOSE ART IS STANDING INSIDE WHOSE ROOM (#123, fix round T, T1; rewritten
 * in fix round T2, T2-2).
 *
 * WHAT THE WINDOW IS FOR, WHICH HAS NOT CHANGED. Rooms that stand INSIDE one
 * another. ANDREA1's 53 (an air shaft) has most of its art inside room 207,
 * with no portal between the two; from inside 207 that art comes through the
 * wall, and the depth buffer has nothing to say about it because both rooms
 * really are at that distance. The portal window - the rectangle the classic
 * walk left in `r->left .. r->bottom`, used as a scissor - is what hides it.
 *
 * WHAT GROUP M GOT WRONG: it gave that window to EVERY HD room. A scissor is
 * strictly stricter than the classic clip it was copied from - the classic
 * path drops a polygon only when EVERY corner of it lies beyond one edge of
 * the window (specific\polyinsert.cpp:1213) and otherwise draws it whole,
 * while a scissor cuts it at the pixel - so the remaster's art near a doorway
 * came out sliced. The play test photographed a vertical strip of sky down
 * the edge of a brick wall at the Piazza dell'Opera.
 *
 * WHAT FIX ROUND T GOT HALF RIGHT: it measured which pairs of rooms really do
 * stand inside one another and left every other room alone. But it gave the
 * window to BOTH partners of a pair, and to the whole of each partner's art.
 * The play test of 2026-09-08 photographed what THAT costs: in ANDREA2's
 * courtyard rooms 134 (the ground, with the palm and the doorways) and 203
 * (the colonnade above it) are both HOLDERS - 134 holds art of 143, 203 holds
 * art of 248 - so both were scissored, and their two rectangles meet along
 * the courtyard's own room boundary. A BLACK BAND straight across the
 * picture, cutting the palm's trunk and the door lintels in half.
 *
 * THE RULE NOW, AND IT IS A RULE ABOUT TRIANGLES:
 *
 *   a triangle of room b is WINDOWED when its centroid stands inside the
 *   inside of some other room a - and nothing else in the game ever is.
 *
 * The holder gets no window (nothing of ITS art is out of place). The
 * intruder gets one only for the triangles that are really standing in the
 * other room; the rest of its art, and all of its overhang, draws over the
 * whole screen like everything else.
 *
 * WHAT COUNTS AS "INSIDE ROOM a", and every part of it was measured before it
 * was written:
 *
 *   - the box is TR5's own (game\draw.cpp:2248-2253, the lines that ask
 *     whether an item fits wholly inside its room): x from r->x + 1024 over
 *     y_size sectors, z from r->z + 1024 over x_size, y from maxceiling to
 *     minfloor.
 *   - AND ONE SECTOR FURTHER IN. A room's border ring is one sector thick, so
 *     two rooms that merely share a wall reach into each other by
 *     construction - ANDREA1's 57 and 197 at the Piazza corner do exactly
 *     that with 60 cells of wall art. Round T measured the difference: with
 *     the wall's own sector counted ANDREA1 has 78 overlapping pairs, without
 *     it one.
 *   - not a neighbour. A room seen through a doorway reaches into the room it
 *     opens on, and that art is seen through the opening it belongs to. The
 *     portal list and the floor data's vertical links say who is next door.
 *   - not a flipmap twin, which is the same room at two moments.
 *   - and the CENTROID, not a cell. Round T asked the question of the group's
 *     256-unit occupancy cells, and that is where the engine and TrgProbe
 *     disagreed on the play test's own log: the engine counted a cell that
 *     merely TOUCHED the box (`p + 255 < mins` is the reject in
 *     HDRenderGroupCellsIn) while the tool counted a cell whose own corner
 *     lay inside it. One 256-unit ring of difference, and in ANDREA1's air
 *     shaft it is 91 cells against 45 and four pairs against one. A centroid
 *     is a point; the two agree on it exactly.
 *
 * NO COUNT THRESHOLD, unlike round T's sixteen cells. That floor existed
 * because the answer was all-or-nothing for a whole room; a single stray
 * triangle must not scissor a facade. Now the answer IS the triangle, so a
 * stray one costs a stray one.
 *
 * IT RUNS BEFORE THE BUILD, beside BuildRoomBoxes, because the renderer tags
 * the triangles as it fills the index buffer. Everything it reads is the
 * classic level - room boxes, portals, floor links - so TrgProbe -roomspace
 * answers with the very same list out of the .TRC and the .TRG. */
static std::vector<HDIntrusion> g_intrusions;

static void BuildRoomIntrusions()
{
	g_intrusions.clear();

	/* Who is next door: the portal list of both rooms, and the vertical
	 * links, which have no portal record at all - a stacked pair is joined
	 * through FLOOR_INFO::pit_room and sky_room. */
	static std::vector<unsigned char> next_door;

	next_door.assign((size_t)number_rooms * (size_t)number_rooms, 0);

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		short* door = info->door;

		if (door)
		{
			const int count = *door++;

			for (int i = 0; i < count; i++, door += 16)
				if (door[0] >= 0 && door[0] < number_rooms)
				{
					next_door[(size_t)r * number_rooms + door[0]] = 1;
					next_door[(size_t)door[0] * number_rooms + r] = 1;
				}
		}

		if (!info->floor)
			continue;

		const int squares = info->x_size * info->y_size;

		for (int i = 0; i < squares; i++)
		{
			const int up = info->floor[i].sky_room;
			const int down = info->floor[i].pit_room;

			if (up != 255 && up < number_rooms)
			{
				next_door[(size_t)r * number_rooms + up] = 1;
				next_door[(size_t)up * number_rooms + r] = 1;
			}

			if (down != 255 && down < number_rooms)
			{
				next_door[(size_t)r * number_rooms + down] = 1;
				next_door[(size_t)down * number_rooms + r] = 1;
			}
		}
	}

	for (int a = 0; a < number_rooms; a++)
	{
		const ROOM_INFO* ra = &room[a];

		for (int b = 0; b < number_rooms; b++)
		{
			if (b == a)
				continue;

			if (next_door[(size_t)a * number_rooms + b])
				continue;

			if (ra->flipped_room == b || room[b].flipped_room == a)
				continue;

			// The boxes have to touch before the art can be asked about.
			if (ra->x + (ra->y_size << 10) <= room[b].x ||
				room[b].x + (room[b].y_size << 10) <= ra->x ||
				ra->z + (ra->x_size << 10) <= room[b].z ||
				room[b].z + (room[b].x_size << 10) <= ra->z ||
				ra->minfloor <= room[b].maxceiling ||
				room[b].minfloor <= ra->maxceiling)
				continue;

			/* a's INSIDE, one sector in from its border ring, put into b's
			 * frame - the .TRG's vertices are room local and the two rooms
			 * have different origins. y is world units in both.
			 *
			 * Group b is room b: this runs at level build, before any flip
			 * has swapped a pair of ROOM_INFO records, which is the same
			 * argument BuildStaticTables makes for its own query. */
			HDIntrusion box;

			box.group = b;
			box.holder = a;
			box.mins[0] = (float)(ra->x + 2048 - room[b].x);
			box.maxs[0] = (float)(ra->x + ((ra->y_size - 2) << 10) - room[b].x);
			box.mins[1] = (float)(ra->maxceiling - room[b].y);
			box.maxs[1] = (float)(ra->minfloor - room[b].y);
			box.mins[2] = (float)(ra->z + 2048 - room[b].z);
			box.maxs[2] = (float)(ra->z + ((ra->x_size - 2) << 10) - room[b].z);

			// A room too small to have an inside past its own wall ring.
			if (box.maxs[0] <= box.mins[0] || box.maxs[2] <= box.mins[2])
				continue;

			g_intrusions.push_back(box);
		}
	}

	if (!g_intrusions.empty())
		HDRenderSetIntrusions(&g_intrusions[0], (int)g_intrusions.size());
	else
		HDRenderSetIntrusions(0, 0);
}

/* AND WHAT THE BUILD MADE OF IT (#123, fix round T2, T2-2). Run after
 * HDRenderLevelBegin, because the triangles are tagged while the index buffer
 * is filled and the counts do not exist before that.
 *
 * THE FLAG IS PER GROUP AND IS ASKED OF THE GROUP. Round T kept it per ROOM
 * and HDFrameRoom read it by room index - but HDFrameFlip swaps which group
 * draws which room, so after a flipmap a windowed room's art could be drawn
 * with no window at all and its twin's art cut to a rectangle that was never
 * measured for it. The art carries the property, like g_group_hd. */
static void ReportRoomIntrusions()
{
	int pairs = 0;
	int tris = 0;
	int groups = 0;

	memset(g_group_windowed, 0, sizeof(g_group_windowed));

	for (size_t i = 0; i < g_intrusions.size(); i++)
	{
		const int hits = HDRenderIntrusionHits((int)i);

		if (!hits)
			continue;

		pairs++;

		if (gLogEnabled)
			Log("HDLOG overlap room=%d holds the art of room=%d - "
				"%d triangles, no portal between them",
				g_intrusions[i].holder, g_intrusions[i].group, hits);
	}

	for (int g = 0; g < HDRenderGroupCount() && g < HD_MAX_ROOMS; g++)
	{
		const int here = HDRenderGroupIntrudingTriangles(g);

		if (!here)
			continue;

		g_group_windowed[g] = 1;
		groups++;
		tris += here;
	}

	Log("HDLOG level=%s overlap pairs=%d intruding tris=%d in %d groups",
		g_stem, pairs, tris, groups);

	// The list was only borrowed for the length of the build.
	HDRenderSetIntrusions(0, 0);
	g_intrusions.clear();
}

/* THE OPENINGS THE RUN MADE, FOR THE RENDERER (#123, fix round Z).
 *
 * The sidecar names them in WORLD units, one box per opening, and says
 * nothing about rooms - deliberately, because the art that shuts a doorway is
 * not always the art of the two rooms it joins. Fix round T measured that the
 * remaster's rooms reach into each other by construction: a wall's relief, a
 * corner pier, a doorway's reveal all stand in the neighbour's space, and
 * ANDREA2's courtyard has whole facades of one room drawn inside another. So
 * EVERY group is asked about EVERY box, and the triangle test decides.
 *
 * One entry per (group, box) pair, in the group's own frame: the .TRG's
 * vertices are room local in x and z and world in y, so the room's origin
 * comes off the two horizontal numbers and room[].y off the vertical one -
 * the same three lines BuildRoomBoxes and BuildRoomIntrusions use, and every
 * TR5 room has y 0 anyway (measured over the fifteen files).
 *
 * No pre-filter by the room's own box, and that is on purpose. A filter is a
 * second rule to get wrong, it would have to guess how far a group's art may
 * stand outside its room, and it would buy nothing measurable: the list is
 * rooms x boxes entries of 32 bytes - ANDY3 with the user's glitchless list
 * is 211 x 2 - and the triangle test runs once per triangle per box at level
 * build, never per frame.
 *
 * Group i is room i while this runs: no flip has happened yet, which is the
 * same argument BuildRoomIntrusions and BuildStaticTables make. */
static std::vector<HDCut> g_cuts;

static void BuildRoomCuts()
{
	g_cuts.clear();

	const int boxes = HDMarkersCutCount();

	if (boxes > 0)
	{
		g_cuts.reserve((size_t)boxes * (size_t)number_rooms);

		for (int b = 0; b < boxes; b++)
		{
			const HDMarkerCut* src = HDMarkersCut(b);

			if (!src)
				continue;

			for (int r = 0; r < number_rooms; r++)
			{
				HDCut one;

				one.group = r;
				one.box = b;
				one.mins[0] = (float)(src->mins[0] - room[r].x);
				one.maxs[0] = (float)(src->maxs[0] - room[r].x);
				one.mins[1] = (float)(src->mins[1] - room[r].y);
				one.maxs[1] = (float)(src->maxs[1] - room[r].y);
				one.mins[2] = (float)(src->mins[2] - room[r].z);
				one.maxs[2] = (float)(src->maxs[2] - room[r].z);
				g_cuts.push_back(one);
			}
		}
	}

	if (!g_cuts.empty())
		HDRenderSetCuts(&g_cuts[0], (int)g_cuts.size());
	else
		HDRenderSetCuts(0, 0);
}

/* AND WHAT THE BUILD MADE OF IT (#123, fix round Z). Run after
 * HDRenderLevelBegin for the same reason ReportRoomIntrusions is: the
 * triangles are tested while the index buffer is filled and the counts do not
 * exist before that.
 *
 * ONE LINE PER BOX, naming the rooms whose art it took and how much of it,
 * and one line for the level. A box that caught nothing gets a line too -
 * that is the failure this round has to be able to see, an opening the
 * remaster's art never covered or a box in the wrong place, and a silent
 * zero would look exactly like a doorway that was already open. */
static void ReportRoomCuts()
{
	const int boxes = HDMarkersCutCount();
	int caught = 0;

	for (int b = 0; b < boxes; b++)
	{
		const HDMarkerCut* src = HDMarkersCut(b);
		int tris = 0;
		int groups = 0;
		char rooms[160];
		int at = 0;

		rooms[0] = 0;

		for (size_t i = 0; i < g_cuts.size(); i++)
		{
			if (g_cuts[i].box != b)
				continue;

			const int hits = HDRenderCutHits((int)i);

			if (!hits)
				continue;

			tris += hits;
			groups++;

			if (at < (int)sizeof(rooms) - 12)
				at += snprintf(rooms + at, sizeof(rooms) - (size_t)at,
					"%s%d(%d)", at ? " " : "", g_cuts[i].group, hits);
		}

		caught += tris;

		Log("HDLOG level=%s cut %d x %d..%d y %d..%d z %d..%d - "
			"%d triangles out of %d rooms: %s",
			g_stem, b,
			src ? src->mins[0] : 0, src ? src->maxs[0] : 0,
			src ? src->mins[1] : 0, src ? src->maxs[1] : 0,
			src ? src->mins[2] : 0, src ? src->maxs[2] : 0,
			tris, groups, at ? rooms : "none - the remaster drew nothing there");
	}

	if (boxes > 0)
		Log("HDLOG level=%s cuts=%d triangles dropped=%d", g_stem, boxes, caught);

	// The list was only borrowed for the length of the build.
	HDRenderSetCuts(0, 0);
	g_cuts.clear();
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

			/* 1. Does the art really COVER this static, or does its box
			 * merely catch a corner of something else?
			 *
			 * FIX ROUND T (T4). Round R3b asked "is there any HD geometry
			 * here at all", and that turned out to be too weak a reason to
			 * take a static away. ANDREA2's courtyard palm is TWO statics -
			 * the trunk (slot 8) in the ground room and the crown (slot 9)
			 * in the room above - and the crown's box catches FOUR cells of
			 * 768, which is the wall behind it and not a palm: the crown was
			 * suppressed and the .TRG had nothing to put in its place, so
			 * the play test saw a trunk with no fronds. The same level's
			 * room 162 crown catches 380 of 768 and IS in the art.
			 *
			 * So the question is how MUCH, against the static's own box.
			 * Measured over the fifteen files and all 2,601 statics, the
			 * coverage falls into 271 at nothing at all, 44 between nothing
			 * and a twentieth, and 2,286 above a twentieth - a gap the
			 * threshold sits in rather than a line drawn through a crowd.
			 * The 44 go back to being drawn the classic way. */
			const int cells = HDRenderGroupCellsIn(r, mins, maxs, 0);
			int box_cells = 1;

			for (int axis = 0; axis < 3; axis++)
			{
				const int lo = (int)mins[axis] >> 8;
				const int hi = (int)maxs[axis] >> 8;

				box_cells *= (hi - lo) + 1;
			}

			if (cells * 20 < box_cells)
			{
				g_static_classic[(size_t)(g_static_first[(size_t)r] + i)] = 1;
				orphans++;

				if (gLogEnabled)
					Log("HDLOG static room=%d mesh=%d slot=%d classic "
						"cells=%d of %d box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f",
						r, i, (int)mesh->static_number, cells, box_cells,
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

	/* THE SIDECAR IS READ BEFORE THE HD GATE (#153, round M3, the fix round),
	 * because one of its lines is not an HD fact at all.
	 *
	 * `sfximport` names the sound ids the run imported from another game, and
	 * the thing that acts on it is the HQ bank bridge - which is deliberately
	 * OUTSIDE this gate (round M1: "sound does not care whether hd_enable is
	 * on", specific\hqsfx.cpp). With the load below the gate the refusal was
	 * inert in exactly the build the engine ships, hd_enable off, and the
	 * vulture went on dying in English there while every tool said it was
	 * fixed - tools call HDMarkersLoad unconditionally and could not see it.
	 *
	 * Reading it here costs one small text file and changes nothing else: the
	 * rooms, the cuts and the donors below are read by HD code alone, and
	 * HDMarkersFree is in HDLevelUnload, which is not gated either. The load
	 * thread runs this before LoadSamples calls HQSfxBuildMap (specific
	 * \file.cpp, LoadLevel), so the list is there when the bridge is built. */
	const bool markers = HDMarkersLoad(wadName);

	if (!HDEnabled())
	{
		if (HDMarkersSoundImportCount())
			Log("HDLOG level=%s markers file=%s: %d imported sound id%s, read "
				"with the HD path off because the sound bridge needs them",
				g_log_stem, HDMarkersPath(), HDMarkersSoundImportCount(),
				HDMarkersSoundImportCount() == 1 ? "" : "s");

		return;
	}

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
	 * an unreadable one falls back to the same answer and says so. It has
	 * already been read, above the gate - see the note there. */
	Log("HDLOG level=%s markers file=%s version=%d rooms=%d cuts=%d donors=%d "
		"outfit=%d tr3_install=%s%s%s",
		g_stem, markers ? HDMarkersPath() : "none", HDMarkersVersion(),
		HDMarkersRoomCount(), HDMarkersCutCount(), HDMarkersDonorCount(),
		HDMarkersOutfit(), HDInstallRootTr3()[0] ? "found" : "absent",
		markers ? "" : " reason=", markers ? "" : HDMarkersError());

	/* data[3] and the sidecar's `outfit` line come out of ONE value in the
	 * randomizer and can only disagree when one of the two halves was written
	 * by an older run. The header byte wins - it is the one the draw path
	 * reads, and it is there before a room is loaded - so this is a line in
	 * the log and nothing else. */
	if (markers && HDMarkersOutfit() != (int)GfHdOutfit)
		Log("HDLOG level=%s markers MISMATCH data[3]=%d, the sidecar says %d "
			"- data[3] wins", g_stem, (int)GfHdOutfit, HDMarkersOutfit());

	/* Round M2 briefly had a second way for a level file to name an outfit -
	 * a `wear <key>` line of the sidecar, rolled by the randomizer - and this
	 * is where the key and data[3] were read against each other. It is gone
	 * (the fix round of M2): the extra outfits of the wardrobe scan are the
	 * PLAYER'S choice and not the run's, so the key lives in the registry and
	 * hd_laradraw.cpp is the only thing that reads it. A sidecar the M2 build
	 * wrote still carries the line; it now reaches the log through the
	 * unknown-line loop below, which is what an engine older than M2 always
	 * did with it. */

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

static void HDLevelBuildHD();

/* THE LEVEL'S HD BUILD, AND THE ONE THING THAT IS NOT PART OF IT (#153, round
 * M3, the review after the fix round).
 *
 * The footsteps hang off the HD gate and off nothing else: the bare set is
 * built by HDFeetLevelBuild whenever hd_enable is on - in LoadSamples, on the
 * load thread, long before this - and the body it follows is decided by
 * HDLaraLevelBuild inside this build, on the MAIN thread after file.cpp has
 * joined the load thread. The build below has five ways to give
 * up (no rooms, no device, no page cache, no HD room, no stem), and NONE of
 * them takes the bare set away or takes the F4 row away. Asking the question
 * at the end of that build therefore left a player whose card cannot do shader
 * model 3 - the worst of the five, because it turns the whole session classic
 * while hd_enable stays 1 - with a footstep choice the menu applied inside the
 * level and the next level start silently dropped. It is asked here instead,
 * on every way out of the build and none of them able to skip it.
 *
 * WHAT IT ANSWERS ON THOSE WAYS OUT is the level's own body and not the
 * player's key, because on them there IS no HD body: HDLaraDrawBuild runs
 * inside HDObjectLevelBuild, which is past all five, so HDLaraDrawOutfit()
 * reads HD_LARA_OUTFIT_NONE and the key is empty. She is wearing Core's
 * outfit, Core's outfit has boots, and the line says exactly that. The point
 * of the fix is that the answer is now GIVEN and written down rather than left
 * to whatever the menu happened to set inside the level before. */
void HDLevelBuild()
{
	HDLevelBuildHD();

	/* Behind the gate, like everything else this round added: with hd_enable
	 * off there is no bare set, no wardrobe and nothing to say about either. */
	if (HDEnabled())
		HDOutfitMenuFollowFeet();
}

static void HDLevelBuildHD()
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
	g_water_calls_worst = 0;
	// GROUP M: the per-level window list is written again for this level.
	g_clip_listed = false;
	g_clip_line_ever = false;
	memset(g_group_hd, 0, sizeof(g_group_hd));
	memset(g_group_windowed, 0, sizeof(g_group_windowed));
	g_room_count = 0;
	g_flips_done = 0;
	g_redress_count = 0;
	g_redress_bytes0 = 0;

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

	/* The level before this one, released here rather than in HDLevelUnload -
	 * see the comment there. This is the main thread; that one is not.
	 *
	 * The water frames come first and OUTSIDE the group test: the reference
	 * they hold is on a texture of the cache HDRenderLevelEnd is about to
	 * empty, so it has to be given back before that, and it has to be given
	 * back even if the group list has somehow gone without it. The call is a
	 * no-op when nothing is borrowed. */
	ReleaseWaterFramePages();

	if (HDRenderGroupCount())
	{
		// The models next: their ranges name page slots in that same cache.
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
	BuildRoomBoxes();		// T1, before the build like the marks
	BuildRoomIntrusions();		// T2-2, the same contract
	BuildRoomCuts();		// Z, the same contract again

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

		// Empty here (the frames are built further down), but the rule is
		// "never empty the page cache with a borrowed reference still out".
		ReleaseWaterFramePages();
		HDRenderLevelEnd();
		return;
	}

	const DWORD build_ms = GetTickCount() - started;

	// The marks were only borrowed for the length of that call.
	HDRenderSetWaterMarks(0, 0);
	g_water_marks.clear();
	HDRenderSetRoomBoxes(0, 0);	// T1, the same contract

	/* T2-2: what the build made of the intrusion list - which groups carry
	 * art standing inside another room, and how much. It reads the counts the
	 * build left behind and gives the borrowed list back. */
	ReportRoomIntrusions();
	// Z: and the same for the openings. Before the room lines below, so the
	// log reads in the order the build ran.
	ReportRoomCuts();

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

	/* N1: THE STILL WATER SURFACES the .TRG builds itself - material 16.
	 * Counted per part/pass block, which is how TrgProbe -matparts counts
	 * them, so the two can be held against each other: ANDREA1 1 part /
	 * 1,460 triangles, ANDREA2 1 / 2,048, ANDY2 2 / 2,384, JOBY3 1 / 128,
	 * JOBY4 1 / 73,728 (the sea of Deepsea Dive), JOBY5 22 / 165,930, and
	 * nothing at all in the other nine levels. `sub_rect` is how many of
	 * them live in a sub-rectangle of their page rather than filling it -
	 * the number the ripple's travel is capped against - and `cap` is the
	 * largest travel any part of this level was given, in page units. */
	if (stats.waterParts)
		Log("HDLOG level=%s basin parts=%d (%d additive, %d opaque, %d still), "
			"triangles=%d, sub-rect tiles=%d, smallest UV span=%d/255, "
			"ripple capped at %.4f page units",
			g_stem, stats.waterParts, stats.waterAdditiveParts,
			stats.waterOpaqueParts, stats.waterStillParts, stats.waterTris,
			stats.waterSubRectParts, stats.waterSmallestSpan,
			stats.waterRippleCap);

	g_room_count = number_rooms;

	int marked_rooms = 0;

	for (int r = 0; r < g_room_count; r++)
	{
		const int tris = HDRenderGroupTriangles(r, -1);
		const bool has_group = r < HDRenderGroupCount();
		/* THE FOURTH WAY A ROOM STAYS CLASSIC (#123, fix round T, T2).
		 *
		 * A group can have triangles and still draw NOTHING INSIDE ITS OWN
		 * ROOM. ANDREA2's room 124 - the ground floor of the courtyard with
		 * the palms - has four, and all four of them stand outside the room
		 * box; the test below it (`tris` non-zero) called that room HD, the
		 * classic roomlets were dropped, and what the player saw was a black
		 * hole where the doorways are. NINE rooms in the whole game answer
		 * this way to the rule as it ships - with HD_OVERHANG_SLACK in it:
		 * ANDREA2 124, JOBY2 19 and 21, RICHCUT2 2, 3 and 4, TITLE 70, 101
		 * and 103. (ANDREA1's room 4 is hollow only with the slack taken
		 * away: all 216 of its triangles stand within 128 units of the box
		 * edge, so the shipped rule leaves that room HD - verified with
		 * `RoomProbe -roombox <classic folder> -window inside`, which draws
		 * the nine at 0 draw calls and room 4 at 1.)
		 *
		 * The classic geometry of such a room is the only geometry there
		 * is, so it is drawn, exactly as for a room the .TRG never had. */
		const bool hollow = tris > 0 &&
			HDRenderGroupOverhangTriangles(r) >= tris;

		/* THE THIRD WAY A ROOM STAYS CLASSIC (#123, stage S7-2, narrowed by
		 * fix round Z). The first two are the .TRG's own: no group for this
		 * room, or a group with no triangle in it - which is what a
		 * Make_Room_Geometry room looks like and needs no marker at all.
		 * This one is the randomizer's, and since fix round Z it is the
		 * SMALL half of what the randomizer can do to a room: the run lifted
		 * it with Move_Room, or built it a surface it never had. Neither
		 * leaves anything of the remaster's art worth keeping, so the room
		 * is drawn classic and the classic geometry IS the randomized
		 * geometry - it is read out of the level file the run wrote.
		 *
		 * The big half - a wall cut open, a window added, a floor or ceiling
		 * opened - used to end up here too, and that is what the play test of
		 * 2026-09-11 photographed: ANDY3 with the user's glitchless list
		 * turned FOUR of 211 rooms classic for two doorways, and the seams
		 * between HD and classic floors showed the void. Those rooms now
		 * keep their art and lose only the box round the opening; see
		 * BuildRoomCuts above. */
		const bool marked = HDMarkersRoomTouched(r);
		// Fix round Z: and how much art an opening took out of this room.
		const int cut_tris = HDRenderGroupCutTriangles(r);

		const char* reason = !has_group ? "no_group"
			: (!tris ? "empty_group"
			: (hollow ? "hollow_group"
			: (marked ? "marker" : (cut_tris ? "cut" : "ok"))));

		if (marked && has_group && tris)
			marked_rooms++;

		/* Per GROUP, and the loop runs over rooms only because group i is
		 * room i while this runs - no flip has happened yet, and the marker
		 * that can take a group away is about the ROOM the randomizer
		 * reshaped. A flipped room's alternate is a room of its own with a
		 * group of its own, so both halves of a pair are decided here. */
		g_group_hd[r] = (unsigned char)((has_group && tris && !hollow && !marked) ? 1 : 0);

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

		Log("HDLOG room=%d group=%d tris=%d/%d/%d overhang=%d cut=%d "
			"ranges=%d statics=%d hd=%d reason=%s",
			r, r, HDRenderGroupTriangles(r, 0), HDRenderGroupTriangles(r, 1),
			HDRenderGroupTriangles(r, 2), HDRenderGroupOverhangTriangles(r),
			cut_tris, HDRenderGroupRanges(r, -1),
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
	 * whenever the run reshaped a room the .TRG had nothing for anyway.
	 * `cuts` is fix round Z's half of it - openings, which cost a box of
	 * triangles each and no room at all. */
	if (HDMarkersPresent())
		Log("HDLOG level=%s markers version=%d rooms=%d of them classic now=%d "
			"cuts=%d cut_tris=%d in %d rooms donors=%d outfit=%d",
			g_stem, HDMarkersVersion(), HDMarkersRoomCount(), marked_rooms,
			HDMarkersCutCount(), stats.cutTris, stats.cutGroups,
			HDMarkersDonorCount(), HDMarkersOutfit());

	Log("HDLOG level=%s built ms=%d rooms=%d hd=%d classic=%d sky_groups=%d "
		"verts=%d idx=%d(%dbit) ranges=%d split=%d layer0_tris=%d "
		"overhang_tris=%d hollow=%d "
		"intruding_tris=%d intruding_ranges=%d intruding_groups=%d "
		"cut_tris=%d cut_groups=%d "
		"vb_mib=%.1f ib_mib=%.1f read_ms=%d build_ms=%d",
		g_stem, (int)build_ms, g_room_count, hd_rooms, classic_rooms,
		stats.skyGroups, stats.vertices, stats.indices, stats.index32 ? 32 : 16,
		stats.ranges, stats.splitRanges, stats.fallbackPageTris,
		stats.overhangTris, stats.hollowGroups,
		stats.intrudingTris, stats.intrudingRanges, stats.intrudingGroups,
		stats.cutTris, stats.cutGroups,
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

	/* N2: and the HD pictures of that water's own animation frames. After
	 * BuildWaterSurfaces, because it asks which faces are really drawn. */
	BuildWaterFrames();

	Log("HDLOG level=%s statics suppressed=%d kept=%d (in %d HD rooms, %d classic)",
		g_stem, suppressed_statics, kept_statics, hd_rooms, classic_rooms);

	if (!g_level_active)
	{
		Log("HDLOG level=%s no room draws HD - releasing again", g_stem);
		// The same rule: the page cache is about to go, so anything borrowed
		// out of it goes first. BuildWaterFrames has already run above.
		ReleaseWaterFramePages();
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

	/* THE MARK THE RE-DRESS ALLOWANCE IS MEASURED FROM (#153, round M3):
	 * what this level's own build left in the page cache. Everything above
	 * it is what the player's own outfit changes have added. */
	g_redress_bytes0 = HDPageCacheBytes();
}

bool HDLevelRedressAllowed()
{
	if (!HDEnabled() || !g_level_active)
		return false;

	return HDPageCacheBytes() - g_redress_bytes0 < HD_REDRESS_ALLOWANCE;
}

int HDLevelRedressCount()
{
	return g_redress_count;
}

bool HDLevelRedress(const char* why)
{
	if (!HDEnabled() || !g_level_active || !g_stem[0])
	{
		Log("HDLOG redress refused (%s): no HD level is built here",
			why ? why : "?");
		return false;
	}

	if (!HDLevelRedressAllowed())
	{
		Log("HDLOG redress deferred to the next level (%s): %d change%s "
			"in this level have already added %.1f MiB of pages and the "
			"allowance is %.0f", why ? why : "?", g_redress_count,
			g_redress_count == 1 ? "" : "s",
			(HDPageCacheBytes() - g_redress_bytes0) / 1048576.0,
			HD_REDRESS_ALLOWANCE / 1048576.0);
		return false;
	}

	const DWORD began = GetTickCount();
	const int slots0 = HDPageCacheSlots();
	const double bytes0 = HDPageCacheBytes();

	/* THE SAME TWO CALLS A LEVEL CHANGE MAKES, in the same order and on the
	 * same thread. HDObjectRelease gives the models' vertex and index buffers
	 * back and NOTHING of the page cache - it never shrinks until
	 * HDPageCacheDestroy, so no slot index another model holds can move,
	 * which is what makes this safe; HDObjectLevelBuild reads the .MAP again, asks
	 * hd_laradraw for the wardrobe (which reads the registry key the menu
	 * has just written) and builds the lot. The ROOMS are not touched: their
	 * cache is the one the new pages are appended to, exactly as at a level
	 * build. */
	HDObjectRelease();
	HDObjectLevelBuild(HDInstallRoot(), g_stem);

	g_redress_count++;

	Log("HDLOG redress %d (%s) ms=%d pages %d -> %d (+%d), cache %.1f -> "
		"%.1f MiB (+%.1f), this level has spent %.1f of %.0f MiB",
		g_redress_count, why ? why : "?", (int)(GetTickCount() - began),
		slots0, HDPageCacheSlots(), HDPageCacheSlots() - slots0,
		bytes0 / 1048576.0, HDPageCacheBytes() / 1048576.0,
		(HDPageCacheBytes() - bytes0) / 1048576.0,
		(HDPageCacheBytes() - g_redress_bytes0) / 1048576.0,
		HD_REDRESS_ALLOWANCE / 1048576.0);

	// The new body may walk on something else than the old one did.
	HDOutfitMenuFollowFeet();
	return true;
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
	/* N2: and the frame table with it - it names texinfos of the level being
	 * freed. The PAGES those frames point at are Direct3D objects and are
	 * given back on the main thread instead, in HDLevelBuild and in
	 * HDLevelDeviceLost (ReleaseWaterFramePages); clearing the list here is
	 * what makes sure nothing looks at them in between. */
	g_water_frames.clear();
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
	// N2: the borrowed water frame pages are references on cache textures
	// that HDRenderShutdown is about to drop.
	ReleaseWaterFramePages();
	HDObjectDeviceLost();

	// The font atlas was made on this device too (#123, fix round Q). Its
	// match table survives; only the texture goes.
	HDFontDeviceLost();

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

/* T4: the one question the room walk asks of the HD path. g_level_active is
 * false with hd_enable off, before a level is built, after HDLevelUnload and
 * in a level whose .TRG had no rooms - and in every one of those the walk in
 * game\draw.cpp carries no reach and is the function it has always been. */
bool HDLevelActive()
{
	return g_level_active && HDEnabled();
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

/* #123, fix round P: the three numbers the frame's ONE projection is built
 * from, for the inventory ring's own screen centre. See the note in
 * hd_level.h. */
void HDFrameProjectionInfo(float* persp, float* centerX, float* centerY)
{
	if (persp)
		*persp = g_persp;

	if (centerX)
		*centerX = g_centerx;

	if (centerY)
		*centerY = g_centery;
}

/* THE PICKUP DISPLAY'S OWN BRACKET (#123, fix round P2). The whole argument -
 * why it is a bracket of its own rather than a place on the frame list, and
 * which two numbers are its own rather than the frame's - stands in hd_level.h
 * over the declaration. */
bool HDFramePickupBegin(float persp, float centreX, float centreY,
	float za, float zb)
{
	if (!HDRenderReady() || HDRenderInFrame() || persp <= 0.0f)
		return false;

	HDRenderMakeProjection((double)persp, (double)centreX, (double)centreY,
		(double)za, (double)zb, &g_pickup_projection);

	HDFrameLighting& frame = g_pickup_light;

	memset(&frame, 0, sizeof(frame));

	frame.centerX = centreX;
	frame.centerY = centreY;
	frame.wibble = (float)wibble;
	frame.cameraUnderwater = camera.underwater ? 1 : 0;
	frame.distanceFogStart = (float)tomb5.distance_fog * 1024.0f;
	frame.wibbleTable = vert_wibble_table;

	/* NO FOG BULBS, exactly as the ring has none and for the same reason -
	 * see the block over HDFrameDraw. The distance fog is left in, again as
	 * the ring leaves it in: the classic display runs the same subtraction
	 * (aTransformLightClipMesh subtracts from every vertex past
	 * DistanceFogStart whoever called it) and the object stands at
	 * INVOBJ::scale1 - a few hundred to a couple of thousand units - where
	 * neither path reaches the threshold. Both do the same nothing, and the
	 * thing that would have to be explained is a difference. */
	frame.bulbs = 0;
	frame.bulbCount = 0;

	/* THE CLOCK IS READ AND NEVER STEPPED. AdvanceClock is
	 * FillFrameLighting's, once per frame; this bracket runs INSIDE a frame
	 * that has already had one, so stepping it here would count the frame
	 * twice - and a material would run at double speed for as long as a
	 * pickup icon is on the screen. */
	frame.clock = (float)g_clock;
	frame.motion = tomb5.hd_motion ? 1 : 0;
	frame.reflect = tomb5.hd_reflect ? 1 : 0;		// R3b

	HDRenderFrameBegin(&kIdentity, &g_pickup_projection, &frame);

	/* HD_PASS_ALL and not the frame's two phases (#123, fix round E, E6). The
	 * two phases exist so that no CLASSIC opaque draw can paint over a glow
	 * that wrote no depth; here there is nothing else in the section at all -
	 * the depth buffer was cleared a few lines up and the display is the one
	 * thing drawn into it - so the solid and the additive pass of the one
	 * object follow each other in the one bracket, which is what ModelProbe
	 * draws with too. */
	HDModelFrameBegin(&g_pickup_projection, 0, HD_PASS_ALL);
	return true;
}

void HDFramePickupEnd()
{
	HDRenderFrameEnd();
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
		// HD_MAX_FRAME is draw_rooms' own capacity (game\draw.cpp) and one
		// entry lands here per listed room, so this cannot happen - but a
		// silent overrun of a fixed array is not a thing to leave to a
		// comment.
		g_frame_dropped++;
		return false;
	}

	HDFrameCaptureProjection();

	FrameRoom& entry = g_frame[g_frame_count++];
	entry.room = roomIndex;
	entry.group = group;
	entry.view = ViewSerial;	// fix round R
	SetD3DMatrix(&entry.world, (float*)viewWorld);
	/* GROUP M: the portal window, taken HERE and not at draw time. These four
	 * numbers are the engine's own `r->left..r->bottom` and they do not
	 * survive the frame - PrintObjects (game\draw.cpp) resets all four of
	 * them at the end of each room's items, which is before S_OutputPolyList
	 * draws anything HD. */
	ClipWindow(entry, left, top, right, bottom);
	/* T1: and whether this room has art that needs the window. Asked of the
	 * GROUP that is really drawing it and not of the room index (T2-2): after
	 * a flipmap the two are different rooms' art, and round T's per-room flag
	 * followed the wrong one of them. */
	entry.windowed = group >= 0 && group < HD_MAX_ROOMS &&
		g_group_windowed[group] != 0;

	return true;
}

/* AN ITEM IS NOT CUT BY ITS ROOM'S WINDOW (#123, group M; WITHDRAWN in fix
 * round R).
 *
 * WHAT GROUP M DID, AND WHY IT IS GONE FOR ITEMS. Round M gave every HD item
 * the portal window of its own room, on the reasoning that "an HD guard in a
 * room that overlaps another one in space came through the wall exactly as
 * the room's geometry did". The rooms needed it and keep it. The items never
 * did, and the round's own note beside this function says why without
 * following it: THE CLASSIC PATH HANDS EVERY ITEM THE WHOLE SCREEN -
 * PrintObjects (game\draw.cpp) sets phd_left = 0, phd_top = 0, phd_right =
 * phd_winxmax + 1, phd_bottom = phd_winymax + 1 before its item loop and puts
 * them back afterwards. Only the ROOM walk uses `r->left..r->bottom`.
 *
 * So round M invented a rule the engine has not got, and the play test of
 * 2026-09-07 found what it costs: an enemy in a neighbouring room, seen
 * through a doorway, is drawn WHOLE by the classic path and was cut to the
 * doorway rectangle here - a soldier at the wall who was not drawn at all
 * while his muzzle flash, which the classic path draws, still fired; a
 * creature close to the camera visible only on one side of a straight edge.
 *
 * The scissor test is switched OFF for every item, which is the classic
 * path's own answer. The frame list still carries the windows and the rooms
 * are still drawn inside them; what is left here is the counter, so that a
 * later round can see from the log how often the withdrawn rule WOULD have
 * cut an item, and decide with numbers whether the guard-through-the-wall it
 * was built for is worth a rule of its own. */
void HDFrameScissorForRoom(int roomIndex)
{
	if (gLogEnabled && roomIndex >= 0)
	{
		for (int i = 0; i < g_frame_count; i++)
		{
			if (g_frame[i].room != roomIndex)
				continue;

			if (!g_frame[i].full)
				g_clip_items_spared++;

			break;
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

	/* THE RING HAS NO FOG BULBS (#123, fix round P). ActiveFogBulbs holds
	 * positions the game already turned into VIEW space, for the view the
	 * last played frame had. The inventory draws through a view of its own -
	 * an identity matrix with the item pushed a few hundred units down +z
	 * (game\newinv2.cpp, DrawThreeDeeObject2D) - so those coordinates mean
	 * nothing here, and a bulb of the room Lara is standing in would sit in
	 * front of her medipacks and tint them. The classic ring has no bulbs
	 * either: phd_PutPolygonsPickup runs no bulb pass at all.
	 *
	 * The distance fog is NOT taken away with them. The classic ring keeps it
	 * (aTransformLightClipMesh subtracts from every vertex past
	 * DistanceFogStart whoever called it) and a ring item stands a few
	 * hundred units from the camera, far short of the threshold on either
	 * path - so both paths do the same nothing, and the one that would have
	 * to be explained is a difference. */
	if (HDObjectRingFrame())
		frame.bulbCount = 0;

	// R3b: a destructible piece follows its own static's Flags bit.
	UpdateDestructibles();
	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	// GROUP M: this frame's windows, counted for the log below.
	g_clip_full = 0;
	g_clip_scissored = 0;
	g_clip_empty = 0;
	g_clip_open = 0;
	g_clip_windowed_tris = 0;
	g_clip_items_spared = 0;	// fix round R, counted by HDFrameScissorForRoom

	for (int i = 0; i < g_frame_count; i++)
	{
		const FrameRoom& entry = g_frame[i];
		const bool empty = ClipEmpty(entry);

		if (!entry.windowed)
			g_clip_open++;			// T1: no window at all
		else if (empty)
			g_clip_empty++;
		else if (entry.full)
			g_clip_full++;
		else
			g_clip_scissored++;

		/* T2-2: EVERY COLLECTED ROOM IS DRAWN. Group M skipped a room whose
		 * window came out empty, because the window WAS the room; round T
		 * spared the rooms that were not windowed at all; and now no room's
		 * ordinary art is windowed, so nothing may be skipped here. What an
		 * empty window silences is the intruding half alone, inside
		 * DrawRoomWindowed. */
		(void)empty;

		// PROF_HD_LIGHT is the light block alone, inside PROF_HD_DRAW: the
		// one part of the room draw that is processor work rather than draw
		// calls, and therefore the one that a single core can stall on.
		ProfBegin(PROF_HD_LIGHT);
		FillRoomLighting(g_frame[i].room, g_frame[i].group,
			g_frame[i].lighting, g_frame[i].lights);
		ProfEnd(PROF_HD_LIGHT);
		DrawRoomWindowed(g_frame[i], HD_PASS_SOLID);
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

	/* Fix round R: this phase walks the same items the solid one did, so the
	 * count starts over here and the clip line below - which is written from
	 * this phase - reports one frame's items and not two phases' worth. */
	g_clip_items_spared = 0;

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
		DrawRoomWindowed(g_frame[i], HD_PASS_BLEND);
	}

	for (int i = 0; i < g_frame_count; i++)
	{
		DrawRoomWindowed(g_frame[i], HD_PASS_ADDITIVE);
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
			"gloss_calls=%d water_calls=%d",
			g_frame_count, g_frame_room_calls, g_frame_dropped,
			frame.bulbCount, frame.distanceFogStart, (int)wibble,
			frame.cameraUnderwater, tomb5.shimmer ? 1 : 0,
			objects_here, HDObjectDrawCalls(),
			HDRenderCutoutCalls(), HDPageCacheCutoutPages(),
			HDRenderFlipbookCalls(), HDRenderGlossCalls(),
			HDRenderWaterCalls());
	}

	/* N1: AND HOW MANY WATER SURFACES ONE FRAME DREW. Throttled the way the
	 * clipped-light line is: only a frame that draws more of them than any
	 * frame of this level before it writes anything, so a level with no
	 * water writes nothing at all and the busiest view of a pool writes one
	 * line. It is the number that says whether a sheet the log counted at
	 * level build ever reaches the screen. */
	if (gLogEnabled && HDRenderWaterCalls() > g_water_calls_worst)
	{
		g_water_calls_worst = HDRenderWaterCalls();
		Log("HDLOG frame water surfaces drawn=%d call(s) - the most in this "
			"level so far", g_water_calls_worst);
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
					!g_frame[i].windowed ? "(no window - no art inside another room)"
						: ClipEmpty(g_frame[i]) ? "EMPTY - the intruding art is not drawn"
						: g_frame[i].full ? "(the whole screen)" : "");
		}

		if (!g_clip_line_ever || (unsigned short)(now - g_clip_line_at) >= 30)
		{
			g_clip_line_ever = true;
			g_clip_line_at = now;
			/* ONE CAMERA PER PICTURE, AS A NUMBER (fix round R).
			 *
			 * `view=` is the ViewSerial the rooms were collected with and
			 * `view_obj=` the one the HD objects were. At sixty hertz the
			 * view is rebuilt twice per in-between frame - the blended eye
			 * on the way in, the live one on the way out - so anything
			 * collected on the wrong side of either would be drawn from a
			 * camera the rest of the frame does not share, and a room seam
			 * that walks with Lara's breathing is what that looks like.
			 *
			 * A HEALTHY FRAME PRINTS ONE NUMBER FOR EACH, and the two are
			 * equal. A range (`a..b`) is the bug, and it names the half of
			 * the frame that was collected late. */
			long ro_lo = 0, ro_hi = 0, ob_lo = 0, ob_hi = 0;

			for (int i = 0; i < g_frame_count; i++)
			{
				if (!i || g_frame[i].view < ro_lo) ro_lo = g_frame[i].view;
				if (!i || g_frame[i].view > ro_hi) ro_hi = g_frame[i].view;
			}

			HDObjectViewSerials(&ob_lo, &ob_hi);

			char views[64];
			char viewsObj[64];

			if (ro_lo == ro_hi)
				std::sprintf(views, "%ld", ro_lo);
			else
				std::sprintf(views, "%ld..%ld SPLIT", ro_lo, ro_hi);

			if (ob_lo == ob_hi)
				std::sprintf(viewsObj, "%ld", ob_lo);
			else
				std::sprintf(viewsObj, "%ld..%ld SPLIT", ob_lo, ob_hi);

			Log("HDLOG clip rooms=%d open=%d full=%d scissored=%d empty=%d "
				"windowed_tris=%d items_spared=%d view=%s view_obj=%s",
				g_frame_count, g_clip_open, g_clip_full, g_clip_scissored,
				g_clip_empty, g_clip_windowed_tris, g_clip_items_spared,
				views, viewsObj);
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

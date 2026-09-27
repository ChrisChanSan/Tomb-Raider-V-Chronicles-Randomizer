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
#include "hd_memory.h"			// the address space, and the rule that spends it
#include "hd_outfitmenu.h"		// the footsteps follow the outfit
#include "hd_install.h"
#include "hd_bind.h"
#include "hd_lara.h"			// HD_LARA_OUTFIT_* - the data[3] agreement
#include "hd_markers.h"
#include "hd_scan.h"
#include "hd_render.h"
#include "hd_object.h"
#include "hd_font.h"			// the HD font's texture goes with the device
#include "../function_stubs.h"
#include "../profile.h"			// PROFLOG phases
#include "../output.h"			// gPresentMinTicks - the budget follows the present
#include "../d3dmatrix.h"
#include "../3dmath.h"
#include "../winmain.h"
#include "../dxshell.h"				// DXTextureMemoryAtStart
#include "../texture.h"				// nTextures
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// number_rooms, room
#include "../../game/camera.h"		// camera.underwater
#include "../../game/draw.h"
#include "../../game/effect2.h"		// dynamics, wibble
#include "../../game/gameflow.h"		// gfLevelId
#include "../../game/lara.h"		// GfHdOutfit - data[3]
#include "../drawroom.h"			// ActiveFogBulbs, aRoomVertexWaterMarks
#include "../file.h"				// textinfo, aranges, nAnimUVRanges
#include "../gamemain.h"			// WaterTable, vert_wibble_table
#include "hd_light.h"

#include <vector>

// The generated slot name table in hd_bind.cpp has to be the enum, entry for
// entry; this stops the build when a slot is added to objects.h and not to
// that table.
static_assert(HD_SLOT_COUNT == NUMBER_OBJECTS,
	"hd_bind.cpp's slot name table and enum object_types have drifted apart - "
	"regenerate the table (see the comment above it) and update HD_SLOT_COUNT");
static_assert(HAIR == 30,
	"HDSlotIsLara() covers slots 0..30 because HAIR is 30 - it no longer is");

/* The four Tomb Raider III guests. hd_bind.cpp's tr3_guests table maps these
 * four slot numbers to the .TRM names of the I-III remaster, and it is built
 * without the engine, so the numbers stand there as literals. This stops the
 * build when one of the four slots moves, instead of a T-Rex wearing a
 * soldier's model. The randomizer picks the same four in
 * TR5Randomizer\src\core\Tr3Source.cpp, dinos(). */
static_assert(SAS == 31 && CROW == 43 && SAILOR == 53 && REAPER == 75,
	"the slots the randomizer imports the Tomb Raider III guests into have "
	"moved - update tr3_guests in hd_bind.cpp with them");

/* The outfit marker, data[3] of the level header. The randomizer writes these
 * very numbers (Patch::hdOutfitId in TR5Randomizer\src\core\ApplyPatch.cpp),
 * so neither side translates. Renumbering the enum stops the build here;
 * without this line a Nevada Lara would quietly become an Antarctica one. */
static_assert(HD_LARA_OUTFIT_TR3_ANTARC == 5 && HD_LARA_OUTFIT_TR3_NEVADA == 6 &&
	HD_LARA_OUTFIT_TR3_COAST == 7 && HD_LARA_OUTFIT_TR3_CATSUIT == 8,
	"data[3] of the level header carries HD_LARA_OUTFIT_* 5..8 by agreement "
	"with the randomizer (Preparation\\Contract.md) - the enum has moved");

/* The engine never has more rooms than HD_MAX_ROOMS, and one frame never
 * holds more of them than draw_rooms (game\draw.cpp) does. Both are fixed
 * arrays here rather than allocations, so that nothing in the frame path can
 * fail.
 *
 * HD_MAX_FRAME follows draw_rooms' own capacity, the engine's room maximum of
 * 256. The walk's own list is trimmed to 128 by reach, but a list of classic
 * rooms is never trimmed and was measured at 106 in one JOBY4 frame
 * (RoomProbe -reachsurvey). One entry per listed room arrives here from
 * InsertRoom (specific\drawroom.cpp), so 256 is the true bound and this array
 * never drops a room the walk kept. sizeof(FrameRoom) is 384 bytes, so the
 * array is 96 KiB of BSS. */
#define HD_MAX_ROOMS	1024
#define HD_MAX_FRAME	256

namespace
{
	struct FrameRoom
	{
		int room;
		int group;		// the .TRG group that draws it right now
		D3DMATRIX world;	// view * world, as the classic path set it

		/* The room's lighting, worked out once and kept. The frame draws its
		 * rooms twice - the solid passes in one phase, the glows in another
		 * after the classic buckets - and FillRoomLighting counts the lights
		 * it turns away, so a second run would count them twice. It runs in
		 * the solid phase and the glow phase reads what it left. */
		HDRoomLighting lighting;
		HDLightSource lights[HD_MAX_LIGHTS];

		/* The portal window, in backbuffer pixels, as the scissor test wants
		 * it - `x0/y0` the first pixel drawn, `x1/y1` the first one that is
		 * not. Worked out when the room is collected, because only then do
		 * the engine's `r->left..r->bottom` hold this room's answer:
		 * PrintObjects resets all four at the end of the frame.
		 *
		 * `full` is a window that covers the whole backbuffer - the camera's
		 * own room, and any room the walk sees all of. Those are drawn with
		 * the test off, which is faster and gives the same picture. */
		int x0, y0, x1, y1;
		bool full;

		/* Whether the group drawing this room carries any art that stands
		 * inside another room. A group that does not draws over the whole
		 * screen, as the classic path does with a polygon that straddles the
		 * window's rim. */
		bool windowed;

		/* The camera this room was collected with: ViewSerial at the
		 * InsertRoom that put it here (see specific\3dmath.cpp). Read only by
		 * the log. */
		long view;

		/* The intruding triangles this frame draws inside the window: all of
		 * them but the pieces DecidePairs answered whole for. Read only by
		 * the log. */
		int scissoredTris;
	};

	char g_stem[64] = { 0 };

	// Set by HDLevelBuild and by nothing else. Every frame-path function
	// tests it first, so switching hd_enable while a level runs changes
	// nothing until the next level starts, as the option row says.
	bool g_level_active = false;

	/* Which .TRG group draws which room.
	 *
	 * Group i is room i until FlipMap runs. FlipMap (game\control.cpp) does
	 * not move a room, it swaps the contents of two ROOM_INFO records: after
	 * FlipMap(n) the ROOM_INFO at index r holds the geometry, floor data,
	 * statics and lights of what was room r->flipped_room. The engine still
	 * calls it room r, so drawing group r would show the art of the room no
	 * longer there - a wall that is gone while its HD texture still stands.
	 *
	 * So a table maps room to group, swapped together with the ROOM_INFO
	 * records (HDFlipRoom, called from FlipMap). Everything decided per room
	 * is read through it:
	 *
	 *   g_group_hd    is per group, not per room - "this art exists and may
	 *                 be drawn" is a property of the .TRG and travels with
	 *                 the art. HDRoomIsHD, which also decides whether a
	 *                 room's static meshes are suppressed, reads
	 *                 g_group_hd[g_room_group[r]].
	 *   water marks   are built once, before any flip, on the classic
	 *                 vertices of every room including the alternates, and
	 *                 carried into the group of the room they were found in,
	 *                 so they follow the art.
	 *   lighting      reads the room's own flags (swapped with ROOM_INFO, so
	 *                 the alternate's) and the group's bounding box.
	 *
	 * A flip pair stands at the same place in the world - that is what makes
	 * it a flip pair - so the world matrix the engine hands in is right for
	 * either group of the pair, and nothing about the matrix changes here.
	 *
	 * The table survives a save: the engine restores a flip by calling
	 * FlipMap once per set flip (game\savegame.cpp), so a loaded game walks
	 * through the same hook. A level starts with the identity. */
	unsigned char g_group_hd[HD_MAX_ROOMS];
	/* Which groups carry art that stands inside another room, and so have
	 * triangles to draw inside their own portal window.
	 *
	 * Scissoring every HD room to its portal rectangle cuts everything that
	 * reaches over a portal's edge, and so does windowing both partners of an
	 * overlapping pair whole. BuildRoomIntrusions measures the intruding
	 * triangles instead, once per level; every other triangle draws over the
	 * whole screen, as on the classic path.
	 *
	 * Per group and not per room, like g_group_hd: HDFlipRoom swaps which
	 * group draws which room, and the property belongs to the art. */
	unsigned char g_group_windowed[HD_MAX_ROOMS];
	/* The pieces that intruding art is split into and the classic faces the
	 * pieces belong to (HDPairFace and HDPairRange in hd_render.h). Built
	 * once per level by BuildPairFaces and ReportRoomIntrusions and kept for
	 * the level, because every frame asks the faces again.
	 *
	 * `g_piece_first[g]` and `g_piece_count[g]` say which entries of
	 * g_pieces are group g's, in the renderer's own order - index j of the
	 * group is g_pieces[g_piece_first[g] + j]. Per group, like
	 * g_group_windowed. `g_face_alive` is this frame's corner rule per face,
	 * written by HDFrameRoom for the faces of the room it collects. */
	std::vector<HDPairFace> g_pair_faces;
	std::vector<unsigned char> g_face_alive;
	std::vector<HDPairRange> g_pieces;
	int g_piece_first[HD_MAX_ROOMS];
	int g_piece_count[HD_MAX_ROOMS];
	short g_room_group[HD_MAX_ROOMS];
	int g_room_count = 0;
	int g_flips_done = 0;		// for the log, once per level

	// Water marks, built once per level out of the classic roomlets and
	// handed to the renderer; the counters below say once per level whether
	// the shader's fixed maxima ever bit.
	std::vector<HDWaterMark> g_water_marks;

	/* The water surface the remaster does not draw.
	 *
	 * One entry per classic room face that is a water surface and that the
	 * .TRG has no art for. See BuildWaterSurfaces for the measurement; the
	 * faces are kept sorted by roomlet so that drawroom.cpp transforms and
	 * lights a roomlet once and then picks its water faces out of it. */
	std::vector<HDRoomWaterFace> g_water_faces[HD_MAX_ROOMS];
	int g_water_face_total = 0;
	int g_water_face_rooms = 0;
	int g_water_face_kept = 0;	// faces the .TRG does cover - left to it

	/* The game clock, in seconds.
	 *
	 * The UV scroll a .TRG part carries is a speed and needs a clock that
	 * runs forward. The engine has one counter that does: GlobalCounter
	 * (control.cpp), incremented once per game update in the loop that steps
	 * `wibble`, so 30 a second, and still whenever the game stands still -
	 * the inventory, the pause, a load.
	 *
	 * It is a ushort and turns over every 36 minutes, which would put a jump
	 * in the middle of a waterfall, so the difference since the last frame is
	 * accumulated instead. The subtraction is done in ushort, where the
	 * turn-over is exactly what unsigned arithmetic gives back.
	 *
	 * No wall clock (timeGetTime, steady_clock): a frame drawn twice while
	 * the game is paused draws the same picture, as on the classic path. */
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

	/* Totals for the whole level, not a per-frame flag: a per-frame reset
	 * would write the "once per level" line on every clipping frame (173
	 * identical lines from one title screen). The first frame that clips
	 * writes one line naming the room, and the level's end writes one total.
	 * Nothing else logs them. */
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
	 * A frame rate dip cannot be judged from the picture, so the engine
	 * measures it: one FPSLOG line per second of wall clock, written from the
	 * one place every presented frame goes through.
	 *
	 * Where it is taken: HDFramePresented() is called from DXShowFrame
	 * (specific\dxshell.cpp) directly after IDirect3DDevice9::Present returns.
	 * That is the only function in this engine that presents anything, and
	 * every path reaches it: the game frame (S_DumpScreen), the loading bar
	 * and the credits (S_DumpScreenFrame), the inventory, the title menu and
	 * an FMV (fmv.cpp calls it itself). Measuring inside S_OutputPolyList
	 * would miss the loading bar and the movies; measuring in the main loop
	 * would count message pumps that drew nothing.
	 *
	 * What the numbers mean: the logic is a fixed 30 Hz in every mode; what
	 * fps_mode changes is how often the same logic state is presented.
	 * S_DumpScreen waits for gPresentMinTicks ticks of the engine's own 60 Hz
	 * counter (specific\time.cpp) before it presents - two at fps_mode 0, one
	 * at fps_mode 1 - so a healthy second is `frames=30` with every interval
	 * on 33.3 ms, or `frames=60` with every interval on 16.7 ms. The line
	 * carries both: `fps=` is the target the budget was taken from, and
	 * `logic_ticks=` is what the simulation really ran, which must stay 30
	 * whatever `frames=` says. That quantisation is why there are two
	 * counters:
	 *
	 *   slow   intervals over the budget (33.3 ms after a two-tick present,
	 *          16.7 after a one-tick one; the line prints which as `budget=`).
	 *          Half a healthy second lands here on jitter alone, so this
	 *          number means nothing on its own.
	 *   late   intervals over the budget plus half a 60 Hz tick - 41.7 ms
	 *          after two ticks, 25.0 after one. Jitter cannot reach it; a
	 *          frame that missed its slot costs a whole tick and lands well
	 *          beyond. This is the number that says the frame rate really
	 *          fell under the target.
	 *
	 * A second spent in a modal menu is not gameplay: `logic_ticks=0`,
	 * `hd_rooms=0` and `frames=30` together mean the pause menu, the load or
	 * save screen, the death menu or the level-change fade was up, and those
	 * keep thirty at any setting; `budget=` shows it. PROFLOG calls such an
	 * interval a `pause` rather than a frame - FPSLOG cannot, because it
	 * never sees where a frame came from.
	 *
	 * The title and the inventory ring are not on that list. DoTitle calls
	 * ControlPhase like the game does, so a title second reads
	 * `logic_ticks=30` and draws rooms, and both present at the option's
	 * rate - GameFrameMinTicks in specific\output.cpp, InvFrameMinTicks in
	 * game\inventory_interp.cpp.
	 *
	 * Nothing happens without -log: the first line of HDFramePresented is
	 * `if (!gLogEnabled) return;` - no counter kept, no clock read. Nothing
	 * here allocates: the accumulators are plain integers and doubles.
	 */
	/* The budget follows the present, not the option: fps_mode is not what a
	 * given frame waited for. Only DrawPhaseGame's own present drops to one
	 * tick; the pause menu, the load/save screen, the death menu and the
	 * level-change fade keep two whatever the option says, and judged against
	 * 16.7 ms a healthy menu second would read as thirty slow and a dozen
	 * late frames.
	 *
	 * gPresentMinTicks is what the present that just returned waited for, and
	 * HDFramePresented runs directly after it, so this reads the frame's own
	 * slot. The loading bar's present (S_DumpScreenFrame waits one tick of
	 * its own) does not set it and is measured against the default two.
	 *
	 * A frame that waited for no tick, because the present itself waited for
	 * the display, has the panel's period as its slot - 6.9 ms at 144 Hz. The
	 * definition lives in specific\output.cpp so that this line and PROFLOG's
	 * cannot drift apart. */
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
	int g_sec_rooms = 0;		// the HD counters, maximum over the second -
	int g_sec_items = 0;		// the heaviest frame is the one that explains
	int g_sec_draws = 0;		// a drop, not the last one before the flush

	/* GlobalCounter when this second began. Its difference at the flush is
	 * how many logic ticks the second really ran: at fps_mode 1 the line must
	 * read frames=60 with logic_ticks=30. GlobalCounter is a ushort and
	 * wraps; the subtraction is done in ushort and wraps with it. */
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

		/* `fps=` is what the mode asks for - 30, 60, or the panel's own
		 * refresh rate - and `real_fps=` is the rate the presents came at,
		 * from the mean interval rather than from `frames=`, which counts over
		 * a second bounded by frames and is a frame out either way. On a
		 * display-bound run both match the monitor: 144 and 143.8 is a
		 * healthy 144 Hz panel. */
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
	 * (for the level before the one being built, after the load thread has
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

	/* What the portal windows of this frame came to, for the log. `full`
	 * drew over the whole backbuffer, `scissored` inside a rectangle, `empty`
	 * were rooms the walk gave a window of no width or no height, which are
	 * not drawn at all. */
	int g_clip_full = 0;
	int g_clip_scissored = 0;
	int g_clip_empty = 0;
	// Rooms with no art inside another room, drawn over the whole screen, and
	// the triangles the rest really drew inside their portal window - the
	// only draw calls in the game the scissor is on for.
	int g_clip_open = 0;
	int g_clip_windowed_tris = 0;
	// The pieces of intruding art DecidePairs asked about this frame, and how
	// many of them it drew whole and how many inside their window.
	int g_clip_pairs = 0;
	int g_clip_pairs_whole = 0;
	int g_clip_pairs_scissored = 0;
	/* HD items whose own room's window is not the whole screen - the ones a
	 * per-room scissor would cut and the classic path never does. Counted per
	 * frame with -log and reported in the clip line. */
	int g_clip_items_spared = 0;
	// Once per level, on the first frame that draws an HD room: one line per
	// room with the window it got. Reset beside g_matrix_checked.
	bool g_clip_listed = false;
	// Throttle for the per-frame line: GlobalCounter when it was last
	// written. The counter ticks 30 times a second at every fps_mode, so a
	// difference of 30 is one second of game time.
	unsigned short g_clip_line_at = 0;
	bool g_clip_line_ever = false;

	/* The engine's inclusive window as a scissor rectangle.
	 *
	 * `r->left..r->bottom` are pixel indices and both ends belong to the room
	 * (game\draw.cpp, SetRoomBounds: the projected portal corner is widened
	 * by a pixel on every side before it is written). Direct3D's rectangle is
	 * half open, so the right and bottom edges gain one to reach the pixel
	 * they name - and one more, with a matching one taken off the left and
	 * the top, as a margin.
	 *
	 * The classic path does not really clip a polygon to this window: it
	 * drops a polygon all of whose corners lie beyond one edge of it
	 * (specific\polyinsert.cpp, `c[v0] & c[v1] & ...`) and clips whatever is
	 * left against the screen. So a polygon that
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
			// for a room on the draw list (SetRoomBounds returns before it
			// writes when tL >= tR), so this is a guard: a room must not be
			// drawn over the whole screen because four numbers came out
			// backwards.
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

	/* Does the classic path draw this face of the room this frame?
	 *
	 * The corner rule of specific\drawroom.cpp, aRoomletTransformLight, line
	 * for line: each corner through the room's view * world matrix; a corner
	 * nearer than f_mznear carries -128, every bit but the four edge bits;
	 * any other one is projected, with the underwater wobble when the camera
	 * is under water, and carries one bit for each edge of the room's window
	 * `r->left..r->bottom` it lies beyond. specific\polyinsert.cpp drops a face
	 * when one bit is common to all its corners - all four beyond the same
	 * edge, or all behind the eye.
	 *
	 * This is the corner rule alone: the classic path also drops a
	 * single-sided face turned away from the eye (polyinsert.cpp, IsVisible)
	 * and moves the vertices of a choppy water room by the chop; neither is
	 * asked here, because the question is only whether the window hides the
	 * face.
	 *
	 * The roomlet's box test in InsertRoom (CheckBoundsClip) is not asked: a
	 * face that survives the corner rule lies partly inside every edge of the
	 * window, so its roomlet's projected box does too. */
	bool ClassicFaceDrawn(const HDPairFace& face, const D3DMATRIX& m,
		int left, int top, int right, int bottom)
	{
		const float clipLeft = (float)left;
		const float clipTop = (float)top;
		const float clipRight = (float)right;
		const float clipBottom = (float)bottom;
		short common = -1;

		for (int c = 0; c < face.corners && c < 4; c++)
		{
			const float* pos = face.v[c];
			float x = pos[0] * m._11 + pos[1] * m._21 + pos[2] * m._31 + m._41;
			float y = pos[0] * m._12 + pos[1] * m._22 + pos[2] * m._32 + m._42;
			const float z = pos[0] * m._13 + pos[1] * m._23 + pos[2] * m._33 + m._43;
			short clipFlag = 0;

			if (z < f_mznear)
				clipFlag = -128;
			else
			{
				const float zv = f_mpersp / z;

				if (camera.underwater)
				{
					const float zv2 = 1.0F / (z / 512.0F);

					x = x * zv + f_centerx + vert_wibble_table[((wibble + long(zv2 * y)) >> 3) & 0x1F];
					y = y * zv + f_centery + vert_wibble_table[((wibble + long(zv2 * x)) >> 3) & 0x1F];
				}
				else
				{
					x = x * zv + f_centerx;
					y = y * zv + f_centery;
				}

				if (x < clipLeft)
					clipFlag++;
				else if (x > clipRight)
					clipFlag += 2;

				if (y < clipTop)
					clipFlag += 4;
				else if (y > clipBottom)
					clipFlag += 8;
			}

			common &= clipFlag;
		}

		return common == 0;
	}

	/* This frame's answer for every piece of a room's intruding art.
	 *
	 * The window is there for a room that is drawn: art of room b standing
	 * inside room a comes through a's walls when a is on the screen. A piece
	 * whose holder is not on this frame's list is not asked and is drawn
	 * whole, like the rest of the room's art - the ANDREA1 pier at the
	 * Piazza corner stands inside room 197, which no view from room 57 lists,
	 * and cut to room 2's window it comes out as a strip of sky down the
	 * brick wall.
	 *
	 * A holder that is listed is asked, per pair: the pieces its classic faces
	 * claimed are drawn whole when the classic path draws any of those faces
	 * this frame (g_face_alive, from HDFrameRoom), and inside the room's own
	 * portal window otherwise. The pieces no face claims are drawn inside it.
	 * Per pair and not per face, because the art of one wall reaches over the
	 * seams of the faces it belongs to: seen from the cafe awning in room 64,
	 * room 101's facade inside room 93 is six classic faces of 101, three of
	 * them drawn by the classic path and three wholly below 101's strip of a
	 * window, the lower of which carries the 144 triangles of the facade's
	 * lower storey - cut to the strip, room 93's interior shows through them
	 * (7,318 pixels). From inside room 93 all six are dropped and the whole
	 * facade goes, which is what hides it there.
	 *
	 * A room whose window is the whole screen draws everything whole anyway
	 * (DrawRoomWindowed); one whose window is empty draws none of its
	 * intruding art, so none of it is answered whole there. */
	void DecidePairs(FrameRoom& entry)
	{
		entry.scissoredTris = 0;

		if (!entry.windowed || entry.group < 0 || entry.group >= HD_MAX_ROOMS)
			return;

		const int first = g_piece_first[entry.group];
		const int count = g_piece_count[entry.group];
		const bool ask = !entry.full && !ClipEmpty(entry);

		for (int j = 0; j < count; j++)
		{
			const HDPairRange& piece = g_pieces[(size_t)(first + j)];
			bool listed = false;
			bool whole = false;

			if (ask)
			{
				for (int i = 0; i < g_frame_count && !listed; i++)
					listed = g_frame[i].room == piece.holder;

				whole = !listed;
			}

			if (listed && piece.face >= 0)
			{
				// Any claimed face of the same pair the classic path draws.
				for (int k = 0; k < count && !whole; k++)
				{
					const HDPairRange& other = g_pieces[(size_t)(first + k)];

					whole = other.intrusion == piece.intrusion &&
						other.face >= 0 &&
						(size_t)other.face < g_face_alive.size() &&
						g_face_alive[(size_t)other.face] != 0;
				}
			}

			if (listed)
			{
				g_clip_pairs++;

				if (whole)
					g_clip_pairs_whole++;
				else
					g_clip_pairs_scissored++;
			}

			if (!whole)
				entry.scissoredTris += piece.triangles;

			HDRenderPairWhole(entry.group, j, whole ? 1 : 0);
		}
	}

	/* One room's art, in as many pieces as the window needs.
	 *
	 * A room with no intruding art, or one whose window is the whole screen,
	 * is one draw with the scissor test off - every room but a handful.
	 *
	 * A room that has intruding art is drawn twice: everything of it over the
	 * whole screen first, the pieces of its intruding art DecidePairs answered
	 * whole among it, then the rest of the triangles that stand inside
	 * another room inside its own portal window. That second call is the only
	 * place in the game the scissor test is ever on - see
	 * BuildRoomIntrusions. Everything else, the overhang included, is drawn
	 * whole, which is what the classic path does with a polygon that
	 * straddles the window's rim. */
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

		/* An empty window silences only the intruding half: the open half is
		 * not windowed and is drawn whatever four numbers the walk came back
		 * with. */
		if (!ClipEmpty(entry))
		{
			HDRenderScissor(entry.x0, entry.y0, entry.x1, entry.y1);
			HDRenderRoomMask(entry.group, &entry.world, &entry.lighting,
				passes, HD_WINDOW_INTRUDING);
			HDRenderScissorOff();
		}

		if (gLogEnabled)
			g_clip_windowed_tris += entry.scissoredTris;
	}

	/* What the glow phase needs again. The solid phase fills these and
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

	/* The pickup display's own projection and lighting, kept apart from the
	 * frame's. The display is drawn in the middle of S_OutputPolyList, after
	 * the frame's bracket has been opened, drawn and closed; writing over
	 * g_frame_projection would take the glow phase's copy away if the two
	 * ever met. Today the glow phase runs above the depth clear and the
	 * display below it, and separate copies keep it that way. */
	D3DMATRIX g_pickup_projection;
	HDFrameLighting g_pickup_light;

	// The projection is taken when the rooms are collected, not when they are
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
	// One line per level about the sky dome, and only one.
	bool g_sky_logged = false;
	// The most water surfaces any frame of this level has drawn, which
	// throttles the line about them - see HDFrameDrawWindowed.
	int g_water_calls_worst = 0;

	// Whether this frame's projection has been taken yet - by the first HD
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
	 * is known before they are made. */
	int g_scan_vertices = 0;
	int g_scan_indices = 0;

	/* What the in-level re-dresses have cost so far: the cache's byte count
	 * when this level was built, and how many outfit changes since. The cap
	 * is on the bytes; the count is for the log. Both are reset by the next
	 * level build. */
	double g_redress_bytes0 = 0;
	int g_redress_count = 0;

	/* What the re-dress rule answered when it was last asked. The F4 window
	 * asks on every drawn frame it is open, and the answer costs a
	 * GlobalMemoryStatusEx and a walk of the whole address space - 203
	 * microseconds over 262 regions (PoolProbe -walk), nothing once and a
	 * great deal sixty times a second. So it is worked out when something has
	 * changed it: at the end of the level build, after every completed change,
	 * and once when the window opens. HDLevelRedressAllowed hands it out.
	 *
	 * Nothing else changes it inside a level: the only callers of
	 * HDPageCacheAppend are this level build - the rooms and the water frames
	 * - and HDObjectLevelBuild, which runs in a level build or in a re-dress,
	 * so the cache cannot grow behind the stored answer's back. */
	HDRedressVerdict g_redress_verdict;
	HDMemoryRoom g_redress_room;
	bool g_redress_said_unknown = false;

	/* The biggest single allocation one page of this level makes, worked out
	 * at the end of the level build, when the format and the device's own cap
	 * are both settled. The fragmentation condition asks the largest free
	 * block against it. The pages themselves are D3DPOOL_DEFAULT and cost the
	 * process almost nothing (0.04 MiB apiece, measured), but every mip of
	 * every one goes through one reused D3DPOOL_SYSTEMMEM staging texture of
	 * the page's whole size
	 * (hd_pagecache.cpp, ensure_staging) - and that is an 85.33 MiB block for
	 * a 4096 page in A8R8G8B8, measured at 85.34 in a single reserve. */
	double g_redress_page_block = 0;

	/* Which pool the pages live in, in one place instead of a literal at each
	 * call. D3DPOOL_DEFAULT: the texture is on the card and the process keeps
	 * no copy, which makes the address space the cheap side of the rule and
	 * the card the expensive one. hd_object.cpp hands the models' pages the
	 * same answer; it is a property of the loader, not a setting. */
	const bool kPagesManaged = false;

	/* The widest page the remaster has. Eleven of them are 4096x4096 and
	 * nothing is wider (hd_render.cpp, where the device's own cap enters the
	 * policy); a card that stops at 2048 clamps them, and the policy says so,
	 * which is why the block below is worked out from both. */
	const int kWidestPage = 4096;

	/* The automatic texture budget.
	 *
	 * hd_texture_budget_mb = 0 means "work it out", and this works it out. It
	 * starts from what the device said it had the moment it was made
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
	 *   and are made before the pages, so they are already spent by the time
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

	/* The re-dress rule, asked and stored. Everything it reads is a fact the
	 * level already has - the cache's bytes, this level's mark, the policy's
	 * budget - and one measurement of the process itself; HDRedressDecide
	 * does the arithmetic and hd_memory.h carries the reasoning.
	 *
	 * `say` writes the room into the log. The level build and the F4 window
	 * do, because those are the two moments at which a reader of the log
	 * wants the numbers; the refresh after a change does not, because that
	 * change's own line already carries them. */
	void RefreshRedress(const char* why, bool say)
	{
		HDPageCachePolicy policy;
		HDRedressAsk ask;

		HDPageCachePolicyNow(&policy);
		HDMemoryMeasure(&g_redress_room);

		ask.cacheBytes = HDPageCacheBytes();
		ask.bytes0 = g_redress_bytes0;
		ask.budgetBytes = policy.budgetBytes;
		ask.allowanceBytes = HD_REDRESS_ALLOWANCE;
		ask.worstBytes = HD_REDRESS_WORST;
		ask.reserveBytes = HD_REDRESS_ADDRESS_RESERVE;
		ask.pageBytes = g_redress_page_block;
		ask.room = g_redress_room;

		HDRedressDecide(&ask, &g_redress_verdict);

		/* A failed measurement gets a line of its own, because the rule then
		 * falls back to the flat 64 MiB allowance and the log must say so.
		 * Once per level: this is asked three times and more. */
		if (!g_redress_room.known && !g_redress_said_unknown)
		{
			g_redress_said_unknown = true;
			Log("HDLOG redress: GlobalMemoryStatusEx refused - the address "
				"space is unknown, so the allowance is the flat %.0f MiB "
				"and nothing more", HD_REDRESS_ALLOWANCE / 1048576.0);
		}

		if (!say)
			return;

		Log("HDLOG redress room (%s): another change is %s, refused by %s, "
			"card=%.0f addr=%.0f largest_free=%.0f MiB (avail_virtual=%.0f "
			"reserve=%.0f page=%.0f MiB, %d regions, walk_ms=%d), this level "
			"has spent %.1f of %.0f MiB and has room for %d more",
			why ? why : "?",
			g_redress_verdict.allowed ? "allowed" : "not allowed",
			HDRedressRefusalText(g_redress_verdict.refused),
			g_redress_verdict.cardRoom / 1048576.0,
			g_redress_verdict.addrRoom / 1048576.0,
			g_redress_room.largestFree / 1048576.0,
			g_redress_room.availVirtual / 1048576.0,
			HD_REDRESS_ADDRESS_RESERVE / 1048576.0,
			g_redress_page_block / 1048576.0, g_redress_room.regions,
			g_redress_room.walkMs, g_redress_verdict.spent / 1048576.0,
			HD_REDRESS_ALLOWANCE / 1048576.0, g_redress_verdict.fits);
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

	/* Checks that the two paths put the same point in the same pixel.
	 *
	 * The classic path transforms a room-local vertex with the matrix on the
	 * stack and divides by z itself (specific\drawroom.cpp,
	 * aRoomletTransformLight):
	 *
	 *     v  = pos * (view * world)          - a row vector, translation in row 3
	 *     sx = v.x * f_persp / v.z + f_centerx
	 *     sy = v.y * f_persp / v.z + f_centery
	 *     sz = f_a - f_boo * (2048 / v.z)
	 *
	 * The HD path hands the device the same matrix times the projection and
	 * lets the hardware divide. If the two disagree by so much as a pixel,
	 * every HD wall stands beside its own floor. So they are compared here,
	 * on the level's own vertices, with the level's own camera - once per
	 * level, into the log.
	 *
	 * The classic side below is the three lines of drawroom.cpp, and the HD
	 * side is the matrix that really goes to the device, through the viewport
	 * rule the device applies. */
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
		 * "the projection is mostly zeroes", so that the check notices a
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

/* ---------------------------------------------------------- water marks -- */

/* The water marks, once per level.
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

/* The rooms' own boxes, for the renderer.
 *
 * The box TR5 itself calls the inside of a room, taken from the lines that
 * ask whether an item fits inside one (game\draw.cpp):
 *
 *     x    r->x + 1024  ..  r->x + (y_size - 1) * 1024
 *     z    r->z + 1024  ..  r->z + (x_size - 1) * 1024
 *     y    r->maxceiling ..  r->minfloor
 *
 * - and note that x runs on y_size and z on x_size, which is TR5's own
 * naming and not a slip: `r->floor[z_index + x_size * x_index]` says which
 * is which.
 *
 * Handed over room local, because that is the frame the .TRG's vertices are
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

		b.originX = (float)r->x;
		b.originZ = (float)r->z;
	}

	if (!g_room_boxes.empty())
		HDRenderSetRoomBoxes(&g_room_boxes[0], (int)g_room_boxes.size());
	else
		HDRenderSetRoomBoxes(0, 0);
}

/* The water surface the remaster does not draw.
 *
 * In some HD rooms a pool shows dark washed-out blotches where the classic
 * level has a moving water surface. It is not a missing texture layer: over
 * all fifteen levels, 0 of 12,247,000 .TRG vertices carry layer 0 (TrgProbe
 * -layer0).
 *
 * What is missing is the geometry. The classic engine draws a water surface
 * as ordinary room faces at the room's own minfloor, and the shimmer branch
 * that makes them move opens with `pos.y == CurrentRoomPtr->minfloor` in a
 * room carrying ROOM_REFLECT or ROOM_MIST (drawroom.cpp). Held against the
 * .TRG plane by plane (TrgProbe -watergap), over 93 such rooms in the fifteen
 * files:
 *
 *     JOBY2   4 of 5 rooms have no .TRG triangle in the classic water plane
 *     JOBY5  13 of 25          -   the whole VCI pool set
 *     ANDY3  23 of 23, ANDY1 5 of 6, ANDREA2 10 of 13, ANDREA1 3 of 3,
 *     JOBY4   1 of 1, RICH3 2 of 4
 *     ANDREA3, JOBY3, RICH2  0 of 15 - those rooms keep their surface
 *
 * JOBY2's room 66, for example: 24 classic faces of one texinfo at y -1280,
 * and 0 .TRG triangles in that plane. The HD room draws the pool floor and
 * nothing over it - a flat dark checkerboard with no water at all.
 *
 * Where the level has a water surface and the .TRG has not, the
 * classic face is drawn on top of the HD room, by the classic path, with its
 * own texture, its own animated texture range, its own light and the shimmer
 * the classic path has always given it. Nothing is invented: the surface that
 * comes back is the one the level ships.
 *
 * The two tests a face has to pass are the engine's own: every corner at the room's minfloor (the classic height test), and every
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
		 * .TRG has any art for (the majority below) and how many it lays its
		 * own water over (a material 16 sheet). The first two count sheeted
		 * faces too; see the note below. */
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

				/* Does the .TRG lay its own water over this piece of ground?
				 * Then the classic face is dropped: that ground has a surface
				 * already, and a second one one to twenty units away is worse
				 * than none. It is still counted above for the majority. */
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

		/* Per room and not per face. A pool is one surface, and half of one
		 * is worse than either whole: JOBY2's room 66 has 24 water faces and
		 * the remaster's coping touches 3 of their sectors, so a face-by-face
		 * rule would leave three square holes in the pool. So the room
		 * decides, by majority - and a room whose surface the remaster did
		 * build (ANDREA3, JOBY3, RICH2, and twelve of JOBY5's twenty-five)
		 * keeps every face out of the way. */
		/* The remaster's own water, however, is dropped per face.
		 *
		 * "Does the .TRG draw any horizontal art in this plane" is answered
		 * by a stone coping as readily as by water, which is why that
		 * question is a majority: a coping is not water, and dropping the
		 * faces it touches one by one would leave holes in a pool.
		 *
		 * Material 16 is the remaster's still water, so where it stands the
		 * classic face is not a hole but a double - a second surface one to
		 * twenty units from the first. Dropping exactly those faces leaves no
		 * hole; the pool stays one continuous surface, part the level's own
		 * and part the remaster's.
		 *
		 * Handing the whole room over on one sheet anywhere in the plane is
		 * far too much: JOBY5's room 184 has 29 classic faces in the plane
		 * and a sheet over 2, room 4 has 30 and a sheet over 6 - 27 and 24
		 * sectors of pool would go bare to save a double over 2 and 6.
		 *
		 * Leaving the sheeted faces out of the majority is wrong the other
		 * way: it turns rooms the majority hands to the .TRG back into
		 * restored ones - ANDREA2's 231 (26 faces, 14 of them the .TRG's, all
		 * 14 the sheet) and eight more of JOBY5. So the majority counts
		 * sheeted faces too, and a sheeted face is simply not drawn.
		 *
		 * Over the fifteen levels that changes six rooms and no others:
		 * JOBY5's 4 (30 -> 24), 27 (6 -> 5), 28 (7 -> 4), 83 (3 -> 2), 115
		 * (6 -> 5) and 184 (29 -> 27). */
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


/* ----------------------------------------------- the HD water frames --- */

/* The remaster's own picture on the classic water surface.
 *
 * Where the .TRG builds no water, the level's own face is drawn over the HD
 * room by the classic path, with its own texinfo - its own animated texture
 * range, light and shimmer. That is right about the geometry and the motion,
 * but it puts a 256-pixel classic tile in a room drawn from 512-pixel HD
 * pages. The remaster ships HD pictures of those animation frames in its TEX
 * folder, and this uses them.
 *
 * Measured over the fifteen levels (RoomProbe -watersurvey, whose
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
 * seven. ANDREA's pages 149..153 and 170 are on tiles no water face names:
 * ANDREA1's .TEX puts 149..153 on five tiles and 170 on three, and none of
 * them is in the fountain's range (its frames are tiles 2417..2423 and 2665,
 * which carry 171..177 and 236).
 *
 * How the classic animation works, because the substitution follows it:
 * S_AnimateTextures (specific\output.cpp) does not change which texinfo a
 * face names. Every six game steps it rotates the contents of the texinfos
 * in a range - textinfo[range[0]] = textinfo[range[1]] all the way along and
 * the first one round to the end. So the face keeps its number and the tile
 * it shows walks along the range. The tile on screen right now is known by
 * the content of its texinfo - its page and its own uv corner - and that is
 * what is matched below. The rotation copies the values whole, so the
 * comparison is on the bits written at load time; nothing rounds.
 *
 * An HD page is not a repaint of the tile but of the whole 64 by 64 cell the
 * tile sits in, at eight times the size (hd_scan.h has the measurement). So
 * the classic u of 0..1 over a 256 page becomes u * 4 minus the cell's own
 * column, and the tile lands where it belongs inside the 512 page. A tile
 * that reaches across a cell edge would land outside it, so those keep the
 * classic picture - 441 tiles in TR4 and TR5 are of that kind and none of
 * them is in a water range here.
 *
 * It is drawn through the classic path as before: the face keeps its light,
 * shimmer, sort and blend; only the texture the bucket binds and the four
 * coordinates change. The HD page is borrowed into the classic Textures[]
 * array above nTextures - never by raising that count, because the font, the
 * sky and the bar sprites are addressed as nTextures minus a small number
 * and would all move (specific\specificfx.cpp) - and the reference is given
 * back at the next level build, by pointer and not by slot number, because
 * the array has been refilled for another level by then (see
 * ReleaseWaterFramePages).
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

/* One borrowed page: the slot in Textures[] and the reference that was put
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
 * The slot number alone is not enough to give one back. The pages are
 * borrowed at the current level's nTextures, and by the time this runs the
 * level has changed under them: FreeLevel calls FreeTextures, which walks
 * 0..nTextures-1 of the old level (specific\texture.cpp) and does not touch a
 * slot above it; then nTextures goes back to 1 and LoadTextures fills
 * Textures[1..] for the new level (specific\file.cpp). Where the new level
 * has more pages than the old one, the slots this list names are by then the
 * new level's own classic pages - JOBY4 has 13 and borrows 13..20 while
 * JOBY5 puts its own pages in 1..18, so six of them would be released and
 * NULLed; with the randomizer permuting the order, every such pairing is
 * reachable.
 *
 * So the reference is given back by pointer, which is always ours, and the
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

		// The slot first, and only while it still holds the borrowed
		// pointer - compared before the Release so that it is never compared
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

	/* 1. The texinfos the restored faces name. The same walk InsertRoomWater
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

	/* 2. The .TEX table. A randomized level carries more texinfos than the
	 * table has entries - the randomizer only ever appends them: JOBY2
	 * vanilla has 2,820 texinfos and its table 2,820, and
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

	/* 3. The ranges that carry one of those texinfos. The walk is
	 * S_AnimateTextures' own: one short with the range count, then per range
	 * one short with the number of frames minus one and that many plus one
	 * texinfo numbers.
	 *
	 * The UV-rotate ranges are left out. Those are the level's waterfalls,
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
			 * Three corners for a triangle: bit 15 of the flag word says
			 * triangle, and then the fourth pair is not a corner - AdjustUV
			 * adjusts three and the draw reads three (specific\file.cpp).
			 * Measured with the offline twin: counting the fourth turns 127
			 * of JOBY2's 382 tiles into false cell crossers; without it 27
			 * remain, the same order TRForge measured over the two games. */
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

	/* 4. The pages themselves, appended to the cache the rooms filled - one
	 * texture per page for the whole level, as the models are added. */
	HDPageCacheStats stats;
	const char* why = "";

	if (!HDPageCacheAppend(App.dx.lpD3DDevice, HDInstallRoot(), &pages[0],
		(int)pages.size(), kPagesManaged, DecodeThreads(), &stats, &why))
	{
		Log("HDLOG level=%s water frames refused reason=%s", g_stem, why);
		g_water_frames.clear();
		return;
	}

	/* 5. Borrowed into the classic texture array, above nTextures. The
	 * count is not raised: game\text.cpp reads the font as nTextures - 2,
	 * drawbars.cpp the sky as nTextures - 1 and specificfx.cpp the menu
	 * pages as nTextures - 4 and - 5, and every one of them would move.
	 * Textures[] holds 256 entries; measured from the .TRC headers, a vanilla
	 * level uses 10 to 22 of them and a randomized one - the randomizer
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

/* The HD frame of one water face, or false for "draw it classically".
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

/* ------------------------------------------------------- the statics --- */

/* The two static mesh tables of an HD level.
 *
 * An HD room's whole MESH_INFO list is suppressed, because the remaster bakes
 * the static meshes into the room's art. That rule has two holes:
 *
 *   - 21 statics of ANDREA1 (rooms 121 and 221) sit in no .TRG group at all.
 *     The room seam takes them away and puts nothing in their place, so they
 *     would be gone from the level.
 *   - a static the game destroys at run time is gone from the classic list
 *     but goes on standing in the baked art, so it could not be destroyed.
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
 *                     slot 50 to 57 (control.cpp), and the piece goes
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

/* Whose art stands inside whose room.
 *
 * Some rooms stand inside one another. ANDREA1's 53 (an air shaft) has most
 * of its art inside room 207, with no portal between the two; from inside
 * 207 that art comes through the wall, and the depth buffer cannot hide it
 * because both rooms really are at that distance. The portal window - the
 * rectangle the classic walk left in `r->left .. r->bottom`, used as a
 * scissor - hides it.
 *
 * The window must not go to every HD room. A scissor is stricter than the
 * classic clip: the classic path drops a polygon only when every corner lies
 * beyond one edge of the window (specific\polyinsert.cpp) and otherwise draws
 * it whole, while a scissor cuts it at the pixel - the remaster's art near a
 * doorway comes out sliced (a vertical strip of sky down the edge of a brick
 * wall at the Piazza dell'Opera).
 *
 * Nor to both partners of a pair, whole: in ANDREA2's courtyard rooms 134
 * (the ground, with the palm and the doorways) and 203 (the colonnade above
 * it) are both holders - 134 holds art of 143, 203 holds art of 248 - and two
 * scissored rectangles meeting along the courtyard's room boundary cut a
 * black band across the picture, through the palm's trunk and the lintels.
 *
 * So the rule is about triangles, and about the frame:
 *
 *   a triangle of room b stands inside room a when its centroid lies in a's
 *   inside; such a pair's triangles are drawn inside b's portal window on a
 *   frame the classic path would not draw b's faces there - and nothing
 *   else in the game ever is.
 *
 * The holder gets no window (none of its art is out of place). The intruder
 * gets one only for the triangles really standing in the other room, and
 * only when the classic path says so; the rest of its art, overhang
 * included, draws over the whole screen.
 *
 * What counts as "inside room a", every part measured:
 *
 *   - the box is TR5's own (game\draw.cpp, the lines that ask whether an
 *     item fits wholly inside its room): x from r->x + 1024 over y_size
 *     sectors, z from r->z + 1024 over x_size, y from maxceiling to minfloor.
 *     That counts the wall sector two rooms share: ANDREA1 has 158 pairs
 *     with it and 7 with one more sector taken off. A box one sector smaller
 *     misses room 93's brick hull standing in room 101's second sector ring,
 *     which then shows on 101's facade from the cafe awning in room 64; it
 *     also leaves JOBY2's rooms 30 (8 x 4 sectors) and 33 (4 x 3) no inside
 *     at all, and the marble of rooms 24 and 60 standing in them unasked.
 *   - not a neighbour. A room seen through a doorway reaches into the room
 *     it opens on, and that art is seen through the opening it belongs to.
 *     The portal list and the floor data's vertical links say who is next
 *     door.
 *   - not a flipmap twin, which is the same room at two moments.
 *   - and the centroid, not a 256-unit occupancy cell. A cell test depends
 *     on whether a cell that merely touches the box counts (`p + 255 < mins`
 *     is the reject in HDRenderGroupCellsIn) or only one whose corner lies
 *     inside, and the two give different lists. A centroid is a point;
 *     engine and TrgProbe agree on it exactly.
 *
 * What decides per frame. The box alone cannot tell a shared wall from a room
 * standing inside another - from room 64 room 101's facade stands inside
 * room 93 and has to be drawn whole, from inside room 93 the same facade has
 * to go. The classic path tells them apart: it draws a face of room b whole
 * unless all its corners lie beyond one edge of b's window. So each
 * intruding triangle belongs to the classic face of b it stands on (see
 * HDPairFace in hd_render.h; BuildPairFaces hands the faces over), and
 * DecidePairs draws a pair's claimed triangles whole on a frame the classic
 * path draws any of the pair's claiming faces, and inside b's window
 * otherwise. A triangle no face claims has no face to answer for it, so it
 * keeps the answer of a box one sector smaller than the inside in x and z:
 * in the window when it stands that far in, drawn over the whole screen
 * otherwise - the palms, the spiral stairs and the walls under the
 * balustrades that reach into a holder's wall ring are unclaimed, and cut at
 * the window's rim they come out sliced where the smaller box leaves them
 * whole. A pair whose
 * holder is not on the frame's list is not asked: ANDREA1's pier at the
 * Piazza corner (room 2's art, 364 triangles inside room 197, which no view
 * from room 57 lists) cut to room 2's window would bring back the strip of
 * sky. In ANDREA2's courtyard room 134 holds 108 triangles of room 143 and
 * room 203 holds 108 of room 248, all of them claimed, and no view from
 * room 134 draws a band.
 *
 * ANDREA1 comes to 16,718 intruding triangles in 89 groups, 15,993 of them
 * claimed by 558 classic faces and 725 by none; 11,020 more that no face
 * claims stand in a holder's wall ring and are drawn open. The air shaft is
 * 411 of room 53's triangles inside room 207, all claimed by 27 faces.
 *
 * No count threshold: the answer is per triangle and per frame, so a stray
 * triangle costs only itself and cannot scissor a facade.
 *
 * It runs before the build, beside BuildRoomBoxes, because the renderer tags
 * the triangles as it fills the index buffer. Everything it reads is the
 * classic level - room boxes, portals, floor links, the roomlets' faces - so
 * TrgProbe -roomspace answers with the very same list out of the .TRC and
 * the .TRG. */
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

			/* a's inside - TR5's own, the box BuildRoomBoxes hands the
			 * renderer for a itself - put into b's frame: the .TRG's
			 * vertices are room local and the two rooms have different
			 * origins. y is world units in both.
			 *
			 * Group b is room b: this runs at level build, before any flip
			 * has swapped a pair of ROOM_INFO records, as in
			 * BuildStaticTables. */
			HDIntrusion box;

			box.group = b;
			box.holder = a;
			box.mins[0] = (float)(ra->x + 1024 - room[b].x);
			box.maxs[0] = (float)(ra->x + ((ra->y_size - 1) << 10) - room[b].x);
			box.mins[1] = (float)(ra->maxceiling - room[b].y);
			box.maxs[1] = (float)(ra->minfloor - room[b].y);
			box.mins[2] = (float)(ra->z + 1024 - room[b].z);
			box.maxs[2] = (float)(ra->z + ((ra->x_size - 1) << 10) - room[b].z);

			// A room of two sectors or fewer on a side is all wall ring.
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

/* The classic faces the intruding art is held against.
 *
 * Every drawn face of every room that is the intruder of some entry - the
 * quads and the triangles of its roomlets, as InsertRoomlet hands them to
 * specific\polyinsert.cpp - in the room's own frame, which is the group's.
 * A face that stands further than HD_PAIR_FACE_REACH from every one of the
 * room's entry boxes cannot claim a triangle inside them and is left out;
 * the rest keep the roomlets' order, so that the nearest face is the same one
 * whatever was left out.
 *
 * Kept for the level (g_pair_faces), because HDFrameRoom asks the corner
 * rule of them every frame; the renderer borrows the list for the build. */
static void BuildPairFaces()
{
	g_pair_faces.clear();
	g_face_alive.clear();

	std::vector<unsigned char> intruder((size_t)number_rooms, 0);

	for (size_t k = 0; k < g_intrusions.size(); k++)
		if (g_intrusions[k].group >= 0 && g_intrusions[k].group < number_rooms)
			intruder[(size_t)g_intrusions[k].group] = 1;

	for (int b = 0; b < number_rooms; b++)
	{
		const ROOM_INFO* r = &room[b];

		if (!intruder[(size_t)b] || !r->pRoomlets)
			continue;

		for (int i = 0; i < r->nRoomlets; i++)
		{
			const ROOMLET* roomlet = &r->pRoomlets[i];
			const ROOMLET_VERTEX* vtx = (const ROOMLET_VERTEX*)roomlet->pSVtx;
			const short* ptr = roomlet->pFac;

			if (!vtx || !ptr || roomlet->nVtx <= 0)
				continue;

			for (int q = 0; q < roomlet->nQuad + roomlet->nTri; q++)
			{
				const int corners = q < roomlet->nQuad ? 4 : 3;
				HDPairFace face;
				bool good = true;
				float lo[3], hi[3];

				face.group = b;
				face.corners = corners;

				for (int c = 0; c < 4; c++)
				{
					const int at = c < corners ? ptr[c] : ptr[0];

					if (at < 0 || at >= roomlet->nVtx)
						good = false;

					const ROOMLET_VERTEX& v = vtx[good ? at : 0];

					face.v[c][0] = v.x;
					face.v[c][1] = v.y;
					face.v[c][2] = v.z;
				}

				ptr += corners + 2;

				if (!good)
					continue;

				for (int axis = 0; axis < 3; axis++)
				{
					lo[axis] = hi[axis] = face.v[0][axis];

					for (int c = 1; c < corners; c++)
					{
						if (face.v[c][axis] < lo[axis]) lo[axis] = face.v[c][axis];
						if (face.v[c][axis] > hi[axis]) hi[axis] = face.v[c][axis];
					}
				}

				bool near_box = false;

				for (size_t k = 0; k < g_intrusions.size() && !near_box; k++)
				{
					const HDIntrusion& box = g_intrusions[k];

					if (box.group != b)
						continue;

					near_box = true;

					for (int axis = 0; axis < 3; axis++)
						if (hi[axis] + (float)HD_PAIR_FACE_REACH < box.mins[axis] ||
							lo[axis] - (float)HD_PAIR_FACE_REACH > box.maxs[axis])
							near_box = false;
				}

				if (near_box)
					g_pair_faces.push_back(face);
			}
		}
	}

	g_face_alive.assign(g_pair_faces.size(), 0);

	if (!g_pair_faces.empty())
		HDRenderSetPairFaces(&g_pair_faces[0], (int)g_pair_faces.size());
	else
		HDRenderSetPairFaces(0, 0);
}

/* What the build made of the intrusions. Run after HDRenderLevelBegin,
 * because the triangles are tagged while the index buffer is filled and the
 * counts do not exist before that.
 *
 * The flag is per group and asked of the group: HDFlipRoom swaps which group
 * draws which room, and a per-room flag would, after a flipmap, draw a
 * windowed room's art with no window and cut its twin's art to a rectangle
 * never measured for it. The art carries the property, like g_group_hd. */
static void ReportRoomIntrusions()
{
	int pairs = 0;
	int tris = 0;
	int groups = 0;

	memset(g_group_windowed, 0, sizeof(g_group_windowed));

	/* The pieces the build split the intruding art into, copied out per
	 * group in the renderer's own order, for DecidePairs and HDFrameRoom. */
	g_pieces.clear();
	memset(g_piece_first, 0, sizeof(g_piece_first));
	memset(g_piece_count, 0, sizeof(g_piece_count));

	for (int g = 0; g < HDRenderGroupCount() && g < HD_MAX_ROOMS; g++)
	{
		g_piece_first[g] = (int)g_pieces.size();

		for (int j = 0; j < HDRenderGroupPairRanges(g); j++)
		{
			HDPairRange piece;

			if (HDRenderGroupPairRange(g, j, &piece))
				g_pieces.push_back(piece);
			else
				break;
		}

		g_piece_count[g] = (int)g_pieces.size() - g_piece_first[g];
	}

	/* What the faces claimed, over the level: the distinct classic faces
	 * that claimed any triangle, the triangles they claimed and the
	 * intruding triangles none did. */
	int faces = 0;
	int bound = 0;
	int unbound = 0;
	std::vector<unsigned char> claimed(g_pair_faces.size(), 0);

	for (size_t p = 0; p < g_pieces.size(); p++)
	{
		const HDPairRange& piece = g_pieces[p];

		if (piece.face < 0 || (size_t)piece.face >= claimed.size())
		{
			unbound += piece.triangles;
			continue;
		}

		bound += piece.triangles;

		if (!claimed[(size_t)piece.face])
		{
			claimed[(size_t)piece.face] = 1;
			faces++;
		}
	}

	for (size_t i = 0; i < g_intrusions.size(); i++)
	{
		const int hits = HDRenderIntrusionHits((int)i);

		if (!hits)
			continue;

		pairs++;

		if (gLogEnabled)
		{
			// The faces of this pair and the triangles they claimed.
			int pairFaces = 0;
			int pairBound = 0;

			for (size_t p = 0; p < g_pieces.size(); p++)
				if (g_pieces[p].intrusion == (int)i && g_pieces[p].face >= 0)
				{
					pairFaces++;
					pairBound += g_pieces[p].triangles;
				}

			Log("HDLOG overlap room=%d holds the art of room=%d - "
				"%d triangles, no portal between them faces=%d bound=%d",
				g_intrusions[i].holder, g_intrusions[i].group, hits,
				pairFaces, pairBound);
		}
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

	/* `holder=inside` names the box (TR5's own inside of the holder) and
	 * `cull=opaque` the back-face rule (RangeCulls in hd_render.cpp), so a
	 * log says which rule drew the picture it goes with. */
	Log("HDLOG level=%s overlap pairs=%d intruding tris=%d in %d groups "
		"holder=inside cull=opaque faces=%d bound=%d unbound=%d",
		g_stem, pairs, tris, groups, faces, bound, unbound);

	// The lists were only borrowed for the length of the build.
	HDRenderSetIntrusions(0, 0);
	HDRenderSetPairFaces(0, 0);
	g_intrusions.clear();
}

/* The openings the randomizer run made, for the renderer.
 *
 * The sidecar names them in world units, one box per opening, and says
 * nothing about rooms, because the art that shuts a doorway is not always
 * the art of the two rooms it joins: the remaster's rooms reach into each
 * other by construction - a wall's relief, a corner pier, a doorway's reveal
 * stand in the neighbour's space, and ANDREA2's courtyard has whole facades
 * of one room drawn inside another. So every group is asked about every box,
 * and the triangle test decides.
 *
 * One entry per (group, box) pair, in the group's own frame: the .TRG's
 * vertices are room local in x and z and world in y, so the room's origin
 * comes off the two horizontal numbers and room[].y off the vertical one -
 * the same three lines BuildRoomBoxes and BuildRoomIntrusions use, and every
 * TR5 room has y 0 anyway (measured over the fifteen files).
 *
 * No pre-filter by the room's own box: it would be a second rule that has to
 * guess how far a group's art may stand outside its room, and it would buy
 * nothing measurable - the list is rooms x boxes entries of 32 bytes (ANDY3
 * with the glitchless secret list is 211 x 2), and the triangle test runs
 * once per triangle per box at level build, never per frame.
 *
 * Group i is room i while this runs: no flip has happened yet, as in
 * BuildRoomIntrusions and BuildStaticTables. */
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

/* What the build made of the openings. Run after HDRenderLevelBegin, as
 * ReportRoomIntrusions is: the triangles are tested while the index buffer is
 * filled and the counts do not exist before that.
 *
 * One line per box, naming the rooms whose art it took and how much, and one
 * line for the level. A box that caught nothing gets a line too - an opening
 * the remaster's art never covered, or a box in the wrong place - because a
 * silent zero would look like a doorway that was already open. */
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

			/* 1. Does the art really cover this static, or does its box
			 * merely catch a corner of something else?
			 *
			 * "Is there any HD geometry here at all" is too weak a reason to
			 * take a static away. ANDREA2's courtyard palm is two statics -
			 * the trunk (slot 8) in the ground room and the crown (slot 9)
			 * in the room above - and the crown's box catches four cells of
			 * 768, which is the wall behind it: suppressing the crown would
			 * leave a trunk with no fronds. The same level's room 162 crown
			 * catches 380 of 768 and is in the art.
			 *
			 * So the question is how much, against the static's own box.
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
			 * control.cpp makes before it shatters one. */
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

/* What the whole frame shares. Every one of these is the engine's own global,
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

	// The clock is stepped once per frame, here, because this is the one
	// function the frame path cannot skip.
	AdvanceClock();
	out.clock = (float)g_clock;
	out.motion = tomb5.hd_motion ? 1 : 0;
	out.reflect = tomb5.hd_reflect ? 1 : 0;

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

/* What one room adds. The dynamic lights are culled against the room's own
 * bounding box with the test aBuildRoomletLights uses on a roomlet's
 * (drawroom.cpp) - x and z only, as there. A room is bigger than a
 * roomlet, so this lets a light through that the classic path would have
 * dropped for one roomlet and kept for the next; the falloff test inside the
 * shader is a real distance test and throws it out again, so the pixel is the
 * same and only the constant upload is one longer.
 *
 * The shimmer branch of drawroom.cpp is a decision about the room,
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

	/* The group's box, not the room's number - after a flip the two are
	 * different rooms of the same pair. */
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
	// Remaster is looked for, no file is opened, and the frame is the classic
	// one, byte for byte.
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

	/* The name for the frame clock, taken before the HD guard and kept in its
	 * own buffer: FPSLOG has nothing to do with hd_enable and has to name the
	 * level either way. This is the load thread, so nobody reads it here -
	 * HDLevelBuild copies it across on the main thread once that thread has
	 * joined (specific\file.cpp, S_LoadLevelFile). */
	LevelStem(wadName, g_log_stem, sizeof(g_log_stem));

	/* The sidecar is read before the HD gate, because one of its lines is not
	 * an HD fact.
	 *
	 * `sfximport` names the sound ids the run imported from another game, and
	 * the HQ bank bridge that acts on it is deliberately outside this gate
	 * (sound does not depend on hd_enable, specific\hqsfx.cpp). Read below the
	 * gate, the list would be missing with hd_enable off - the default - and
	 * an imported sound would be replaced by the wrong HQ sample (the
	 * vulture's death cry in English).
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

	/* What the randomizer changed, before anything else is measured: the
	 * sidecar names the rooms whose shape no longer matches the Remaster's art
	 * and the slots whose model came out of another file, and both decide what
	 * the rest of this load may do.
	 *
	 * The path is the .TRC's own with .hdm in its place, so the file is found
	 * wherever the level is - a moved folder, a data folder beside a second
	 * installation. A missing file is the normal case and not a fault; an
	 * unreadable one falls back to the same answer and says so. It has been
	 * read above the gate - see the note there. */
	Log("HDLOG level=%s markers file=%s version=%d rooms=%d cuts=%d donors=%d "
		"outfit=%d tr3_install=%s%s%s",
		g_stem, markers ? HDMarkersPath() : "none", HDMarkersVersion(),
		HDMarkersRoomCount(), HDMarkersCutCount(), HDMarkersDonorCount(),
		HDMarkersOutfit(), HDInstallRootTr3()[0] ? "found" : "absent",
		markers ? "" : " reason=", markers ? "" : HDMarkersError());

	/* data[3] and the sidecar's `outfit` line come out of one value in the
	 * randomizer and can only disagree when one of the two halves was written
	 * by an older run. The header byte wins - it is the one the draw path
	 * reads, and it is there before a room is loaded - so this is a line in
	 * the log and nothing else. */
	if (markers && HDMarkersOutfit() != (int)GfHdOutfit)
		Log("HDLOG level=%s markers MISMATCH data[3]=%d, the sidecar says %d "
			"- data[3] wins", g_stem, (int)GfHdOutfit, HDMarkersOutfit());

	/* A sidecar does not name a wardrobe outfit: the extra outfits of the
	 * wardrobe scan are the player's choice and not the run's, so the key
	 * lives in the registry and hd_laradraw.cpp is the only thing that reads
	 * it. A `wear <key>` line in an older sidecar reaches the log through the
	 * unknown-line loop below. */

	for (int i = 0; i < HDMarkersUnknownCount(); i++)
		Log("HDLOG level=%s markers line this engine has no rule for: %s",
			g_stem, HDMarkersUnknown(i));

	for (int i = 0; i < NUMBER_OBJECTS; i++)
		loaded[i] = objects[i].loaded ? 1 : 0;

	// measureBc7 is off: HDLevelBuild decodes every page for real a moment
	// later and reports what that cost, so decoding them here too would only
	// make the level load twice as slow to measure the same thing.
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

/* The level's HD build, and the one thing that is not part of it.
 *
 * The footsteps depend on the HD gate and nothing else: the bare set is built
 * by HDFeetLevelBuild whenever hd_enable is on - in LoadSamples, on the load
 * thread, long before this - and the body it follows is decided by
 * HDLaraLevelBuild inside this build, on the main thread after file.cpp has
 * joined the load thread. The build has five ways to give up (no rooms, no
 * device, no page cache, no HD room, no stem), and none of them takes the
 * bare set or the F4 row away. So the footsteps are decided here, on every
 * way out of the build: decided only at its end, a card without shader model
 * 3 (which turns the whole session classic while hd_enable stays 1) would
 * keep whatever footstep choice the menu applied inside the previous level.
 *
 * On those ways out the answer is the level's own body and not the chosen
 * key, because there is no HD body: HDLaraDrawBuild runs inside
 * HDObjectLevelBuild, past all five, so HDLaraDrawOutfit() reads
 * HD_LARA_OUTFIT_NONE and the key is empty. She wears Core's outfit, which
 * has boots, and the log line says so. */
void HDLevelBuild()
{
	HDLevelBuildHD();

	/* Behind the gate: with hd_enable off there is no bare set, no wardrobe
	 * and nothing to say about either. */
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
	// The per-level window list is written again for this level.
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

	/* The level before this one, for the two diagnostic counters: its frame
	 * clock summary and its one clipped total. Here and not in HDLevelUnload,
	 * because this is the main thread and the load thread has joined - the
	 * counters are written by the thread that presents frames, and reading
	 * them from the load thread would be a race. Not behind HDEnabled():
	 * FPSLOG counts frames whatever hd_enable says. */
	LevelDiagnosticsEnd();
	strcpy(g_fps_level, g_log_stem);

	/* The level before this one, released here rather than in HDLevelUnload -
	 * see the comment there. This is the main thread; that one is not.
	 *
	 * The water frames come first and outside the group test: the reference
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

	/* The texture policy of this level, set before anything is loaded and in
	 * force for the rooms and for the models and Lara's wardrobe that follow
	 * them - one cache and one budget, not three.
	 * The device's own size limit is added inside HDRenderLevelBegin, which
	 * is the one place that has the caps in hand.
	 *
	 * Both settings are registry only and not in the options menu: they are
	 * for a card that cannot hold the art. */
	{
		HDPageCachePolicy policy;

		policy.format = _stricmp(tomb5_hd_texture_format, "dxt5") == 0
			? HD_PAGE_FORMAT_DXT5 : HD_PAGE_FORMAT_RGBA;
		policy.budgetBytes = tomb5.hd_texture_budget_mb
			? (double)tomb5.hd_texture_budget_mb * 1048576.0
			: AutoBudgetBytes();
		policy.maxSide = 0;

		HDPageCacheConfigure(&policy);

		/* And what this process has left, which the card's number says
		 * nothing about: the pages and the level's outfit changes are
		 * measured against the room a 32 bit process has. Once per level
		 * build, on the load path, where 203 microseconds do not show. */
		HDMemoryRoom room;

		HDMemoryMeasure(&room);

		Log("HDLOG level=%s budget=%.0f MiB (%s) format=%s card=%.0f MiB "
			"classic_pages=%d framebuffer=%dx%d pool=%s avail_virtual=%.0f "
			"MiB largest_free=%.0f MiB reserve=%.0f MiB walk_ms=%d",
			g_stem, policy.budgetBytes / 1048576.0,
			tomb5.hd_texture_budget_mb ? "hd_texture_budget_mb" : "worked out",
			policy.format == HD_PAGE_FORMAT_DXT5 ? "dxt5" : "rgba",
			(double)DXTextureMemoryAtStart / 1048576.0, (int)nTextures,
			(int)App.dx.PresentParams.BackBufferWidth,
			(int)App.dx.PresentParams.BackBufferHeight,
			kPagesManaged ? "managed" : "default",
			room.known ? room.availVirtual / 1048576.0 : 0.0,
			room.known ? room.largestFree / 1048576.0 : 0.0,
			HD_REDRESS_ADDRESS_RESERVE / 1048576.0, room.walkMs);
	}

	const DWORD started = GetTickCount();

	g_clipped_lights = 0;
	g_clipped_bulbs = 0;
	g_clock = 0.0;
	g_clock_started = false;
	BuildWaterMarks();
	// No pieces until ReportRoomIntrusions has this level's.
	memset(g_piece_count, 0, sizeof(g_piece_count));
	g_pieces.clear();
	BuildRoomBoxes();		// before the build, like the marks
	BuildRoomIntrusions();		// the same contract
	BuildPairFaces();		// and its faces, the same contract
	BuildRoomCuts();		// the same contract again

	if (!HDRenderLevelBegin(HDInstallRoot(), g_stem, number_rooms,
		kPagesManaged, DecodeThreads(), &stats, &why))
	{
		Log("HDLOG level=%s build failed reason=%s", g_stem, why);

		/* The page cache's own notes before it is emptied - on the way out
		 * they are the only place that says which page the card gave up on.
		 * Without this the log names the failure and not the cause. */
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
	HDRenderSetRoomBoxes(0, 0);	// the same contract

	/* What the build made of the intrusion list - which groups carry art
	 * standing inside another room, and how much. It reads the counts the
	 * build left behind and gives the borrowed list back. */
	ReportRoomIntrusions();
	// And the same for the openings. Before the room lines below, so the log
	// reads in the order the build ran.
	ReportRoomCuts();

	/* How many of the level's draw calls are drawn with their back faces
	 * culled and how many two-sided - see RangeCulls in hd_render.cpp. */
	if (gLogEnabled)
	{
		int culled = 0;
		int both = 0;

		HDRenderCullRanges(&culled, &both);
		Log("HDLOG cull level=%s ranges opaque=%d two_sided=%d", g_stem,
			culled, both);
	}

	/* `flooded` is how many of the two shimmer sets no classic corner
	 * named and the plane answered for - the number that says whether a
	 * level's water animates over its whole surface or over a sliver of it -
	 * and `planes_dropped` must stay 0 (see HDRenderStats). */
	Log("HDLOG level=%s water stamped=%d choppy=%d shimmer=%d room_shimmer=%d "
		"flooded=%d planes_dropped=%d ps_slots=%lu",
		g_stem, stats.markedVertices, stats.markedChoppy, stats.markedShimmer,
		stats.markedRoomShimmer, stats.markedFlooded,
		stats.waterPlanesDropped, HDRenderPixelSlots());

	// Everything that moves, counted out of the .TRG itself: scroll =
	// material 1 parts and the draw calls they cost, sway = vertices with a
	// wind weight, flipbook = material 13 parts. Of those, flipbook_pages is
	// how many run as a four page flipbook (their own page plus the three
	// layers their material names, one per frame time), flipbook_dropped how
	// many kept their own page because the file's numbers did not resolve,
	// and flipbook_ranges the draw calls the running ones cost.
	// cutout_parts is the opaque parts of materials 6, 7 and 8 - the ones
	// that draw with the alpha test on. Held against cutout_pages in the
	// frame line below, the two say whether a page the alpha measurement
	// calls a cut-out sheet is really used as one.
	// add_weighted / add_min / add_swinging is the additive
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

	/* The still water surfaces the .TRG builds itself - material 16.
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
		/* The fourth way a room stays classic: a hollow group.
		 *
		 * A group can have triangles and still draw nothing inside its own
		 * room. ANDREA2's room 124 - the ground floor of the courtyard with
		 * the palms - has four, and all four stand outside the room box;
		 * called HD on `tris` alone, its classic roomlets would be dropped
		 * and a black hole would show where the doorways are. Nine rooms in
		 * the game are hollow under this rule with HD_OVERHANG_SLACK:
		 * ANDREA2 124, JOBY2 19 and 21, RICHCUT2 2, 3 and 4, TITLE 70, 101
		 * and 103. (ANDREA1's room 4 is hollow only without the slack: all
		 * 216 of its triangles stand within 128 units of the box edge, so it
		 * stays HD - `RoomProbe -roombox <classic folder> -window inside`
		 * draws the nine at 0 draw calls and room 4 at 1.)
		 *
		 * The classic geometry of such a room is the only geometry there
		 * is, so it is drawn, exactly as for a room the .TRG never had. */
		const bool hollow = tris > 0 &&
			HDRenderGroupOverhangTriangles(r) >= tris;

		/* The third way a room stays classic: a randomizer marker. The first
		 * two are the .TRG's own: no group for this room, or a group with no
		 * triangle in it - which is what a Make_Room_Geometry room looks like
		 * and needs no marker. This one is the randomizer's: the run lifted
		 * the room with Move_Room, or built it a surface it never had.
		 * Neither leaves anything of the remaster's art worth keeping, so the
		 * room is drawn classic, and the classic geometry is the randomized
		 * geometry - read out of the level file the run wrote.
		 *
		 * A wall cut open, a window added, a floor or ceiling opened does not
		 * turn a room classic: that would cost whole rooms for a doorway
		 * (four of ANDY3's 211 rooms with the glitchless secret list) and the
		 * seams between HD and classic floors would show the void. Those
		 * rooms keep their art and lose only the box round the opening; see
		 * BuildRoomCuts above. */
		const bool marked = HDMarkersRoomTouched(r);
		// And how much art an opening took out of this room.
		const int cut_tris = HDRenderGroupCutTriangles(r);

		const char* reason = !has_group ? "no_group"
			: (!tris ? "empty_group"
			: (hollow ? "hollow_group"
			: (marked ? "marker" : (cut_tris ? "cut" : "ok"))));

		if (marked && has_group && tris)
			marked_rooms++;

		/* Per group; the loop runs over rooms only because group i is room i
		 * while this runs - no flip has happened yet, and the marker that
		 * can take a group away is about the room the randomizer
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

	/* The flip pairs of this level, one line each and only with -log. A pair
	 * whose two halves disagree about HD turns a room classic (or HD) in
	 * mid-level, a visible change, so it is named in the log. */
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
	 * `cuts` are the openings, which cost a box of triangles each and no
	 * room at all. */
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

	/* The budget: what was allowed, what the rooms of this level wanted, and
	 * what it cost them to fit. `dropped_mip0` is pages that gave up their
	 * largest level to the budget; `caps` is pages the device could not hold
	 * at full size, which no setting changes. The models and Lara add their own
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

	/* The static meshes. A .TRG room has the level's static meshes baked into
	 * its art, so DrawStaticObjects must not draw them a second time - the
	 * "double lantern" TROne guards against (game_renderer.cpp:3722-3736, a
	 * baked mesh pointer passes only while the moveable that claims it is
	 * streaming).
	 *
	 * This seam is narrower and the rule with it: a room's own MESH_INFO
	 * list is suppressed, and only for a room that really draws HD. A
	 * moveable that shares a static's MESH_DATA* goes through
	 * DrawAnimatingItem and is never touched, and a room that stayed classic
	 * keeps every static it had, so TROne's exception cannot arise here.
	 *
	 * A static the game removes at run time - a shattered vase - is gone from
	 * the classic list but still stands in the baked art; the destructible
	 * pairs of BuildStaticTables hide its piece. BuildStaticTables also finds
	 * the statics the .TRG does not cover. */
	BuildStaticTables();

	/* The water surface the remaster left out - see the note over
	 * BuildWaterSurfaces. After the loop above, because it asks per room
	 * whether that room draws HD at all. */
	BuildWaterSurfaces();

	/* And the HD pictures of that water's own animation frames. After
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

	/* The objects, after the rooms and never before them: a model's pages
	 * are appended to the cache the rooms built, and a page
	 * slot handed out before that cache exists points nowhere.
	 *
	 * They ride on the rooms in one more way. A level whose .TRG is missing
	 * or empty has no page cache at all, so it has no HD objects either -
	 * which is the same answer the rooms give and one line to explain. */
	HDObjectLevelBuild(HDInstallRoot(), g_stem);

	/* The mark the re-dress allowance is measured from: what this level's own
	 * build left in the page cache. Everything above it is what outfit
	 * changes have added. */
	g_redress_bytes0 = HDPageCacheBytes();

	/* And the rule's first answer, after the mark and after every
	 * page this level is going to have of its own. The page block is worked
	 * out here and not per question: the format is the policy's and the side
	 * is the widest page there is, clamped by what the device said it can
	 * hold - both settled by now and neither of them changing again inside
	 * this level. */
	{
		HDPageCachePolicy policy;

		HDPageCachePolicyNow(&policy);

		const int side = policy.maxSide > 0 && policy.maxSide < kWidestPage
			? policy.maxSide : kWidestPage;

		g_redress_page_block =
			HDRedressPageBlock(policy.format == HD_PAGE_FORMAT_DXT5, side);
	}

	g_redress_said_unknown = false;
	RefreshRedress("the level build", true);
}

bool HDLevelRedressAllowed()
{
	if (!HDEnabled() || !g_level_active)
		return false;

	/* The stored answer and not a fresh one: this is asked on every
	 * drawn frame the F4 window is open. What changes it - a level build, a
	 * completed change, the window opening - asks RefreshRedress itself. */
	return g_redress_verdict.allowed != 0;
}

void HDLevelRedressRefresh(const char* why)
{
	if (!HDEnabled() || !g_level_active)
		return;

	RefreshRedress(why, true);
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

	/* Measured afresh, and this measurement is the decision. The stored
	 * answer is what the window has been showing; the change itself is
	 * decided by the room at the moment it is asked for. */
	RefreshRedress(why, false);

	if (!g_redress_verdict.allowed)
	{
		Log("HDLOG redress deferred to the next level (%s): %d change%s "
			"in this level have already added %.1f MiB of pages and the "
			"allowance is %.0f - refused by %s, room card=%.0f addr=%.0f "
			"largest_free=%.0f MiB and one change needs %.0f MiB (a page "
			"needs %.0f)", why ? why : "?", g_redress_count,
			g_redress_count == 1 ? "" : "s",
			(HDPageCacheBytes() - g_redress_bytes0) / 1048576.0,
			HD_REDRESS_ALLOWANCE / 1048576.0,
			HDRedressRefusalText(g_redress_verdict.refused),
			g_redress_verdict.cardRoom / 1048576.0,
			g_redress_verdict.addrRoom / 1048576.0,
			g_redress_room.largestFree / 1048576.0,
			HD_REDRESS_WORST / 1048576.0, g_redress_page_block / 1048576.0);
		return false;
	}

	const DWORD began = GetTickCount();
	const int slots0 = HDPageCacheSlots();
	const double bytes0 = HDPageCacheBytes();
	const double avail0 = g_redress_room.availVirtual;

	/* The same two calls a level change makes, in the same order and on the
	 * same thread. HDObjectRelease gives the models' vertex and index buffers
	 * back and nothing of the page cache - it never shrinks until
	 * HDPageCacheDestroy, so no slot index another model holds can move,
	 * which makes this safe. HDObjectLevelBuild reads the .MAP again, asks
	 * hd_laradraw for the wardrobe (which reads the registry key the menu has
	 * just written) and builds the lot. The rooms are not touched: their
	 * cache is the one the new pages are appended to, as at a level build. */
	HDObjectRelease();
	HDObjectLevelBuild(HDInstallRoot(), g_stem);

	g_redress_count++;

	/* What this change cost the process, measured the same way. The drop in
	 * avail_virtual against the pages added is the pool's real price in
	 * address space, and it shows whether the measurement this rule rests on
	 * holds on the running machine: with D3DPOOL_DEFAULT pages it is a
	 * rounding error beside the MiB of pages; if the log shows it tracking
	 * them, the pages are not in the pool the rule was measured for. */
	RefreshRedress(why, false);

	Log("HDLOG redress %d (%s) ms=%d pages %d -> %d (+%d), cache %.1f -> "
		"%.1f MiB (+%.1f), this level has spent %.1f of %.0f MiB, room "
		"card=%.0f addr=%.0f largest_free=%.0f MiB, avail_virtual %.0f -> "
		"%.0f MiB (-%.1f for +%.1f MiB of pages)",
		g_redress_count, why ? why : "?", (int)(GetTickCount() - began),
		slots0, HDPageCacheSlots(), HDPageCacheSlots() - slots0,
		bytes0 / 1048576.0, HDPageCacheBytes() / 1048576.0,
		(HDPageCacheBytes() - bytes0) / 1048576.0,
		(HDPageCacheBytes() - g_redress_bytes0) / 1048576.0,
		HD_REDRESS_ALLOWANCE / 1048576.0,
		g_redress_verdict.cardRoom / 1048576.0,
		g_redress_verdict.addrRoom / 1048576.0,
		g_redress_room.largestFree / 1048576.0, avail0 / 1048576.0,
		g_redress_room.availVirtual / 1048576.0,
		(avail0 - g_redress_room.availVirtual) / 1048576.0,
		(HDPageCacheBytes() - bytes0) / 1048576.0);

	// The new body may walk on something else than the old one did.
	HDOutfitMenuFollowFeet();
	return true;
}

void HDLevelUnload()
{
	/* Not behind HDEnabled(): a level built while HD was on has to come down
	 * even when HD was switched off in the options in the meantime; asking
	 * the setting here would let a vertex buffer outlive its level.
	 *
	 * And not a single Direct3D call here. FreeLevel runs at the top of
	 * LoadLevel, on the load thread, while the main thread presents the
	 * loading bar - two threads on one device freeze the game
	 * (specific\file.cpp, the comment at the end of LoadLevel). The
	 * objects are released in HDLevelBuild instead, on the thread that owns
	 * the device, immediately before the next level's are made; and DXClose
	 * releases them through HDLevelDeviceLost when the device itself goes. */
	g_level_active = false;
	g_frame_count = 0;
	g_room_count = 0;
	g_stem[0] = 0;
	/* The static tables point into this level's rooms. */
	g_static_classic.clear();
	g_static_first.clear();
	g_destruct_pairs.clear();

	/* And so does the water surface table - a roomlet number and a face
	 * number in the room that is being freed. */
	for (int i = 0; i < HD_MAX_ROOMS; i++)
		g_water_faces[i].clear();

	g_water_face_total = 0;
	g_water_face_rooms = 0;
	g_water_face_kept = 0;
	/* And the frame table with it - it names texinfos of the level being
	 * freed. The pages those frames point at are Direct3D objects and are
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
	 * here as well as in HDLevelBuild: without it the level the game quit
	 * from would never get its FPSLOG summary. Main
	 * thread in every one of those cases. */
	LevelDiagnosticsEnd();

	g_level_active = false;
	g_frame_count = 0;
	g_room_count = 0;
	// The borrowed water frame pages are references on cache textures that
	// HDRenderShutdown is about to drop.
	ReleaseWaterFramePages();
	HDObjectDeviceLost();

	// The font atlas was made on this device too. Its match table survives;
	// only the texture goes.
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

/* The one question the room walk asks of the HD path. g_level_active is
 * false with hd_enable off, before a level is built, after HDLevelUnload and
 * in a level whose .TRG had no rooms - and in every one of those the walk in
 * game\draw.cpp carries no reach and is the classic walk. */
bool HDLevelActive()
{
	return g_level_active && HDEnabled();
}

/* The classic water faces of one HD room. Read by
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

/* One FlipMap swap.
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

	/* And the restored water faces with it. They are a roomlet number and a
	 * face number inside a ROOM_INFO, and FlipMap is the statement where
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
	// Whatever glow phase was still owing belongs to a frame that is over.
	// A new frame's HDFrameDraw sets it again.
	g_frame_pending = false;
	HDObjectFrameReset();
}

/* The projection of this frame, taken the moment the first HD thing turns up -
 * a room out of InsertRoom or an item out of DrawAnimatingItem, whichever the
 * engine reaches first, and never twice. It is taken while the world is
 * collected and not when it is drawn, because AlterFOV moves f_persp and the
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

/* The three numbers the frame's one projection is built from, for the
 * inventory ring's own screen centre. See the note in hd_level.h. */
void HDFrameProjectionInfo(float* persp, float* centerX, float* centerY)
{
	if (persp)
		*persp = g_persp;

	if (centerX)
		*centerX = g_centerx;

	if (centerY)
		*centerY = g_centery;
}

/* The pickup display's own bracket. Why it is a bracket of its own rather
 * than a place on the frame list, and which numbers are its own rather than
 * the frame's, stands in hd_level.h over the declaration. */
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

	/* No fog bulbs, as the ring has none and for the same reason - see the
	 * block over HDFrameDraw. The distance fog is left in, as the ring leaves
	 * it in: the classic display runs the same subtraction
	 * (aTransformLightClipMesh subtracts from every vertex past
	 * DistanceFogStart whoever called it) and the object stands at
	 * INVOBJ::scale1 - a few hundred to a couple of thousand units - where
	 * neither path reaches the threshold, so both paths agree. */
	frame.bulbs = 0;
	frame.bulbCount = 0;

	/* The clock is read and never stepped. AdvanceClock is
	 * FillFrameLighting's, once per frame; this bracket runs inside a frame
	 * that has already had one, so stepping it here would count the frame
	 * twice - and a material would run at double speed for as long as a
	 * pickup icon is on the screen. */
	frame.clock = (float)g_clock;
	frame.motion = tomb5.hd_motion ? 1 : 0;
	frame.reflect = tomb5.hd_reflect ? 1 : 0;

	HDRenderFrameBegin(&kIdentity, &g_pickup_projection, &frame);

	/* HD_PASS_ALL and not the frame's two phases. The two phases exist so
	 * that no classic opaque draw can paint over a glow
	 * that wrote no depth; here there is nothing else in the section at all -
	 * the depth buffer was cleared a few lines up and the display is the one
	 * thing drawn into it - so the solid and the additive pass of the one
	 * object follow each other in the one bracket, as in ModelProbe. */
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
		// entry lands here per listed room, so this cannot happen; the test
		// guards the fixed array against a silent overrun.
		g_frame_dropped++;
		return false;
	}

	HDFrameCaptureProjection();

	FrameRoom& entry = g_frame[g_frame_count++];
	entry.room = roomIndex;
	entry.group = group;
	entry.view = ViewSerial;
	SetD3DMatrix(&entry.world, (float*)viewWorld);
	/* The portal window, taken here and not at draw time. These four
	 * numbers are the engine's own `r->left..r->bottom` and they do not
	 * survive the frame - PrintObjects (game\draw.cpp) resets all four of
	 * them at the end of each room's items, which is before S_OutputPolyList
	 * draws anything HD. */
	ClipWindow(entry, left, top, right, bottom);
	/* And whether this room has art that needs the window. Asked of the
	 * group really drawing it and not of the room index: after a flipmap the
	 * two are different rooms' art. */
	entry.windowed = group >= 0 && group < HD_MAX_ROOMS &&
		g_group_windowed[group] != 0;
	entry.scissoredTris = 0;

	/* The corner rule for the classic faces this room's intruding art
	 * belongs to, here and now: this is InsertRoom, where the classic path
	 * transforms the roomlets it is skipping, with the same matrix, the same
	 * projection and the same window. DecidePairs reads the answers. */
	if (entry.windowed && !entry.full && !ClipEmpty(entry))
	{
		const int first = g_piece_first[group];
		const int count = g_piece_count[group];

		for (int j = 0; j < count; j++)
		{
			const int face = g_pieces[(size_t)(first + j)].face;

			if (face >= 0 && (size_t)face < g_face_alive.size())
				g_face_alive[(size_t)face] = ClassicFaceDrawn(
					g_pair_faces[(size_t)face], entry.world,
					left, top, right, bottom) ? 1 : 0;
		}
	}

	return true;
}

/* An item is not cut by its room's window.
 *
 * The classic path hands every item the whole screen: PrintObjects
 * (game\draw.cpp) sets phd_left = 0, phd_top = 0, phd_right = phd_winxmax +
 * 1, phd_bottom = phd_winymax + 1 before its item loop and puts them back
 * afterwards. Only the room walk uses `r->left..r->bottom`. Cutting an item
 * to its room's portal window would clip an enemy seen through a doorway,
 * which the classic path draws whole - a soldier at the wall not drawn at all
 * while his muzzle flash still fires, a creature near the camera visible only
 * on one side of a straight edge.
 *
 * So the scissor test is off for every item. The frame list still carries
 * the windows and the rooms are still drawn inside them; with -log this
 * counts how often a per-room item scissor would have cut an item (an HD
 * item in a room that overlaps another could otherwise show through a
 * wall). */
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

/* The HD sky, in the classic horizon's place.
 *
 * Called from SkyDrawPhase (game\draw.cpp) at the line where the classic path
 * draws its HORIZON mesh, with the matrix that is on the engine's stack right
 * there - phd_TranslateAbs(camera.pos), no rotation, no scale. Returns true
 * when the .TRG's own sky was drawn, and the caller then leaves out the
 * horizon mesh and the two flat colour layers, which is exactly what the dome
 * replaces. A level whose .TRG has no sky group (JOBY3, JOBY5) answers false
 * and keeps the classic sky whole.
 *
 * Here and not in HDFrameDraw, where the rooms are drawn: the classic sky
 * writes no depth (OutputSky switches D3DRS_ZWRITEENABLE off), so the order
 * inside SkyDrawPhase is the order on the screen: the moon first, then the
 * sky, then the stars. Drawn with the rooms instead, the dome would paint
 * over the moon that was drawn a moment earlier. Here it takes the horizon
 * mesh's own place in that order and nothing else moves.
 *
 * The projection is taken fresh and HDFrameCaptureProjection is not used: SkyDrawPhase moves f_persp itself while the binoculars are up
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
	frame.reflect = tomb5.hd_reflect ? 1 : 0;

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

/* The frame in two phases.
 *
 * The first two passes write depth and the third does not (hd_render.cpp,
 * draw_pass: pass 2 is ONE / ONE with D3DRS_ZWRITEENABLE FALSE). Drawn
 * room-major - all passes of one room, then the next, then the objects, then
 * the classic buckets - every later draw that writes depth and stands behind
 * a glow passes the depth test (the glow left no depth to fail against) and
 * paints over it: a RICH2 wall lamp whose cone hangs in a portal is cut off
 * along the portal's edge by the far wall of the room beyond, and an
 * object's glow disappears behind a classic crate.
 *
 * So the order is the classic engine's own: everything that writes depth
 * first, whatever it belongs to, then everything that does not.
 *
 *   HDFrameDraw       every room's passes 0 and 1, then every HD object and
 *                     Lara's passes 0 and 1
 *   DrawBuckets       the classic opaque world (output.cpp)
 *   HDFrameDrawGlow   every room's pass 2, then every object's pass 2
 *   DrawSortList      TR5's own see-through polygons, last of all
 *
 * so an HD glow is behind the classic sort list and in front of every opaque
 * thing it should be in front of - as the classic path does with its own
 * additive polygons.
 *
 * The cost is one more state bracket per frame (HDRenderFrameBegin does a
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

	/* The inventory ring has no fog bulbs. ActiveFogBulbs holds positions
	 * the game already turned into view space, for the view the
	 * last played frame had. The inventory draws through a view of its own -
	 * an identity matrix with the item pushed a few hundred units down +z
	 * (game\newinv2.cpp, DrawThreeDeeObject2D) - so those coordinates mean
	 * nothing here, and a bulb of the room Lara is standing in would sit in
	 * front of her medipacks and tint them. The classic ring has no bulbs
	 * either: phd_PutPolygonsPickup runs no bulb pass at all.
	 *
	 * The distance fog is not taken away with them. The classic ring keeps it
	 * (aTransformLightClipMesh subtracts from every vertex past
	 * DistanceFogStart whoever called it) and a ring item stands a few
	 * hundred units from the camera, far short of the threshold on either
	 * path, so both paths agree. */
	if (HDObjectRingFrame())
		frame.bulbCount = 0;

	// A destructible piece follows its own static's Flags bit.
	UpdateDestructibles();
	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	// This frame's windows, counted for the log below.
	g_clip_full = 0;
	g_clip_scissored = 0;
	g_clip_empty = 0;
	g_clip_open = 0;
	g_clip_windowed_tris = 0;
	g_clip_pairs = 0;
	g_clip_pairs_whole = 0;
	g_clip_pairs_scissored = 0;
	g_clip_items_spared = 0;	// counted by HDFrameScissorForRoom

	for (int i = 0; i < g_frame_count; i++)
	{
		// Whole or windowed, per piece - for this phase and the glow one.
		DecidePairs(g_frame[i]);

		const FrameRoom& entry = g_frame[i];
		const bool empty = ClipEmpty(entry);

		if (!entry.windowed)
			g_clip_open++;			// no window at all
		else if (empty)
			g_clip_empty++;
		else if (entry.full)
			g_clip_full++;
		else
			g_clip_scissored++;

		/* Every collected room is drawn: no room's ordinary art is windowed,
		 * so nothing may be skipped here. An empty window silences only the
		 * intruding half, inside DrawRoomWindowed. */
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

	/* The rooms are done. The objects switch the scissor off per item, so
	 * this line is for the frame that has none: without it the last room's
	 * window would still be in force when the bracket closes, and the
	 * classic path must get the device state it had. */
	HDRenderScissorOff();

	/* The objects, after the rooms and inside the same bracket.
	 *
	 * One bracket a frame: hd_model sets its own shader, declaration and
	 * stream and finds the rest of the device state exactly as the room path
	 * left it, which is why hd_render splits the bracket into a save and a
	 * restore instead of hiding it inside a room draw. It also means the fog
	 * and the fog bulbs are already in their registers - a model reads the
	 * very ones a room was just given.
	 *
	 * After the rooms and before DrawBuckets. The depth buffer is shared and
	 * exact (hd_render.cpp, HDRenderSelfTest: both paths write a - b/z for
	 * the same point), so an opaque HD guard and an opaque classic crate sort
	 * themselves out by depth whatever order they are drawn in. Only the pass
	 * that does not write depth cares:
	 *
	 *   - A model's alpha pass writes depth (it is an alpha test, not a
	 *     blend), so it behaves like the opaque one and travels with it.
	 *   - A model's additive pass does not, so it is drawn in
	 *     HDFrameDrawGlow, after the classic buckets, where nothing opaque
	 *     can follow it. The classic sort list, where TR5 puts its own
	 *     see-through polygons, still draws last of all and over both.
	 *
	 * Drawing the objects inside the room loop would be worse for a reason
	 * that has nothing to do with depth: a model's state is not a room's, and
	 * swapping shader, declaration and stream twice per room costs more than
	 * the whole object path does. */
	HDObjectFrameDraw(&projection, frame.bulbCount, HD_PASS_SOLID);

	HDRenderFrameEnd();

	// The glow phase is owing, and this is what the rooms cost so far -
	// HDRenderFrameBegin zeroes the renderer's counter per bracket.
	g_frame_room_calls = HDRenderDrawCalls();
	g_frame_pending = true;
}

/* The glow phase. Called from S_OutputPolyList right after DrawBuckets - see
 * the note over HDFrameDraw for the order. Everything drawn here writes no
 * depth and is tested against a depth buffer that holds the whole opaque
 * frame, HD and
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

	/* This phase walks the same items the solid one did, so the count starts
	 * over here and the clip line below - written from this phase - reports
	 * one frame's items and not two phases' worth. */
	g_clip_items_spared = 0;

	D3DMATRIX& projection = g_frame_projection;
	HDFrameLighting& frame = g_frame_light;
	const int objects_here = g_frame_objects;

	HDRenderFrameBegin(&kIdentity, &projection, &frame);

	/* The glass first and the glows after it. Both are drawn without a depth
	 * write against a depth buffer that holds the whole opaque frame; the
	 * order between them is the classic path's own - a see-through surface
	 * is a surface, and a glow is painted on top of whatever it shines
	 * through.
	 *
	 * The same window per room in this phase as in the solid one: a glow is
	 * a surface of its room like any other - the RICH2 lamp cone hangs inside
	 * a portal and is cut by that portal's window and no other.
	 * DrawRoomWindowed asks ClipEmpty of the same four numbers HDFrameDraw
	 * used, so both phases treat every room alike. */
	for (int i = 0; i < g_frame_count; i++)
	{
		DrawRoomWindowed(g_frame[i], HD_PASS_BLEND);
	}

	for (int i = 0; i < g_frame_count; i++)
	{
		DrawRoomWindowed(g_frame[i], HD_PASS_ADDITIVE);
	}

	// The rooms are done - see the same line in HDFrameDraw.
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

	/* How many water surfaces one frame drew. Throttled the way the
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

	/* The portal windows of this frame. Two lines, both behind -log and both
	 * throttled, because this runs once per drawn frame:
	 *
	 *   once per level, at the first frame that drew an HD room, one line per
	 *   room with the window it got - the numbers a picture of a bleed can be
	 *   held against;
	 *
	 *   once a second after that, the tally. GlobalCounter ticks thirty times
	 *   a second whatever the frame rate is, so thirty of it is one second of
	 *   game time. */
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
			/* One camera per picture, as a number.
			 *
			 * `view=` is the ViewSerial the rooms were collected with and
			 * `view_obj=` the one the HD objects were. At sixty hertz the
			 * view is rebuilt twice per in-between frame - the blended eye
			 * on the way in, the live one on the way out - so anything
			 * collected on the wrong side of either is drawn from a camera
			 * the rest of the frame does not share (a room seam that walks
			 * with Lara's breathing).
			 *
			 * A healthy frame prints one number for each, and the two are
			 * equal. A range (`a..b`) names the half of the frame that was
			 * collected late. */
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

			/* `pairs` `whole` `scissored` at the end are the pieces of
			 * intruding art DecidePairs asked about, and its two answers -
			 * the second `scissored` counts pieces, the first rooms. */
			Log("HDLOG clip rooms=%d open=%d full=%d scissored=%d empty=%d "
				"windowed_tris=%d items_spared=%d view=%s view_obj=%s "
				"pairs=%d whole=%d scissored=%d",
				g_frame_count, g_clip_open, g_clip_full, g_clip_scissored,
				g_clip_empty, g_clip_windowed_tris, g_clip_items_spared,
				views, viewsObj, g_clip_pairs, g_clip_pairs_whole,
				g_clip_pairs_scissored);
		}
	}

	/* The frame's HD counters, handed to the frame clock as maxima over the
	 * second (see FrameClockFlushSecond). Draw calls are rooms plus objects,
	 * which is every call the HD path made in this frame. Costs three
	 * comparisons and only when the log is on. */
	if (gLogEnabled)
	{
		/* g_frame_room_calls is the two brackets of this frame added up -
		 * HDRenderDrawCalls is reset by HDRenderFrameBegin and would answer
		 * for the glow phase alone. HDObjectDrawCalls is not reset per frame
		 * at all - hd_object.cpp resets it per level - so it is only
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

		/* Once per level, at the first frame that clips - not once per frame
		 * (see the note over the counters). The total goes into the level's
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

/* One presented frame. Called from DXShowFrame (specific\dxshell.cpp) straight
 * after Present returns - the note beside the frame clock's counters says why
 * that call site and no other. */
void HDFramePresented()
{
	// The guard. Without -log this is a single bool test and a return: no
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

	/* The next second starts one second after this one unless the gap was
	 * bigger than two - a level load presents nothing for a while, and a
	 * catch-up loop would write a run of empty lines about a second nobody
	 * drew in. Then it simply starts again here. */
	if (now.QuadPart - g_qpc_second < 2 * g_qpc_freq)
		g_qpc_second += g_qpc_freq;
	else
		g_qpc_second = now.QuadPart;

	FrameClockNewSecond();
}

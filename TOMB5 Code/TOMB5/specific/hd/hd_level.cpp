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
#include "hd_switch.h"			// HDDrawn - the world drawn HD or classic
#include "hd_memory.h"			// the address space, and the rule that spends it
#include "hd_outfitmenu.h"		// the footsteps follow the outfit
#include "hd_install.h"
#include "hd_bind.h"
#include "hd_lara.h"			// HD_LARA_OUTFIT_* - the data[3] agreement
#include "hd_markers.h"
#include "hd_scan.h"
#include "hd_levelkey.h"		// the HD data kept across a same-level load
#include "../registry.h"		// REG_PeekString - the outfit in the key
#include "hd_render.h"
#include "hd_vis.h"			// the visibility path's tables
#include "hd_book.h"			// HDBookClock - the time the material 15 books turn on
#include "hd_object.h"
#include "hd_model.h"			// HDModelCastBegin - the projected shadow's map
#include "hd_laradraw.h"		// HDShadowFrameState - whether it runs
#include "hd_shadow.h"
#include "hd_font.h"			// the HD font's texture goes with the device
#include "hd_life.h"			// the water's life - floats, fish, drips
#include "hd_spring.h"			// the springs - rings, bubbles, column
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
#include "../../game/tomb4fx.h"		// ripples - the rings on the sheets
#include "../../game/rat.h"			// Rats - the creatures' mark
#include "../../game/gameflow.h"		// gfLevelId
#include "../../game/lara.h"		// GfHdOutfit - data[3]; lara, lara_item
#include "../../game/interpolate.h"	// InterpFrameAlpha - the state this frame draws
#include "../drawroom.h"			// ActiveFogBulbs, aRoomVertexWaterMarks
#include "../file.h"				// textinfo, aranges, nAnimUVRanges
#include "../gamemain.h"			// WaterTable, vert_wibble_table
#include "../LoadSave.h"			// MonoScreenOn - a menu over a picture
#include "../drawbars.h"			// S_LoadBarBuild - the loading bar
#include "hd_light.h"
#include "hd_splash_math.h"		// the water's cards: HDFilmStrength
#include "hd_water_under.h"		// HDWaterSurfaceRoom - the sea's surface

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

// Whether the level was built for the visibility path (BuildRoomVis).
static bool VisLevel();

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

	// Set by HDLevelBuild and by nothing else: this level was built HD at
	// its load. The frame path asks it through HDLevelActive, together with
	// the HD textures row and the HD/SD switch, which change that answer in
	// a running level; the build itself follows the row at a load only.
	bool g_level_active = false;

	/* Whether the level was drawing HD when its device went (DXClose on
	 * Alt+Enter or a resolution change), so that HDLevelDeviceRecreated
	 * builds it again on the new device. HDLevelUnload clears it: a level
	 * that has gone is never built again. */
	bool g_rebuild_wanted = false;

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
	 *                 the art. HDRoomIsHD reads g_group_hd[g_room_group[r]];
	 *                 whether a room's static meshes are suppressed is
	 *                 HDStaticDrawnClassic's answer, read through the same
	 *                 table.
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
	/* Per entry of the level's intrusion list, numbered as
	 * HDPairRange::intrusion numbers them: the triangles no face claims that
	 * stand outside the box one sector smaller, which the build hands to the
	 * open half (HDRenderIntrusionBand). Kept for the level for the reachart
	 * line; their sum is the level line's band_unclaimed. */
	std::vector<int> g_pair_band;
	int g_band_unclaimed = 0;
	/* Per entry of g_pieces, the wall its claimed face is part of among the
	 * claimed faces of its pair (HDVisWallLabels): the index in g_pieces of
	 * the pair's first piece on a face of that wall, -1 for a piece no face
	 * claims. Built by the level's first DecidePairs on the visibility path
	 * (BuildVisWalls), once the pieces exist; BuildRoomVis drops the last
	 * level's. */
	std::vector<int> g_piece_wall;
	bool g_piece_wall_built = false;
	int g_piece_first[HD_MAX_ROOMS];
	int g_piece_count[HD_MAX_ROOMS];
	short g_room_group[HD_MAX_ROOMS];
	int g_room_count = 0;
	int g_flips_done = 0;		// for the log, once per level

	/* A room drawn classic whose faces another group's art stands on - see
	 * hd_cover.h and BuildStandFaces. Per group, like g_group_hd, and read
	 * through g_room_group, so a flipmap that swaps which group draws a room
	 * keeps both right:
	 *
	 *   g_group_stand          the group is drawn classic and art of some
	 *                          HD group stands on its faces (a stand range)
	 *   g_group_stand_covered  and the carrier's art covers every face, so
	 *                          the room is drawn as that art and its roomlets
	 *                          never
	 *   g_group_stand_part     or at least half of them: those are
	 *                          left out of the roomlets (g_group_drop) and
	 *                          drawn as the carrier's art, the rest stay
	 *   g_group_stand_carrier  the group whose art covers the most faces
	 *                          (HDCoverCarrierBetter), -1 for a room no
	 *                          group covers a face of, one its carrier
	 *                          covers fewer than half of, or one of the
	 *                          guards keeps whole
	 *   g_group_stand_shadow   a room drawn as the carrier's art, wholly or
	 *                          in part, that keeps no classic face Lara can
	 *                          stand on (HDCoverFaceFloor), and a classic
	 *                          room with no classic face and no bare floor:
	 *                          it counts as an HD room for her shadow
	 *                          (HDRoomShadowHD)
	 *
	 * `g_group_drop` is, per group of a room drawn in part, one flag per face
	 * of its roomlets in InsertRoomlet's order (the quads, then the
	 * triangles, roomlet after roomlet), 1 for a face the carrier covers, and
	 * `g_group_drop_first` where each roomlet's run starts. For the log, the
	 * faces of any area, the covered ones and the kept floors.
	 *
	 * `g_stand_pieces` is every stand range of an HD group whose holder is
	 * such a room, answered per frame by DecideStand. */
	unsigned char g_group_stand[HD_MAX_ROOMS];
	unsigned char g_group_stand_covered[HD_MAX_ROOMS];
	unsigned char g_group_stand_part[HD_MAX_ROOMS];
	short g_group_stand_carrier[HD_MAX_ROOMS];
	unsigned char g_group_stand_shadow[HD_MAX_ROOMS];
	short g_group_stand_faces[HD_MAX_ROOMS];
	short g_group_stand_faces_covered[HD_MAX_ROOMS];
	short g_group_stand_floors_kept[HD_MAX_ROOMS];
	std::vector<std::vector<unsigned char> > g_group_drop;
	std::vector<std::vector<int> > g_group_drop_first;

	struct StandPiece
	{
		int group;		// the HD group whose art it is
		int index;		// its stand range in that group
		int holder;		// the classic-drawn group it stands in
		int kind;		// HDStandRange::kind
		int faces;		// HDStandRange::faces
	};

	std::vector<StandPiece> g_stand_pieces;
	// Every room's origin and twin, kept for the frame's matrices.
	std::vector<HDStandRoom> g_stand_rooms;

	// Water marks, built once per level out of the classic roomlets and
	// handed to the renderer; the counters below say once per level whether
	// the shader's fixed maxima ever bit.
	std::vector<HDWaterMark> g_water_marks;

	/* The water surface the .TRG has no geometry for.
	 *
	 * One entry per classic room face that is a water surface and that the
	 * .TRG has no art for. See BuildWaterSurfaces for the measurement; the
	 * faces are kept sorted by roomlet so that drawroom.cpp transforms and
	 * lights a roomlet once and then picks its water faces out of it. */
	std::vector<HDRoomWaterFace> g_water_faces[HD_MAX_ROOMS];
	int g_water_face_total = 0;
	int g_water_face_rooms = 0;
	int g_water_face_kept = 0;	// faces the .TRG does cover - left to it
	int g_water_face_dry = 0;	// faces over dry ground the art covers

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

	/* The time of the state the frame draws - the tick clock less the part
	 * of a tick the drawn state stands behind it (hd_book.h, HDBookClock) -
	 * and the blend factor it was made from. Worked out once per frame beside
	 * the tick clock, where the frame is filled, and read by the two brackets
	 * that draw inside that frame and never step the clock. Every motion of
	 * the HD path turns on it, the .TRG material 15 books included
	 * (FillFrameLighting says why). */
	double g_drawn_clock = 0.0;
	float g_drawn_alpha = 1.0f;
	/* The HD frames filled in this level, counted where the clock is
	 * stepped - the `present` column of the bookstep line - and that line's
	 * own count and the page it wrote last. */
	long g_book_presents = 0;
	int g_bookstep_lines = 0;
	int g_bookstep_page = -1;

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

	/* The rooms of this frame that are drawn classic and hold stand art
	 * (g_group_stand), collected by HDFrameRoom beside g_frame: the room, the
	 * group drawing it, its matrix and its portal window. Kept apart from
	 * g_frame because nothing of their own group is drawn and every reader of
	 * g_frame - the pieces' `listed`, the lighting, the projected shadow -
	 * asks about HD groups. */
	struct StandHolderFrame
	{
		int room;
		int group;
		D3DMATRIX world;
		int x0, y0, x1, y1;
		bool full;
	};

	StandHolderFrame g_stand_frame[HD_MAX_FRAME];
	int g_stand_frame_count = 0;

	/* The stand ranges this frame draws inside their holder's window, with
	 * their own group's matrix and lighting (HDStandDraw's `inHolder`): the
	 * carrier's ranges - at most five per holder: its relief, and its art
	 * reaching onto or lying on covered faces alone or on covered and kept
	 * ones - of each holder drawn as art, wholly or in part, on the list
	 * whose carrier is off it. The game has eight such holders, four of them
	 * in one level (TrgProbe -classicart -perface: RICH2 15, 16, 18 and 19),
	 * so 64 is far past any frame. One past it is not drawn and counted. */
	#define HD_MAX_STAND_DRAW	64

	struct StandDraw
	{
		int group;
		int index;
		bool behind;
		D3DMATRIX world;
		HDRoomLighting lighting;
		HDLightSource lights[HD_MAX_LIGHTS];
		int x0, y0, x1, y1;
		bool full;
	};

	StandDraw g_stand_draw[HD_MAX_STAND_DRAW];
	int g_stand_draw_count = 0;

	/* The static pieces this frame draws (hd_cover.h, HDStaticDrawSource),
	 * each inside its static's room's window with its group's matrix and
	 * lighting, noted by HDStaticDrawnClassic as DrawStaticObjects asks it.
	 * The game has ten statics with a piece, eight of them in ANDREA2
	 * (RoomProbe -statictable), so 32 is past any frame; one past it stays
	 * classic. */
	#define HD_MAX_PIECE_DRAW	32

	struct PieceDraw
	{
		int piece;
		int group;
		int roomOfArt;
		D3DMATRIX world;
		HDRoomLighting lighting;
		HDLightSource lights[HD_MAX_LIGHTS];
		int x0, y0, x1, y1;
		bool full;
	};

	PieceDraw g_piece_draw[HD_MAX_PIECE_DRAW];
	int g_piece_draw_count = 0;
	// This frame's stand ranges drawn with their group, in their holder's
	// window and left out, the in-holder draws past HD_MAX_STAND_DRAW, and
	// of those drawn with their group the ones pushed back.
	int g_clip_stand_with = 0;
	int g_clip_stand_holder = 0;
	int g_clip_stand_out = 0;
	int g_clip_stand_dropped = 0;
	int g_clip_stand_behind = 0;

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
	/* The pieces of the rooms whose window is empty - rooms the reach lists
	 * and the portal walk gives no window - which DecidePairs does not ask
	 * and counts apart from the ones above: the rooms that have any, their
	 * pieces, the pieces drawn whole (outside the box one sector smaller)
	 * and the pieces inside that box, left out whatever the holder. */
	int g_clip_reach_rooms = 0;
	int g_clip_reach_pieces = 0;
	int g_clip_reach_whole = 0;
	int g_clip_reach_hidden = 0;
	/* The pieces whose art stands in the air of a room its own room has no
	 * link with (HDPairRange::air), over the rooms of this frame's list:
	 * all of them, those drawn - inside their room's window, or whole on a
	 * claimed face the classic path draws - and those left out because the
	 * window is empty. */
	int g_clip_unlinked_pieces = 0;
	int g_clip_unlinked_drawn = 0;
	int g_clip_unlinked_hidden = 0;
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

	/* The reachart lines. `g_room_reach` is the rectangle the reach carried
	 * each listed room in on, handed over by the room walk's log note
	 * (HDFrameNoteReach) because the HD path gets only the classic window;
	 * `g_reachart_due` is set by the ROOMLIST dump written at a window event
	 * (HDFrameNoteDump) and makes the next drawn frame write the lines
	 * outside the clip line's throttle. Both are read only by the log. */
	short g_room_reach[HD_MAX_ROOMS][4];
	bool g_reachart_due = false;

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
	 * (DrawRoomWindowed). One whose window is empty - a room the reach lists
	 * and the portal walk gives no window - has no classic face to ask: the
	 * classic path draws none of its faces. Its pieces keep the answer of
	 * the box one sector smaller than the inside, like the rest of the
	 * room's art: drawn whole outside it, left out inside it, whatever
	 * holder they stand in. Such a piece is often the holder's own wall or
	 * doorway, modelled in the neighbour's group - a wall panel beside a
	 * corridor, the room behind a wire fence - and left out in a listed
	 * holder it opens a hole there; drawn whole it comes through a holder's
	 * own walls at some places - a brick hull in the sky above a piazza, a
	 * marble ceiling in a metal hub - which is the smaller fault of the
	 * two.
	 *
	 * Art that stands in the air of a room its own room has no portal,
	 * door or vertical link with, and that an intrusion box holds
	 * (HDPairRange::air, BuildRoomAir), is drawn only inside its own room's
	 * window, whatever the answer above, and
	 * left out when that window is empty. Such art stands where the classic
	 * path can show room b only through b's own window. Drawn on the answer
	 * above alone, it comes through the holder's walls wherever b's window
	 * is open to the eye or the holder is not listed: a brick hull in the
	 * sky beside a flag, a window frame in a brick loft, a marble strip
	 * over a metal hub, black bars beside a pillar, a lit floor in a
	 * doorway. A piece of a pair with a claimed face the classic path draws
	 * this frame keeps the answer above, listed holder or not: that face of
	 * b is in sight, and the art bound to it is its door, frame or wall top,
	 * which b's window cuts where it stands proud of the face: without it
	 * two of room 0's doors seen from ANDREA1's room 109 lose their lower
	 * half, and the edge of a metal wall seen from RICH2's room 106 goes
	 * (74,088 pixels at 1280x720). So does a piece of that pair no face
	 * claims whose holder is not drawn this frame: it stands with those
	 * faces, and whole is its answer without the air. The rule can only
	 * take art away: a piece not drawn whole is already drawn inside that
	 * window, so it narrows the answer and never widens it. Art in the air
	 * of a linked room keeps the answer above, and so does art in no
	 * entry's box, which is no piece at all (BuildRoomIntrusions says why).
	 * Such art the open half would draw - no face claims it and it stands
	 * outside the smaller box - is a piece of its own with no face outside
	 * the smaller box (HDPairRange), whole on the answer above and never one
	 * of the pieces the counters ask. With tomb5.hd_air off no piece has
	 * air, and the answer above is the whole answer. */
	/* The walls of every pair's claimed faces, per piece (g_piece_wall):
	 * the faces of one pair asked face to face once, so that a frame asks
	 * only which drawn face carries the piece's wall. A pair claims 173
	 * faces at most over the fifteen levels; the pieces of faces past
	 * HD_VIS_WALL_FACES keep -1, the answer of a piece no face claims. */
	void BuildVisWalls()
	{
		g_piece_wall.assign(g_pieces.size(), -1);
		g_piece_wall_built = true;

		for (int g = 0; g < HD_MAX_ROOMS; g++)
		{
			const int first = g_piece_first[g];
			const int count = g_piece_count[g];

			for (int j = 0; j < count; j++)
			{
				const HDPairRange& piece = g_pieces[(size_t)(first + j)];

				// No face, or labelled with an earlier piece of its pair.
				if (piece.face < 0 ||
					(size_t)piece.face >= g_pair_faces.size() ||
					g_piece_wall[(size_t)(first + j)] >= 0)
					continue;

				HDVisWallFace wall[HD_VIS_WALL_FACES];
				int pieceOf[HD_VIS_WALL_FACES];
				int label[HD_VIS_WALL_FACES];
				int walls = 0;

				for (int k = j; k < count; k++)
				{
					const HDPairRange& other = g_pieces[(size_t)(first + k)];
					bool known = false;

					if (other.intrusion != piece.intrusion || other.face < 0 ||
						(size_t)other.face >= g_pair_faces.size())
						continue;

					for (int i = 0; i < walls && !known; i++)
						known = g_pieces[(size_t)pieceOf[i]].face == other.face;

					if (known || walls >= HD_VIS_WALL_FACES)
						continue;

					const HDPairFace& face = g_pair_faces[(size_t)other.face];

					pieceOf[walls] = first + k;
					wall[walls].v = face.v;
					wall[walls].corners = face.corners;
					wall[walls].drawn = false;
					walls++;
				}

				HDVisWallLabels(wall, walls, label, HD_VIS_PLANE_TOLERANCE);

				for (int k = j; k < count; k++)
				{
					const HDPairRange& other = g_pieces[(size_t)(first + k)];

					if (other.intrusion != piece.intrusion || other.face < 0)
						continue;

					for (int i = 0; i < walls; i++)
						if (g_pieces[(size_t)pieceOf[i]].face == other.face)
						{
							g_piece_wall[(size_t)(first + k)] =
								pieceOf[label[i]];
							break;
						}
				}
			}
		}
	}

	void DecidePairs(FrameRoom& entry)
	{
		entry.scissoredTris = 0;

		if (!entry.windowed || entry.group < 0 || entry.group >= HD_MAX_ROOMS)
			return;

		const int first = g_piece_first[entry.group];
		const int count = g_piece_count[entry.group];

		/* The visibility path asks the same: a piece is art of the room
		 * standing inside another room, beyond the room's own walls, where
		 * the classic path shows it only through the room's window. Drawn
		 * whole there it hangs in the other room's space - ANDREA1's brick
		 * block of room 93 over room 101's roof (7,112 pixels at camE,
		 * 1280x720), the window frame in room 93's loft, JOBY2's black bars
		 * by the pillar of room 152 - which the owner played as ghosts. The
		 * rest of a room's art, the open half, stays whole on that path
		 * (DrawRoomWindowed). */
		const bool empty = ClipEmpty(entry);
		const bool ask = !entry.full && !empty;
		int asked = 0;

		/* A piece with no face outside the smaller box is art the air took
		 * out of the open half (HDPairRange): drawn whole without the air,
		 * and none of the pieces the counters below are about. */
		for (int j = 0; j < count; j++)
			if (g_pieces[(size_t)(first + j)].face >= 0 ||
				g_pieces[(size_t)(first + j)].inner)
				asked++;

		if (empty && asked > 0)
			g_clip_reach_rooms++;

		for (int j = 0; j < count; j++)
		{
			const HDPairRange& piece = g_pieces[(size_t)(first + j)];
			bool listed = false;
			bool whole = false;

			// Whether the holder is on this frame's list, whatever its window.
			// The holder is a group - the room of that number at the build,
			// before any flipmap - and the walls that hide its art are that
			// group's, so it is looked up among the groups drawn this frame:
			// after a flipmap swaps a pair, room and group are not the same.
			for (int i = 0; i < g_frame_count && !listed; i++)
				listed = g_frame[i].group == piece.holder;

			const bool open = piece.face < 0 && !piece.inner;
			/* In unlinked air (1), or held by the box's height alone (2,
			 * the index fill's height_at: art of an unlinked room on the
			 * plane of the holder's wall), which the visibility path holds
			 * to the window while the holder is drawn - the two walls
			 * fight in one plane then; with the holder not drawn, and on
			 * the other path, it is the open half it was. ANDREA1's room
			 * 37 has such art on the wall plane of room 208, seen from
			 * room 228 with room 208 not listed: cut, it left 324 pixels
			 * of cracks at 1,280 x 720. */
			const bool air = piece.air == 1 ||
				(piece.air == 2 && VisLevel() && listed);

			if (ask)
				whole = !listed;
			else if (empty)
				whole = piece.inner == 0;

			if (open)
				whole = true;

			/* Any claimed face of the same pair the classic path draws -
			 * asked for a claimed piece, and for a piece in unlinked air
			 * that no face claims while its holder is not drawn: that art
			 * stands with the pair's faces, as a pipe stands in the
			 * hollow of a pillar whose face it is drawn on, and the
			 * answer whole is the one it has without the air. */
			bool claimed = false;
			const bool askPair = piece.face >= 0 || (air && !listed);

			for (int k = 0; k < count && ask && askPair && !claimed; k++)
			{
				const HDPairRange& other = g_pieces[(size_t)(first + k)];

				claimed = other.intrusion == piece.intrusion &&
					other.face >= 0 &&
					(size_t)other.face < g_face_alive.size() &&
					g_face_alive[(size_t)other.face] != 0;
			}

			if (ask && listed && piece.face >= 0)
				whole = claimed;

			/* In unlinked air: inside the room's own window, or not at all -
			 * but for a piece of a pair with a claimed face the classic path
			 * draws this frame, listed holder or not: b's wall is in sight
			 * there, and the art bound to it is that wall's - a door, its
			 * frame, a wall top - which b's window cuts where it stands proud
			 * of the face. Such a piece is whole on the answer above too, so
			 * this keeps that answer and never widens it. So is a piece no
			 * face claims whose holder is not drawn (askPair): cut to the
			 * window, the coiled pipe in the hollow of JOBY2's broken
			 * pillar by the stairs falls into shards seen from room 33
			 * (16,177 pixels at 1280x720). */
			if (air)
			{
				whole = claimed;

				/* On the visibility path only a claimed face the classic
				 * path draws that is one wall with the piece's own face -
				 * in its plane, or round a corner the wall turns - keeps
				 * it whole (HDVisWallDrawn says why): the art of another
				 * surface of the room is seen where the classic path shows
				 * that surface, in the room's window. Kept whole by the
				 * floor of ANDREA1's room 93 in sight, the walls of its
				 * brick block stood in room 101 beside the facade's corner
				 * (2,304 pixels at 1280x720). A piece no face claims has no
				 * wall and keeps the pair's answer. */
				if (whole && VisLevel() && piece.face >= 0)
				{
					if (!g_piece_wall_built ||
						g_piece_wall.size() != g_pieces.size())
						BuildVisWalls();

					const int wall = g_piece_wall[(size_t)(first + j)];
					bool drawn = wall < 0;

					for (int k = 0; k < count && !drawn; k++)
					{
						const HDPairRange& other =
							g_pieces[(size_t)(first + k)];

						drawn = other.intrusion == piece.intrusion &&
							g_piece_wall[(size_t)(first + k)] == wall &&
							other.face >= 0 &&
							(size_t)other.face < g_face_alive.size() &&
							g_face_alive[(size_t)other.face] != 0;
					}

					whole = drawn;
				}

				g_clip_unlinked_pieces++;

				if (empty)
					g_clip_unlinked_hidden++;
				else
					g_clip_unlinked_drawn++;
			}

			if (ask && listed && !open)
			{
				g_clip_pairs++;

				if (whole)
					g_clip_pairs_whole++;
				else
					g_clip_pairs_scissored++;
			}

			if (empty && !open)
			{
				g_clip_reach_pieces++;

				if (whole)
					g_clip_reach_whole++;
				else
					g_clip_reach_hidden++;
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

		/* An empty window silences only the pieces of the intruding half
		 * DecidePairs did not answer whole: the open half is not windowed and
		 * is drawn whatever four numbers the walk came back with. */
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

	/* HDLOG reachart: one line per room with an empty window and holder its
	 * intruding pieces stand in, written with the clip line and on the frame
	 * of a window event's ROOMLIST dump. Behind -log.
	 *
	 *   room       the listed room the walk left with an empty window (a room
	 *              index, as ROOMLIST prints it)
	 *   reach      the rectangle the reach carried it in on (ROOMLIST reach=)
	 *   holder     the group the pieces stand in - the room of that number
	 *              until a flipmap swaps it with its pair, the pair's room
	 *              after; listed 1 when that group is drawn this frame,
	 *              which changes nothing of the answer
	 *   pieces     the room's pieces in that holder, tris their triangles,
	 *              all three passes together
	 *   answer     whole when any of them stands outside the box one sector
	 *              smaller, in no unlinked room's air, and is drawn whole,
	 *              hidden when none is drawn
	 *   band_tris  the room's art in that holder no face claims that stands
	 *              outside the smaller box - drawn with the open half on every
	 *              frame, whatever the answer says, but for the art in
	 *              unlinked air, which is a piece of its own */
	void LogReachArt()
	{
		for (int i = 0; i < g_frame_count; i++)
		{
			const FrameRoom& entry = g_frame[i];

			if (!entry.windowed || !ClipEmpty(entry) || entry.group < 0 ||
				entry.group >= HD_MAX_ROOMS || entry.room < 0 ||
				entry.room >= HD_MAX_ROOMS)
				continue;

			const int first = g_piece_first[entry.group];
			const int count = g_piece_count[entry.group];
			const short* reach = g_room_reach[entry.room];

			for (int j = 0; j < count; j++)
			{
				const HDPairRange& piece = g_pieces[(size_t)(first + j)];
				bool seen = false;

				// Art the air took out of the open half has no reach line.
				if (piece.face < 0 && !piece.inner)
					continue;

				// The first piece of each holder writes the holder's line.
				for (int k = 0; k < j && !seen; k++)
				{
					const HDPairRange& other = g_pieces[(size_t)(first + k)];

					seen = other.holder == piece.holder &&
						(other.face >= 0 || other.inner);
				}

				if (seen)
					continue;

				bool listed = false;
				bool anyWhole = false;
				int pieces = 0;
				int tris = 0;

				for (int f = 0; f < g_frame_count && !listed; f++)
					listed = g_frame[f].group == piece.holder;

				for (int k = j; k < count; k++)
					if (g_pieces[(size_t)(first + k)].holder == piece.holder &&
						(g_pieces[(size_t)(first + k)].face >= 0 ||
						g_pieces[(size_t)(first + k)].inner))
					{
						pieces++;
						tris += g_pieces[(size_t)(first + k)].triangles;

						if (g_pieces[(size_t)(first + k)].inner == 0 &&
							!g_pieces[(size_t)(first + k)].air)
							anyWhole = true;
					}

				// One entry of the intrusion list per pair of rooms, so the
				// pieces of one holder share it.
				const int band = (size_t)piece.intrusion < g_pair_band.size()
					? g_pair_band[(size_t)piece.intrusion] : 0;

				Log("HDLOG reachart level=%s room=%d reach=%d,%d..%d,%d "
					"holder=%d listed=%d pieces=%d tris=%d answer=%s "
					"band_tris=%d", g_stem, entry.room, (int)reach[0],
					(int)reach[1], (int)reach[2], (int)reach[3], piece.holder,
					listed ? 1 : 0, pieces, tris, anyWhole ? "whole" : "hidden",
					band);
			}
		}
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
	int g_frame_book_calls = 0;	// the two brackets' book calls, for the frame line
	int g_frame_clear_calls = 0;	// the solid bracket's clear floors, the same
	int g_frame_sheen_calls = 0;	// the solid bracket's gloss-variant draws, the same
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
	// One WATERLOG undergate line per level (HDFrameDraw).
	bool g_undergate_logged = false;
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
	 * a 4096x4096 texture in A8R8G8B8, measured at 85.34 in a single
	 * reserve, the bound kWidestPage below works with. */
	double g_redress_page_block = 0;

	/* Which pool the pages live in, in one place instead of a literal at each
	 * call. D3DPOOL_DEFAULT: the texture is on the card and the process keeps
	 * no copy, which makes the address space the cheap side of the rule and
	 * the card the expensive one. hd_object.cpp hands the models' pages the
	 * same answer; it is a property of the loader, not a setting. */
	const bool kPagesManaged = false;

	/* The widest page the remaster has: eleven pages of 5\TEX and eight of
	 * 4\TEX are 4096 wide by 2048 high and nothing is wider (TrgProbe
	 * -ddssizes). The block below is worked out for a square of that side,
	 * which bounds the real page from above by a factor of two; a card that
	 * stops at 2048 clamps them (hd_render.cpp, where the device's own cap
	 * enters the policy), and the policy says so, which is why the block is
	 * worked out from both. */
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

	/* The clock of the load's phases, in milliseconds: GetTickCount's steps
	 * of about 16 ms are coarser than most of the phases. */
	double LoadClockMs()
	{
		LARGE_INTEGER now, rate;

		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&rate);
		return (double)now.QuadPart * 1000.0 / (double)rate.QuadPart;
	}

	/* The parts of a level's HD build in hd_level.cpp's own order, for the
	 * log: the steps before HDRenderLevelBegin (on the built line, beside
	 * the build's own parts) and the steps after it (on the hd_share line). */
	enum LoadPhase
	{
		LOAD_MARKS, LOAD_BOXES, LOAD_GLOSS, LOAD_INTR, LOAD_AIR, LOAD_PAIRS,
		LOAD_CUTS, LOAD_STAND, LOAD_PLACES, LOAD_BEGIN, LOAD_TABLES,
		LOAD_STATICS, LOAD_SHEETS, LOAD_SURFACES, LOAD_FRAMES, LOAD_OBJECTS,
		LOAD_PHASES
	};

	const char* const kLoadPhaseName[LOAD_PHASES] =
	{
		"marks", "boxes", "gloss", "intr", "air", "pairs", "cuts", "stand",
		"places", "begin", "tables", "statics", "sheets", "surfaces",
		"frames", "objects"
	};

	// What HDScanLevel took on the load thread, for the hd_share line.
	double g_scan_ms = 0.0;

	/* The threads of the level's HD load - the page decode, the room build
	 * and the models' pages (HDPoolRun): tomb5.hd_threads, 0 the machine's
	 * own count, never above 16 (HDPoolThreads). */
	int DecodeThreads()
	{
		return HDPoolThreads((int)tomb5.hd_threads);
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

/* The classic floor squares, for the renderer's sheen: the footstep
 * material and the floor height of every square of every room, in the order
 * of ROOM_INFO::floor - floor[z_index + x_size * x_index], so x runs over
 * y_size squares and z over x_size, as in BuildRoomBoxes above. The renderer
 * finds the .TRG's light cube of a square by the same order and seeds its
 * page classes from the materials (hd_gloss_math.h). Borrowed for the length
 * of the build, like the boxes. */
static std::vector<HDRoomGrid> g_room_grids;
static std::vector<HDSectorFloor> g_room_sectors;

static void BuildRoomSectors()
{
	g_room_grids.clear();
	g_room_sectors.clear();
	g_room_grids.resize((size_t)number_rooms);

	for (int i = 0; i < number_rooms; i++)
	{
		const ROOM_INFO* r = &room[i];
		HDRoomGrid& grid = g_room_grids[(size_t)i];

		grid.first = (int)g_room_sectors.size();
		grid.xSectors = r->floor ? r->y_size : 0;
		grid.zSectors = r->floor ? r->x_size : 0;
		grid.originX = (float)r->x;
		grid.originZ = (float)r->z;

		for (int sx = 0; sx < grid.xSectors; sx++)
			for (int sz = 0; sz < grid.zSectors; sz++)
			{
				const FLOOR_INFO& f = r->floor[sz + r->x_size * sx];
				HDSectorFloor s;

				s.floorY = (short)(f.floor * 256);
				s.fx = (unsigned char)f.fx;
				s.wall = f.floor == -127 ? 1 : 0;	// NO_HEIGHT in clicks
				g_room_sectors.push_back(s);
			}
	}

	if (!g_room_grids.empty() && !g_room_sectors.empty())
		HDRenderSetSectors(&g_room_grids[0], (int)g_room_grids.size(),
			&g_room_sectors[0], (int)g_room_sectors.size());
	else
		HDRenderSetSectors(0, 0, 0, 0);
}

/* The water sheets (hd_render.h, HDRenderSetWaterSheets): every room's
 * floor squares handed to the renderer, which finds the water squares,
 * joins them into bodies, classes them and builds the sheets over them.
 * tomb5.hd_water 0 builds the bodies and no sheet and draws no caustic under
 * them, so BuildWaterSurfaces restores every classic water face as it would
 * without this. The section picks the palette, by the game's own level id
 * rather than by a file name. */
static_assert(HD_WATER_ROOM_UNDERWATER == ROOM_UNDERWATER &&
	HD_WATER_ROOM_OPEN == (ROOM_OUTSIDE | ROOM_NOT_INSIDE),
	"the water sheets read the engine's own room flags");

static int WaterSection()
{
	if (gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE)
		return HD_WATER_RUSSIA;

	if (gfLevelId >= LVL5_GALLOWS_TREE && gfLevelId <= LVL5_OLD_MILL)
		return HD_WATER_IRELAND;

	if (gfLevelId >= LVL5_THIRTEENTH_FLOOR && gfLevelId <= LVL5_RED_ALERT)
		return HD_WATER_VCI;

	return HD_WATER_ROME;
}

/* The ripples the sheets draw as rings (hd_render.cpp, "the rings"): those
 * with flags 0x10 (Lara wading or swimming, a drip) or 0x02 (a bubble at
 * the surface, a rat, a shell) and without 0x20, which marks the blood
 * TriggerUnderwaterBlood keeps in the same array. A raindrop's ripple
 * (flags 0, specificfx.cpp) is left out: up to 128 of them would crowd the
 * eight rings a sheet draw has, so the rain keeps its classic sprite. */
static_assert(MAX_RIPPLES == HD_WATER_RIPPLE_SLOTS,
	"the rings follow ripples[] slot by slot");

static bool RippleIsRing(int flags)
{
	return !(flags & 0x20) && (flags & 0x12);
}

/* Every live ripple of that kind, once a frame (HDFrameDrawGlow): 128 slots
 * read, nothing written. */
static void FeedWaterRipples()
{
	HDRenderRipple live[MAX_RIPPLES];
	int count = 0;

	for (int i = 0; i < MAX_RIPPLES; i++)
	{
		const RIPPLE_STRUCT& r = ripples[i];

		if (!(r.flags & 1) || !RippleIsRing(r.flags))
			continue;

		HDRenderRipple& out = live[count++];

		out.slot = i;
		out.x = (int)r.x;
		out.y = (int)r.y;
		out.z = (int)r.z;
		out.flags = r.flags;
		out.life = r.life;
		out.size = r.size;
		out.init = r.init;
	}

	HDRenderWaterRipples(live, count);
	HDSpringFeed();
}

/* Lara's place for the sheets' waterline, which leaves her out: a thin wet
 * line round a swimming or wading body is white on her legs and shoulders
 * and not where water meets a bank. In the water while she swims at the
 * surface or wades (lara.water_status); under water the sheet lies over her
 * and a waterline does not reach her, and on dry land she meets no water. */
static void FeedWaterLara()
{
	const bool in = lara_item && (lara.water_status == LW_SURFACE ||
		lara.water_status == LW_WADE);

	HDRenderWaterLara(in ? 1 : 0, in ? (float)lara_item->pos.x_pos : 0.0f,
		in ? (float)lara_item->pos.z_pos : 0.0f);
}

bool HDWaterRippleOnSheet(long x, long y, long z, int flags)
{
	return HDLevelActive() && tomb5.hd_water && tomb5.hd_motion &&
		(flags & 1) && RippleIsRing(flags) &&
		HDRenderWaterSheetUnder((int)x, (int)y, (int)z);
}

/* The waterfall objects for their feet (hd_render.cpp, "the foot of a
 * waterfall"): every item of WATERFALL1 to 3 and WATERFALLSS1 and 2, its
 * classic mesh placed as DrawAnimatingItem places it - the item's place
 * and turn, the frame's root offset (GetBestFrame, the six shorts of the
 * box first) - and of that mesh the highest and the lowest y and the x, z
 * of the vertices within HD_WATER_FOOT_BAND of the lowest. One mesh each:
 * the five slots carry one (ModelOffset on the fifteen classic files). */
static void BuildWaterFalls()
{
	std::vector<HDWaterFall> falls;

	for (int i = 0; i < level_items; i++)
	{
		ITEM_INFO* item = &items[i];
		const int slot = item->object_number;

		if (!(slot >= WATERFALL1 && slot <= WATERFALL3) &&
			!(slot >= WATERFALLSS1 && slot <= WATERFALLSS2))
			continue;

		const OBJECT_INFO& obj = objects[slot];

		if (!obj.loaded || obj.nmeshes <= 0 || obj.mesh_index < 0 ||
			obj.mesh_index >= num_level_meshes)
			continue;

		const MESH_DATA* mesh = mesh_vtxbuf[obj.mesh_index];
		const short* frame = GetBestFrame(item);

		if (!mesh || !mesh->nVerts || !mesh->aVtx || !frame)
			continue;

		const float c = (float)phd_cos(item->pos.y_rot) * (1.0f / 16384.0f);
		const float s = (float)phd_sin(item->pos.y_rot) * (1.0f / 16384.0f);
		HDWaterFall f = {};

		f.item = i;
		f.slot = slot;
		f.room = item->room_number;
		f.top = 1.0e9f;
		f.bottom = -1.0e9f;

		for (int pass = 0; pass < 2; pass++)
			for (int v = 0; v < mesh->nVerts; v++)
			{
				const ACMESHVERTEX& at = mesh->aVtx[v];
				const float lx = at.x + frame[6];
				const float lz = at.z + frame[8];
				const float y = (float)item->pos.y_pos + at.y + frame[7];

				if (!pass)
				{
					f.top = y < f.top ? y : f.top;
					f.bottom = y > f.bottom ? y : f.bottom;
					continue;
				}

				if (y < f.bottom - (float)HD_WATER_FOOT_BAND ||
					f.points >= HD_WATER_FOOT_POINTS)
					continue;

				f.foot[f.points * 2] = (float)item->pos.x_pos + lx * c + lz * s;
				f.foot[f.points * 2 + 1] = (float)item->pos.z_pos + lz * c -
					lx * s;
				f.points++;
			}

		falls.push_back(f);
	}

	HDRenderWaterFalls(falls.empty() ? 0 : &falls[0], (int)falls.size(),
		g_stem);
}

/* The mist over a waterfall's foot. A WATERFALLMIST object lays its grey
 * sparks along a line 1,536 units long that starts 512 behind it and 256 to
 * its side (game\effects.cpp WaterFall, game\effect2.cpp
 * TriggerWaterfallMist) - under the classic waterfall it was placed for.
 * Where the art draws the fall, its water lands elsewhere: in ANDREA2 room
 * 231 the mist rose 479 and 486 units beside the art's two strands, the
 * foot's foam and the art's own spray, a white column apart from the foam
 * (mist lines at x 56,064, the strands' feet at x 55,518 to 55,591, the
 * WATERLOG seg= lines). So an emitter whose line's middle lies within a
 * square of a wet stretch the art's feet placed (HDRenderWaterFootNearest)
 * is moved by the way from that middle to the stretch: the mist then rises
 * where the art's water lands. A projection, so a second build moves it no
 * further. Over the fifteen levels only ANDREA2 has such emitters, its two
 * in room 231. Nothing without sheets (tomb5.hd_water 0 lays no curtain);
 * the item's place is not saved (the object saves its flags alone), so a
 * level loaded again starts from the classic place and is moved again. */
static void MoveWaterfallMist()
{
	for (int i = 0; i < level_items; i++)
	{
		ITEM_INFO* item = &items[i];

		if (item->object_number != WATERFALLMIST)
			continue;

		// The line's start and way, as WaterFall computes them.
		const long ang = item->pos.y_rot + 0x8000;
		long x = item->pos.x_pos - (512 * phd_sin(ang) >> W2V_SHIFT);
		long z = item->pos.z_pos - (512 * phd_cos(ang) >> W2V_SHIFT);
		const long side = ang ? ang - 0x4000 : ang + 0x4000;

		x += 256 * phd_sin(side) >> W2V_SHIFT;
		z += 256 * phd_cos(side) >> W2V_SHIFT;

		const float mx = (float)(x + (768 * phd_sin(ang) >> W2V_SHIFT));
		const float mz = (float)(z + (768 * phd_cos(ang) >> W2V_SHIFT));
		float nx = mx, nz = mz;

		if (!HDRenderWaterFootNearest(mx, mz, &nx, &nz))
			continue;

		const long dx = (long)floorf(nx - mx + 0.5f);
		const long dz = (long)floorf(nz - mz + 0.5f);

		item->pos.x_pos += dx;
		item->pos.z_pos += dz;

		if (gLogEnabled)
			Log("WATERLOG level=%s mist=%d room=%d x=%d z=%d moved=%d,%d",
				g_stem, i, item->room_number, (int)mx, (int)mz, (int)dx,
				(int)dz);
	}
}

/* The drops at the waterfalls' feet, on the logic tick (game\control.cpp,
 * beside AnimateWaterfalls): TriggerSmallSplash (game\tomb4fx.cpp), the
 * engine's own splash of a rat or a shell landing in water, two drops a
 * tick at every foot within kDropReach of the camera, at a random place
 * along it and at most kDropAcross to either side, on the surface, and no
 * more than kDropsMax a tick in all. Nothing in a level without sheets
 * (tomb5.hd_water 0 builds none) and nothing while tomb5.hd_motion is
 * off - the water stands still. A drop lives 24 ticks: two a tick are about
 * 48 at a foot - ANDREA2 room 231's curtain has four feet, about 200 of the
 * game's 1024 sparks - and the cap holds a level to about 380 (JOBY5 has 37
 * feet, the WATERLOG foot= lines, 1,776 drops alive without it). Two, 64
 * units, eight squares and 16 are free choices; a drop eight squares off is
 * a dot. */
void HDWaterFallsTick()
{
	const int kDropsPerTick = 2;
	const int kDropAcross = 64;
	const float kDropReach = 8192.0f;
	const int kDropsMax = 16;

	if (!HDLevelActive() || !tomb5.hd_water || !tomb5.hd_motion)
		return;

	const int feet = HDRenderWaterFeet();
	int made = 0;

	for (int f = 0; f < feet && made < kDropsMax; f++)
	{
		float place[3], dir[2], half;

		if (!HDRenderWaterFoot(f, place, dir, &half))
			continue;

		const float cx = place[0] - (float)camera.pos.x;
		const float cy = place[1] - (float)camera.pos.y;
		const float cz = place[2] - (float)camera.pos.z;

		if (cx * cx + cy * cy + cz * cz > kDropReach * kDropReach)
			continue;

		made += kDropsPerTick;

		for (int k = 0; k < kDropsPerTick; k++)
		{
			const float along = half * ((GetRandomControl() & 1023) *
				(2.0f / 1023.0f) - 1.0f);
			const float across = (float)((GetRandomControl() & 127) - 64) *
				(kDropAcross / 64.0f);

			TriggerSmallSplash((long)(place[0] + dir[0] * along -
				dir[1] * across), (long)place[1], (long)(place[2] +
				dir[1] * along + dir[0] * across), 1);
		}
	}
}

/* What the water squares read of a square's floor data (HDWaterRoom): the
 * floor's slope entry, walked as GetHeight walks the chain (game\
 * control.cpp: a portal, a slope or a ceiling slope carry one data word, a
 * trigger its setup word and command words to the one with the end bit -
 * a camera's and a flyby's command one word more -, lava, climb, monkey
 * and the trigger trigger none); the ceiling's slope entry as
 * GetMinimumCeiling reads it, the first entry or the one behind a floor
 * slope; and the first TO_SINK command's camera entry. */
static void WaterSquareWords(const FLOOR_INFO& f, unsigned short* slope,
	short* sink)
{
	slope[0] = slope[1] = slope[2] = slope[3] = 0;
	*sink = -1;

	if (!f.index || !floor_data)
		return;

	const short* data = &floor_data[f.index];
	const int first = data[0] & 0x1F;
	const bool floorSlope = first == TILT_TYPE || first == SPLIT1 ||
		first == SPLIT2 || (first >= NOCOLF1T && first <= NOCOLF2B);
	const unsigned short ceil = floorSlope ?
		((data[0] & 0x8000) ? 0 : (unsigned short)data[2]) :
		(unsigned short)data[0];
	const int cfn = ceil & 0x1F;

	if (cfn == ROOF_TYPE || cfn == SPLIT3 || cfn == SPLIT4 ||
		(cfn >= NOCOLC1T && cfn <= NOCOLC2B))
	{
		slope[2] = ceil;
		slope[3] = (unsigned short)(floorSlope ? data[3] : data[1]);
	}

	for (int guard = 0; guard < 64; guard++)
	{
		const unsigned short type = (unsigned short)*data++;
		const int fn = type & 0x1F;

		if (fn == DOOR_TYPE || fn == ROOF_TYPE || fn == SPLIT3 ||
			fn == SPLIT4 || (fn >= NOCOLC1T && fn <= NOCOLC2B))
			data++;
		else if (fn == TILT_TYPE || fn == SPLIT1 || fn == SPLIT2 ||
			(fn >= NOCOLF1T && fn <= NOCOLF2B))
		{
			if (!slope[0])
			{
				slope[0] = type;
				slope[1] = (unsigned short)*data;
			}

			data++;
		}
		else if (fn == TRIGGER_TYPE)
		{
			unsigned short trigger;

			data++;

			do
			{
				trigger = (unsigned short)*data++;

				if ((trigger & 0x3C00) == (TO_SINK << 10) && *sink < 0)
					*sink = (short)(trigger & 0x3FF);

				if ((trigger & 0x3C00) == (TO_CAMERA << 10) ||
					(trigger & 0x3C00) == (TO_FLYBY << 10))
					trigger = (unsigned short)*data++;
			} while (!(trigger & 0x8000));
		}
		else if (fn != LAVA_TYPE && fn != CLIMB_TYPE && fn != MONKEY_TYPE &&
			fn != TRIGTRIGGER_TYPE)
			break;

		if (type & 0x8000)
			break;
	}
}

/* The water's cards (hd_render.cpp, "the cards"; hd_level.h). The hooks
 * read the classic struct and the spark's place and touch nothing of the
 * game: the classic splash and spark are made and run as before, only
 * their sprites are left out where the cards draw. */
bool HDWaterSplash(const SPLASH_SETUP* setup)
{
	if (!setup || !HDLevelActive() || !tomb5.hd_water || !tomb5.hd_motion)
		return false;

	HDSplashSetup s;

	s.x = (float)setup->x;
	s.y = (float)setup->y;
	s.z = (float)setup->z;
	s.innerRad = setup->InnerRad;
	s.innerSize = setup->InnerSize;
	s.innerRadVel = setup->InnerRadVel;
	s.innerYVel = setup->InnerYVel;
	s.middleRad = setup->MiddleRad;
	s.middleSize = setup->MiddleSize;
	s.middleRadVel = setup->MiddleRadVel;
	s.middleYVel = setup->MiddleYVel;
	s.outerRad = setup->OuterRad;
	s.outerSize = setup->OuterSize;
	s.outerRadVel = setup->OuterRadVel;
	return HDRenderWaterSplash(&s, 0.0f);
}

bool HDWaterSpray(long x, long y, long z, long strength)
{
	if (!HDLevelActive() || !tomb5.hd_water || !tomb5.hd_motion)
		return false;

	return HDRenderWaterSpray((float)x, (float)y, (float)z, (int)strength,
		0.0f);
}

/* The tanks' films (hd_render.cpp, "the tank's film"): every FLOOR_LASERS
 * item that shares its room with a FISHTANK - the floor GetFishTank shorts
 * out (game\lasers.cpp) - over the squares of its box as
 * InitialiseFloorLasers lays it (game\init.cpp: v1 at -512, -512 from the
 * item, v4 at its trigger_flags' width and height in squares less 512),
 * at the item's floor. RICH2 has two tanks; the one in room 23 has a
 * film.
 *
 * The FISHTANK mesh goes to the renderer first, for the tank's water
 * (hd_render.cpp, "the tank's water"): its vertices as DrawScaledSpike
 * places them, at the item's place without a frame offset, and its quads
 * and triangles (a triangle as a quad whose last corner repeats its
 * third). */
struct WaterFilmItems
{
	short tank, laser;
};

static std::vector<WaterFilmItems> g_film_items;
// The FISHTANK items, for their wet floors.
static std::vector<short> g_wet_items;

static void TankMeshToRenderer()
{
	const OBJECT_INFO& obj = objects[FISHTANK];
	std::vector<float> xyz;
	std::vector<int> quads;

	if (obj.loaded && obj.nmeshes > 0 && obj.mesh_index >= 0 &&
		obj.mesh_index < num_level_meshes)
	{
		const MESH_DATA* mesh = mesh_vtxbuf[obj.mesh_index];

		for (int v = 0; mesh && mesh->aVtx && v < mesh->nVerts; v++)
		{
			xyz.push_back(mesh->aVtx[v].x);
			xyz.push_back(mesh->aVtx[v].y);
			xyz.push_back(mesh->aVtx[v].z);
		}

		for (int q = 0; mesh && mesh->gt4 && q < mesh->ngt4; q++)
			for (int k = 0; k < 4; k++)
				quads.push_back(mesh->gt4[q * 6 + k]);

		for (int q = 0; mesh && mesh->gt3 && q < mesh->ngt3; q++)
			for (int k = 0; k < 4; k++)
				quads.push_back(mesh->gt3[q * 5 + (k < 3 ? k : 2)]);
	}

	HDRenderWaterTankMesh(xyz.empty() ? 0 : &xyz[0], (int)xyz.size() / 3,
		quads.empty() ? 0 : &quads[0], (int)quads.size() / 4);
}

static void BuildWaterFilms()
{
	std::vector<HDWaterFilm> films;
	std::vector<HDWaterWetTank> tanks;

	TankMeshToRenderer();
	g_film_items.clear();
	g_wet_items.clear();

	for (int t = 0; t < level_items; t++)
	{
		if (items[t].object_number != FISHTANK)
			continue;

		// Every tank for its wet floor (hd_render.cpp, "the wet floor").
		const HDWaterWetTank wet = { t, items[t].room_number,
			items[t].pos.x_pos, items[t].pos.y_pos, items[t].pos.z_pos,
			items[t].pos.y_rot };

		tanks.push_back(wet);
		g_wet_items.push_back((short)t);

		for (int l = 0; l < level_items; l++)
		{
			const ITEM_INFO* laser = &items[l];
			const FLOORLASER_STRUCT* box =
				(const FLOORLASER_STRUCT*)laser->data;

			if (laser->object_number != FLOOR_LASERS || !box ||
				laser->room_number != items[t].room_number)
				continue;

			const int ax = laser->pos.x_pos + box->v1.x;
			const int bx = laser->pos.x_pos + box->v4.x;
			const int az = laser->pos.z_pos + box->v1.z;
			const int bz = laser->pos.z_pos + box->v4.z;
			HDWaterFilm f;

			f.room = laser->room_number;
			f.x0 = (ax < bx ? ax : bx) & ~1023;
			f.x1 = ((ax < bx ? bx : ax) + 1023) & ~1023;
			f.z0 = (az < bz ? az : bz) & ~1023;
			f.z1 = ((az < bz ? bz : az) + 1023) & ~1023;
			f.y = laser->pos.y_pos;
			f.tank = t;
			f.laser = l;
			films.push_back(f);

			WaterFilmItems w = { (short)t, (short)l };

			g_film_items.push_back(w);
		}
	}

	HDRenderWaterFilms(films.empty() ? 0 : &films[0], (int)films.size());
	HDRenderWaterWetTanks(tanks.empty() ? 0 : &tanks[0], (int)tanks.size());
}

/* Each film's strength this frame, out of its tank's and its lasers' state
 * (hd_splash_math.h, HDFilmStrength): the tank sprays while ControlFishtank
 * runs it, and is done once KillItem cleared it; the lasers live until
 * their own KillItem. Read only. */
static void FeedWaterFilms()
{
	for (size_t f = 0; f < g_film_items.size() &&
		(int)f < HDRenderWaterFilmCount(); f++)
	{
		const ITEM_INFO* tank = &items[g_film_items[f].tank];
		const ITEM_INFO* laser = &items[g_film_items[f].laser];
		const int state = (tank->flags & IFL_CLEARBODY) ? 2 :
			tank->status == ITEM_ACTIVE && tank->active ? 1 : 0;
		const int live = !(laser->flags & IFL_CLEARBODY) &&
			laser->status == ITEM_ACTIVE && laser->active ? 1 : 0;

		HDRenderWaterFilmWet((int)f, HDFilmStrength(state,
			tank->item_flags[1], live, laser->trigger_flags));
	}
}

/* Each tank's state for its wet floor this frame, as the films read it: 2
 * once KillItem took it - in the game, or as a loaded game kills a shot
 * tank (game\savegame.cpp) -, 1 while ControlFishtank runs it, 0 whole.
 * Read only. */
static void FeedWaterWet()
{
	for (size_t i = 0; i < g_wet_items.size(); i++)
	{
		const ITEM_INFO* tank = &items[g_wet_items[i]];
		const int state = (tank->flags & IFL_CLEARBODY) ? 2 :
			tank->status == ITEM_ACTIVE && tank->active ? 1 : 0;

		HDRenderWaterWet(g_wet_items[i], state, tank->item_flags[1], -1.0f);
	}
}

/* The world into the HD frame's view space: the engine's own matrix, which
 * in the HD frame is the view itself (HDFrameDrawGlow hands the bracket an
 * identity view). Row vectors, as the renderer's matrices are. */
static void WorldToFrame(float* m)
{
	phd_PushMatrix();
	phd_TranslateAbs(0, 0, 0);
	m[0] = aMXPtr[M00];
	m[1] = aMXPtr[M10];
	m[2] = aMXPtr[M20];
	m[3] = 0.0f;
	m[4] = aMXPtr[M01];
	m[5] = aMXPtr[M11];
	m[6] = aMXPtr[M21];
	m[7] = 0.0f;
	m[8] = aMXPtr[M02];
	m[9] = aMXPtr[M12];
	m[10] = aMXPtr[M22];
	m[11] = 0.0f;
	m[12] = aMXPtr[M03];
	m[13] = aMXPtr[M13];
	m[14] = aMXPtr[M23];
	m[15] = 1.0f;
	phd_PopMatrix();
}

/* A FISHTANK's mesh as the classic draw is about to draw it
 * (game\traps.cpp, DrawScaledSpike): handed to the renderer for the tank's
 * water of this frame. */
bool HDWaterTank(const ITEM_INFO* item)
{
	if (!item || !HDLevelActive() || !tomb5.hd_water || !tomb5.hd_motion)
		return false;

	return HDRenderWaterTank((int)(item - items), (float)item->pos.x_pos,
		(float)item->pos.y_pos, (float)item->pos.z_pos, item->pos.y_rot,
		item->item_flags[1], -1.0f);
}

/* The pages of the cards, appended to the level's page cache - with the
 * sheets alone: tomb5.hd_water 0 loads nothing more. */
static void AppendWaterCardPages()
{
	if (!tomb5.hd_water || !HDRenderWaterSheetsBuilt())
		return;

	int pages[4];
	const int n = HDRenderWaterCardPages(pages, 4);
	HDPageCacheStats stats;
	const char* why = "";

	if (!HDPageCacheAppend(App.dx.lpD3DDevice, HDInstallRoot(), pages, n,
		kPagesManaged, DecodeThreads(), &stats, &why) && gLogEnabled)
		Log("HDLOG level=%s water cards' pages refused reason=%s", g_stem,
			why);
}

/* The cards of this frame, after the rooms' blend and additive phases:
 * the world into view space through the engine's own matrix, which in the
 * HD frame is the view itself (HDFrameDrawGlow hands the bracket an
 * identity view).
 *
 * SPLASHLOG, with -log, once a second of game time (30 ticks) while a
 * splash, a card, a wet film or a breaking tank's water draws, or while a
 * tank's water still runs out over its floor:
 *
 *   SPLASHLOG level=<stem> splashes=<n> cards=<n> rings=<n>
 *             tank=<item>:<state> tanks=<n> floor=<item>:<wet>
 *
 * splashes the HD splashes alive, cards the cards drawn this frame, rings
 * the splash rings handed to the sheets (two a splash), tank the item of
 * the first wet film's FISHTANK and that film's strength 0..1 (-1:0.00
 * when none is wet), tanks the breaking tanks whose water the frame drew
 * ("the tank's water"), floor the first tank whose floor is wet and the
 * mean wet over its points, 0..1 (-1:0.00 when none is; "the wet
 * floor"). */
static void DrawWaterCards()
{
	static long logged = -1;

	if (!HDRenderWaterSheetsBuilt())
		return;

	float m[16];

	WorldToFrame(m);
	HDRenderWaterCardsDraw(m);
	HDSpringDraw(m);

	HDRenderSplashStats st;

	HDRenderSplashStatsNow(&st);

	const bool running = st.floor >= 0 && st.floorWet > 0.0f &&
		st.floorWet < 0.999f;

	if (!gLogEnabled || (!st.cards && !st.rings && st.wet <= 0.0f &&
		!st.tanks && !running) || GlobalCounter / 30 == logged)
		return;

	logged = GlobalCounter / 30;
	Log("SPLASHLOG level=%s splashes=%d cards=%d rings=%d tank=%d:%.2f "
		"tanks=%d floor=%d:%.2f", g_stem, st.splashes, st.cards, st.rings,
		st.tank, st.wet, st.tanks, st.floor, st.floorWet);
}

static void BuildWaterSheets()
{
	std::vector<HDWaterRoom> rooms((size_t)number_rooms);
	std::vector<signed char> floors;
	std::vector<short> below;
	std::vector<signed char> ceilings;
	std::vector<unsigned short> slopes;
	std::vector<short> sinks;
	std::vector<size_t> first((size_t)number_rooms, 0);

	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < number_rooms; i++)
		{
			const ROOM_INFO* r = &room[i];
			const int nx = r->floor ? r->y_size : 0;
			const int nz = r->floor ? r->x_size : 0;

			if (!pass)
			{
				first[(size_t)i] = floors.size();
				floors.resize(floors.size() + (size_t)(nx * nz));
				below.resize(floors.size());
				ceilings.resize(floors.size());
				slopes.resize(floors.size() * 4);
				sinks.resize(floors.size());
				continue;
			}

			HDWaterRoom& w = rooms[(size_t)i];

			w.originX = r->x;
			w.originY = r->y;
			w.originZ = r->z;
			w.sizeX = nx;
			w.sizeZ = nz;
			w.flags = r->flags;
			w.flip = r->flipped_room;
			w.drawn = i < HD_MAX_ROOMS && g_group_hd[i] ? 1 : 0;
			w.floor = nx * nz ? &floors[first[(size_t)i]] : 0;
			w.below = nx * nz ? &below[first[(size_t)i]] : 0;
			w.ceiling = nx * nz ? &ceilings[first[(size_t)i]] : 0;
			w.slope = nx * nz ? &slopes[first[(size_t)i] * 4] : 0;
			w.sink = nx * nz ? &sinks[first[(size_t)i]] : 0;

			for (int sx = 0; sx < nx; sx++)
				for (int sz = 0; sz < nz; sz++)
				{
					const FLOOR_INFO& f = r->floor[sz + r->x_size * sx];
					const size_t at = first[(size_t)i] + (size_t)(sx * nz + sz);

					floors[at] = f.floor;
					below[at] = f.pit_room == NO_ROOM ? -1 : f.pit_room;
					ceilings[at] = f.ceiling;
					WaterSquareWords(f, &slopes[at * 4], &sinks[at]);
				}
		}

	// The sinks: the camera table's places and strengths.
	std::vector<HDWaterSink> table;

	for (int c = 0; camera.fixed && c < number_cameras; c++)
	{
		HDWaterSink k;

		k.x = camera.fixed[c].x;
		k.y = camera.fixed[c].y;
		k.z = camera.fixed[c].z;
		k.strength = camera.fixed[c].data;
		table.push_back(k);
	}

	HDRenderWaterSinks(table.empty() ? 0 : &table[0], (int)table.size());
	HDRenderWaterFile("hd_water.txt");
	BuildWaterFilms();
	HDRenderSetWaterSheets(rooms.empty() ? 0 : &rooms[0], (int)rooms.size(),
		WaterSection(), g_stem, tomb5.hd_water ? 1 : 0);
	BuildWaterFalls();
	AppendWaterCardPages();
	MoveWaterfallMist();

	/* The depth texture the sheets read (HDRenderWaterDepthBind), bound
	 * here, between frames, only when this level built sheets - with
	 * tomb5.hd_water 0, and in a level with no water square, the device
	 * keeps its own depth buffer. It goes with the level's buffers
	 * (release_buffers): at the next level, and before a Reset or DXClose
	 * through HDLevelDeviceLost, after which the HD path waits for the next
	 * level as it does for everything else. */
	HDRenderWaterLevel built;
	const char* refused = "ok";

	HDRenderWaterLevelInfo(&built);

	if (built.built && built.depth && !HDRenderWaterDepthBind(&refused) &&
		gLogEnabled)
		Log("HDLOG water depth texture refused: %s - the sheets read the "
			"classic floor's depth", refused);

	/* The water's shaders warmed once, now, so the first frame that shows
	 * a sheet does not wait for the driver (hd_render.cpp, "the
	 * warm-up"). A new line with -log: the whole time and each draw's. */
	double warm[8];
	const double warmed = HDRenderWaterWarmUp(warm, 8);

	if (warmed >= 0.0 && gLogEnabled)
		Log("HDLOG level=%s water warm-up %.2f ms: sheet %.2f, depth %.2f, "
			"under %.2f, foot %.2f, river %.2f, foam %.2f, foam depth %.2f, "
			"cards %.2f", g_stem, warmed, warm[0], warm[1], warm[2], warm[3],
			warm[4], warm[5], warm[6], warm[7]);

	/* And the marble floors' mirror, so the first frame with one does not
	 * wait for the driver (hd_render.cpp, HDRenderMarbleWarmUp). */
	double marble[5];
	const double marbled = HDRenderMarbleWarmUp(marble, 5);

	if (marbled >= 0.0 && gLogEnabled)
		Log("HDLOG level=%s marble warm-up %.2f ms: target %.2f, plain "
			"%.2f, gloss %.2f, glow %.2f, layer %.2f", g_stem, marbled,
			marble[0], marble[1], marble[2], marble[3], marble[4]);

	/* WATERLOG, with -log: one level line and one line per body - the
	 * legend is over HDRenderWaterLogLine in hd_render.cpp, which writes
	 * RoomProbe -hdwater's lines too. */
	if (!gLogEnabled)
		return;

	char line[512];

	HDRenderWaterLogLine(-1, g_stem, tomb5.hd_water ? 1 : 0, line,
		sizeof(line));
	Log("%s", line);

	for (int b = 0; b < HDRenderWaterBodies(); b++)
		if (HDRenderWaterLogLine(b, g_stem, 0, line, sizeof(line)))
			Log("%s", line);

	for (int f = 0; HDRenderWaterFallLine(f, g_stem, line, sizeof(line)); f++)
		Log("%s", line);

	for (int f = 0; HDRenderWaterFootLine(f, g_stem, line, sizeof(line)); f++)
		Log("%s", line);

	for (int s = 0; HDRenderWaterSegLine(s, g_stem, line, sizeof(line)); s++)
		Log("%s", line);

	for (int a = 0; HDRenderWaterArtLine(a, g_stem, line, sizeof(line)); a++)
		Log("%s", line);

	for (int w = 0; HDRenderWaterWetLine(w, g_stem, line, sizeof(line)); w++)
		Log("%s", line);
}

/* The water surface the .TRG has no geometry for.
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
 *     ANDREA3, JOBY3, RICH2  0 of 13 - those rooms keep their surface
 *
 * 61 of the 93 rooms, then, whose plane has less than half the art.
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
	g_water_face_dry = 0;

	int underSheets = 0;	// faces a water sheet takes the place of

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
		int underSheet = 0;
		std::vector<unsigned char> dryCopies;	// per restored face

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

				/* This project's water sheet over this piece of ground takes
				 * the face's place (BuildWaterSheets). With tomb5.hd_water 0
				 * no sheet is built and the answer is always no; so it is for
				 * a body a row of class none switched off. */
				if (HDRenderWaterSheetAt(i, (int)x0, (int)z0, (int)x1, (int)z1))
				{
					underSheet++;
					continue;
				}

				/* A face over a square with no water room below it is no
				 * water: a floor at the water's height whose corners border
				 * the water and so carry the surface mark. Where the art
				 * draws its own floor in that plane there, the face is a
				 * second copy of it and the two fight for the same depth -
				 * JOBY5 room 4's deck, five such faces beside the sheet. It
				 * is left out once the room's water is a sheet's (dryCopy,
				 * after the loop); with no sheet the room keeps it, as the
				 * majority decides. */
				const int fsx = (int)floorf((x0 + x1) * 0.5f / 1024.0f);
				const int fsz = (int)floorf((z0 + z1) * 0.5f / 1024.0f);
				bool dryCopy = false;

				if (r->floor && fsx >= 0 && fsz >= 0 && fsx < r->y_size &&
					fsz < r->x_size)
				{
					const FLOOR_INFO& below = r->floor[fsz + r->x_size * fsx];
					const bool water = below.pit_room != NO_ROOM &&
						(room[below.pit_room].flags & ROOM_UNDERWATER);

					dryCopy = !water && HDRenderGroupFlatArt(i,
						(int)r->minfloor, (int)x0, (int)z0, (int)x1, (int)z1);
				}

				HDRoomWaterFace entry;

				entry.roomlet = (short)n;
				entry.face = (short)(quad ? f : f - roomlet->nQuad);
				entry.quad = quad ? 1 : 0;
				g_water_faces[i].push_back(entry);
				dryCopies.push_back(dryCopy ? 1 : 0);
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
		 * (6 -> 5) and 184 (29 -> 27).
		 *
		 * This project's water sheet is dropped per face as well, and before
		 * the majority, which still counts the face. A sheet is no hole
		 * either: it covers every water square of its room, whatever art lies
		 * on it, so a face under it is never restored, and the majority only
		 * decides the faces no sheet covers - over a square with no
		 * underwater room below, or of a body an hd_water.txt row of class
		 * none switched off. With tomb5.hd_water 0 no face is under a sheet
		 * and the majority decides alone. */
		if (inPlane && covered * 2 >= inPlane)
		{
			g_water_face_kept += inPlane;
			g_water_faces[i].clear();
		}
		else
			// A sheeted face is left to the .TRG as surely as a covered one.
			g_water_face_kept += sheeted;

		// The dry copies (see above), once a sheet draws the room's water.
		int dryLeft = 0;

		if (underSheet && dryCopies.size() == g_water_faces[i].size())
		{
			size_t keep = 0;

			for (size_t k = 0; k < g_water_faces[i].size(); k++)
				if (dryCopies[k])
					dryLeft++;
				else
					g_water_faces[i][keep++] = g_water_faces[i][k];

			g_water_faces[i].resize(keep);
		}

		g_water_face_dry += dryLeft;

		if (!g_water_faces[i].empty())
		{
			g_water_face_rooms++;
			g_water_face_total += (int)g_water_faces[i].size();
			g_water_faces[i].shrink_to_fit();

			Log("HDLOG room=%d water surface restored=%d of %d face(s) at "
				"y=%d, the .TRG covers %d of them and lays its own water over "
				"%d, under a water sheet %d, dry copies left out %d", i,
				(int)g_water_faces[i].size(), inPlane, (int)r->minfloor,
				covered, sheeted, underSheet, dryLeft);
		}
		else if (sheeted)
			Log("HDLOG room=%d water surface left to the .TRG, %d of its %d "
				"face(s) under the .TRG's own sheet, under a water sheet %d, "
				"dry copies left out %d", i, sheeted, inPlane, underSheet,
				dryLeft);
		else if (underSheet)
			Log("HDLOG room=%d water surface under a water sheet, %d of its "
				"%d face(s), dry copies left out %d", i, underSheet, inPlane,
				dryLeft);

		underSheets += underSheet;
	}

	Log("HDLOG level=%s water surfaces restored=%d in %d room(s), "
		"left to the .TRG=%d, under a water sheet=%d, dry copies left "
		"out=%d", g_stem, g_water_face_total, g_water_face_rooms,
		g_water_face_kept, underSheets, g_water_face_dry);
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
 * The remaster bakes the static meshes into the art, so a static whose place
 * the art draws must not be drawn a second time. The art that holds a static
 * is not always its room's own: ANDREA2's palm crowns stand in the rooms
 * above the palms and are modelled in the trunk's room, and a room drawn as
 * another group's art (hd_cover.h, HDCoverRoomState) has its statics in that
 * group. And the art does not hold every static: 103 of the game's 2,845
 * stay classic, 24 of them in JOBY3 (RoomProbe -statictable). A static the
 * game destroys at run time is gone from the classic list but goes on standing in
 * the baked art, so it could not be destroyed.
 *
 * All of it is answered here, once per level, against the .TRG's own
 * geometry:
 *
 *   g_static_art      one answer per static of the level (hd_cover.h,
 *                     HDStaticAnswer): HD_STATIC_ART - the art draws its
 *                     place in every frame that lists its room, so
 *                     DrawStaticObjects never draws it; HD_STATIC_GROUPS -
 *                     the art of the groups it names draws it, so it is
 *                     drawn the classic way only in a frame that lists none
 *                     of them; HD_STATIC_CLASSIC - drawn the classic way in
 *                     every frame.
 *   g_destruct_pairs  the destructible pieces of the .TRG - a part whose
 *                     material carries bit 15 - paired with the classic
 *                     MESH_INFO of slot 50 to 59 standing inside their box,
 *                     whichever group the piece is in. The engine clears
 *                     that mesh's Flags bit 0 when Lara shoots it
 *                     (control.cpp), and the piece goes with it.
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

struct HDStaticEntry
{
	int answer;
	short holder[HD_STATIC_MAX_GROUPS];	// HD_STATIC_GROUPS: the groups, -1 after
	int piece;		// HD_STATIC_GROUPS: its piece (HDRenderStaticPiece) or -1
	int pieceGroup;		// and the group that draws it
};

static std::vector<HDStaticEntry> g_static_art;
static std::vector<int> g_static_first;		// per room, index into the above
static std::vector<HDStaticPair> g_destruct_pairs;
/* Every static's place, in the order of g_static_art, handed to the build
 * (HDRenderSetStaticPlaces) and given back once BuildStaticTables has made
 * the pieces. */
static std::vector<HDStaticPlace> g_static_places;

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

/* The statics' places for the build: every MESH_INFO of every room in the
 * order BuildStaticTables numbers them, its box moved to world x and z; one
 * with no static slot gets an empty box. Before HDRenderLevelBegin, like the
 * marks; group r is room r here, before any flip. */
static void BuildStaticPlaces()
{
	g_static_places.clear();

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];

		for (int i = 0; i < info->num_meshes; i++)
		{
			const MESH_INFO* mesh = &info->mesh[i];
			HDStaticPlace place;

			place.room = r;
			place.mins[0] = place.mins[1] = place.mins[2] = 1.0f;
			place.maxs[0] = place.maxs[1] = place.maxs[2] = 0.0f;

			if (mesh->static_number >= 0 && mesh->static_number < 70)
			{
				StaticBox(mesh, info, place.mins, place.maxs);
				place.mins[0] += (float)info->x;
				place.maxs[0] += (float)info->x;
				place.mins[2] += (float)info->z;
				place.maxs[2] += (float)info->z;
			}

			g_static_places.push_back(place);
		}
	}

	HDRenderSetStaticPlaces(g_static_places.empty() ? 0 : &g_static_places[0],
		(int)g_static_places.size());
}

/* The visibility path (tomb5.hd_vis, hd_vis.h), taken at the start of the
 * level. With it the rooms a frame draws are the portal walk's, extended by
 * the tables hd_vis.cpp makes, every listed room's art drawn whole but for
 * its art standing inside another room, which DecidePairs answers as the
 * other path does, and no room is carried. What
 * the build marks from the level data is kept, the two depth pushes: the
 * intrusion list (BuildRoomIntrusions) and the rooms' air (BuildRoomAir)
 * go to the renderer, whose hulls - a group whose art outside its own room
 * doubles other rooms' art - and covered faces - a room's classic face
 * inside a larger one of the room it stands in, in one plane - draw that
 * art pushed back, so the art of the room it stands in is the copy seen.
 * Two copies of one wall in the depth buffer fight in a speckle that
 * moves with the eye; pushed, one of them is drawn. `g_vis_main` is, per
 * room of a flipmap pair, the pair's room the portals lead to (the one whose
 * flipped_room names the other), -1 for every other room: a group of a pair
 * is in the world only while that room draws it.
 *
 * The classic side goes to hd_vis.cpp here, before the build: every room's
 * origin, sectors, box in y and twin, its classic faces in world units as
 * BuildStandFaces reads them (none for a room a randomizer marker names
 * whole, which is drawn classic), and its portal records as GetRoomBounds
 * reads them. The renderer's build adds the art (HDVisLevelBuild). */
static bool g_vis_level = false;
static bool g_vis_logged = false;
static std::vector<short> g_vis_main;
/* Per room, where its statics begin in the list handed to hd_vis.cpp
 * (HDVisLevelStatics): every MESH_INFO of every room, in the rooms' order
 * and the lists' - the index of a room's mesh i is its first plus i. */
static std::vector<int> g_vis_static_first;

/* A static mesh's faces in world units, as DrawStaticObjects places the
 * mesh - its MESH_DATA turned by y_rot and moved to the MESH_INFO's place,
 * as BuildStaticTables reads its vertices -, a quad as four corners and a
 * triangle as three; how many went in. */
static int VisStaticFaces(const MESH_INFO* mesh, std::vector<HDCoverFace>& out)
{
	const STATIC_INFO* sinfo = &static_objects[mesh->static_number];
	const MESH_DATA* data = (const MESH_DATA*)meshes[sinfo->mesh_number];

	if (!data || !data->aVtx || data->nVerts <= 0)
		return 0;

	const double c = (double)phd_cos(mesh->y_rot) * (1.0 / 16384.0);
	const double sn = (double)phd_sin(mesh->y_rot) * (1.0 / 16384.0);
	int made = 0;

	const auto put = [&](const short* index, int corners)
	{
		HDCoverFace face;

		face.corners = corners;
		face.doubleSided = 0;

		for (int k = 0; k < 4; k++)
		{
			const int at = index[k < corners ? k : 0];

			if (at < 0 || at >= data->nVerts)
				return;

			const ACMESHVERTEX& v = data->aVtx[at];

			face.v[k][0] = (double)mesh->x + v.x * c + v.z * sn;
			face.v[k][1] = (double)mesh->y + v.y;
			face.v[k][2] = (double)mesh->z + v.z * c - v.x * sn;
		}

		out.push_back(face);
		made++;
	};

	for (int q = 0; data->gt4 && q < data->ngt4; q++)
		put(&data->gt4[q * 6], 4);

	for (int q = 0; data->gt3 && q < data->ngt3; q++)
		put(&data->gt3[q * 5], 3);

	return made;
}

static bool VisLevel()
{
	return g_vis_level;
}

static void BuildRoomVis()
{
	g_vis_level = tomb5.hd_vis == 1;
	g_vis_logged = false;
	g_piece_wall.clear();
	g_piece_wall_built = false;
	g_vis_main.assign((size_t)(number_rooms > 0 ? number_rooms : 0), -1);

	if (!g_vis_level || number_rooms <= 0)
	{
		HDVisLevelClear();
		return;
	}

	std::vector<HDVisRoom> rooms((size_t)number_rooms);
	std::vector<HDCoverFace> faces;
	std::vector<HDVisPortal> portals;
	std::vector<int> links;

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		HDVisRoom& one = rooms[(size_t)r];

		if (info->flipped_room >= 0 && info->flipped_room < number_rooms)
		{
			g_vis_main[(size_t)r] = (short)r;
			g_vis_main[(size_t)info->flipped_room] = (short)r;
		}

		one.x = info->x;
		one.y = info->y;
		one.z = info->z;
		one.xs = info->y_size;
		one.zs = info->x_size;
		one.top = info->maxceiling;
		one.bottom = info->minfloor;
		one.flags = info->flags;
		one.twin = info->flipped_room;
		one.firstFace = (int)faces.size();
		one.faceCount = 0;
		one.firstPortal = (int)portals.size();
		one.portalCount = 0;
		one.firstLink = (int)links.size();
		one.linkCount = 0;

		for (int i = 0; info->pRoomlets && !HDMarkersRoomTouched(r) &&
			i < info->nRoomlets; i++)
		{
			const ROOMLET* roomlet = &info->pRoomlets[i];
			const ROOMLET_VERTEX* vtx = (const ROOMLET_VERTEX*)roomlet->pSVtx;
			const short* ptr = roomlet->pFac;

			if (!vtx || !ptr || roomlet->nVtx <= 0)
				continue;

			for (int q = 0; q < roomlet->nQuad + roomlet->nTri; q++)
			{
				const int corners = q < roomlet->nQuad ? 4 : 3;
				HDCoverFace face;
				bool good = true;

				face.corners = corners;
				face.doubleSided = (ptr[corners] >> 15) & 1;

				for (int c = 0; c < 4; c++)
				{
					const int at = c < corners ? ptr[c] : ptr[0];

					if (at < 0 || at >= roomlet->nVtx)
						good = false;

					const ROOMLET_VERTEX& v = vtx[good ? at : 0];

					face.v[c][0] = (double)v.x + info->x;
					face.v[c][1] = (double)v.y + info->y;
					face.v[c][2] = (double)v.z + info->z;
				}

				ptr += corners + 2;

				if (good)
				{
					faces.push_back(face);
					one.faceCount++;
				}
			}
		}

		// The door list: a count, then 16 words a portal - the room, the
		// normal, four corners.
		if (info->door)
		{
			const short* door = info->door;
			const int count = *door++;

			for (int i = 0; i < count; i++, door += 16)
			{
				HDVisPortal p;

				p.to = door[0];

				for (int k = 0; k < 3; k++)
					p.n[k] = door[1 + k];

				for (int c = 0; c < 4; c++)
					for (int k = 0; k < 3; k++)
						p.v[c][k] = door[4 + c * 3 + k];

				portals.push_back(p);
				one.portalCount++;
			}
		}

		// The floor links, 255 for none, as BuildRoomIntrusions reads them.
		for (int i = 0; info->floor && i < info->x_size * info->y_size; i++)
			for (const int to : { (int)info->floor[i].sky_room,
				(int)info->floor[i].pit_room })
				if (to != 255 && to < number_rooms)
				{
					links.push_back(to);
					one.linkCount++;
				}
	}

	HDVisLevelInput(&rooms[0], (int)rooms.size(),
		faces.empty() ? 0 : &faces[0], (int)faces.size(),
		portals.empty() ? 0 : &portals[0], (int)portals.size(),
		links.empty() ? 0 : &links[0], (int)links.size());

	/* And the static meshes, for the art that lies on them
	 * (HDVisStaticCovers): no face for a static no slot of 0 to 69 names,
	 * one in a room a randomizer marker names, or a flat one (HDStaticFlat:
	 * a pane or a square laid on a wall lies on whatever art stands behind
	 * it, which need not model it), which stay classic. */
	std::vector<HDVisStatic> statics;
	std::vector<HDCoverFace> meshFaces;

	g_vis_static_first.assign((size_t)number_rooms + 1, 0);

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];

		g_vis_static_first[(size_t)r] = (int)statics.size();

		for (int i = 0; info->mesh && i < info->num_meshes; i++)
		{
			const MESH_INFO* mesh = &info->mesh[i];
			HDVisStatic one;

			one.room = r;
			one.mesh = i;
			one.firstFace = (int)meshFaces.size();
			one.faceCount = 0;

			for (int k = 0; k < 3; k++)
				one.mins[k] = one.maxs[k] = 0.0f;

			if (mesh->static_number >= 0 && mesh->static_number < 70 &&
				!HDMarkersRoomTouched(r))
			{
				StaticBox(mesh, info, one.mins, one.maxs);

				if (!HDStaticFlat(one.mins, one.maxs))
					one.faceCount = VisStaticFaces(mesh, meshFaces);
			}

			statics.push_back(one);
		}
	}

	g_vis_static_first[(size_t)number_rooms] = (int)statics.size();
	HDVisLevelStatics(statics.empty() ? 0 : &statics[0], (int)statics.size(),
		meshFaces.empty() ? 0 : &meshFaces[0], (int)meshFaces.size());
}

/* Whether the visibility path leaves a static mesh the frame would draw
 * classic to the art that lies on it (HDVisStaticDrawnByArt): the art of
 * one group lies on enough of its samples, and a room the frame lists is
 * drawn by that group. `group` is the room whose MESH_INFO list stands at
 * the static's room now (g_room_group), the room of the build. */
static int GroupOfRoom(int roomIndex);

static bool VisStaticLeftToArt(int group, int meshIndex, const short* listed,
	int listedCount)
{
	const HDVisTables* tables = HDVisLevelTables();

	if (!g_vis_level || !tables || !listed || group < 0 ||
		group + 1 >= (int)g_vis_static_first.size())
		return false;

	const int at = g_vis_static_first[(size_t)group] + meshIndex;

	if (meshIndex < 0 || at >= g_vis_static_first[(size_t)group + 1] ||
		at >= (int)tables->staticGroup.size())
		return false;

	const int holder = tables->staticGroup[(size_t)at];
	bool drawn = false;

	for (int i = 0; holder >= 0 && i < listedCount && !drawn; i++)
		drawn = GroupOfRoom(listed[i]) == holder;

	return HDVisStaticDrawnByArt(tables->staticShare[(size_t)at],
		HDVisDefaults().staticShare, drawn);
}

/* Whether the visibility path draws none of a classic room's faces
 * (HDVisClassicRoomHidden in hd_vis.h says which rooms and why): asked by
 * HDFrameRoom for a room no group draws, before the stand rule. The room's
 * number is the walk's and the tables'; its group is the one the flip table
 * names, empty when the room's own art is nothing at all. A room a
 * randomizer marker names is drawn classic whatever the art. Per frame,
 * because the stand rule's state and the tables are the level's and cost
 * one look each. */
static bool VisHidesClassicRoom(int roomIndex)
{
	if (!g_vis_level || roomIndex < 0 || roomIndex >= g_room_count ||
		HDMarkersRoomTouched(roomIndex))
		return false;

	const HDVisTables* tables = HDVisLevelTables();
	const int group = g_room_group[roomIndex];

	if (!tables || roomIndex >= tables->rooms || group < 0 ||
		group >= HD_MAX_ROOMS || roomIndex >= number_rooms)
		return false;

	const bool covered = g_group_stand_covered[group] != 0 ||
		g_group_stand_part[group] != 0;
	const ROOM_INFO* r = &room[roomIndex];
	const int faces = r->nRoomlets > 0 && r->nVerts > 0 ? 1 : 0;
	const int tris = HDRenderGroupTriangles(group, -1);

	if (HDVisClassicRoomHidden(tris == 0, covered, faces,
		tables->guestCount[(size_t)roomIndex]))
		return true;

	// A room standing inside another whose art models its inside.
	return group < (int)tables->enclosedBy.size() &&
		HDVisClassicRoomEnclosed(tris > HDRenderGroupOverhangTriangles(group),
		covered, faces, tables->enclosedBy[(size_t)group]);
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
 * room 134 draws a band. A room the reach lists with no window has no
 * classic face to ask either, and every piece of its art keeps the smaller
 * box's answer, whatever holder it stands in: left out in a listed holder,
 * such a piece opens a hole wherever it is that holder's own wall or
 * doorway, modelled in the neighbour's group.
 *
 * ANDREA1 comes to 16,718 intruding triangles in 89 groups, 15,993 of them
 * claimed by 558 classic faces and 725 by none; 11,020 more that no face
 * claims stand in a holder's wall ring and are drawn open. The air shaft is
 * 411 of room 53's triangles inside room 207, all claimed by 27 faces.
 *
 * No count threshold: the answer is per triangle and per frame, so a stray
 * triangle costs only itself and cannot scissor a facade.
 *
 * A second test stands beside the box: whether a triangle stands - its
 * centroid's x and z, the heights its corners span - in the air of a room
 * its own room has no portal, door or vertical link with, not even
 * through a flipmap twin - the space the room's floor squares enclose,
 * off its walls, floors and ceilings and out of its doorways (HDRoomAir
 * in hd_render.h, BuildRoomAir). Such art that an entry's box holds too -
 * by its centroid, or by the centroid's x and z when it reaches into the
 * holder's air (hd_render.cpp, the pair_at loop of the index fill) - is
 * drawn only inside its own room's window and
 * left out when that window is empty (DecidePairs), whatever the box's
 * answer, unless the classic path draws a face of its room that claims
 * it. The box alone cannot tell it from the art of a wall two rooms share,
 * and on the box's answer it comes through a holder's walls wherever its
 * own room's window is open to the eye or the holder is not listed: a
 * brick hull in the sky beside a flag, a window frame in a brick loft, a
 * marble strip over a metal hub, black bars beside a pillar, a lit floor
 * in a doorway.
 *
 * The air is asked of every triangle, for the level's counts, and only the
 * art a box holds is held to the window: over the fifteen levels 112,265
 * of the 364,423 triangles in such air stand in an entry's box (TrgProbe
 * -roomspace, air_boxed; asked by the centroid's height alone, 109,000 of
 * 355,806, and 40,798 of those art the open half would draw - no face
 * claims them outside the smaller box -), the black bars among them
 * (1,607 of the 2,160 triangles of room 30 in room 16). The rest stands in
 * no entry's box, most of it hung far outside its own room - ANDREA2's
 * street art of rooms 10 and 236 in rooms 206 and 79, the palms of rooms
 * 117 and 127 in room 125 - where the classic frame shows it. Held to its
 * own window as well, it removed art that stands in its place - a tower
 * top, a door, palm crowns: on the 131 room-centre views of ANDREA1,
 * ANDREA2, JOBY2 and RICH2 that the air changes it took 1,379,566 pixels,
 * 378,633 of them clear colour, against 190,895 and 111,279 with the boxed
 * art alone, both with the air asked by the centroid's height (216,318 and
 * 119,107 by the corners' heights); the five faults named above go the
 * same way either way, pixel for pixel.
 *
 * It runs before the build, beside BuildRoomBoxes, because the renderer tags
 * the triangles as it fills the index buffer. Everything it reads is the
 * classic level - room boxes, portals, floor links, the roomlets' faces - so
 * TrgProbe -roomspace answers with the very same list out of the .TRC and
 * the .TRG. */
static std::vector<HDIntrusion> g_intrusions;
// Who is next door, n * n - kept for BuildRoomAir, which asks the same.
static std::vector<unsigned char> next_door;

static void BuildRoomIntrusions()
{
	g_intrusions.clear();

	/* Who is next door: the portal list of both rooms, and the vertical
	 * links, which have no portal record at all - a stacked pair is joined
	 * through FLOOR_INFO::pit_room and sky_room. */
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

/* Every room's air for the renderer (HDRoomAir in hd_render.h): per floor
 * square the lowest ceiling corner and the highest floor corner, out of the
 * square's floor and ceiling bytes and its floor data (HDSectorAir), in the
 * order of ROOM_INFO::floor - x over y_size squares, z over x_size, as in
 * BuildRoomBoxes. The renderer asks it of every triangle of every room's
 * group; DecidePairs draws those that stand in an unlinked room's air, and
 * in an intrusion entry's box, only inside their own room's window.
 *
 * With it the pairs of rooms the air is not asked about, row by the art's
 * room: the same room, the rooms next door by portal or vertical link
 * (next_door, as BuildRoomIntrusions found them), the rooms a floor
 * square's portal leads to, and flipmap twins. The portal list does not
 * name every floor portal: ANDY1's room 72 reaches room 2 only through
 * its floor data, and art of 2 standing in 72's air - 1,567 triangles -
 * is seen through that doorway.
 *
 * Read at level build like BuildRoomIntrusions and after it, before any
 * flipmap, and borrowed by the renderer for the build. `g_air_build_ms` is
 * what reading it cost, for the level's line.
 *
 * Only with tomb5.hd_air on, the default, taken at the start of the level:
 * off, no room has air for the renderer to ask, no triangle stands in
 * unlinked air, and every piece and every answer is the box's alone, as
 * before the air was asked. `g_air_on` is what this level took, for the
 * level's line: 0, 1, or 2, which hands the renderer the hulls too
 * (HDRenderSetAirHulls). */
static std::vector<HDRoomAir> g_room_air;
static std::vector<float> g_room_air_sectors;
static std::vector<unsigned char> g_room_links;
static double g_air_build_ms = 0.0;
static int g_air_on = 0;

static void BuildRoomAir()
{
	g_air_on = tomb5.hd_air <= 2 ? (int)tomb5.hd_air : 1;
	HDRenderSetAirHulls(g_air_on == 2 ? 1 : 0);

	if (!g_air_on)
	{
		g_room_air.clear();
		g_room_air_sectors.clear();
		g_room_links.clear();
		g_air_build_ms = 0.0;
		HDRenderSetRoomAir(0, 0, 0);
		return;
	}

	LARGE_INTEGER from, to, rate;
	std::vector<size_t> first((size_t)number_rooms, 0);

	QueryPerformanceCounter(&from);
	g_room_air.assign((size_t)number_rooms, HDRoomAir());
	g_room_air_sectors.clear();
	g_room_links.assign((size_t)number_rooms * (size_t)number_rooms, 0);

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		HDRoomAir& air = g_room_air[(size_t)r];

		air.x = (float)info->x;
		air.y = (float)info->y;
		air.z = (float)info->z;
		air.nx = info->floor ? info->y_size : 0;
		air.nz = info->floor ? info->x_size : 0;
		air.sectors = 0;
		first[(size_t)r] = g_room_air_sectors.size();

		for (int sx = 0; sx < air.nx; sx++)
			for (int sz = 0; sz < air.nz; sz++)
			{
				const FLOOR_INFO& f = info->floor[sz + info->x_size * sx];
				float top, bottom;
				int door;

				/* GetHeight reads a chain to its end bit with no bound; this
				 * one stops a broken chain at 65536 words, the reach of the
				 * 16 bit FLOOR_INFO::index itself. */
				HDSectorAir(f.floor, f.ceiling, f.index && floor_data ?
					(const unsigned short*)&floor_data[f.index] : 0, 65536,
					&top, &bottom, &door);
				g_room_air_sectors.push_back(top);
				g_room_air_sectors.push_back(bottom);

				// A square Lara walks through into another room links the two.
				if (door >= 0 && door < number_rooms)
				{
					g_room_links[(size_t)r * number_rooms + door] = 1;
					g_room_links[(size_t)door * number_rooms + r] = 1;
				}
			}
	}

	for (int r = 0; r < number_rooms; r++)
	{
		HDRoomAir& air = g_room_air[(size_t)r];

		if (air.nx > 0 && air.nz > 0)
			air.sectors = &g_room_air_sectors[first[(size_t)r]];
	}

	for (int b = 0; b < number_rooms; b++)
		for (int a = 0; a < number_rooms; a++)
		{
			const size_t at = (size_t)b * number_rooms + a;

			if (a == b || (at < next_door.size() && next_door[at]) ||
				room[a].flipped_room == b || room[b].flipped_room == a)
				g_room_links[at] = 1;
		}

	/* And closed over the flipmap twins: a flipmap swaps a room's contents
	 * with its twin's, so in one state the portals of the twin's data lead
	 * out of the room standing at that place. Art of b in a's air is seen
	 * through such a portal when b, or b's twin, has a link with a or with
	 * a's twin. Without it 31 (group, host) pairs of 24,936 triangles over
	 * the fifteen levels (TrgProbe -roomspace with and without the step)
	 * are held to their own room's window in every flip state. */
	std::vector<int> twin((size_t)number_rooms, -1);

	for (int r = 0; r < number_rooms; r++)
		if (room[r].flipped_room >= 0 && room[r].flipped_room < number_rooms)
		{
			twin[(size_t)r] = room[r].flipped_room;
			twin[(size_t)room[r].flipped_room] = r;
		}

	const std::vector<unsigned char> direct = g_room_links;

	for (int b = 0; b < number_rooms; b++)
		for (int a = 0; a < number_rooms; a++)
		{
			const int tb = twin[(size_t)b];
			const int ta = twin[(size_t)a];
			const size_t n = (size_t)number_rooms;

			if ((ta >= 0 && direct[(size_t)b * n + ta]) ||
				(tb >= 0 && direct[(size_t)tb * n + a]) ||
				(ta >= 0 && tb >= 0 && direct[(size_t)tb * n + ta]))
				g_room_links[(size_t)b * n + a] = 1;
		}

	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&rate);
	g_air_build_ms = rate.QuadPart ? (double)(to.QuadPart - from.QuadPart) *
		1000.0 / (double)rate.QuadPart : 0.0;

	if (!g_room_air.empty())
		HDRenderSetRoomAir(&g_room_air[0], (int)g_room_air.size(),
			&g_room_links[0]);
	else
		HDRenderSetRoomAir(0, 0, 0);
}

/* The carried art (HDCarryBoxHolds in hd_render.h): the rooms under the sky
 * and the flipmap twins handed to the build, and after it, per room under
 * the sky, the rooms whose art the build found standing in it - what
 * HDRoomCarries answers the room walk. Only with tomb5.hd_air on: with it
 * off the walk is the one before the air rule, room for room. */
static std::vector<unsigned char> g_carry_sky;
static std::vector<short> g_carry_twin;
static std::vector<std::vector<short> > g_room_carry;
/* Per room, the room of its flipmap pair the portals lead to - the one
 * whose flipped_room names the other, which FlipMap leaves in place - or
 * -1 for a room with no twin (HDCarriedRoom). */
static std::vector<short> g_carry_main;

static void BuildRoomCarry()
{
	g_carry_sky.clear();
	g_carry_twin.clear();
	g_room_carry.clear();
	g_carry_main.clear();

	if (!g_air_on || number_rooms <= 0)
	{
		HDRenderSetCarryHosts(0, 0, 0);
		return;
	}

	g_carry_sky.resize((size_t)number_rooms);
	g_carry_twin.resize((size_t)number_rooms);

	g_carry_main.assign((size_t)number_rooms, -1);

	for (int i = 0; i < number_rooms; i++)
	{
		g_carry_sky[(size_t)i] = (room[i].flags & ROOM_OUTSIDE) ? 1 : 0;
		g_carry_twin[(size_t)i] = room[i].flipped_room;

		if (room[i].flipped_room >= 0 && room[i].flipped_room < number_rooms)
		{
			g_carry_main[(size_t)i] = (short)i;
			g_carry_main[(size_t)room[i].flipped_room] = (short)i;
		}
	}

	HDRenderSetCarryHosts(&g_carry_sky[0], &g_carry_twin[0], number_rooms);
}

/* HDLOG carry: what the build found, read back before the borrowed lists
 * go back - one line per (host, group) pair and the level's total. Behind
 * -log for the lines; the table is kept either way.
 *
 *   host    a room under the sky, as ROOMLIST prints rooms
 *   group   the room whose art stands in it, taken in with it
 *   tris    that art's triangles, all three passes together */
static void ReportRoomCarry()
{
	g_room_carry.assign((size_t)(number_rooms > 0 ? number_rooms : 0),
		std::vector<short>());

	int tris = 0;
	const int pairs = HDRenderCarryPairs();

	for (int i = 0; i < pairs; i++)
	{
		int group = -1, host = -1, n = 0;

		if (!HDRenderCarryPair(i, &group, &host, &n) || host < 0 ||
			host >= number_rooms || group < 0 || group >= number_rooms)
			continue;

		g_room_carry[(size_t)host].push_back((short)group);
		tris += n;

		if (gLogEnabled)
			Log("HDLOG carry level=%s host=%d group=%d tris=%d", g_stem, host,
				group, n);
	}

	if (gLogEnabled)
		Log("HDLOG carry level=%s pairs=%d tris=%d air=%d", g_stem, pairs,
			tris, g_air_on);

	HDRenderSetCarryHosts(0, 0, 0);
	g_carry_sky.clear();
	g_carry_twin.clear();
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

/* The classic faces another group's art may stand on (hd_cover.h).
 *
 * A room drawn classic can have its floor and walls modelled in another
 * group: ANDREA2's room 124 is a hollow group, and group 121 - the room
 * through its west portal - carries 14,700 triangles inside it that cover
 * all 46 of its classic faces. Neither the intrusion list nor the pieces see
 * that art (a neighbour is never on the list, BuildRoomIntrusions), so it is
 * drawn with 121 whenever 121 is listed, in the same place as 124's
 * roomlets, and the two surfaces fight; which one wins depends on whether the
 * view lists 121. TrgProbe -classicart finds 22 such rooms with faces in the
 * game, two of them covered completely (ANDREA2 124 by group 121, RICH2 15
 * by group 14).
 *
 * Every room with a classic face that the randomizer did not mark is handed
 * in - its faces in world units, as InsertRoomlet draws them, and its whole
 * box (the grid with its wall ring, maxceiling to minfloor) grown by
 * HD_COVER_REACH - with every room's origin and flipmap twin. The renderer
 * keeps the rooms whose group draws nothing inside the room's own box (no
 * group, an empty or a hollow one) and gives the art of every other group
 * that stands on one of their faces a stand range of its own; once the build
 * has said which groups are drawn HD, BuildStandRanges below decides which
 * of those rooms the art covers. Before the build, beside BuildPairFaces,
 * on the same contract; the lists are kept until BuildStandRanges has read
 * the faces back. */
static std::vector<HDStandHolder> g_stand_holders_built;
static std::vector<HDCoverFace> g_stand_faces_built;
// Per face handed in, its roomlet and its place in InsertRoomlet's loops.
static std::vector<int> g_stand_face_roomlet;
static std::vector<int> g_stand_face_slot;

static void BuildStandFaces()
{
	g_stand_rooms.clear();
	g_stand_holders_built.clear();
	g_stand_faces_built.clear();
	g_stand_face_roomlet.clear();
	g_stand_face_slot.clear();
	g_stand_rooms.resize((size_t)number_rooms);

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		HDStandRoom& one = g_stand_rooms[(size_t)r];

		one.origin[0] = (float)info->x;
		one.origin[1] = (float)info->y;
		one.origin[2] = (float)info->z;
		one.twin = info->flipped_room;
	}

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];

		if (!info->pRoomlets || HDMarkersRoomTouched(r))
			continue;

		HDStandHolder holder;

		holder.room = r;
		holder.firstFace = (int)g_stand_faces_built.size();
		holder.faceCount = 0;
		holder.mins[0] = (float)(info->x - HD_COVER_REACH);
		holder.maxs[0] = (float)(info->x + (info->y_size << 10) + HD_COVER_REACH);
		holder.mins[1] = (float)(info->maxceiling - HD_COVER_REACH);
		holder.maxs[1] = (float)(info->minfloor + HD_COVER_REACH);
		holder.mins[2] = (float)(info->z - HD_COVER_REACH);
		holder.maxs[2] = (float)(info->z + (info->x_size << 10) + HD_COVER_REACH);

		for (int i = 0; i < info->nRoomlets; i++)
		{
			const ROOMLET* roomlet = &info->pRoomlets[i];
			const ROOMLET_VERTEX* vtx = (const ROOMLET_VERTEX*)roomlet->pSVtx;
			const short* ptr = roomlet->pFac;

			if (!vtx || !ptr || roomlet->nVtx <= 0)
				continue;

			// The face layout InsertRoomlet reads: the corners, the texture
			// word (bit 15 double-sided) and one more word.
			for (int q = 0; q < roomlet->nQuad + roomlet->nTri; q++)
			{
				const int corners = q < roomlet->nQuad ? 4 : 3;
				HDCoverFace face;
				bool good = true;

				face.corners = corners;
				face.doubleSided = (ptr[corners] >> 15) & 1;

				for (int c = 0; c < 4; c++)
				{
					const int at = c < corners ? ptr[c] : ptr[0];

					if (at < 0 || at >= roomlet->nVtx)
						good = false;

					const ROOMLET_VERTEX& v = vtx[good ? at : 0];

					face.v[c][0] = (double)v.x + info->x;
					face.v[c][1] = (double)v.y + info->y;
					face.v[c][2] = (double)v.z + info->z;
				}

				ptr += corners + 2;

				if (good)
				{
					g_stand_faces_built.push_back(face);
					g_stand_face_roomlet.push_back(i);
					g_stand_face_slot.push_back(q);
					holder.faceCount++;
				}
			}
		}

		if (holder.faceCount)
			g_stand_holders_built.push_back(holder);
	}

	if (!g_stand_holders_built.empty())
		HDRenderSetStandFaces(&g_stand_rooms[0], (int)g_stand_rooms.size(),
			&g_stand_holders_built[0], (int)g_stand_holders_built.size(),
			&g_stand_faces_built[0], (int)g_stand_faces_built.size());
	else
		HDRenderSetStandFaces(0, 0, 0, 0, 0, 0);
}

/* Which rooms the stand art covers, and which of their faces, once the build
 * has said which groups are drawn HD. Per holder that is drawn classic, the
 * renderer's coverage of its faces by each group's tagged triangles
 * (HDRenderStandCoverAt: HDCoverFaces, every face sampled every
 * HD_COVER_STEP units, a sample covered when art facing the way the face
 * does lies within HD_COVER_REACH of it along the face's normal, a face
 * covered when every sample is). Of the HD groups the one that covers the
 * most faces is the carrier - then the most triangles, then the lowest group
 * (HDCoverCarrierBetter) - and the room is drawn as its art where it covers a
 * face: wholly when it covers every face, in part when it covers at least
 * half of them (HDCoverRoomState). A room drawn in part gets its drop flags,
 * the faces InsertRoomlet leaves out; a room drawn as art, wholly or in part,
 * that keeps no classic face Lara can stand on counts as HD for her shadow
 * (HDCoverRoomShadowHD). Then the list of stand ranges the frame answers.
 * Returns the triangles found, for the log; `onFaces[r]` gets each holder's. */
static int BuildStandRanges(std::vector<int>& onFaces)
{
	memset(g_group_stand, 0, sizeof(g_group_stand));
	memset(g_group_stand_covered, 0, sizeof(g_group_stand_covered));
	memset(g_group_stand_part, 0, sizeof(g_group_stand_part));
	memset(g_group_stand_shadow, 0, sizeof(g_group_stand_shadow));
	memset(g_group_stand_faces, 0, sizeof(g_group_stand_faces));
	memset(g_group_stand_faces_covered, 0, sizeof(g_group_stand_faces_covered));
	memset(g_group_stand_floors_kept, 0, sizeof(g_group_stand_floors_kept));

	for (int g = 0; g < HD_MAX_ROOMS; g++)
		g_group_stand_carrier[g] = -1;

	g_stand_pieces.clear();
	g_group_drop.assign((size_t)(g_room_count > 0 ? g_room_count : 0),
		std::vector<unsigned char>());
	g_group_drop_first.assign((size_t)(g_room_count > 0 ? g_room_count : 0),
		std::vector<int>());
	onFaces.assign((size_t)(g_room_count > 0 ? g_room_count : 0), 0);

	const int count = HDRenderStandTriangleCount();
	std::vector<int> tagged((size_t)onFaces.size(), 0);
	int total = 0;

	for (int i = 0; i < count; i++)
	{
		HDCoverTri tri;
		int holder = -1;

		if (!HDRenderStandTriangleAt(i, &tri, &holder) || holder < 0 ||
			holder >= (int)tagged.size() || tri.group < 0 ||
			tri.group >= HD_MAX_ROOMS || !g_group_hd[tri.group])
			continue;

		tagged[(size_t)holder]++;
	}

	for (const HDStandHolder& holder : g_stand_holders_built)
	{
		const int h = holder.room;

		if (h < 0 || h >= (int)tagged.size() || h >= HD_MAX_ROOMS ||
			g_group_hd[h] || !tagged[(size_t)h])
			continue;

		int carrier = -1, carrierFaces = 0, carrierTris = 0, faces = 0;
		const unsigned char* covered = 0;

		for (int c = 0; c < HDRenderStandCovers(); c++)
		{
			HDStandCover cover;

			if (!HDRenderStandCoverAt(c, &cover) || cover.holder != h ||
				cover.group < 0 || cover.group >= HD_MAX_ROOMS ||
				!g_group_hd[cover.group] || cover.faceCount != holder.faceCount ||
				!cover.covered)
				continue;

			faces = cover.faces;

			if (HDCoverCarrierBetter(cover.facesCovered, cover.tris, cover.group,
				carrierFaces, carrierTris, carrier))
			{
				carrier = cover.group;
				carrierFaces = cover.facesCovered;
				carrierTris = cover.tris;
				covered = cover.covered;
			}
		}

		/* Two rooms stay classic whatever the art covers. One whose flags
		 * let the classic path lay a water surface on its faces (ROOM_MIST,
		 * ROOM_REFLECT): BuildWaterSurfaces restores water only for HD
		 * groups, so drawn as the art it would lose its water. And one whose
		 * carrier a flipmap can swap away: the in-holder draw finds the
		 * carrier's room by its group (RoomOfGroup), and while a flip draws
		 * another group there the room would be drawn by nothing. Neither
		 * holds for a room of the game the art covers a face of (TrgProbe
		 * -classicart -perface, `guard=-` on all twelve). */
		bool carrierFlips = false;

		if (carrier >= 0 && carrier < (int)g_stand_rooms.size())
		{
			carrierFlips = g_stand_rooms[(size_t)carrier].twin >= 0;

			for (const HDStandRoom& one : g_stand_rooms)
				carrierFlips = carrierFlips || one.twin == carrier;
		}

		if (carrier >= 0 && (carrierFlips || h >= number_rooms ||
			(room[h].flags & (ROOM_MIST | ROOM_REFLECT))))
			carrier = -1;

		const int state = carrier >= 0 ? HDCoverRoomState(faces, carrierFaces) :
			HD_STAND_ROOM_CLASSIC;
		int floorsKept = 0;

		for (int f = 0; f < holder.faceCount; f++)
		{
			const size_t at = (size_t)(holder.firstFace + f);

			if (at < g_stand_faces_built.size() &&
				!(state != HD_STAND_ROOM_CLASSIC && covered[f]) &&
				HDCoverFaceFloor(g_stand_faces_built[at]))
				floorsKept++;
		}

		/* The faces InsertRoomlet leaves out of a room drawn in part: one
		 * flag per face of every roomlet, each roomlet's run starting after
		 * the last one's quads and triangles. */
		if (state == HD_STAND_ROOM_PART && h < number_rooms)
		{
			const ROOM_INFO* info = &room[h];
			std::vector<int>& first = g_group_drop_first[(size_t)h];
			std::vector<unsigned char>& drop = g_group_drop[(size_t)h];
			int run = 0;

			for (int i = 0; i < info->nRoomlets; i++)
			{
				first.push_back(run);
				run += info->pRoomlets[i].nQuad + info->pRoomlets[i].nTri;
			}

			drop.assign((size_t)run, 0);

			for (int f = 0; f < holder.faceCount; f++)
			{
				const size_t at = (size_t)(holder.firstFace + f);

				if (!covered[f] || at >= g_stand_face_roomlet.size())
					continue;

				const int roomlet = g_stand_face_roomlet[at];
				const int slot = first[(size_t)roomlet] + g_stand_face_slot[at];

				if (roomlet >= 0 && roomlet < (int)first.size() && slot >= 0 &&
					slot < run)
					drop[(size_t)slot] = 1;
			}
		}

		g_group_stand[h] = 1;
		g_group_stand_covered[h] = state == HD_STAND_ROOM_COVERED ? 1 : 0;
		g_group_stand_part[h] = state == HD_STAND_ROOM_PART ? 1 : 0;
		g_group_stand_carrier[h] = (short)(state != HD_STAND_ROOM_CLASSIC ? carrier : -1);
		g_group_stand_shadow[h] = HDCoverRoomShadowHD(false, state, floorsKept,
			holder.faceCount) ? 1 : 0;
		g_group_stand_faces[h] = (short)faces;
		g_group_stand_faces_covered[h] = (short)(state != HD_STAND_ROOM_CLASSIC ?
			carrierFaces : 0);
		g_group_stand_floors_kept[h] = (short)floorsKept;
		onFaces[(size_t)h] = tagged[(size_t)h];
		total += tagged[(size_t)h];
	}

	for (int g = 0; g < g_room_count && g < HD_MAX_ROOMS; g++)
	{
		if (!g_group_hd[g])
			continue;

		for (int j = 0; j < HDRenderGroupStandRanges(g); j++)
		{
			HDStandRange range;

			if (!HDRenderGroupStandRange(g, j, &range) || range.holder < 0 ||
				range.holder >= HD_MAX_ROOMS || !g_group_stand[range.holder])
				continue;

			StandPiece piece;

			piece.group = g;
			piece.index = j;
			piece.holder = range.holder;
			piece.kind = range.kind;
			piece.faces = range.faces;
			g_stand_pieces.push_back(piece);
		}
	}

	g_stand_holders_built.clear();
	g_stand_faces_built.clear();
	g_stand_face_roomlet.clear();
	g_stand_face_slot.clear();
	return total;
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

		// Art the air took out of the open half is no intruding art.
		if (piece.face < 0 && !piece.inner)
			continue;

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

	std::vector<int> seen(g_pair_faces.size(), 0);

	/* The art no face claims in the holder's wall ring, per entry, kept for
	 * the reachart line; the list itself goes at the end of this function. */
	g_pair_band.assign(g_intrusions.size(), 0);
	g_band_unclaimed = 0;

	for (size_t i = 0; i < g_intrusions.size(); i++)
	{
		g_pair_band[i] = HDRenderIntrusionBand((int)i);
		g_band_unclaimed += g_pair_band[i];
	}

	for (size_t i = 0; i < g_intrusions.size(); i++)
	{
		const int hits = HDRenderIntrusionHits((int)i);

		if (!hits)
			continue;

		pairs++;

		if (gLogEnabled)
		{
			/* The faces of this pair and the triangles they claimed. A face
			 * can own two pieces of the pair, one per side of the smaller
			 * box, and is counted once: `seen` holds the pair it was last
			 * counted for. */
			int pairFaces = 0;
			int pairBound = 0;

			for (size_t p = 0; p < g_pieces.size(); p++)
				if (g_pieces[p].intrusion == (int)i && g_pieces[p].face >= 0 &&
					(size_t)g_pieces[p].face < seen.size())
				{
					if (seen[(size_t)g_pieces[p].face] != (int)i + 1)
					{
						seen[(size_t)g_pieces[p].face] = (int)i + 1;
						pairFaces++;
					}

					pairBound += g_pieces[p].triangles;
				}

			Log("HDLOG overlap room=%d holds the art of room=%d - "
				"%d triangles, no portal between them faces=%d bound=%d",
				g_intrusions[i].holder, g_intrusions[i].group, hits,
				pairFaces, pairBound);
		}
	}

	/* A group is windowed when it has any piece; the counts of the line are
	 * the art standing inside another room, the pieces of art the air took
	 * out of the open half left out of them. */
	for (int g = 0; g < HDRenderGroupCount() && g < HD_MAX_ROOMS; g++)
	{
		int here = HDRenderGroupIntrudingTriangles(g);

		if (!here)
			continue;

		g_group_windowed[g] = 1;

		for (int j = 0; j < g_piece_count[g]; j++)
		{
			const HDPairRange& piece = g_pieces[(size_t)(g_piece_first[g] + j)];

			if (piece.face < 0 && !piece.inner)
				here -= piece.triangles;
		}

		groups += here ? 1 : 0;
		tris += here;
	}

	/* `holder=inside` names the box (TR5's own inside of the holder) and
	 * `cull=opaque` the back-face rule (RangeCulls in hd_render.cpp), so a
	 * log says which rule drew the picture it goes with. `band_unclaimed`
	 * at the end is the art that stands inside another room, is claimed by
	 * no face and lies outside the box one sector smaller: the build hands
	 * it to the open half, and it is drawn over the whole screen on every
	 * frame, never asked. TrgProbe -roomspace prints the same number. */
	/* The art in the air of a room its own room has no link with (HDRoomAir):
	 *
	 *   air_ms      reading the rooms' air and asking it of the triangles,
	 *               milliseconds over the level build
	 *   air_tris    the triangles standing in such air, each counted once,
	 *               all three passes
	 *   air_groups  the groups with at least one of them
	 *   air_pairs   the (group, host) pairs with at least one, one HDLOG
	 *               unlinked line each
	 *   air_boxed   those of air_tris an intrusion entry's box holds too:
	 *               the art drawn only inside its own room's window, but
	 *               what a stand range or a static's box takes
	 *   air         tomb5.hd_air as the level took it: 1 the air asked,
	 *               2 the hulls' too (HDLOG hull below), 0 not asked,
	 *               every air_ field 0 */
	int airTris = 0;
	int airGroups = 0;
	int airBoxed = 0;
	const int airPairs = HDRenderAirPairs();

	for (int g = 0; g < HDRenderGroupCount() && g < HD_MAX_ROOMS; g++)
	{
		const int here = HDRenderGroupAirTriangles(g);

		airTris += here;
		airGroups += here ? 1 : 0;
		airBoxed += HDRenderGroupAirBoxed(g);
	}

	Log("HDLOG level=%s overlap pairs=%d intruding tris=%d in %d groups "
		"holder=inside cull=opaque faces=%d bound=%d unbound=%d "
		"band_unclaimed=%d air_ms=%d air_tris=%d air_groups=%d air_pairs=%d "
		"air_boxed=%d air=%d covered=%d",
		g_stem, pairs, tris, groups, faces, bound, unbound, g_band_unclaimed,
		(int)(g_air_build_ms + HDRenderAirMicroseconds() / 1000.0 + 0.5),
		airTris, airGroups, airPairs, airBoxed, g_air_on,
		HDRenderCoveredFaces());

	/* HDLOG unlinked: one line per (group, host) pair with art in the air of
	 * a room its own room has no link with - `group` the art's room,
	 * `host` the room whose air it stands in, `tris` how many of its
	 * triangles do. TrgProbe -roomspace prints the same lines. Behind -log. */
	if (gLogEnabled)
		for (int i = 0; i < airPairs; i++)
		{
			int group = 0, host = 0, here = 0;

			if (HDRenderAirPair(i, &group, &host, &here))
				Log("HDLOG unlinked level=%s group=%d host=%d tris=%d",
					g_stem, group, host, here);
		}

	/* HDLOG hull, with tomb5.hd_air 1 or 2: one line per group whose art
	 * in unlinked air no entry's box holds - `air_tris` those triangles,
	 * `host_art` those standing on their host's own art, `hull` 1 when
	 * that is more than half, and then the art is drawn pushed back in
	 * depth and, with hd_air 2, held to the group's own window
	 * (MarkAirHulls in hd_render.cpp); `behind` the triangles drawn pushed
	 * back, all three passes. Behind -log. */
	if (gLogEnabled && g_air_on)
		for (int g = 0; g < HDRenderGroupCount() && g < HD_MAX_ROOMS; g++)
		{
			int air = 0, covered = 0;
			const bool hull = HDRenderGroupHull(g, &air, &covered);

			if (air)
				Log("HDLOG hull level=%s group=%d air_tris=%d host_art=%d "
					"hull=%d behind=%d", g_stem, g, air, covered,
					hull ? 1 : 0, HDRenderGroupHullBehind(g));
		}

	/* HDLOG covered, with the air on: one line per pair face found inside
	 * a larger face of its holder (HDRenderCoveredFace) - `face` its index
	 * in the pair-face list, `group` its room, `holder` the larger face's,
	 * `area` its area in square units, then per room the art over it in
	 * percent of that area: `own` the face's room's, `held` the holder's,
	 * each as on/n/back (within a unit of the plane, all corners on the
	 * side its normal points to, all on the other), `airs` in which rooms'
	 * air the point 128 units off its centre stands (1 and 2 its room's on
	 * the normal's side and the other, 4 and 8 the holder's), `centre` and
	 * `normal` (in thousandths) where it is; `behind` 1 when its art is
	 * drawn pushed back. Behind -log. */
	if (gLogEnabled && g_air_on)
		for (int i = 0; ; i++)
		{
			int face = 0, group = 0, holder = 0, behind = 0, airs = 0;
			float area = 0.0f, own[3], held[3];
			float centre[3], normal[3];

			if (!HDRenderCoveredFace(i, &face, &group, &holder, &area, own,
				held, &airs, centre, normal, &behind))
				break;

			const float to = area > 0.0f ? 100.0f / area : 0.0f;

			Log("HDLOG covered level=%s face=%d group=%d holder=%d area=%d "
				"own=%d/%d/%d held=%d/%d/%d airs=%d centre=%d,%d,%d "
				"normal=%d,%d,%d behind=%d", g_stem, face, group,
				holder, (int)(area + 0.5f), (int)(own[0] * to + 0.5f),
				(int)(own[1] * to + 0.5f), (int)(own[2] * to + 0.5f),
				(int)(held[0] * to + 0.5f), (int)(held[1] * to + 0.5f),
				(int)(held[2] * to + 0.5f), airs, (int)floorf(centre[0] + 0.5f),
				(int)floorf(centre[1] + 0.5f), (int)floorf(centre[2] + 0.5f),
				(int)floorf(normal[0] * 1000.0f + 0.5f),
				(int)floorf(normal[1] * 1000.0f + 0.5f),
				(int)floorf(normal[2] * 1000.0f + 0.5f), behind);
		}

	// The lists were only borrowed for the length of the build.
	HDRenderSetIntrusions(0, 0);
	HDRenderSetPairFaces(0, 0);
	HDRenderSetRoomAir(0, 0, 0);
	g_intrusions.clear();
	g_room_air.clear();
	g_room_air_sectors.clear();
	g_room_links.clear();
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

/* The counts of the two summary lines, kept from BuildStaticTables for the
 * second of them, which the level build writes after the water. */
static int g_statics_art = 0;
static int g_statics_by_group = 0;
static int g_statics_classic = 0;
static int g_statics_piece = 0;

static void BuildStaticTables()
{
	g_static_art.clear();
	g_static_first.assign((size_t)number_rooms + 1, 0);
	g_destruct_pairs.clear();
	g_statics_art = 0;
	g_statics_by_group = 0;
	g_statics_classic = 0;
	g_statics_piece = 0;

	int total = 0;

	for (int r = 0; r < number_rooms; r++)
	{
		g_static_first[(size_t)r] = total;
		total += room[r].num_meshes;
	}

	g_static_first[(size_t)number_rooms] = total;

	HDStaticEntry none;

	none.answer = HD_STATIC_CLASSIC;
	none.piece = -1;
	none.pieceGroup = -1;

	for (int k = 0; k < HD_STATIC_MAX_GROUPS; k++)
		none.holder[k] = -1;

	g_static_art.assign((size_t)total, none);

	/* What HDRenderStaticArt is told about every group: drawn HD or not, the
	 * state of a room drawn classic (BuildStandRanges) and its carrier, and
	 * the room's origin. Group r is room r: this runs at level build, before
	 * any flip. */
	const int groups = number_rooms < HD_MAX_ROOMS ? number_rooms : HD_MAX_ROOMS;
	std::vector<HDStaticGroup> asked((size_t)groups);

	for (int g = 0; g < groups; g++)
	{
		HDStaticGroup& one = asked[(size_t)g];

		one.hd = g_group_hd[g];
		one.state = (unsigned char)(g_group_hd[g] ? HD_STAND_ROOM_CLASSIC :
			g_group_stand_covered[g] ? HD_STAND_ROOM_COVERED :
			g_group_stand_part[g] ? HD_STAND_ROOM_PART : HD_STAND_ROOM_CLASSIC);
		one.carrier = one.state != HD_STAND_ROOM_CLASSIC ? g_group_stand_carrier[g] : -1;
		one.originX = (float)room[g].x;
		one.originZ = (float)room[g].z;
	}

	int orphans = 0;
	int paired = 0;
	int paired_other = 0;
	int shatterable = 0;
	int classic_room_art = 0;
	int classic_room_group = 0;
	const int pieces = HDRenderDestructibleCount();
	std::vector<float> verts;
	/* The answers first, every static of the level, then the rule over a
	 * room's pieces (HDRenderStaticPiecesWhole), then the table and the log
	 * below. */
	std::vector<HDStaticArt> arts((size_t)total);
	std::vector<int> artRoom((size_t)total, -1);

	for (int r = 0; r < groups; r++)
	{
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
			 * 768 of the upper room's own art, which is the wall behind it.
			 * The crown is modelled in the art of the trunk's room, and that
			 * art holds it (HDRenderStaticArt asks every group); the same
			 * level's room 162 crown is in its own room's art, 313 of 768.
			 *
			 * So the question is how much, against the static's own box.
			 * Measured over the fifteen files (RoomProbe -statictable), the
			 * 2,776 statics of rooms drawn HD fall into 79 at nothing, 20
			 * between nothing and a twentieth (the highest 1 in 21) and 2,677
			 * at a twentieth or more (the lowest exactly 1 in 20) - the
			 * statics the own art does not hold are mostly the empty boxes.
			 * The other sources are asked at the static's own vertices,
			 * which the floor or the wall of another place does not reach
			 * (hd_cover.h, over HD_STATIC_SHARE). */
			/* The static's own mesh where it stands, room local - its
			 * vertices, which the other sources are asked about. */
			const STATIC_INFO* sinfo = &static_objects[mesh->static_number];
			const MESH_DATA* data = (const MESH_DATA*)meshes[sinfo->mesh_number];
			const int nv = data && data->aVtx ? data->nVerts : 0;
			const float c = (float)phd_cos(mesh->y_rot) * (1.0f / 16384.0f);
			const float sn = (float)phd_sin(mesh->y_rot) * (1.0f / 16384.0f);

			verts.resize((size_t)(nv > 0 ? nv : 1) * 3);

			for (int v = 0; v < nv; v++)
			{
				const ACMESHVERTEX& at = data->aVtx[v];

				verts[(size_t)v * 3 + 0] = (float)(mesh->x - info->x) + at.x * c + at.z * sn;
				verts[(size_t)v * 3 + 1] = (float)(mesh->y - info->y) + at.y;
				verts[(size_t)v * 3 + 2] = (float)(mesh->z - info->z) + at.z * c - at.x * sn;
			}

			const int place = g_static_first[(size_t)r] + i;

			HDRenderStaticArt(r, mins, maxs, &verts[0], nv, &asked[0], groups,
				place < (int)g_static_places.size() ? place : -1, &arts[(size_t)place]);
			artRoom[(size_t)place] = r;
		}
	}

	if (total > 0)
		HDRenderStaticPiecesWhole(&artRoom[0], &arts[0], total);

	for (int r = 0; r < groups; r++)
	{
		const ROOM_INFO* info = &room[r];
		const bool hd = g_group_hd[r] != 0;

		for (int i = 0; i < info->num_meshes; i++)
		{
			const MESH_INFO* mesh = &info->mesh[i];

			if (mesh->static_number < 0 || mesh->static_number >= 70)
				continue;

			float mins[3], maxs[3];

			StaticBox(mesh, info, mins, maxs);

			const int place = g_static_first[(size_t)r] + i;
			const HDStaticArt& art = arts[(size_t)place];
			HDStaticEntry& entry = g_static_art[(size_t)place];
			int held = 0;

			entry.answer = art.answer;
			entry.piece = art.piece;
			entry.pieceGroup = art.pieceGroup;

			if (art.piece >= 0)
				g_statics_piece++;

			for (int k = 0; k < art.groupCount && art.answer == HD_STATIC_GROUPS; k++)
				if (art.holds[k])
					entry.holder[held++] = (short)art.group[k];

			if (art.answer == HD_STATIC_ART)
				g_statics_art++;
			else if (art.answer == HD_STATIC_GROUPS)
				g_statics_by_group++;
			else
				g_statics_classic++;

			if (hd && art.answer == HD_STATIC_CLASSIC)
				orphans++;

			if (!hd && art.answer == HD_STATIC_ART)
				classic_room_art++;
			else if (!hd && art.answer == HD_STATIC_GROUPS)
				classic_room_group++;

			/* The other groups' art at its vertices, the most first, a
			 * group that holds the static marked with a star. */
			char named[160];
			int at = 0;

			named[0] = 0;

			// At most HD_STATIC_MAX_GROUPS entries of at most 16 characters.
			for (int k = 0; k < art.groupCount && at < (int)sizeof(named) - 20; k++)
				at += snprintf(named + at, sizeof(named) - (size_t)at, "%s%d:%d%s",
					k ? "," : "", art.group[k], art.groupVerts[k],
					art.holds[k] ? "*" : "");

			const char* said = art.answer == HD_STATIC_ART ? "art" :
				art.answer == HD_STATIC_GROUPS ? "groups" : "classic";

			/* The piece: its group and triangles, the static's vertices its
			 * cells hold, the triangles of the runs it reaches into that go
			 * on past HD_STATIC_PIECE_OPEN and the statics of its room held
			 * by that group with no piece; `-` and the three counts for a
			 * static held by groups with no piece, `-` for every other
			 * answer. */
			char piece[80];

			if (art.piece >= 0)
				snprintf(piece, sizeof(piece), "%d:%d held=%d open=%d others=%d",
					art.pieceGroup, art.pieceTris, art.pieceHeld, art.pieceOpen,
					art.pieceOthers);
			else if (art.answer == HD_STATIC_GROUPS)
				snprintf(piece, sizeof(piece), "- held=%d open=%d others=%d",
					art.pieceHeld, art.pieceOpen, art.pieceOthers);
			else
				snprintf(piece, sizeof(piece), "-");

			if (gLogEnabled && hd && !HDStaticCellsHold(art.own, art.boxCells))
				Log("HDLOG static room=%d mesh=%d slot=%d classic "
					"cells=%d of %d box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f "
					"verts=%d groups=%s answer=%s piece=%s",
					r, i, (int)mesh->static_number, art.own, art.boxCells,
					mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2],
					art.verts, at ? named : "-", said, piece);
			else if (gLogEnabled && !hd)
				Log("HDLOG static room=%d mesh=%d slot=%d classic_room "
					"box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f verts=%d carrier=%d "
					"sure=%d groups=%s answer=%s piece=%s",
					r, i, (int)mesh->static_number, mins[0], maxs[0], mins[1],
					maxs[1], mins[2], maxs[2], art.verts, art.carrier, art.sure,
					at ? named : "-", said, piece);

			/* 2. And if the engine can destroy it, which piece of the art is
			 * it? Slots 50 to 59 are the ones the engine takes out of the
			 * classic list and the savegame keeps (hd_cover.h,
			 * HDStaticShatterSlot). Every piece of a group drawn HD whose
			 * box overlaps the static's by more than the edge, or whose
			 * plane a flat static stands at (HDStaticPiecePaired), in any
			 * group but the room's flipmap twin (HDStaticPieceInState),
			 * whatever the static's answer. */
			if (!HDStaticShatterSlot(mesh->static_number))
				continue;

			shatterable++;

			/* The twin: the main room names it in flipped_room, and the
			 * alternate is the room whose flipped_room names it. */
			int twin = info->flipped_room;

			for (int m = 0; m < number_rooms && twin < 0; m++)
				if (room[m].flipped_room == r)
					twin = m;

			for (int p = 0; p < pieces; p++)
			{
				int group = -1;
				float pmins[3], pmaxs[3];

				if (!HDRenderDestructibleInfo(p, &group, 0, pmins, pmaxs) ||
					group < 0 || group >= groups || !g_group_hd[group] ||
					!HDStaticPieceInState(twin, group))
					continue;

				if (!HDStaticPiecePaired(mins, maxs, (float)(info->x - room[group].x),
					(float)(info->z - room[group].z), pmins, pmaxs))
					continue;

				HDStaticPair pair;

				pair.room = (short)r;
				pair.mesh = (short)i;
				pair.piece = p;
				g_destruct_pairs.push_back(pair);
				paired++;

				if (group != r)
					paired_other++;

				if (gLogEnabled)
					Log("HDLOG static room=%d mesh=%d slot=%d destructible "
						"piece=%d group=%d", r, i, (int)mesh->static_number, p,
						group);
			}
		}
	}

	Log("HDLOG level=%s statics classic=%d of %d, destructible pieces=%d "
		"shatterable=%d paired=%d classic_room_art=%d classic_room_group=%d "
		"paired_other=%d",
		g_stem, orphans, total, pieces, shatterable, paired, classic_room_art,
		classic_room_group, paired_other);

	// The pieces' index buffer; the places were borrowed until now.
	HDRenderStaticPiecesUpload();
	HDRenderSetStaticPlaces(0, 0);
	std::vector<HDStaticPlace>().swap(g_static_places);
}

/* Indexed through g_room_group: a flipmap swaps two ROOM_INFO records, their
 * MESH_INFO lists with them, and the group number is the room whose list now
 * stands at this index - the entry, its holders and its piece follow the
 * flip. A group the frame lists is one a listed room is drawn by now
 * (GroupOfRoom). At most HD_STATIC_MAX_GROUPS groups per static and one walk
 * of the list for each static that names any; a frame that lists none of
 * them draws the static's piece where it has one (NoteStaticPiece) and the
 * classic static otherwise. */
static int GroupOfRoom(int roomIndex);
static int RoomOfGroup(int group);

/* A static's piece for this frame (HDStaticDrawSource): drawn inside the
 * window of the static's room - which HDFrameRoom (a room drawn HD) or
 * StandHolderRoom (a room drawn classic that holds stand art) took before
 * DrawStaticObjects asks - with that room's matrix moved by the difference of
 * the origins of its group and the piece's, as DecideStand moves a stand
 * range, and the piece group's own lighting (HDFrameDraw). False, and the
 * classic static stays, where the room has no such entry, the piece's group
 * is no room's now or the frame has no entry left. */
static bool NoteStaticPiece(int roomIndex, int group, const HDStaticEntry& entry)
{
	if (g_piece_draw_count >= HD_MAX_PIECE_DRAW || !HDRenderStaticPieceReady(entry.piece) ||
		entry.pieceGroup < 0 || (size_t)entry.pieceGroup >= g_stand_rooms.size() ||
		(size_t)group >= g_stand_rooms.size())
		return false;

	PieceDraw& draw = g_piece_draw[g_piece_draw_count];
	const D3DMATRIX* world = 0;

	for (int i = 0; i < g_frame_count && !world; i++)
		if (g_frame[i].room == roomIndex)
		{
			world = &g_frame[i].world;
			draw.x0 = g_frame[i].x0;
			draw.y0 = g_frame[i].y0;
			draw.x1 = g_frame[i].x1;
			draw.y1 = g_frame[i].y1;
			draw.full = g_frame[i].full;
		}

	for (int i = 0; i < g_stand_frame_count && !world; i++)
		if (g_stand_frame[i].room == roomIndex)
		{
			world = &g_stand_frame[i].world;
			draw.x0 = g_stand_frame[i].x0;
			draw.y0 = g_stand_frame[i].y0;
			draw.x1 = g_stand_frame[i].x1;
			draw.y1 = g_stand_frame[i].y1;
			draw.full = g_stand_frame[i].full;
		}

	const int roomOfArt = world ? RoomOfGroup(entry.pieceGroup) : -1;

	if (roomOfArt < 0)
		return false;

	const D3DMATRIX& m = *world;
	float off[3];

	for (int k = 0; k < 3; k++)
		off[k] = g_stand_rooms[(size_t)entry.pieceGroup].origin[k] -
			g_stand_rooms[(size_t)group].origin[k];

	draw.piece = entry.piece;
	draw.group = entry.pieceGroup;
	draw.roomOfArt = roomOfArt;
	draw.world = m;
	draw.world._41 = off[0] * m._11 + off[1] * m._21 + off[2] * m._31 + m._41;
	draw.world._42 = off[0] * m._12 + off[1] * m._22 + off[2] * m._32 + m._42;
	draw.world._43 = off[0] * m._13 + off[1] * m._23 + off[2] * m._33 + m._43;
	draw.world._44 = off[0] * m._14 + off[1] * m._24 + off[2] * m._34 + m._44;
	g_piece_draw_count++;

	// A frame that lists no HD room draws its HD part for the piece too.
	HDFrameCaptureProjection();
	return true;
}

bool HDStaticDrawnClassic(int roomIndex, int meshIndex, const short* listed,
	int listedCount)
{
	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return true;

	const int group = g_room_group[roomIndex];

	if (group < 0 || group + 1 >= (int)g_static_first.size())
		return true;

	const int at = g_static_first[(size_t)group] + meshIndex;

	if (meshIndex < 0 || at >= g_static_first[(size_t)group + 1])
		return true;

	const HDStaticEntry& entry = g_static_art[(size_t)at];
	bool holderListed = false;

	for (int i = 0; entry.answer == HD_STATIC_GROUPS && listed && i < listedCount &&
		!holderListed; i++)
	{
		const int drawnBy = GroupOfRoom(listed[i]);

		for (int k = 0; k < HD_STATIC_MAX_GROUPS && entry.holder[k] >= 0; k++)
			if (entry.holder[k] == drawnBy)
				holderListed = true;
	}

	const bool pieceDrawn = entry.answer == HD_STATIC_GROUPS && !holderListed &&
		entry.piece >= 0 && NoteStaticPiece(roomIndex, group, entry);

	return HDStaticDrawSource(entry.answer, holderListed, pieceDrawn) ==
		HD_STATIC_FROM_CLASSIC && !VisStaticLeftToArt(group, meshIndex, listed,
		listedCount);
}

/* Once per frame, before the rooms are drawn: a piece whose static the engine
 * has taken out of the classic list goes with it. At most 69 entries in the
 * whole game, so this is a walk and not a lookup. The static is read where
 * its list stands now (HDStaticListRoom): a pair names the room the list was
 * built in, and after a flip that list stands in the twin's place - ANDREA2's
 * broken window is the static of room 65's list, cleared by the controller
 * while room 132 holds it, and a save restores it there after its flips. */
static void UpdateDestructibles()
{
	for (size_t i = 0; i < g_destruct_pairs.size(); i++)
	{
		const HDStaticPair& pair = g_destruct_pairs[i];
		const int at = HDStaticListRoom(g_room_group, g_room_count, pair.room);

		if (at < 0)
			continue;

		const MESH_INFO* mesh = &room[at].mesh[pair.mesh];

		HDRenderDestructibleHide(pair.piece, (mesh->Flags & 1) == 0);
	}
}

/* A fog bulb the level puts in a water room a sheet lies over
 * (HDRenderWaterUnderOn): ANDREA1 has seven, ANDREA2 sixteen, no other
 * level any (the classic .TRC files' room data). It is the classic path's
 * stand-in for the murk of that water, and with sheets the water's own look
 * takes its place on both sides of the surface - under the water the under
 * look (HDFrameDraw drops every bulb then), over it the sheet's extinction
 * along the path through the water. Handed to the frame over the water, its
 * haze lay on whatever the eye ray reached inside its sphere and the sheet
 * then passed it on as the frame had it: a body swimming just under the
 * surface - a few units of water in front of it - kept the haze whole, pale
 * against the dark water round it (ANDREA2's canal: Lara's excess over the
 * water 13, 18, 15, the canal bulbs' own colour 207, 255, 223 in the same
 * ratio). Found by the bulb's place: LevelFogBulbs and ActiveFogBulbs copy
 * it from the room's list unchanged (drawroom.cpp). */
static bool BulbUnderSheet(const FOGBULB_STRUCT& b)
{
	if (!HDRenderWaterSheetsBuilt())
		return false;

	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO& ri = room[r];

		if (!(ri.flags & ROOM_UNDERWATER) || ri.nFogBulbs <= 0 ||
			!ri.fogbulb || !HDRenderWaterUnderOn(r))
			continue;

		for (int k = 0; k < ri.nFogBulbs; k++)
			if (ri.fogbulb[k].px == b.world.x &&
				ri.fogbulb[k].py == b.world.y &&
				ri.fogbulb[k].pz == b.world.z)
				return true;
	}

	return false;
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
	out.motion = tomb5.hd_motion ? 1 : 0;
	out.reflect = tomb5.hd_reflect ? 1 : 0;

	/* And the drawn state's time. InterpFrameAlpha is the factor this
	 * frame's state was really blended at: 1 at fps_mode 0, on a frame
	 * drawn live, on a camera cut and outside DrawPhaseGame
	 * (game\interpolate.cpp, InterpBeginFrame), so there it is g_clock
	 * itself.
	 *
	 * Every motion of the HD path runs on it - the water's waves, foam and
	 * rings, the wind, the caustic, the scroll and the flipbooks - and not on
	 * the tick clock: on the frames between two ticks Lara and the items are
	 * drawn at the blended pose, and a motion on the tick clock would stand
	 * still for those frames and jump at the next tick, a thirty-hertz step
	 * at sixty frames a second and more. With the drawn time it moves on
	 * every frame, at the pose's own time. At fps_mode 0 it is g_clock. */
	g_drawn_alpha = InterpFrameAlpha();
	g_drawn_clock = HDBookClock(g_clock, g_drawn_alpha);
	out.clock = (float)g_drawn_clock;
	out.bookClock = g_drawn_clock;
	g_book_presents++;

	int offered = 0;

	for (int i = 0; i < NumActiveFogBulbs && i < 64; i++)
	{
		const FOGBULB_STRUCT& in = ActiveFogBulbs[i];

		if (!in.visible || BulbUnderSheet(in))
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
	// The build gate. With hd_build 0 nothing below this line ever runs, no
	// Remaster is looked for, no file is opened, and the engine is the
	// classic one, byte for byte. hd_enable, the HD textures row, is the
	// draw and not asked here: the data are built whatever it says.
	if (!tomb5.hd_build)
		return false;

	return HDInstallRoot()[0] != 0;
}

bool HDLevelBuilt()
{
	return g_level_active;
}

/* The HD data kept across a load of the same level (hd_levelkey.h).
 *
 * LoadLevel names the level it is about to read (HDLevelKeyNext) before
 * FreeLevel, and HDLevelUnload holds that key against the key of the data in
 * memory: when the two are equal it keeps them, HDLevelLoad skips the .TRG
 * scan and HDLevelBuildHD the build (HDLevelBuildResident). The classic level
 * read again is the one the data were made from, so the room art, its pages,
 * the room and static tables and the models are what a build would make
 * again; what ties into the classic level or changes in play is made again.
 * With hd_build 0 no key is stored and nothing is ever kept. */
static HDLevelKey g_key_next;		// the level LoadLevel is about to read
static HDLevelKey g_key_loading;	// the level being loaded or drawn now
static HDLevelKey g_key_built;		// the level the HD data in memory are of
static bool g_keep = false;		// HDLevelUnload kept them for this load
// The kept level's scan counts, for the texture budget (AutoBudgetBytes).
static int g_kept_vertices = 0;
static int g_kept_indices = 0;

/* A level the load does not keep: its .TRG read and its first pages decoded
 * ahead, on a thread of their own from the moment LoadLevel names the level
 * (HDLevelKeyNext) and beside the classic level's read - HDScanReadTrg, then
 * HDPageCacheAhead, which decodes on threads of its own. The scan waits for
 * the read (HDLevelLoad) and takes the parse; the build's Create takes the
 * pages. Joined at the latest when the process ends: a std::thread still
 * joinable when it goes would end the process. */
struct AheadRead
{
	std::thread t;

	~AheadRead()
	{
		if (t.joinable())
			t.join();
	}
};

static double g_ahead_read_ms = 0.0;	// the read's own time
static AheadRead g_ahead_read;		// after what it writes

static void AheadReadWait()
{
	if (g_ahead_read.t.joinable())
		g_ahead_read.t.join();
}

/* What was read and decoded ahead and no build takes: the parse (RICH2
 * about 45 MiB) and up to one batch of decoded pages (96 MiB), given back
 * by a build that stops before HDRenderLevelBegin takes them. */
static void AheadDrop()
{
	AheadReadWait();
	HDScanDropTrg();
	HDPageCacheAheadDrop();
}

// One file's size and last write time, both -1 when it is not there.
static void FileStampOf(const char* path, HDLevelFileStamp* out)
{
	WIN32_FILE_ATTRIBUTE_DATA a;

	out->size = -1;
	out->written = -1;

	if (!path || !*path || !GetFileAttributesExA(path, GetFileExInfoStandard,
		&a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return;

	out->size = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	out->written = ((long long)a.ftLastWriteTime.dwHighDateTime << 32) |
		a.ftLastWriteTime.dwLowDateTime;
}

/* The key stamped before the loading thread starts (HDLevelKeyEarly) and
 * the file it is of: LoadLevel's HDLevelKeyNext takes it for that file, so
 * the loading screen's decision and the keep stand on one key. */
static HDLevelKey g_key_early;
static char g_key_early_wad[HD_LEVEL_KEY_PATH];

static int KeyStampOf(const char* wadName, HDLevelKey& k);

void HDLevelKeyNext(const char* wadName)
{
	HDLevelKey& k = g_key_next;

	memset(&k, 0, sizeof(k));

	if (!wadName || !*wadName || !HDEnabled())
		return;

	// The key S_LoadLevelFile stamped for this file, else a stamp now.
	const int early = g_key_early.valid &&
		!strcmp(g_key_early_wad, wadName);

	if (early)
		k = g_key_early;

	memset(&g_key_early, 0, sizeof(g_key_early));
	g_key_early_wad[0] = 0;

	if (!early && !KeyStampOf(wadName, k))
		return;

	/* Whatever of the level before no build took goes first; a level the
	 * load keeps needs nothing read (HDLevelUnload decides the same from
	 * the same keys). */
	AheadReadWait();
	HDScanDropTrg();
	HDPageCacheAheadDrop();

	if (!HDLevelKeepRefused(&g_key_built, &k, g_redress_count))
		return;

	const std::string root = HDInstallRoot();
	const std::string stem = k.name;
	const int threads = DecodeThreads();

	g_ahead_read_ms = 0.0;
	g_ahead_read.t = std::thread([root, stem, threads]()
	{
		const double started = LoadClockMs();
		const bool read = HDScanReadTrg(root.c_str(), stem.c_str());

		g_ahead_read_ms = LoadClockMs() - started;

		if (!read)
			return;

		std::vector<int> pages((size_t)HDScanKeptPageCount());

		for (int i = 0; i < (int)pages.size(); i++)
			pages[(size_t)i] = HDScanKeptPage(i);

		HDPageCacheAhead(root.c_str(), pages.empty() ? 0 : &pages[0],
			(int)pages.size(), threads);
	});
}

/* HDLevelUnload's decision, asked before it from the same keys: nothing
 * between LoadLevel's HDLevelKeyNext and FreeLevel's HDLevelUnload changes
 * them, so both give the same answer. */
bool HDLevelKeepsNext()
{
	return !HDLevelKeepRefused(&g_key_built, &g_key_next, g_redress_count);
}

/* The level's key into `k`, 0 when the file name gives no level. Every
 * path is the one its reader opens: the .TRC as LoadLevel's FileOpen,
 * the sidecar as HDMarkersLoad finds it beside it, the .TRG as
 * HDRenderLevelBegin reads it, the two text files in the working folder as
 * their readers do. */
static int KeyStampOf(const char* wadName, HDLevelKey& k)
{
	memset(&k, 0, sizeof(k));
	LevelStem(wadName, k.name, sizeof(k.name));

	if (!k.name[0])
		return 0;

	std::string sidecar = wadName;
	const size_t slash = sidecar.find_last_of("/\\");
	const size_t dot = sidecar.find_last_of('.');

	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		sidecar.erase(dot);

	std::string trg = HDInstallRoot();

	while (!trg.empty() && (trg.back() == '\\' || trg.back() == '/'))
		trg.pop_back();

	trg += std::string("\\DATA\\") + k.name + ".TRG";

	k.level = (int)gfLevelId;
	FileStampOf(wadName, &k.trc);
	FileStampOf((sidecar + ".hdm").c_str(), &k.hdm);
	FileStampOf(trg.c_str(), &k.trg);
	FileStampOf("hd_gloss.txt", &k.glossFile);
	FileStampOf("hd_water.txt", &k.waterFile);
	snprintf(k.install, sizeof(k.install), "%s", HDInstallRoot());
	snprintf(k.installTr3, sizeof(k.installTr3), "%s", HDInstallRootTr3());
	k.air = (int)tomb5.hd_air;
	k.water = (int)tomb5.hd_water;
	k.objects = (int)tomb5.hd_objects;
	k.budgetMb = (long)tomb5.hd_texture_budget_mb;
	snprintf(k.format, sizeof(k.format), "%s", tomb5_hd_texture_format);
	k.marble = HDRenderMarbleOn();

	if (!REG_PeekString("tomb5", "hd_outfit", k.outfit, sizeof(k.outfit)))
		k.outfit[0] = 0;

	k.valid = 1;
	return 1;
}

/* The coming level's key, stamped now in S_LoadLevelFile, and what the
 * load will do with it: the comparison is HDLevelKeepRefused on the key
 * LoadLevel's HDLevelKeepsNext and HDLevelUnload will hold. */
void HDLevelKeyEarly(const char* wadName, HDLevelNextLoad* out)
{
	memset(out, 0, sizeof(*out));
	memset(&g_key_early, 0, sizeof(g_key_early));
	g_key_early_wad[0] = 0;

	if (!wadName || !*wadName)
		return;

	if (!HDEnabled())
		return;

	if (!KeyStampOf(wadName, g_key_early))
		return;

	snprintf(g_key_early_wad, sizeof(g_key_early_wad), "%s", wadName);
	out->gate = 1;
	out->art = g_key_early.trg.size >= 0;
	out->kept = !HDLevelKeepRefused(&g_key_built, &g_key_early,
		g_redress_count);
}

void HDLevelLoad(const char* wadName)
{
	static unsigned char loaded[NUMBER_OBJECTS];
	HDScanStats stats;
	const char* why;

	g_stem[0] = 0;
	g_scan_vertices = 0;
	g_scan_indices = 0;
	g_scan_ms = 0.0;

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
	const double loadlog_markers = LoadClockMs();
	const bool markers = HDMarkersLoad(wadName);
	Log("LOADLOG level=%s step=markers ms=%.1f", g_log_stem,
		LoadClockMs() - loadlog_markers);

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

	/* The HD data of this very level are still in memory (HDLevelUnload
	 * kept them): the scan would read the same .TRG and bind the same slots
	 * to measure what is already built. Its counts are the kept level's. */
	if (g_keep)
	{
		g_scan_vertices = g_kept_vertices;
		g_scan_indices = g_kept_indices;
		Log("LOADLOG level=%s step=scan ms=0.0 skipped=1", g_stem);
		return;
	}

	for (int i = 0; i < NUMBER_OBJECTS; i++)
		loaded[i] = objects[i].loaded ? 1 : 0;

	/* The .TRG read ahead of the scan (HDLevelKeyNext): waited for here, and
	 * the scan takes its parse as its own read. `ms` is the read's own time,
	 * which ran beside the classic level's steps; `waited` what of it this
	 * thread still had to wait for. */
	const double ahead_at = LoadClockMs();

	AheadReadWait();
	Log("LOADLOG level=%s step=trg_ahead ms=%.1f waited=%.1f", g_stem,
		g_ahead_read_ms, LoadClockMs() - ahead_at);

	// measureBc7 is off: HDLevelBuild decodes every page for real a moment
	// later and reports what that cost, so decoding them here too would only
	// make the level load twice as slow to measure the same thing.
	const double scan_started = LoadClockMs();

	/* The parse is kept for the build, which takes it instead of reading
	 * the file a second time (HDRenderLevelBegin). */
	HDScanKeepTrg(true);

	if (!HDScanLevel(HDInstallRoot(), g_stem, number_rooms, loaded, NUMBER_OBJECTS,
		false, 1, &stats, &why))
	{
		Log("HDLOG level=%s FAILED %s", g_stem, why);
		g_stem[0] = 0;
		return;
	}

	g_scan_ms = LoadClockMs() - scan_started;

	// What the buffers of this level will cost, for the budget - see
	// AutoBudgetBytes. This is the only moment they are known in advance.
	g_scan_vertices = stats.vertices;
	g_scan_indices = stats.indices;

	Log("HDLOG level=%s trg=v%d groups=%d rooms=%d sky=%d empty=%d pages=%d "
		"verts=%d idx=%d ranges=%d changes=%d mixed=%d map=%d slots=%d "
		"trm_bound=%d trm_missing=%d trm_dummy=%d trm_lara=%d heads=%d "
		"scan_ms=%d",
		g_stem, stats.trgVersion, stats.groups, stats.rooms, stats.skyGroups,
		stats.emptyGroups, stats.pages, stats.vertices, stats.indices,
		stats.ranges, stats.pageChanges, stats.mixedPageTriangles,
		stats.mapEntries, stats.slotsLoaded, stats.trmBound, stats.trmMissing,
		stats.trmDummy, stats.trmLara, stats.headsBound,
		(int)(g_scan_ms + 0.5));
	Log("LOADLOG level=%s step=scan ms=%.1f", g_stem, g_scan_ms);

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

	/* Behind the gate: with hd_build 0 there is no bare set, no wardrobe
	 * and nothing to say about either. */
	if (HDEnabled())
		HDOutfitMenuFollowFeet();
}

/* The build of a level whose HD data HDLevelUnload kept for this load
 * (hd_levelkey.h). Kept as built: the room art and its pages, the room
 * tables (g_group_hd, the stand rule), the static tables and the models -
 * made of the level's files and settings, which the key holds equal, and of
 * the classic level read again from the same file. Made again, as a build
 * makes them at every level start: the frame counters and the clocks, the
 * flip table (each room drawn by its own group again, so that sgRestoreGame
 * flips a savegame's rooms from there through HDFlipRoom), the water sheets
 * with their splashes, rings, wet films, tanks and falls, the restored water
 * faces (HDFlipRoom swaps them in play), the water frames' borrowed pages in
 * the classic Textures[] the load has filled again, the objects' binding to
 * the classic slots (HDObjectLevelRebind), the springs, the water's life and
 * the re-dress rule. False when the data are not there any more. */
static bool HDLevelBuildResident(double loadlog_at)
{
	if (!HDEnabled() || !g_stem[0] || !HDRenderReady() ||
		HDRenderGroupCount() <= 0 || number_rooms <= 0 ||
		number_rooms > HD_MAX_ROOMS)
		return false;

	g_stand_frame_count = 0;
	g_flips_done = 0;
	g_redress_count = 0;
	g_redress_bytes0 = 0;

	for (int i = 0; i < HD_MAX_ROOMS; i++)
		g_room_group[i] = (short)i;

	LevelDiagnosticsEnd();
	strcpy(g_fps_level, g_log_stem);

	g_clipped_lights = 0;
	g_clipped_bulbs = 0;
	g_clock = 0.0;
	g_clock_started = false;
	g_drawn_clock = 0.0;
	g_drawn_alpha = 1.0f;
	g_book_presents = 0;
	g_bookstep_lines = 0;
	g_bookstep_page = -1;
	g_room_count = number_rooms;
	g_level_active = true;

	const double started = LoadClockMs();

	BuildWaterSheets();
	const double sheets = LoadClockMs();
	S_LoadBarBuild(1, 5);		// the loading bar, as the full build's laps
	BuildWaterSurfaces();
	const double surfaces = LoadClockMs();
	S_LoadBarBuild(2, 5);
	// Given back on this thread, which owns the device, and borrowed again.
	ReleaseWaterFramePages();
	BuildWaterFrames();
	const double frames = LoadClockMs();
	S_LoadBarBuild(3, 5);
	HDObjectLevelRebind(HDInstallRoot(), g_stem);
	const double objects = LoadClockMs();
	S_LoadBarBuild(4, 5);
	/* The springs, as the full build begins them: after the sheets set again
	 * above, whose bodies they mark, and before the life, which reads those
	 * marks. HDLevelUnload ended them, as it does at every load. */
	HDSpringLevelBegin(g_stem);
	HDLifeLevelBegin(g_stem);
	const double life = LoadClockMs();
	S_LoadBarBuild(5, 5);

	g_redress_bytes0 = HDPageCacheBytes();

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

	const double ended = LoadClockMs();

	Log("HDLOG level=%s resident build ms=%.1f - the room art, %d page(s), "
		"the tables and the models kept from the load before", g_stem,
		ended - loadlog_at, HDPageCacheSlots());

	/* LOADLOG, with -log: the build's steps in the order and under the names
	 * of a full build's, each step that did not run at 0 with skipped=1. */
	if (gLogEnabled)
	{
		static const char* const skipped[] =
		{
			"release", "budget", "marks", "boxes", "gloss", "intr", "air",
			"pairs", "cuts", "stand", "places", "begin", "trg_read", "prep",
			"pages", "owners", "verts", "setup", "fill", "index", "hash",
			"pages_ahead", "tables", "statics"
		};

		for (const char* step : skipped)
			Log("LOADLOG level=%s step=%s ms=0.0 skipped=1", g_stem, step);

		Log("LOADLOG level=%s step=sheets ms=%.1f", g_stem, sheets - started);
		Log("LOADLOG level=%s step=surfaces ms=%.1f", g_stem,
			surfaces - sheets);
		Log("LOADLOG level=%s step=frames ms=%.1f", g_stem,
			frames - surfaces);
		Log("LOADLOG level=%s step=objects ms=%.1f", g_stem,
			objects - frames);
		Log("LOADLOG level=%s step=models_hash ms=0.0 skipped=1", g_stem);
		Log("LOADLOG level=%s step=life ms=%.1f", g_stem, life - objects);
		Log("LOADLOG level=%s step=redress ms=%.1f", g_stem, ended - life);
	}

	return true;
}

static void HDLevelBuildHD()
{
	HDRenderStats stats;
	const char* why = "";
	int hd_rooms = 0;
	int classic_rooms = 0;
	int suppressed_statics = 0;
	int kept_statics = 0;
	const double loadlog_at = LoadClockMs();

	g_level_active = false;
	g_frame_count = 0;
	g_frame_dropped = 0;
	g_matrix_checked = false;
	g_sky_logged = false;
	g_undergate_logged = false;
	g_water_calls_worst = 0;
	// The per-level window list is written again for this level.
	g_clip_listed = false;
	g_clip_line_ever = false;

	/* The key of the load this build follows, taken once: a build with no
	 * load in front of it (HDLevelDeviceRecreated) reads the settings and
	 * the files as they are by then, which the load's key need not name any
	 * more, so its data are recorded without a key and the next load of the
	 * level builds them again. */
	const HDLevelKey loaded = g_key_loading;

	memset(&g_key_loading, 0, sizeof(g_key_loading));

	/* The level whose HD data HDLevelUnload kept for this load: no build,
	 * only what ties into the classic level and what play changes is made
	 * again. A keep the device no longer allows falls through to the build,
	 * whose release below gives back whatever is left. */
	if (g_keep)
	{
		g_keep = false;

		if (HDLevelBuildResident(loadlog_at))
			return;
	}

	// Released below, and built again or not at all.
	g_key_built.valid = 0;

	memset(g_group_hd, 0, sizeof(g_group_hd));
	memset(g_group_windowed, 0, sizeof(g_group_windowed));
	memset(g_group_stand, 0, sizeof(g_group_stand));
	memset(g_group_stand_covered, 0, sizeof(g_group_stand_covered));
	memset(g_group_stand_part, 0, sizeof(g_group_stand_part));
	memset(g_group_stand_shadow, 0, sizeof(g_group_stand_shadow));
	g_group_drop.clear();
	g_group_drop_first.clear();
	g_stand_pieces.clear();
	g_stand_frame_count = 0;
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
	const double loadlog_released = LoadClockMs();

	/* A scan that failed (HDLevelLoad clears the stem) leaves what was read
	 * ahead for it; a load with HD off read nothing ahead, and its unload
	 * gave back what a level before left. */
	if (tomb5.hd_build && !g_stem[0])
		AheadDrop();

	if (!HDEnabled() || !g_stem[0])
		return;

	if (number_rooms <= 0 || number_rooms > HD_MAX_ROOMS)
	{
		Log("HDLOG level=%s build refused reason=room_count(%d)", g_stem, (int)number_rooms);
		AheadDrop();
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
			AheadDrop();
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

	const double loadlog_budget = LoadClockMs();
	const DWORD started = GetTickCount();

	/* The steps' times for the log (LoadPhase): each lap books the time
	 * since the previous one to its step. */
	double load_ms[LOAD_PHASES] = {};
	double lap_at = LoadClockMs();
	const auto lap = [&](int phase)
	{
		const double now = LoadClockMs();

		load_ms[phase] += now - lap_at;
		lap_at = now;
		// The loading screen's bar moves with the laps (drawbars.cpp).
		S_LoadBarBuild(phase + 1, LOAD_PHASES);
	};

	g_clipped_lights = 0;
	g_clipped_bulbs = 0;
	g_clock = 0.0;
	g_clock_started = false;
	g_drawn_clock = 0.0;
	g_drawn_alpha = 1.0f;
	g_book_presents = 0;
	g_bookstep_lines = 0;
	g_bookstep_page = -1;
	BuildWaterMarks();
	lap(LOAD_MARKS);
	// No pieces until ReportRoomIntrusions has this level's.
	memset(g_piece_count, 0, sizeof(g_piece_count));
	g_pieces.clear();
	BuildRoomBoxes();		// before the build, like the marks
	BuildRoomSectors();		// the same contract
	lap(LOAD_BOXES);
	/* The sheen's override file: hd_gloss.txt in the game's working
	 * directory, beside the executable and SCRIPT.DAT, where Balance.txt is
	 * read from too. Read by the build itself, at every level load, so an
	 * edit shows at the next one without a restart. */
	HDRenderGlossFile("hd_gloss.txt");
	lap(LOAD_GLOSS);
	BuildRoomVis();			// first: it says whether the rules below run
	BuildRoomIntrusions();		// the same contract
	lap(LOAD_INTR);
	BuildRoomAir();			// and the rooms' air, the same contract
	BuildRoomCarry();		// and the rooms under the sky
	lap(LOAD_AIR);
	BuildPairFaces();		// and its faces, the same contract
	lap(LOAD_PAIRS);
	BuildRoomCuts();		// the same contract again
	lap(LOAD_CUTS);
	BuildStandFaces();		// and once more
	lap(LOAD_STAND);
	BuildStaticPlaces();		// kept until BuildStaticTables
	lap(LOAD_PLACES);

	/* The fingerprints of the build and of the models (HDRenderBuildHash),
	 * with the log: one more walk over what was built, after it and timed
	 * apart, for the hash= fields of the built and the models lines. */
	HDRenderBuildHash(gLogEnabled ? 1 : 0);
	HDModelBuildHash(gLogEnabled ? 1 : 0);

	if (!HDRenderLevelBegin(HDInstallRoot(), g_stem, number_rooms,
		kPagesManaged, DecodeThreads(), &stats, &why))
	{
		Log("HDLOG level=%s build failed reason=%s", g_stem, why);
		HDRenderSetStaticPlaces(0, 0);

		/* The page cache's own notes before it is emptied - on the way out
		 * they are the only place that says which page the card gave up on.
		 * Without this the log names the failure and not the cause. */
		for (int i = 0; i < HDPageCacheNoteCount(); i++)
			Log("HDLOG level=%s page note: %s", g_stem, HDPageCacheNote(i));

		// Empty here (the frames are built further down), but the rule is
		// "never empty the page cache with a borrowed reference still out".
		ReleaseWaterFramePages();
		HDRenderLevelEnd();
		AheadDrop();
		return;
	}

	/* Without the fingerprint's time (stats.hashMs, its own field on the
	 * built line), so that built ms and the hd_share line read the same with
	 * the log on as the load takes without it. */
	const int build_ms = (int)(GetTickCount() - started) -
		(int)(stats.hashMs + 0.5);

	lap(LOAD_BEGIN);
	load_ms[LOAD_BEGIN] -= stats.hashMs;

	// The marks were only borrowed for the length of that call.
	HDRenderSetWaterMarks(0, 0);
	g_water_marks.clear();
	HDRenderSetRoomBoxes(0, 0);	// the same contract
	HDRenderSetSectors(0, 0, 0, 0);
	// The faces stay here until BuildStandRanges has read them back.
	HDRenderSetStandFaces(0, 0, 0, 0, 0, 0);
	g_room_grids.clear();
	g_room_sectors.clear();

	/* What the build made of the intrusion list - which groups carry art
	 * standing inside another room, and how much. It reads the counts the
	 * build left behind and gives the borrowed list back. */
	ReportRoomIntrusions();
	// And the art the rooms under the sky carry (HDRoomCarries).
	ReportRoomCarry();
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
	// clear_pages / clear_parts are the clear floors (HD_PASS_CLEAR): the
	// distinct part-way clear pages the rooms' alpha-pass triangles lie on
	// and the parts holding such a triangle. RICH2, RICHCUT2 and TITLE have
	// them; every other level prints 0 and 0.
	// gloss_pages / gloss_draws are the sheen's: the pages that shine and
	// the draw calls that take the room shader's gloss variant while
	// hd_reflect is on (the GLOSSLOG lines below say which and why).
	HDRenderGlossLevel gloss;

	HDRenderGlossLevelInfo(&gloss);
	Log("HDLOG level=%s motion=%d scroll_parts=%d scroll_ranges=%d "
		"scroll_tris=%d sway_verts=%d sway_parts=%d sway_dropped=%d "
		"flipbook_parts=%d flipbook_pages=%d flipbook_dropped=%d "
		"flipbook_ranges=%d cutout_parts=%d add_weighted=%d add_min=%.3f "
		"add_swinging=%d gloss_parts=%d reflect=%d book_parts=%d "
		"book_pages=%d book_dropped=%d book_additive=%d book_step_min=%.3f "
		"book_step_max=%.3f book_ranges=%d clear_pages=%d clear_parts=%d "
		"mirrors=%d gloss_pages=%d gloss_draws=%d",
		g_stem, tomb5.hd_motion ? 1 : 0, stats.scrollParts, stats.scrollRanges,
		stats.scrollTris, stats.swayVertices, stats.swayParts,
		stats.swayDropped, stats.flipbookParts, stats.flipbookDrawn,
		stats.flipbookDropped, stats.flipbookRanges, stats.cutoutParts,
		stats.addWeightParts, stats.addWeightMin, stats.addSwingParts,
		stats.glossParts, tomb5.hd_reflect ? 1 : 0, stats.bookParts,
		stats.bookPages, stats.bookDropped, stats.bookAdditive,
		stats.bookStepMin, stats.bookStepMax, stats.bookRanges,
		stats.clearPages, stats.clearParts, HDRenderMirrorCount(),
		gloss.pages, gloss.draws);

	/* GLOSSLOG, with -log: which surfaces shine and why. One level line -
	 * the table's lines, the override file's pages (none when there is no
	 * hd_gloss.txt) and its unreadable lines, the parts each source named,
	 * the lamps that went into the light field and the light cubes the .TRG
	 * holds for the floor squares, and the light field itself: its cells
	 * along x, y and z (0x0x0 when the level has none), the cells a lamp
	 * reaches, the weak ones between lamps, its textures' kilobytes and the
	 * build's milliseconds. One skipped line - the draw calls whose page
	 * names a gloss and that keep the plain variant, per reason, and the
	 * clear floors' (HDRenderSheenSkipped). And one line per page and source
	 * that shines, with the numbers the gloss variant draws it with. */
	if (gLogEnabled)
	{
		char file[16];

		if (gloss.filePages >= 0)
			sprintf(file, "%d", gloss.filePages);
		else
			strcpy(file, "none");

		Log("GLOSSLOG level=%s table=%d file=%s bad=%d parts_mat=%d "
			"parts_seed=%d parts_table=%d lights=%d cubes=%d parts_file=%d "
			"field=%dx%dx%d field_lit=%d field_weak=%d field_kb=%d field_ms=%.1f",
			g_stem, gloss.tableLines, file, gloss.fileBad, gloss.partsMat,
			gloss.partsSeed, gloss.partsTable, gloss.lamps, gloss.cubes,
			gloss.partsFile, gloss.fieldDims[0], gloss.fieldDims[1],
			gloss.fieldDims[2], gloss.fieldLit, gloss.fieldWeak,
			gloss.fieldBytes / 1024, gloss.fieldMs);
		Log("GLOSSLOG skipped level=%s ripple=%d caustic=%d flat=%d scroll=%d "
			"flipbook=%d book=%d clear=%d", g_stem, gloss.skipped.ripple,
			gloss.skipped.caustic, gloss.skipped.flat, gloss.skipped.scroll,
			gloss.skipped.flipbook, gloss.skipped.book, gloss.skipped.clear);

		for (int i = 0; i < HDRenderGlossPageCount(); i++)
		{
			HDRenderGlossPage p;

			if (!HDRenderGlossPageInfo(i, &p))
				continue;

			Log("GLOSSLOG page=%d class=%s source=%s strength=%.3f power=%.0f "
				"f0=%.3f tris=%d", p.page, HDGlossClassName(p.cls),
				HDGlossSourceName(p.source), p.strength, p.power, p.f0, p.tris);
		}
	}

	/* The mirrors in the rooms' walls, one line each: the room, the plane,
	 * which side the copy is on, the three counts the rule decided on (see
	 * hd_render.cpp, FindMirrors) and the copy's box - the volume a
	 * reflected item has to land in to be drawn. */
	for (int i = 0; i < HDRenderMirrorCount(); i++)
	{
		HDMirror m;

		if (!HDRenderMirrorInfo(i, &m))
			continue;

		Log("HDLOG mirrorwall level=%s room=%d axis=%c plane=%.0f side=%d "
			"pane=%d behind=%d mirrored=%d box=%.0f..%.0f,%.0f..%.0f,%.0f..%.0f",
			g_stem, m.group, m.axis == 0 ? 'x' : 'z', m.plane, m.side, m.pane,
			m.behind, m.mirrored, m.mins[0], m.maxs[0], m.mins[1], m.maxs[1],
			m.mins[2], m.maxs[2]);
	}

	/* The material 15 books, one line per part and pass: the pages the book
	 * runs over, how many, how long each stands, where in the cycle the part
	 * starts, the pass and the weight it is drawn with (1 outside the
	 * additive pass). `dropped=1` is a part that keeps its own page because
	 * a page or a number did not resolve - frames=1 and step=0.000 then.
	 * The motion line above carries the totals: book_parts counts the parts
	 * that turn, book_pages the distinct pages their books hold,
	 * book_additive those of them in the additive pass, book_ranges the draw
	 * calls they cost. */
	if (gLogEnabled)
		for (int i = 0; i < HDRenderBookPartCount(); i++)
		{
			HDRenderBookPart book;

			if (!HDRenderBookPartInfo(i, &book))
				continue;

			Log("HDLOG book group=%d part=%d page=%d..%d frames=%d step=%.3f "
				"phase=%d pass=%d weight=%.3f dropped=%d", book.group,
				book.part, book.page0, book.pageLast, book.pages, book.step,
				book.phase, book.pass, book.weight, book.dropped);
		}

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
	// Each room's reason and cut, for its log line after the stand ranges.
	std::vector<const char*> reasons((size_t)g_room_count, "");
	std::vector<int> cuts((size_t)g_room_count, 0);

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
		 * So such a room is drawn classic, as a room the .TRG never had.
		 * Its floor and walls may still exist as another group's art: room
		 * 124's stand in group 121, the room through its west portal. Whether
		 * a frame draws that art in the room's place instead of the roomlets
		 * is the stand ranges' question (BuildStandFaces, BuildStandRanges):
		 * room 124 is covered and drawn as group 121's art, the other eight
		 * keep their classic geometry. */
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

		reasons[(size_t)r] = reason;
		cuts[(size_t)r] = cut_tris;
	}

	/* Which classic rooms another group's art covers - it needs every
	 * group's g_group_hd, so it runs once all of them are decided. */
	std::vector<int> stand_on;
	const int stand_tris = BuildStandRanges(stand_on);
	int stand_rooms = 0, stand_covered = 0, stand_part = 0, shadow_rooms = 0;

	/* A room drawn classic with no classic face at all - the air over a
	 * court, a shaft whose walls another group models - draws nothing classic
	 * Lara could stand on: whatever is under her there is art, drawn with its
	 * group and taking the receive, or the room below. It counts as HD for
	 * her shadow like a room drawn as art that keeps no floor
	 * (g_group_stand_shadow). A room the randomizer marked stays out, as it
	 * does of the stand rule. So does one with a sector she can stand on - no
	 * wall, no opening into a room below - while no HD group but its flipmap
	 * twin has a vertex in a 256-unit cell that meets the room's whole box
	 * (the grid with its wall ring, maxceiling to minfloor): nothing is drawn
	 * under her there, and the blob stays. TrgProbe -classicart -perface
	 * counts 17 such rooms in the game (its noface lines, bare=1), and each
	 * holds no triangle centroid of another group's art in that box either;
	 * the inside box made one cell smaller would call 21 rooms bare that hold
	 * such art. */
	for (int r = 0; r < g_room_count && r < number_rooms && r < HD_MAX_ROOMS; r++)
	{
		if (g_group_hd[r] || HDMarkersRoomTouched(r))
			continue;

		int faces = 0;

		for (int i = 0; room[r].pRoomlets && i < room[r].nRoomlets; i++)
			faces += room[r].pRoomlets[i].nQuad + room[r].pRoomlets[i].nTri;

		bool bare = false;

		if (faces <= 0 && room[r].floor)
		{
			const ROOM_INFO* rm = &room[r];

			for (int i = 0; i < rm->x_size * rm->y_size && !bare; i++)
				bare = rm->floor[i].floor != NO_HEIGHT / 256 &&
					rm->floor[i].pit_room == NO_ROOM;

			for (int g = 0; g < g_room_count && g < number_rooms && bare; g++)
			{
				if (g == r || !g_group_hd[g] || rm->flipped_room == g ||
					room[g].flipped_room == r)
					continue;

				// The box in group g's own frame: x and z from its room's origin.
				const float dx = (float)(rm->x - room[g].x);
				const float dz = (float)(rm->z - room[g].z);
				const float mins[3] = { dx, (float)(rm->maxceiling - rm->y), dz };
				const float maxs[3] = { (float)(rm->y_size << 10) + dx,
					(float)(rm->minfloor - rm->y), (float)(rm->x_size << 10) + dz };

				if (HDRenderGroupCellsIn(g, mins, maxs, 0) > 0)
					bare = false;
			}
		}

		if (HDCoverRoomShadowHD(false, HD_STAND_ROOM_CLASSIC, 0, faces, bare))
			g_group_stand_shadow[r] = 1;
	}

	/* One line per room. `stand` is the triangles of HD groups that stand on
	 * the room's classic faces - 0 for a room drawn HD and for a classic one
	 * no other art stands on - `covered` 1 when the carrier's art covers
	 * every face, so that the room is drawn as that art (hd_cover.h), and
	 * `carrier` the group whose art covers the most of its faces, -1 for a
	 * room drawn classic. At the end: `faces` the room's classic faces of any
	 * area that stand art was asked about (0 for a room no art stands on),
	 * `faces_covered` those the carrier covers - left out of the roomlets and
	 * drawn as its art - `floors_kept` the faces Lara can stand on that stay
	 * classic (HDCoverFaceFloor), and `shadow` 1 when the room counts as HD
	 * for her projected shadow (HDRoomShadowHD). */
	for (int r = 0; r < g_room_count; r++)
	{
		const int on = (size_t)r < stand_on.size() ? stand_on[(size_t)r] : 0;
		const int covered = r < HD_MAX_ROOMS ? (int)g_group_stand_covered[r] : 0;
		const int carrier = r < HD_MAX_ROOMS ? (int)g_group_stand_carrier[r] : -1;
		const bool here = r < HD_MAX_ROOMS;

		if (on)
			stand_rooms++;

		if (covered)
			stand_covered++;

		if (here && g_group_stand_part[r])
			stand_part++;

		if (here && !g_group_hd[r] && g_group_stand_shadow[r])
			shadow_rooms++;

		Log("HDLOG room=%d group=%d tris=%d/%d/%d overhang=%d cut=%d "
			"ranges=%d statics=%d hd=%d reason=%s stand=%d covered=%d "
			"carrier=%d faces=%d faces_covered=%d floors_kept=%d shadow=%d",
			r, r, HDRenderGroupTriangles(r, 0), HDRenderGroupTriangles(r, 1),
			HDRenderGroupTriangles(r, 2), HDRenderGroupOverhangTriangles(r),
			cuts[(size_t)r], HDRenderGroupRanges(r, -1),
			(int)room[r].num_meshes, (int)g_group_hd[r], reasons[(size_t)r],
			on, covered, carrier, here ? (int)g_group_stand_faces[r] : 0,
			here ? (int)g_group_stand_faces_covered[r] : 0,
			here ? (int)g_group_stand_floors_kept[r] : 0,
			here && (g_group_hd[r] || g_group_stand_shadow[r]) ? 1 : 0);
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

	/* The parts of `built ms`: the steps before HDRenderLevelBegin, then
	 * the .TRG read and the build's own parts (HD_BEGIN_*), in the order
	 * they ran. */
	char phases[512];
	{
		int at = 0;

		for (int p = LOAD_MARKS; p < LOAD_BEGIN && at >= 0 &&
			at < (int)sizeof(phases); p++)
			at += snprintf(phases + at, sizeof(phases) - (size_t)at,
				"%s%s:%.1f", p ? "," : "", kLoadPhaseName[p], load_ms[p]);

		if (at >= 0 && at < (int)sizeof(phases))
			at += snprintf(phases + at, sizeof(phases) - (size_t)at,
				",read:%.1f", stats.readMs);

		for (int p = 0; p < HD_BEGIN_PHASES && at >= 0 &&
			at < (int)sizeof(phases); p++)
			at += snprintf(phases + at, sizeof(phases) - (size_t)at, ",%s:%.1f",
				HDRenderBeginPhaseName(p), stats.phaseMs[p]);
	}

	/* At the end, the stand rule's counts: the rooms stand art stands on, of
	 * those the ones drawn wholly as art (`covered`) and in part (`part`),
	 * the triangles, and the classic-drawn rooms that count as HD for her
	 * shadow (`shadow_rooms`: drawn as art keeping no floor, or with no
	 * classic face at all and no bare floor). */
	Log("HDLOG level=%s built ms=%d rooms=%d hd=%d classic=%d sky_groups=%d "
		"verts=%d idx=%d(%dbit) ranges=%d split=%d layer0_tris=%d "
		"overhang_tris=%d hollow=%d "
		"intruding_tris=%d intruding_ranges=%d intruding_groups=%d "
		"cut_tris=%d cut_groups=%d "
		"vb_mib=%.1f ib_mib=%.1f read_ms=%d build_ms=%d "
		"stand_rooms=%d covered=%d stand_tris=%d part=%d shadow_rooms=%d"
		" phases=%s threads=%d hash=%016llx hash_ms=%d",
		g_stem, (int)build_ms, g_room_count, hd_rooms, classic_rooms,
		stats.skyGroups, stats.vertices, stats.indices, stats.index32 ? 32 : 16,
		stats.ranges, stats.splitRanges, stats.fallbackPageTris,
		stats.overhangTris, stats.hollowGroups,
		stats.intrudingTris, stats.intrudingRanges, stats.intrudingGroups,
		stats.cutTris, stats.cutGroups,
		stats.vertexBytes / 1048576.0, stats.indexBytes / 1048576.0,
		(int)(stats.readMs + 0.5), (int)(stats.buildMs + 0.5),
		stand_rooms, stand_covered, stand_tris, stand_part, shadow_rooms,
		phases, DecodeThreads(), stats.hash, (int)(stats.hashMs + 0.5));

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
	 * "double lantern" TROne guards against (TROne-main (copy of
	 * 2026-10-05), draw_mesh: a baked mesh pointer passes only while the
	 * moveable that claims it is streaming).
	 *
	 * This seam is narrower and the rule with it: a room's own MESH_INFO
	 * list is asked, static by static, whether the art draws its place - the
	 * room's own group, the carrier's art of a room drawn as it, or another
	 * group's art in a frame that lists that group. A moveable that shares a
	 * static's MESH_DATA* goes through DrawAnimatingItem and is never
	 * touched, so TROne's exception cannot arise here.
	 *
	 * A static the game removes at run time - a shattered vase - is gone from
	 * the classic list but still stands in the baked art; the destructible
	 * pairs of BuildStaticTables hide its piece. After BuildStandRanges,
	 * whose carriers and room states BuildStaticTables asks. */
	lap(LOAD_TABLES);
	BuildStaticTables();
	lap(LOAD_STATICS);

	/* The water surface the .TRG has no geometry for - see the note over
	 * BuildWaterSurfaces. After the loop above, because it asks per room
	 * whether that room draws HD at all. The water sheets first: a face under
	 * one is not restored. */
	BuildWaterSheets();
	lap(LOAD_SHEETS);
	BuildWaterSurfaces();
	lap(LOAD_SURFACES);

	/* And the HD pictures of that water's own animation frames. After
	 * BuildWaterSurfaces, because it asks which faces are really drawn. */
	BuildWaterFrames();
	lap(LOAD_FRAMES);

	/* `suppressed` and `kept` count the statics of the rooms drawn HD and of
	 * the rooms drawn classic; `art`, `by_group` and `classic` the answers of
	 * BuildStaticTables over all of them - never drawn, drawn only while no
	 * group holding them is on the frame's list, drawn in every frame - and
	 * `pieces` those of `by_group` with a piece, drawn as the art in its
	 * place while no holder is listed. */
	Log("HDLOG level=%s statics suppressed=%d kept=%d (in %d HD rooms, %d classic) "
		"art=%d by_group=%d classic=%d pieces=%d",
		g_stem, suppressed_statics, kept_statics, hd_rooms, classic_rooms,
		g_statics_art, g_statics_by_group, g_statics_classic, g_statics_piece);

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
	const double model_hash_ms = HDModelHashMs();
	HDObjectLevelBuild(HDInstallRoot(), g_stem);
	lap(LOAD_OBJECTS);
	// The models' fingerprint is on the models line; the step is without it.
	load_ms[LOAD_OBJECTS] -= HDModelHashMs() - model_hash_ms;

	/* The whole of the level's HD load in one line: the .TRG scan on the
	 * load thread and every step of this build, the build itself as one
	 * part (its own parts are on the built line). `ms` is their sum - what
	 * the HD path adds to a level load - and leaves out the classic load,
	 * the waits between the two threads, the first frame and the two
	 * fingerprints (hash_ms on the built and the models lines). */
	{
		const double built = load_ms[LOAD_MARKS] + load_ms[LOAD_BOXES] +
			load_ms[LOAD_GLOSS] + load_ms[LOAD_INTR] + load_ms[LOAD_AIR] +
			load_ms[LOAD_PAIRS] + load_ms[LOAD_CUTS] + load_ms[LOAD_STAND] +
			load_ms[LOAD_PLACES] + load_ms[LOAD_BEGIN];
		double total = g_scan_ms + built;
		char share[256];
		int at = snprintf(share, sizeof(share), "scan:%.0f,built:%.0f",
			g_scan_ms, built);

		for (int p = LOAD_TABLES; p < LOAD_PHASES; p++)
		{
			total += load_ms[p];

			if (at > 0 && at < (int)sizeof(share))
				at += snprintf(share + at, sizeof(share) - (size_t)at,
					",%s:%.0f", kLoadPhaseName[p], load_ms[p]);
		}

		Log("HDLOG level=%s hd_share ms=%.0f phases=%s threads=%d", g_stem,
			total, share, DecodeThreads());
	}

	/* The water's life, after the objects whose model list carried its
	 * models and after the sheets whose bodies it lives on. */
	HDSpringLevelBegin(g_stem);
	HDLifeLevelBegin(g_stem);
	const double loadlog_life = LoadClockMs();

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

	/* LOADLOG, with -log: every step of this build on a line of its own, in
	 * the order it ran, with its milliseconds - the steps of the hd_share
	 * line and of the built line's phases, and the parts no other line
	 * books: the release of the level before, the budget, the two
	 * fingerprints only -log computes, the water's life and the re-dress
	 * rule. trg_read, the build's own parts and hash follow `begin` and are
	 * its parts; hash and models_hash are in no other step. A way out of
	 * the build above returns before this and writes none of it. */
	if (gLogEnabled)
	{
		const double loadlog_end = LoadClockMs();

		Log("LOADLOG level=%s step=release ms=%.1f", g_stem,
			loadlog_released - loadlog_at);
		Log("LOADLOG level=%s step=budget ms=%.1f", g_stem,
			loadlog_budget - loadlog_released);

		for (int p = 0; p < LOAD_PHASES; p++)
		{
			Log("LOADLOG level=%s step=%s ms=%.1f", g_stem,
				kLoadPhaseName[p], load_ms[p]);

			if (p != LOAD_BEGIN)
				continue;

			Log("LOADLOG level=%s step=trg_read ms=%.1f%s", g_stem,
				stats.readMs, HDScanTrgTaken() ? " kept=1" : "");

			for (int b = 0; b < HD_BEGIN_PHASES; b++)
				Log("LOADLOG level=%s step=%s ms=%.1f", g_stem,
					HDRenderBeginPhaseName(b), stats.phaseMs[b]);

			Log("LOADLOG level=%s step=hash ms=%.1f", g_stem, stats.hashMs);

			/* The pages decoded ahead on the load side (HDPageCacheAhead):
			 * their own time, which ran beside the load thread's and this
			 * thread's steps and is in none of them; `taken` of them spared
			 * the step `pages` its decode. */
			int ahead = 0, taken = 0;
			double ahead_ms = 0.0;

			HDPageCacheAheadInfo(&ahead, &taken, &ahead_ms);
			Log("LOADLOG level=%s step=pages_ahead ms=%.1f pages=%d taken=%d",
				g_stem, ahead_ms, ahead, taken);
		}

		Log("LOADLOG level=%s step=models_hash ms=%.1f", g_stem,
			HDModelHashMs() - model_hash_ms);
		Log("LOADLOG level=%s step=life ms=%.1f", g_stem,
			loadlog_life - lap_at);
		Log("LOADLOG level=%s step=redress ms=%.1f", g_stem,
			loadlog_end - loadlog_life);
	}

	// The data of this level, for the next load of it to keep.
	g_key_built = loaded;
	g_kept_vertices = g_scan_vertices;
	g_kept_indices = g_scan_indices;
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
	g_rebuild_wanted = false;
	g_frame_count = 0;
	g_room_count = 0;
	g_stem[0] = 0;

	/* The level about to be read is the one the HD data in memory are of:
	 * they stay (hd_levelkey.h, HDLevelBuildResident), and so do the static
	 * tables, which number this same level's rooms and statics. Otherwise
	 * everything goes as for any other level. A FreeLevel with no load after
	 * it (GameClose) finds no key and keeps nothing. */
	const char* refused = HDLevelKeepRefused(&g_key_built, &g_key_next,
		g_redress_count);

	g_keep = !refused;
	g_key_loading = g_key_next;
	memset(&g_key_next, 0, sizeof(g_key_next));
	/* No load follows (GameClose, or HD off): what was read or decoded
	 * ahead and never taken goes. A load's own was started for it by
	 * HDLevelKeyNext and stays. */
	if (!g_key_loading.valid)
	{
		AheadReadWait();
		HDScanDropTrg();
		HDPageCacheAheadDrop();
	}

	if (g_key_loading.valid)
		Log("HDLOG level=%s resident=%d%s%s", g_key_loading.name,
			g_keep ? 1 : 0, refused ? " refused=" : "", refused ? refused : "");

	/* The static tables point into this level's rooms. */
	if (!g_keep)
	{
		g_static_art.clear();
		g_static_first.clear();
		g_destruct_pairs.clear();
	}

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
	HDLifeLevelEnd();	// no device either: the pool is plain memory
	HDSpringLevelEnd();
}

void HDLevelDeviceLost()
{
	/* The device is going away - a resolution change, Alt+Enter, or DXClose
	 * at the end of the session. The last one is why the summary is flushed
	 * here as well as in HDLevelBuild: without it the level the game quit
	 * from would never get its FPSLOG summary. Main
	 * thread in every one of those cases. */
	LevelDiagnosticsEnd();

	g_rebuild_wanted = g_level_active;
	g_level_active = false;
	g_frame_count = 0;
	g_room_count = 0;
	// The HD data go with the device, so no later load can keep them.
	g_key_built.valid = 0;
	// The borrowed water frame pages are references on cache textures that
	// HDRenderShutdown is about to drop.
	ReleaseWaterFramePages();
	HDObjectDeviceLost();
	HDSpringDeviceLost();

	// The font atlas was made on this device too. Its match table survives;
	// only the texture goes.
	HDFontDeviceLost();

	if (!HDRenderReady() && !HDRenderGroupCount())
		return;

	// Shaders, declaration, both buffers and every page texture belong to the
	// device that is going away. HDRenderShutdown drops all of it and forgets
	// the device; the next HDLevelBuild calls HDRenderInit again on whatever
	// device exists then - HDLevelDeviceRecreated's, once DXCreate has made
	// the new one, or the next level's. Until then the rooms are classic,
	// which is a picture, not a crash, and a crash is what a dangling
	// IDirect3DVertexBuffer9 would be.
	HDRenderShutdown();
	Log("HDLOG device lost - HD released, classic until the next level");
}

/* The rooms FlipMap has swapped (game\control.cpp): every room with an
 * alternate whose flip stands set in flip_stats - the same test FlipMap
 * makes, and the same truth the savegame keeps. */
static void FlippedPairs(std::vector<int>& pairs)
{
	pairs.clear();

	for (int i = 0; i < number_rooms; i++)
	{
		const ROOM_INFO* r = &room[i];

		if (r->flipped_room >= 0 && r->flipped_room < number_rooms &&
			r->FlipNumber < 255 && flip_stats[r->FlipNumber])
		{
			pairs.push_back(i);
			pairs.push_back(r->flipped_room);
		}
	}
}

/* The contents of two ROOM_INFO records change places as in FlipMap, while
 * the fields FlipMap keeps with the room number - flipped_room, item_number
 * and fx_number - stay with each number. Done twice it gives back every
 * byte, which FlipMap's own swap does not (it hands the alternate the
 * room's item list), so the build below can see the rooms as a level load
 * does and leave them exactly as the game had them. */
static void SwapRoomContents(int a, int b)
{
	ROOM_INFO keep;
	const short flipA = room[a].flipped_room, flipB = room[b].flipped_room;
	const short itemA = room[a].item_number, itemB = room[b].item_number;
	const short fxA = room[a].fx_number, fxB = room[b].fx_number;

	memcpy(&keep, &room[a], sizeof(ROOM_INFO));
	memcpy(&room[a], &room[b], sizeof(ROOM_INFO));
	memcpy(&room[b], &keep, sizeof(ROOM_INFO));
	room[a].flipped_room = flipA;
	room[b].flipped_room = flipB;
	room[a].item_number = itemA;
	room[b].item_number = itemB;
	room[a].fx_number = fxA;
	room[b].fx_number = fxB;
}

/* A new device in place of the old one (DXCreate, after the classic pages
 * have moved): the level that was drawing HD is built again for the level
 * still loaded, by HDLevelBuild itself - room[], the floor data, objects[]
 * and the markers are all still in memory, and the build reads the art and
 * its pages from the Remaster's DATA again as at a level load. Then the
 * font. Runs on the window thread while the game thread waits at
 * BeginScene, before the frame's lists are begun (S_InitialisePolyList).
 *
 * Two things differ from a load. The build takes group i for room i, which
 * is true only before any flip, so the swapped pairs are swapped back for
 * it and swapped again after, and HDFlipRoom then moves the art with them
 * as it does when a savegame's flips are applied after its load. And the
 * HD clock keeps running: a load starts it at 0, which would set every
 * scroll, wave and book back to its start in mid-level.
 *
 * Returns whether the level draws HD again. A level that was classic when
 * the device went stays classic; nothing here runs with hd_enable 0. */
bool HDLevelDeviceRecreated()
{
	bool rebuilt = false;

	if (g_rebuild_wanted && HDEnabled() && g_stem[0])
	{
		std::vector<int> pairs;
		const double clock = g_clock;
		const unsigned short clockLast = g_clock_last;
		const bool clockStarted = g_clock_started;

		g_rebuild_wanted = false;
		FlippedPairs(pairs);

		for (size_t i = 0; i + 1 < pairs.size(); i += 2)
			SwapRoomContents(pairs[i], pairs[i + 1]);

		HDLevelBuild();

		for (size_t i = 0; i + 1 < pairs.size(); i += 2)
		{
			SwapRoomContents(pairs[i], pairs[i + 1]);
			HDFlipRoom(pairs[i], pairs[i + 1]);
		}

		g_clock = clock;
		g_clock_last = clockLast;
		g_clock_started = clockStarted;
		rebuilt = g_level_active;
		Log("HDLOG device recreated - level=%s rebuilt=%d flipped_pairs=%d",
			g_stem, rebuilt ? 1 : 0, (int)(pairs.size() / 2));
	}

	HDFontBuild();
	return rebuilt;
}

/* The group that draws room `roomIndex` right now, or -1. The one place the
 * flip table is read; everything else asks this. */
static int GroupOfRoom(int roomIndex)
{
	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
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

/* Read through g_room_group like every stand answer, so a flipmap that swaps
 * a room's roomlets swaps its flags with them. */
const unsigned char* HDRoomletDropped(int roomIndex, int roomlet)
{
	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return 0;

	const int group = g_room_group[roomIndex];

	if (group < 0 || group >= HD_MAX_ROOMS || !g_group_stand_part[group] ||
		(size_t)group >= g_group_drop.size())
		return 0;

	const std::vector<int>& first = g_group_drop_first[(size_t)group];
	const std::vector<unsigned char>& drop = g_group_drop[(size_t)group];

	if (roomlet < 0 || roomlet >= (int)first.size() ||
		(size_t)first[(size_t)roomlet] >= drop.size())
		return 0;

	return &drop[(size_t)first[(size_t)roomlet]];
}

bool HDRoomShadowHD(int roomIndex)
{
	if (HDRoomIsHD(roomIndex))
		return true;

	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return false;

	const int group = g_room_group[roomIndex];

	return group >= 0 && group < HD_MAX_ROOMS && g_group_stand_shadow[group] != 0;
}

bool HDRoomArtListed(int roomIndex, const short* listed, int listedCount)
{
	if (HDRoomIsHD(roomIndex))
		return true;

	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return false;

	const int group = g_room_group[roomIndex];

	if (group < 0 || group >= HD_MAX_ROOMS || !g_group_stand_covered[group])
		return false;

	const int carrier = g_group_stand_carrier[group];

	for (int i = 0; listed && i < listedCount; i++)
		if (carrier >= 0 && GroupOfRoom(listed[i]) == carrier)
			return true;

	return false;
}

/* The rooms a room under the sky carries (hd_level.h), out of the list the
 * build made of the renderer's carry pairs. The list is the art's, kept by
 * the numbers of the level as built: after a flip a room's number holds its
 * twin's group (HDFlipRoom), and what stands under its sky is that group's
 * list - ANDY1's rooms 48 and 151 carry 215 and 177, their alternates 214
 * and 213 nothing, the only two hosts of the fifteen levels whose two
 * states carry different rooms. */
int HDRoomCarries(int roomIndex, const short** rooms)
{
	*rooms = 0;

	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return 0;

	const int key = g_room_group[roomIndex] >= 0 ? g_room_group[roomIndex]
		: roomIndex;

	if (key >= (int)g_room_carry.size() || g_room_carry[(size_t)key].empty())
		return 0;

	*rooms = &g_room_carry[(size_t)key][0];
	return (int)g_room_carry[(size_t)key].size();
}

/* CarryArt takes in room numbers, and after a flip the number of a pair's
 * room no longer names its art: FlipMap swaps the two records and
 * HDFlipRoom the groups, so the art of the twin the portals do not lead to
 * - out of the world in this state - is drawn by whichever of the two
 * numbers holds it. Taken in by its number, ANDY1's room 48 draws the
 * alternate art of room 179 (group 215, the room 215 is) beside room 179's
 * own while unflipped, and RICH3's room 176 the alternate of room 170
 * (group 174): art of a state the level is not in. */
int HDCarriedRoom(int group)
{
	if (!HDLevelActive() || group < 0 || group >= (int)g_carry_main.size())
		return -1;

	const int main = g_carry_main[(size_t)group];

	if (main < 0)
		return group;

	return GroupOfRoom(main) == group ? main : -1;
}

/* The room whose art is the sea's surface over the dive (hd_water_under.h,
 * HDWaterSurfaceRoom), in a level that takes the deep look; -1 in every
 * other level and while HDLevelActive() is false. Worked out at the first
 * frame that asks and kept for the level: the rooms' flags and squares as
 * the water build reads them for the deep look's top (hd_render.cpp,
 * g_wdeep_top) and the highest point of each room's art. The deep look is
 * the water build's answer whatever tomb5.hd_water says, and the art is
 * room art, so the answer does not depend on the sheets. */
static int SurfaceRoom()
{
	static char stem[64] = { 0 };
	static int rooms = -1;
	static int answer = -1;

	if (!HDLevelActive())
		return -1;

	if (rooms == g_room_count && !strcmp(stem, g_stem))
		return answer;

	HDRenderWaterLevel water;

	strncpy(stem, g_stem, sizeof(stem) - 1);
	rooms = g_room_count;
	answer = -1;
	HDRenderWaterLevelInfo(&water);

	if (!water.underDeep || g_room_count <= 0 || g_room_count > number_rooms)
		return answer;

	std::vector<int> wet((size_t)g_room_count, 0);
	std::vector<int> hasArt((size_t)g_room_count, 0);
	std::vector<float> ceilTop((size_t)g_room_count, 1.0e9f);
	std::vector<float> artTop((size_t)g_room_count, 0.0f);

	for (int r = 0; r < g_room_count; r++)
	{
		const ROOM_INFO* rr = &room[r];
		const int squares = rr->floor ? rr->x_size * rr->y_size : 0;
		const int group = GroupOfRoom(r);
		float mins[3], maxs[3];

		wet[(size_t)r] = (rr->flags & ROOM_UNDERWATER) ? 1 : 0;

		// A square of the water build: floor and ceiling both a height.
		for (int i = 0; i < squares; i++)
			if (rr->floor[i].floor != -127 && rr->floor[i].ceiling != -127 &&
				rr->floor[i].ceiling * 256.0f < ceilTop[(size_t)r])
				ceilTop[(size_t)r] = rr->floor[i].ceiling * 256.0f;

		if (group >= 0 && HDRenderGroupBounds(group, mins, maxs))
		{
			hasArt[(size_t)r] = 1;
			artTop[(size_t)r] = mins[1] + (float)rr->y;
		}
	}

	answer = HDWaterSurfaceRoom(&wet[0], &ceilTop[0], &hasArt[0], &artTop[0],
		g_room_count, kHDWaterSurfaceReach);
	return answer;
}

/* The visibility path's questions (hd_level.h). The tables are the art's,
 * kept by the numbers of the level as built - a room's guests, openings and
 * wides are those of the group it holds now (g_room_group, which HDFlipRoom
 * swaps with the records) - and a group is drawn by the room the portals
 * lead to while that room holds it, by its own number when it is no
 * flipmap pair's, and by none while its pair's other state is in the world.
 *
 * HDLOG vis, once per level at the first frame that asks, with -log:
 *
 *   HDLOG level=<stem> vis=1 rooms= art_rooms= walls= bare= void= guests=
 *         guest_tris= wide= ms= twin= wide_bare= edge= statics_held=
 *         statics_ms= enclosed=
 *
 * art_rooms the rooms whose walls were asked, walls their classic wall
 * faces, bare those the art leaves bare (the openings), void those with no
 * room behind, guests the (holder, group) pairs and their triangles, wide
 * the portals with a wider opening, ms the tables' build; twin the
 * openings that named a flipmap pair's other room, named by its main room
 * instead, wide_bare and edge HDVisDefaults' two numbers of the walk;
 * statics_held the static meshes the art lies on (HDVisStaticCovers) and
 * statics_ms what asking them cost; enclosed the rooms that stand inside
 * another whose art stands in them (HDVisTables::enclosedBy). */

static int VisContents(int roomIndex)
{
	if (roomIndex < 0 || roomIndex >= g_room_count)
		return -1;

	return g_room_group[roomIndex];
}

bool HDVisWalkOn()
{
	const HDVisTables* t = HDVisLevelTables();

	if (!g_vis_level || !t || !HDLevelActive())
		return false;

	if (gLogEnabled && !g_vis_logged)
	{
		g_vis_logged = true;
		const HDVisParams p = HDVisDefaults();

		Log("HDLOG level=%s vis=1 rooms=%d art_rooms=%d walls=%d bare=%d "
			"void=%d guests=%d guest_tris=%lld wide=%d ms=%.0f twin=%d "
			"wide_bare=%.2f edge=%.5f statics_held=%d statics_ms=%.0f "
			"enclosed=%d", g_stem, t->rooms, t->artRooms, t->wallFaces,
			t->bareFaces, t->voidFaces, t->guestPairs, t->guestTris,
			t->widePortals, t->ms, t->openingsTwin, p.wideBare, p.edge,
			t->staticsHeld, t->staticMs, t->roomsEnclosed);
	}

	return true;
}

int HDVisRoomGuests(int roomIndex, const HDVisGuest** out)
{
	const HDVisTables* t = HDVisLevelTables();
	const int c = VisContents(roomIndex);

	*out = 0;

	if (!t || c < 0 || c >= t->rooms || !t->guestCount[(size_t)c])
		return 0;

	*out = &t->guests[(size_t)t->guestFirst[(size_t)c]];
	return t->guestCount[(size_t)c];
}

int HDVisRoomOpenings(int roomIndex, const HDVisOpening** out)
{
	const HDVisTables* t = HDVisLevelTables();
	const int c = VisContents(roomIndex);

	*out = 0;

	if (!t || c < 0 || c >= t->rooms || !t->openCount[(size_t)c])
		return 0;

	*out = &t->openings[(size_t)t->openFirst[(size_t)c]];
	return t->openCount[(size_t)c];
}

const HDVisWide* HDVisRoomWide(int roomIndex, int portal)
{
	const HDVisTables* t = HDVisLevelTables();
	const int c = VisContents(roomIndex);

	if (!t || c < 0 || c >= t->rooms)
		return 0;

	for (int k = 0; k < t->wideCount[(size_t)c]; k++)
	{
		const HDVisWide& w = t->wides[(size_t)(t->wideFirst[(size_t)c] + k)];

		if (w.portal == portal)
			return &w;
	}

	return 0;
}

int HDVisRoomOfGroup(int group)
{
	if (group < 0 || group >= g_room_count)
		return -1;

	const int main = (size_t)group < g_vis_main.size() ?
		g_vis_main[(size_t)group] : -1;

	if (main < 0)
		return group;

	return g_room_group[main] == group ? main : -1;
}

/* The one question the room walk asks of the HD path: a level built HD
 * (g_level_active) and the draw at HD (hd_switch.h, HDDrawn: the HD
 * textures row, which the HD/SD switch flips). g_level_active is false
 * with the build gate off at the load (HDEnabled: hd_build 0 or no
 * installation, fixed for the session), before a level is built, after
 * HDLevelUnload and in a level whose .TRG had no rooms; it is set only past
 * HDEnabled, so the gate needs no second asking here. The draw is read on
 * every call, so a press or a step of the row turns a running level
 * classic from the next frame on, and back. Whenever this answers false the
 * walk in game\draw.cpp carries no reach and is the classic walk. */
bool HDLevelActive()
{
	return g_level_active && HDDrawn();
}

/* The mirrors the room build found, handed to the game side in the engine's
 * own integer units. Behind HDLevelActive, so a classic frame never sees
 * one. */
int HDLevelMirrorCount()
{
	return HDLevelActive() ? HDRenderMirrorCount() : 0;
}

bool HDLevelMirrorAt(int at, HDLevelMirror* out)
{
	HDMirror m;

	if (!out || !HDLevelActive() || !HDRenderMirrorInfo(at, &m))
		return false;

	out->room = m.group;
	out->axis = m.axis;
	out->side = m.side;
	out->plane = (long)m.plane;

	for (int k = 0; k < 3; k++)
	{
		out->mins[k] = (long)floorf(m.mins[k]);
		out->maxs[k] = (long)ceilf(m.maxs[k]);
	}

	return true;
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

	HDRenderWaterFlipRoom(roomIndex, otherIndex);
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
	g_stand_frame_count = 0;
	g_piece_draw_count = 0;
	g_projection_taken = false;
	// Whatever glow phase was still owing belongs to a frame that is over.
	// A new frame's HDFrameDraw sets it again.
	g_frame_pending = false;
	HDObjectFrameReset();
}

void HDFrameNoteReach(int roomIndex, const short* box)
{
	if (roomIndex < 0 || roomIndex >= HD_MAX_ROOMS || !box)
		return;

	for (int k = 0; k < 4; k++)
		g_room_reach[roomIndex][k] = box[k];
}

void HDFrameNoteDump()
{
	g_reachart_due = true;
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
	frame.clock = (float)g_drawn_clock;
	frame.motion = tomb5.hd_motion ? 1 : 0;
	frame.reflect = tomb5.hd_reflect ? 1 : 0;
	frame.bookClock = g_drawn_clock;

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

/* A room drawn classic that holds stand art (g_group_stand), on this frame's
 * list: noted with its matrix and window for DecideStand. A room the art
 * covers is drawn as the art, so the answer is true and InsertRoom draws none
 * of its roomlets; every other one answers false, as a classic room always
 * did, and its roomlets are drawn - for a room the carrier covers in part
 * without the covered faces (HDRoomletDropped). A room drawn as art, wholly
 * or in part, may need the carrier's art in its window with no HD room on
 * the list, so it takes the projection. */
static bool StandHolderRoom(int roomIndex, const float* viewWorld,
	int left, int top, int right, int bottom)
{
	if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)
		return false;

	const int group = g_room_group[roomIndex];

	if (group < 0 || group >= HD_MAX_ROOMS || !g_group_stand[group])
		return false;

	if (g_stand_frame_count >= HD_MAX_FRAME)
		return false;	// one entry per listed room, as for g_frame

	StandHolderFrame& entry = g_stand_frame[g_stand_frame_count++];
	FrameRoom window;

	entry.room = roomIndex;
	entry.group = group;
	SetD3DMatrix(&entry.world, (float*)viewWorld);
	ClipWindow(window, left, top, right, bottom);
	entry.x0 = window.x0;
	entry.y0 = window.y0;
	entry.x1 = window.x1;
	entry.y1 = window.y1;
	entry.full = window.full;

	if (!g_group_stand_covered[group] && !g_group_stand_part[group])
		return false;

	HDFrameCaptureProjection();
	return g_group_stand_covered[group] != 0;
}

bool HDFrameRoom(int roomIndex, const float* viewWorld,
	int left, int top, int right, int bottom)
{
	if (!viewWorld)
		return false;

	const int group = GroupOfRoom(roomIndex);

	if (group < 0)
	{
		// A room the visibility path leaves out: none of its roomlets.
		if (VisHidesClassicRoom(roomIndex))
			return true;

		return StandHolderRoom(roomIndex, viewWorld, left, top, right, bottom);
	}

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

/* The sea seen from below, drawn by HDFrameSky right after the sky
 * (HDRenderWaterLook, which answers false at once outside a level with the
 * deep look or with tomb5.hd_water off). The eye under the water is
 * HDFrameDraw's own test on the room the drawn eye stands in - a water
 * room the under look holds (HDRenderWaterUnderOn) - and world up in view
 * space is the sky's view's second row, as HDFrameDraw takes it from its
 * first room's; the sky's block takes those fields in a copy, so the dome
 * it has just drawn is untouched. */
static bool HDFrameSkyLook(HDFrameLighting frame, const D3DMATRIX& world)
{
	const short eye_room = InterpEyeRoom(camera.pos.room_number);

	if (HDObjectRingFrame() || eye_room < 0 || eye_room >= number_rooms ||
		!(room[eye_room].flags & ROOM_UNDERWATER) ||
		!HDRenderWaterUnderOn(eye_room))
		return false;

	const float len = sqrtf(world._21 * world._21 + world._22 * world._22 +
		world._23 * world._23);

	if (len <= 0.0f)
		return false;

	frame.under = 1;
	frame.underRoom = eye_room;
	frame.underEyeY = (float)camera.pos.y;
	frame.underUp[0] = -world._21 / len;
	frame.underUp[1] = -world._22 / len;
	frame.underUp[2] = -world._23 / len;

	const bool drew = HDRenderWaterLook(&frame, (const float*)&world,
		(float)f_persp, (float)f_centerx, (float)f_centery,
		(float)camera.pos.x, (float)camera.pos.z);

	/* WATERLOG, with -log, once per level: the first frame the look is
	 * drawn, the room and the eye. The level's first sky frame comes before
	 * HDFrameSky's own line sets g_sky_logged, so that frame starts the
	 * level's count afresh. */
	static bool logged = false;

	if (!g_sky_logged)
		logged = false;

	if (gLogEnabled && drew && !logged)
	{
		logged = true;
		Log("WATERLOG level=%s look room=%d eye=%ld,%ld,%ld", g_stem,
			(int)eye_room, (long)camera.pos.x, (long)camera.pos.y,
			(long)camera.pos.z);
	}

	return drew;
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
	if (!viewWorld || !HDLevelActive())
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
	frame.clock = (float)g_drawn_clock;
	frame.motion = tomb5.hd_motion ? 1 : 0;
	frame.reflect = tomb5.hd_reflect ? 1 : 0;
	frame.bookClock = g_drawn_clock;

	SetD3DMatrix(&world, (float*)viewWorld);

	HDRenderFrameBegin(&kIdentity, &projection, &frame);
	const bool drew = HDRenderSky(&world);

	if (drew)
		HDFrameSkyLook(frame, world);

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

/* Asked by SkyDrawPhase after HDFrameSky answered true, so the HD level is
 * built and its sky groups are the ones just drawn. */
bool HDFrameSkyCoversLayers()
{
	return HDRenderSkyCoversLayers();
}

/* The classic flat layers draw the level's own sky page, Textures[nTextures
 * - 1] (specificfx.cpp, DrawFlatSky); over an HD sky with a cloud sheet that
 * entry holds the sheet's HD page for as long as the layers' one OutputSky
 * runs, and the classic page goes back right after. SkyDrawPhase draws the
 * layers on the flat path while the page is lent, also with PSX_skies on,
 * whose tiles take the SKY_GRAPHICS sprites' own pages instead. The
 * reference stays the page cache's: nothing is added to the classic array
 * or counted. The layers keep the classic motion (UpdateSky), quads and
 * colour; the page brings its own alpha, so the clouds stand over the dome
 * instead of the opaque classic page covering it. */
static LPDIRECT3DTEXTURE9 g_sky_page_kept = 0;
static bool g_sky_page_lent = false;

bool HDFrameSkyLayerPage(bool lend)
{
	if (!lend)
	{
		if (g_sky_page_lent && nTextures > 0)
			Textures[nTextures - 1].tex = g_sky_page_kept;

		g_sky_page_lent = false;
		return false;
	}

	IDirect3DTexture9* const page = HDLevelActive() ? HDRenderSkyLayerPage()
		: 0;

	if (!page || nTextures < 1 || g_sky_page_lent)
		return false;

	g_sky_page_kept = Textures[nTextures - 1].tex;
	Textures[nTextures - 1].tex = page;
	g_sky_page_lent = true;
	return true;
}

/* The projected shadow's cast pass: Lara's parts that her main draw marked,
 * into the map, as seen along the frame's key light. Inside the frame's
 * bracket and after the room loops, so the device is in the HD path's state;
 * HDRenderShadowCastBegin and End leave it as they found it. Nothing at all
 * happens in a frame the projected shadow does not run in. */
static HDShadowCastStats g_shadow_stats = { 0, 0, 0, 0.0, 0, 0, 0.0, 0, 0, 0, 0.0, 0, 0, 0, 0,
	0, 0.0, 0 };

/* Whether this frame's map was drawn: the receive pass reads the map only
 * then, never a map an earlier frame left behind. */
static bool g_shadow_cast_now = false;

/* The same for the second map (ShadowOthersPass). */
static bool g_others_cast_now = false;

/* Whether the rooms' receive pass ran in this frame, and with the second
 * map: the models' pass takes the maps only then, the same maps. */
static bool g_receive_now = false;
static bool g_receive_others_now = false;

/* Whether a mirror's room group took the maps through its mirror's blocks in
 * this frame's receive pass, for the log's mirror= count. */
static bool g_receive_mirror_now = false;

static void ShadowCastPass()
{
	const HDShadowFrame* shadow = HDShadowFrameState();

	g_shadow_cast_now = false;
	g_receive_now = false;
	g_receive_others_now = false;
	g_receive_mirror_now = false;

	if (!shadow->runs)
		return;

	g_shadow_stats.frames++;

	if (shadow->presence < 1.0f)
		g_shadow_stats.arriving++;

	if (!HDObjectCastCount())
		return;

	LARGE_INTEGER from, to, rate;

	QueryPerformanceCounter(&from);

	if (!HDRenderShadowCastBegin())
		return;

	HDModelCastBegin(shadow->castClip);
	g_shadow_stats.parts += HDObjectCastFrame();
	HDModelCastEnd();
	HDRenderShadowCastEnd();

	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&rate);

	g_shadow_stats.cast++;
	g_shadow_cast_now = true;

	if (rate.QuadPart)
		g_shadow_stats.ms += (double)(to.QuadPart - from.QuadPart) * 1000.0 /
			(double)rate.QuadPart;
}

/* The second map's cast pass: the items whose blob gave way and whose place
 * lies inside the second box grown by OTHERS_MARGIN on all six sides
 * (hd_object.cpp marks them HD_OBJ_CAST_OTHERS, or HD_OBJ_CAST_OTHERS_ONLY
 * for one the picture clipped away), into the second map, along the same
 * light, with the same cast draw her parts use. Right after hers, in the
 * same bracket. Only in a frame in which the projected shadow runs, her step
 * ran (the box exists) and at least one item is marked; in every other frame
 * the second map is neither drawn nor read, and the frame is the frame it is
 * without it. */
static void ShadowOthersPass()
{
	const HDShadowFrame* shadow = HDShadowFrameState();

	g_others_cast_now = false;

	if (!shadow->runs || !shadow->stepped)
		return;

	const int items = HDObjectCastOthersCount();

	if (!items)
		return;

	LARGE_INTEGER from, to, rate;

	QueryPerformanceCounter(&from);

	if (!HDRenderShadowOthersCastBegin())
		return;

	HDModelCastBegin(shadow->othersCastClip);
	g_shadow_stats.othersModels += HDObjectCastOthersFrame();
	HDModelCastEnd();
	HDRenderShadowCastEnd();

	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&rate);

	g_shadow_stats.othersCast++;
	g_shadow_stats.othersItems += items;
	g_shadow_stats.castOnly += HDObjectCastUnseenCount();
	g_others_cast_now = true;

	if (rate.QuadPart)
		g_shadow_stats.othersMs += (double)(to.QuadPart - from.QuadPart) * 1000.0 /
			(double)rate.QuadPart;
}

/* One room's art again for the receive, in exactly the pieces and windows
 * DrawRoomWindowed drew it with, so every triangle is rasterised inside the
 * scissor it wrote its depth in. */
static void DrawRoomShadowWindowed(const FrameRoom& entry)
{
	if (!entry.windowed || entry.full || !HDRenderScissorAvailable())
	{
		HDRenderScissorOff();
		HDRenderRoomShadow(entry.group, &entry.world, &entry.lighting, HD_WINDOW_ALL);
		return;
	}

	HDRenderScissorOff();
	HDRenderRoomShadow(entry.group, &entry.world, &entry.lighting, HD_WINDOW_OPEN);

	if (!ClipEmpty(entry))
	{
		HDRenderScissor(entry.x0, entry.y0, entry.x1, entry.y1);
		HDRenderRoomShadow(entry.group, &entry.world, &entry.lighting,
			HD_WINDOW_INTRUDING);
		HDRenderScissorOff();
	}
}

/* A mirror's two blocks and its plane, out of the frame's (hd_shadow.h,
 * HDShadowMirrorView, HDShadowMirrorPlane): her block silenced in a frame her
 * map was not drawn in, the second in one the second map was not drawn in.
 * False for a mirror seen from behind. */
static bool ShadowMirrorBlocks(const HDShadowFrame* shadow, const HDMirror& m, bool hers,
	bool others, float* regs, float* oregs, float* plane)
{
	if (!HDShadowMirrorFaces(shadow->R, shadow->t, m.axis, m.plane, m.side))
		return false;

	float R[9], t[3];

	HDShadowMirrorView(shadow->R, shadow->t, m.axis, m.plane, R, t);
	HDShadowReceiveConstants(&shadow->box, R, t, shadow->strength, regs);

	if (!hers)
		HDShadowReceiveWithoutHer(regs);

	HDShadowReceiveOthersConstants(&shadow->others, R, t, oregs);

	if (!others)
		HDShadowReceiveWithoutOthers(oregs);

	HDShadowMirrorPlane(shadow->R, shadow->t, m.axis, m.plane, m.side, plane);
	return true;
}

/* The projected shadow's receive pass: every HD room group of this frame
 * whose box comes within the shadow's reach (HDShadowReach - her map's
 * cross-section along the light, down to where the height term ends) is
 * drawn again with the receive shader. The statics baked into a group
 * receive with it. Only in a frame whose map was drawn just now.
 *
 * In a frame whose second map was drawn too, the shader is the two-map
 * variant, and a group within the second map's reach (HDShadowReachOthers)
 * is drawn again as well. A frame whose second map was not drawn runs the
 * one-map receive alone, with the very calls it makes without the second
 * map. Should her map not have been drawn while the second was, her block
 * is made to give nothing (HDShadowReceiveWithoutHer), so an earlier
 * frame's map in her target is never read.
 *
 * A mirror's room group (hd_render.h, HDMirror) is drawn twice: first with
 * the mirror's variant and the mirror's blocks (ShadowMirrorBlocks), which
 * darkens the copy behind the glass with the shadows in front of it,
 * reflected, and marks those pixels in the stencil; then as every group, which
 * the stencil keeps off the copy. Only while the mirror is seen from its
 * room's side, the variant exists and the stencil guards - otherwise the copy
 * takes the ordinary receive. A frame without a mirror's group in the pass
 * binds nothing new. */
static void ReceiveStandInHolders();
static void DrawStaticPieces(unsigned int passes, bool receive);

static void ShadowReceivePass()
{
	const HDShadowFrame* shadow = HDShadowFrameState();
	const bool others = g_others_cast_now && HDRenderShadowReceive2Ready();

	if (!shadow->runs || !(g_shadow_cast_now || others))
		return;

	LARGE_INTEGER from, to, rate;

	QueryPerformanceCounter(&from);

	float regs[HD_SHADOW_RECEIVE_COUNT * 4];
	float oregs[HD_SHADOW_RECEIVE2_COUNT * 4];
	float reachMin[3], reachMax[3];
	float otherMin[3], otherMax[3];

	HDShadowReceiveConstants(&shadow->box, shadow->R, shadow->t, shadow->strength, regs);
	HDShadowReach(&shadow->box, reachMin, reachMax);

	if (others)
	{
		if (!g_shadow_cast_now)
			HDShadowReceiveWithoutHer(regs);

		HDShadowReceiveOthersConstants(&shadow->others, shadow->R, shadow->t, oregs);
		HDShadowReachOthers(&shadow->others, otherMin, otherMax);
	}

	if (!(others ? HDRenderShadowReceive2Begin(regs, oregs) : HDRenderShadowReceiveBegin(regs)))
		return;

	g_receive_now = true;
	g_receive_others_now = others;

	for (int i = 0; i < g_frame_count; i++)
	{
		const FrameRoom& entry = g_frame[i];
		float mins[3], maxs[3];

		if (!HDRenderGroupBounds(entry.group, mins, maxs))
			continue;

		const float origin[3] =
		{
			entry.lighting.originX, entry.lighting.originY, entry.lighting.originZ
		};

		for (int k = 0; k < 3; k++)
		{
			mins[k] += origin[k];
			maxs[k] += origin[k];
		}

		const bool hers = g_shadow_cast_now && HDShadowBoxesTouch(mins, maxs, reachMin, reachMax);

		if (!hers && !(others && HDShadowBoxesTouch(mins, maxs, otherMin, otherMax)))
			continue;

		// A mirror's group: the copy behind its glass first.
		for (int m = 0; m < HDRenderMirrorCount(); m++)
		{
			HDMirror mirror;
			float mregs[HD_SHADOW_RECEIVE_COUNT * 4];
			float moregs[HD_SHADOW_RECEIVE2_COUNT * 4];
			float plane[4];

			if (!HDRenderMirrorInfo(m, &mirror) || mirror.group != entry.group ||
				!ShadowMirrorBlocks(shadow, mirror, g_shadow_cast_now, others, mregs, moregs,
					plane) ||
				!HDRenderShadowReceiveMirrorBegin(mregs, moregs, plane))
				continue;

			DrawRoomShadowWindowed(entry);
			HDRenderShadowReceiveMirrorEnd();
			g_receive_mirror_now = true;
		}

		DrawRoomShadowWindowed(entry);
		g_shadow_stats.rooms++;

		if (others)
			g_shadow_stats.othersRooms++;
	}

	// And the art drawn in the window of a room drawn as art, and the pieces.
	ReceiveStandInHolders();
	DrawStaticPieces(0, true);
	g_shadow_stats.calls += HDRenderShadowReceiveCalls();
	HDRenderShadowReceiveEnd();
	HDRenderScissorOff();

	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&rate);

	if (rate.QuadPart)
		g_shadow_stats.receiveMs += (double)(to.QuadPart - from.QuadPart) * 1000.0 /
			(double)rate.QuadPart;
}

/* The projected shadow on the HD models: after the models' solid pass, in a
 * frame whose rooms' receive ran, every entry of the frame list that takes
 * one of the maps the rooms took and comes within its reach is drawn again
 * with the model receive shader (hd_object.cpp, HDObjectShadowReceiveFrame;
 * which entry takes which map, hd_shadow.h's HDShadowReceiverTakes). After
 * the solid pass, because the test is EQUAL against the depth each model
 * has just written; before DrawBuckets, so a classic polygon drawn over a
 * model keeps its own picture. A frame without the rooms' receive draws
 * nothing here and binds nothing. A mirror's reflections take the maps
 * through their mirror's blocks (HDObjectShadowReceiveFrame). The entries
 * drawn again and their draw calls are counted for the log, with the frame
 * when a mirror's image took the maps - its room group in the receive pass
 * or a reflection here. */
static void ShadowModelsPass()
{
	int models = 0, calls = 0, reflections = 0;
	LARGE_INTEGER from, to, rate;

	QueryPerformanceCounter(&from);

	if (g_receive_now && HDModelReceiveReady() &&
		HDRenderShadowModelsBegin(g_shadow_cast_now, g_receive_others_now))
	{
		HDModelReceiveBegin();
		models = HDObjectShadowReceiveFrame(g_shadow_cast_now, g_receive_others_now,
			&reflections);
		calls = HDModelReceiveCalls();
		HDModelReceiveEnd();
		HDRenderShadowModelsEnd();
	}

	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&rate);

	g_shadow_stats.models += models;
	g_shadow_stats.modelCalls += calls;

	if (g_receive_now && rate.QuadPart)
		g_shadow_stats.modelsMs += (double)(to.QuadPart - from.QuadPart) *
			1000.0 / (double)rate.QuadPart;

	if (g_receive_mirror_now || reflections)
		g_shadow_stats.mirror++;
}

/* HDLOG shadow second - once a second of game time while the projected
 * shadow runs, behind -log:
 *
 *   dir=       the drawn direction of the light, world axes (y down)
 *   target=    the key light's direction this frame, the fixed slant while
 *              HD_SHADOW_PULL is 0; the drawn one differs from it only while
 *              a change of the lights blends in
 *   strength=  the drawn strength / the target's, DARK * LOW while the pull
 *              is 0
 *   lights=    lights of her list that gave a weight this frame, before the
 *              pull
 *   weight=    their summed weight (luminance times falloff), before the
 *              pull
 *   slant=     degrees between the drawn direction and straight down
 *   snaps=     snaps of the following in that second (a reset, a gap, a jump)
 *   cast=      frames whose map was drawn / frames the shadow ran
 *   parts=     models into the map, a frame (the mean over cast frames)
 *   rooms=     room groups drawn again, a frame
 *   calls=     receive draw calls, a frame
 *   ms=        processor time of the two passes, a frame (the mean)
 *   stencil=   1 when the depth surface's stencil guards each pixel
 *   lag=       the largest angle in that second between the drawn direction
 *              and the frame's target, degrees: 0 while the pull is 0; above
 *              it, near 0 while only she moves, up at a change of the lights
 *              and back down over about a second and a half
 *   others=    items cast into the second map, a frame (the mean over the
 *              frames whose second map was drawn); 0.0 in a second without
 *              one
 *   omap=      frames whose second map was drawn / frames the shadow ran
 *   orooms=    room groups drawn again, a frame, in the frames whose second
 *              map was read (the mean over those frames) - hers and those
 *              only the second map's reach brings in
 *   oms=       processor time of the second map's cast pass, a frame (the
 *              mean over the frames the shadow ran); its receive is in ms=
 *   models=    entries of the frame list the models' pass drew again - Lara,
 *              a creature, an item, a reflection - a frame (the mean over the
 *              frames the shadow ran)
 *   mcalls=    the models' pass's draw calls, a frame (the same mean)
 *   castonly=  items cast into the second map that the picture clipped away,
 *              a frame (the same mean)
 *   mirror=    frames in which a mirror's image took the maps through its
 *              mirror's blocks (its room group's copy or a reflection) /
 *              frames the shadow ran
 *   mms=       processor time of the models' pass, a frame (the mean over the
 *              frames the shadow ran)
 *   arrive=    frames in which the shadow was still coming in and the blobs
 *              going out (hd_shadow.h, HDShadowArrive) / frames the shadow ran:
 *              the first quarter second after it starts to run */
static bool g_shadow_window = false;
static unsigned short g_shadow_window_at = 0;

static void ShadowSecond()
{
	const HDShadowFrame* shadow = HDShadowFrameState();

	if (!shadow->runs)
	{
		g_shadow_window = false;
		return;
	}

	const unsigned short tick = GlobalCounter;

	if (!g_shadow_window)
	{
		g_shadow_window = true;
		g_shadow_window_at = tick;
		HDFrameShadowCastStats(0, true);
		HDShadowFollowState()->snaps = 0;
		HDShadowFollowState()->lag = 0.0f;
		return;
	}

	if ((unsigned short)(tick - g_shadow_window_at) < 30)
		return;

	HDShadowCastStats s;

	HDFrameShadowCastStats(&s, true);

	HDShadowFollow* follow = HDShadowFollowState();

	if (gLogEnabled)
	{
		const double frames = s.frames > 0 ? (double)s.frames : 1.0;
		const double cast = s.cast > 0 ? (double)s.cast : 1.0;
		const double ocast = s.othersCast > 0 ? (double)s.othersCast : 1.0;

		Log("HDLOG shadow second dir=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) "
			"strength=%.3f/%.3f lights=%d weight=%.3f slant=%.1f snaps=%d cast=%d/%d "
			"parts=%.1f rooms=%.1f calls=%.1f ms=%.3f stencil=%d lag=%.2f "
			"others=%.1f omap=%d/%d orooms=%.1f oms=%.3f models=%.1f mcalls=%.1f "
			"castonly=%.1f mirror=%d/%d mms=%.3f arrive=%d/%d",
			shadow->dir[0], shadow->dir[1], shadow->dir[2], shadow->target.dir[0],
			shadow->target.dir[1], shadow->target.dir[2], shadow->strength,
			shadow->target.strength, shadow->target.lights, shadow->target.weight,
			HDShadowSlant(shadow->dir), follow->snaps, s.cast, s.frames,
			(double)s.parts / cast, (double)s.rooms / cast, (double)s.calls / cast,
			(s.ms + s.receiveMs) / frames, HDRenderShadowReceiveStencil() ? 1 : 0,
			follow->lag, (double)s.othersItems / ocast, s.othersCast, s.frames,
			(double)s.othersRooms / ocast, s.othersMs / frames, (double)s.models / frames,
			(double)s.modelCalls / frames, (double)s.castOnly / frames,
			s.mirror, s.frames, s.modelsMs / frames, s.arriving, s.frames);
	}

	follow->snaps = 0;
	follow->lag = 0.0f;
	g_shadow_window_at = tick;
}

void HDFrameShadowCastStats(HDShadowCastStats* out, bool clear)
{
	if (out)
		*out = g_shadow_stats;

	if (clear)
		memset(&g_shadow_stats, 0, sizeof(g_shadow_stats));
}

// The room the frame draws `group` for, -1 while a flip draws another
// group in its place.
static int RoomOfGroup(int group)
{
	for (int r = 0; r < g_room_count; r++)
		if (g_room_group[r] == group)
			return r;

	return -1;
}

/* This frame's answer for every stand range (HDStandDraw in hd_cover.h):
 * whether its holder is on the list (g_stand_frame), whether its own group
 * is (g_frame), and whether the art covers the holder. The answer for
 * drawing with its group goes to the renderer for both phases; a range to be
 * drawn in its holder's window gets an entry of g_stand_draw with its
 * group's matrix - the holder's view * world moved by the difference of the
 * two rooms' origins, since a flipmap pair stands at one place - and its
 * group's lighting, so that it looks the same whichever room brings it. */
static void DecideStand()
{
	g_stand_draw_count = 0;
	g_clip_stand_with = 0;
	g_clip_stand_holder = 0;
	g_clip_stand_out = 0;
	g_clip_stand_dropped = 0;
	g_clip_stand_behind = 0;

	for (size_t p = 0; p < g_stand_pieces.size(); p++)
	{
		const StandPiece& piece = g_stand_pieces[p];
		int holderAt = -1;
		bool artListed = false;

		for (int i = 0; i < g_stand_frame_count && holderAt < 0; i++)
			if (g_stand_frame[i].group == piece.holder)
				holderAt = i;

		for (int i = 0; i < g_frame_count && !artListed; i++)
			artListed = g_frame[i].group == piece.group;

		const int state = g_group_stand_covered[piece.holder] ? HD_STAND_ROOM_COVERED :
			g_group_stand_part[piece.holder] ? HD_STAND_ROOM_PART : HD_STAND_ROOM_CLASSIC;
		const HDStandAnswer answer = HDStandDrawFaces(holderAt >= 0, artListed, state,
			g_group_stand_carrier[piece.holder] == piece.group, piece.kind, piece.faces);

		HDRenderStandWith(piece.group, piece.index, answer.behind && answer.withArt ?
			HD_STAND_BEHIND : answer.withArt ? HD_STAND_WITH : HD_STAND_OUT);

		if (answer.withArt)
			g_clip_stand_with++;
		else if (!answer.inHolder && artListed)
			g_clip_stand_out++;

		if (answer.behind && answer.withArt)
			g_clip_stand_behind++;

		if (!answer.inHolder)
			continue;

		const StandHolderFrame& holder = g_stand_frame[holderAt];
		const int room_of_art = RoomOfGroup(piece.group);

		if (holder.x1 <= holder.x0 || holder.y1 <= holder.y0 || room_of_art < 0 ||
			piece.group >= (int)g_stand_rooms.size() ||
			piece.holder >= (int)g_stand_rooms.size())
			continue;	// an empty window draws nothing of the room

		if (g_stand_draw_count >= HD_MAX_STAND_DRAW)
		{
			g_clip_stand_dropped++;
			continue;
		}

		StandDraw& draw = g_stand_draw[g_stand_draw_count++];
		const D3DMATRIX& m = holder.world;
		float off[3];

		for (int k = 0; k < 3; k++)
			off[k] = g_stand_rooms[(size_t)piece.group].origin[k] -
				g_stand_rooms[(size_t)piece.holder].origin[k];

		draw.group = piece.group;
		draw.index = piece.index;
		draw.behind = answer.behind;
		draw.world = m;
		draw.world._41 = off[0] * m._11 + off[1] * m._21 + off[2] * m._31 + m._41;
		draw.world._42 = off[0] * m._12 + off[1] * m._22 + off[2] * m._32 + m._42;
		draw.world._43 = off[0] * m._13 + off[1] * m._23 + off[2] * m._33 + m._43;
		draw.world._44 = off[0] * m._14 + off[1] * m._24 + off[2] * m._34 + m._44;
		draw.x0 = holder.x0;
		draw.y0 = holder.y0;
		draw.x1 = holder.x1;
		draw.y1 = holder.y1;
		draw.full = holder.full;
		FillRoomLighting(room_of_art, piece.group, draw.lighting, draw.lights);
		g_clip_stand_holder++;
	}
}

/* The stand ranges of g_stand_draw, the passes the mask names, each inside its
 * holder's portal window. */
static void DrawStandInHolders(unsigned int passes)
{
	for (int i = 0; i < g_stand_draw_count; i++)
	{
		const StandDraw& draw = g_stand_draw[i];

		if (passes == HD_PASS_CLEAR && !HDRenderGroupHasClear(draw.group))
			continue;

		if (draw.full || !HDRenderScissorAvailable())
			HDRenderScissorOff();
		else
			HDRenderScissor(draw.x0, draw.y0, draw.x1, draw.y1);

		HDRenderRoomStand(draw.group, &draw.world, &draw.lighting, passes,
			draw.index, draw.behind);
	}

	HDRenderScissorOff();
}

/* The static pieces this frame noted (NoteStaticPiece), the passes the mask
 * names, each inside its static's room's window; with `receive` the same
 * triangles in the projected shadow's receive instead, so that her shadow
 * falls on a palm's trunk drawn as its piece as it does on the art that holds
 * it. Their group's lighting is filled once, before the solid pass. */
static void DrawStaticPieces(unsigned int passes, bool receive)
{
	for (int i = 0; i < g_piece_draw_count; i++)
	{
		PieceDraw& draw = g_piece_draw[i];

		if (draw.x1 <= draw.x0 || draw.y1 <= draw.y0)
			continue;	// an empty window draws nothing of the room

		if (!receive && (passes & HD_PASS_SOLID))
			FillRoomLighting(draw.roomOfArt, draw.group, draw.lighting, draw.lights);

		if (draw.full || !HDRenderScissorAvailable())
			HDRenderScissorOff();
		else
			HDRenderScissor(draw.x0, draw.y0, draw.x1, draw.y1);

		if (receive)
			HDRenderStaticPieceShadow(draw.piece, &draw.world, &draw.lighting);
		else
			HDRenderStaticPiece(draw.piece, &draw.world, &draw.lighting, passes);
	}

	HDRenderScissorOff();
}

/* The same draws once more in the projected shadow's receive, with the
 * matrix, lighting, depth push and window they were drawn with, so that her
 * shadow lands on a room drawn as art whose carrier is off the list as it
 * does on the carrier's own art. */
static void ReceiveStandInHolders()
{
	for (int i = 0; i < g_stand_draw_count; i++)
	{
		const StandDraw& draw = g_stand_draw[i];

		if (draw.full || !HDRenderScissorAvailable())
			HDRenderScissorOff();
		else
			HDRenderScissor(draw.x0, draw.y0, draw.x1, draw.y1);

		HDRenderRoomShadowStand(draw.group, &draw.world, &draw.lighting, draw.index,
			draw.behind);
	}
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
	/* The water's life onto the frame's object list, before it is counted;
	 * at the clock of the frame before (FillFrameLighting steps it below),
	 * one frame's lag nobody sees. Not in the inventory ring, which draws
	 * through a view of its own, and not while a menu stands over a picture
	 * of the world (MonoScreenOn, specific\LoadSave.cpp, CreateMonoScreen:
	 * the pause menu, the inventory, the load and save screens): the world
	 * stands still there and no room is drawn, so nothing in the depth
	 * buffer would hide a school behind the walls of the picture under it. */
	if (!HDObjectRingFrame() && !MonoScreenOn)
		HDLifeFrame(g_drawn_clock);

	const int objects_here = HDObjectFrameCount();

	g_frame_objects = objects_here;
	g_frame_pending = false;

	/* A room drawn as art, wholly or in part, listed with no HD room is
	 * drawn by DrawStandInHolders below; StandHolderRoom took the
	 * projection for it. */
	if (!g_frame_count && !objects_here &&
		!(g_stand_frame_count && g_projection_taken))
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

	/* The eye under the water (hd_render.cpp, "under the water"): in a water
	 * room of a level with sheets the scene fades with the distance into the
	 * water's own colour, and that takes the place of the distance fog and
	 * of the fog bulbs - ANDREA2's sixteen bulbs in its water rooms lay a
	 * flat pale haze over every pixel there (the rooms' headers, read with
	 * the randomizer's Tr5Level). Not in the inventory ring, which draws
	 * through a view of its own. World up in view space is the second row
	 * of a room's view matrix, negated: every room of the frame shares the
	 * camera's turn.
	 *
	 * A water room is one whose own flags carry ROOM_UNDERWATER: the test
	 * camera.cpp makes for the camera's room, and draw.cpp for the frame's.
	 * camera.underwater, the answer camera.cpp keeps, is not asked. It is 0
	 * for the whole of JOBY4 (LVL5_DEEPSEA_DIVE) - the classic engine keeps
	 * its underwater sound and screen wobble out of the dive -, it is set
	 * before the camera moves, a logic tick behind the room the frame is
	 * drawn from, and a fly-by moves the camera without setting it. The
	 * screen wobble (FillFrameLighting) reads it, as the classic faces of
	 * the same frame do.
	 *
	 * The room asked is the one the drawn eye stands in (InterpEyeRoom), the
	 * room the frame's portal walk starts from, and not the logic room. On a
	 * frame blended between two ticks the eye is drawn between the two tick
	 * eyes while the logic room is already the newer one's: at a dive the
	 * eye still stands over the surface while the room says water, and on
	 * the way out it is still under while the room says air - one frame of
	 * the other look, the under look over the open air or the classic fog
	 * bulbs' pale haze round an eye in the water. In the camera's swims of
	 * an ANDREA1 run (16 tick pairs crossing the surface) 11 of the 16
	 * frames a 60 Hz draw blends between them, and 32 of 48 at 120 Hz, put
	 * the eye on the other side of the surface from the logic room. On
	 * every frame not blended InterpEyeRoom answers the logic room. */
	const short eye_room = InterpEyeRoom(camera.pos.room_number);
	const bool eye_wet = eye_room >= 0 && eye_room < number_rooms &&
		(room[eye_room].flags & ROOM_UNDERWATER) != 0;

	if (!HDObjectRingFrame() && eye_wet && g_frame_count &&
		HDRenderWaterUnderOn(eye_room))
	{
		const D3DMATRIX& m = g_frame[0].world;
		const float len = sqrtf(m._21 * m._21 + m._22 * m._22 +
			m._23 * m._23);

		if (len > 0.0f)
		{
			frame.under = 1;
			frame.underRoom = eye_room;
			frame.underEyeY = (float)camera.pos.y;
			frame.underUp[0] = -m._21 / len;
			frame.underUp[1] = -m._22 / len;
			frame.underUp[2] = -m._23 / len;
			frame.bulbCount = 0;
		}
	}

	/* WATERLOG, with -log, once per level: the first frame outside the
	 * inventory ring whose camera room is a water room writes
	 *
	 *   WATERLOG level=<stem> undergate room=<n> flag=<0|1> cam=<0|1>
	 *            on=<0|1> deep=<0|1>
	 *
	 * room the room the drawn eye stands in, which the test above asks (the
	 * camera's logic room on a frame not blended); flag its ROOM_UNDERWATER,
	 * the test above (1 on this line); cam camera.underwater - 0 throughout
	 * JOBY4, and 0 as
	 * well on a frame the camera has just swum into, a tick behind; on 1
	 * when this frame is drawn under the water; deep 1 when the level takes
	 * the deep look (the level line's under_deep). on=0 with flag=1: the
	 * level built no sheets (tomb5.hd_water 0, a device without the water
	 * shaders, a level with no water square) or the frame drew no HD
	 * room. surface the room whose art is the sea's surface (SurfaceRoom,
	 * drawn at every frame below), -1 for none. */
	if (gLogEnabled && !g_undergate_logged && eye_wet && !HDObjectRingFrame())
	{
		HDRenderWaterLevel water;

		HDRenderWaterLevelInfo(&water);
		g_undergate_logged = true;
		Log("WATERLOG level=%s undergate room=%d flag=%d cam=%d on=%d "
			"deep=%d surface=%d", g_stem, (int)eye_room, eye_wet ? 1 : 0,
			camera.underwater ? 1 : 0, frame.under, water.underDeep,
			SurfaceRoom());
	}

	/* The sea's surface at every frame. In a level that takes the deep look
	 * the art of one room is the surface over the whole dive (SurfaceRoom),
	 * and that room is out of reach of the portals of the rooms the dive is
	 * swum in: JOBY4's room 19 comes onto the list only as a guest of room
	 * 100, so that by the list alone the surface would leave the picture
	 * and come back within a tick wherever room 100 is not walked. So the
	 * room's art joins every frame the list leaves it out of, with the
	 * whole screen as its window - no portal, list, window or budget
	 * decides it -, at the matrix of the frame's first room moved by the
	 * difference of the two rooms' origins (as DecideStand moves a stand
	 * range) and with its own lighting, under the frame's extinction like
	 * every listed room. Only the art: the room's classic faces, statics
	 * and items stay with the walk. Before DecideStand, which then counts
	 * the art as listed. */
	const int surface = SurfaceRoom();
	const int surfaceGroup = GroupOfRoom(surface);
	bool surfaceListed = surfaceGroup < 0;

	for (int i = 0; i < g_frame_count && !surfaceListed; i++)
		surfaceListed = g_frame[i].group == surfaceGroup;

	if (!surfaceListed && !HDObjectRingFrame() && g_frame_count > 0 &&
		g_frame_count < HD_MAX_FRAME)
	{
		const FrameRoom& first = g_frame[0];
		FrameRoom& entry = g_frame[g_frame_count++];
		const D3DMATRIX& m = first.world;
		const float off[3] = {
			(float)(room[surface].x - room[first.room].x),
			(float)(room[surface].y - room[first.room].y),
			(float)(room[surface].z - room[first.room].z) };

		entry.room = surface;
		entry.group = surfaceGroup;
		entry.view = first.view;
		entry.world = m;

		for (int k = 0; k < 4; k++)
			entry.world.m[3][k] = off[0] * m.m[0][k] + off[1] * m.m[1][k] +
				off[2] * m.m[2][k] + m.m[3][k];

		ClipWindow(entry, 0, 0, (int)phd_winwidth, (int)phd_winheight);
		entry.windowed = surfaceGroup < HD_MAX_ROOMS &&
			g_group_windowed[surfaceGroup] != 0;
		entry.scissoredTris = 0;
	}

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
	g_clip_reach_rooms = 0;
	g_clip_reach_pieces = 0;
	g_clip_reach_whole = 0;
	g_clip_reach_hidden = 0;
	g_clip_unlinked_pieces = 0;
	g_clip_unlinked_drawn = 0;
	g_clip_unlinked_hidden = 0;
	g_clip_items_spared = 0;	// counted by HDFrameScissorForRoom

	// The stand ranges' answers, for this phase and the glow one.
	DecideStand();

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
		 * intruding pieces DecidePairs did not answer whole - those in the
		 * smaller box - inside DrawRoomWindowed. */
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

	// The art that stands in a room drawn as art whose carrier is not listed,
	// and the statics' pieces whose holders are not.
	DrawStandInHolders(HD_PASS_SOLID);
	DrawStaticPieces(HD_PASS_SOLID, false);

	/* The clear floors, once every room's solid art is in, so that whatever
	 * lies behind a floor - the copy of the room beneath RICH2's marble, and
	 * any other room seen through it - is in the picture before the floor is
	 * laid over it; and before the models and the classic buckets, which
	 * stand on the floor and are drawn over it. See HD_PASS_CLEAR in
	 * hd_render.h. A room without such a floor is not asked. */
	for (int i = 0; i < g_frame_count; i++)
	{
		if (HDRenderGroupHasClear(g_frame[i].group))
			DrawRoomWindowed(g_frame[i], HD_PASS_CLEAR);
	}

	DrawStandInHolders(HD_PASS_CLEAR);
	DrawStaticPieces(HD_PASS_CLEAR, false);

	/* The rooms are done. The objects switch the scissor off per item, so
	 * this line is for the frame that has none: without it the last room's
	 * window would still be in force when the bracket closes, and the
	 * classic path must get the device state it had. */
	HDRenderScissorOff();

	/* The projected shadow's two maps, after every room's solid art and
	 * before the models: Lara's pose and the items' of this very drawn frame
	 * are on the list already. Then the rooms near her take them, before the
	 * models, so that no model is darkened by a room's draw - the models
	 * take the maps in a pass of their own after their solid one - and
	 * before DrawBuckets, so that no classic polygon is: both are drawn over
	 * the darkened rooms and keep their own picture. */
	ShadowCastPass();
	ShadowOthersPass();
	ShadowReceivePass();

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
	 * the whole object path does.
	 *
	 * Lara and the objects mark their pixels for the water sheets, which
	 * draw no waterline on them (hd_render.cpp, "the creatures' mark");
	 * nothing without sheets that read the depth texture. */
	if (gLogEnabled)
	{
		char line[256];

		if (HDRenderWaterMarkLine(g_stem, line, sizeof(line)))
			Log("%s", line);
	}

	HDRenderWaterMarkBegin();
	HDObjectFrameDraw(&projection, frame.bulbCount, HD_PASS_SOLID);
	HDRenderWaterMarkEnd(objects_here);

	/* And the models take the projected shadow, on the depth they have just
	 * written; the shadow's log line then has the whole frame's passes. */
	ShadowModelsPass();
	ShadowSecond();

	HDRenderFrameEnd();

	// The glow phase is owing, and this is what the rooms cost so far -
	// HDRenderFrameBegin zeroes the renderer's counter per bracket.
	g_frame_room_calls = HDRenderDrawCalls();
	g_frame_book_calls = HDRenderBookCalls();
	g_frame_clear_calls = HDRenderClearCalls();
	g_frame_sheen_calls = HDRenderSheenCalls();
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
/* The rats for the creatures' mark (hd_render.cpp): a swimming rat is drawn
 * by the classic path (DrawRats, phd_PutPolygons), between this frame's two
 * brackets and outside the mark, so the sheets drew their waterline and the
 * bank's foam over it - white rats in ANDY1's pool. Every live rat in a
 * water room of this frame's rooms is marked here with the mesh DrawRats
 * picks for it and the matrix it draws it with (the engine's own stack, the
 * view in aMXPtr as the HD objects take it), its points pulled toward the
 * eye by kMarkPull of their distance so that they pass the depth test
 * against the classic draw of the same mesh - the fraction the stand rule
 * pushes art back by (hd_render.h, HD_STAND_PUSH). Rats are the only
 * creatures the classic path draws that swim; bats and spiders do not.
 * Nothing without sheets (tomb5.hd_water 0 builds none). */
static void MarkWaterCreatures()
{
	const float kMarkPull = HD_STAND_PUSH;

	if (!Rats || !objects[RAT].loaded)
		return;

	std::vector<float> tris;

	for (int i = 0; i < 32; i++)
	{
		const RAT_STRUCT* fx = &Rats[i];

		if (!fx->On || fx->room_number < 0 ||
			fx->room_number >= number_rooms ||
			!(room[fx->room_number].flags & ROOM_UNDERWATER))
			continue;

		bool listed = false;

		for (int k = 0; k < g_frame_count && !listed; k++)
			listed = RoomOfGroup(g_frame[k].group) == fx->room_number;

		if (!listed)
			continue;

		// The frame DrawRats draws (game\rat.cpp).
		const int frame = (((wibble + (i << 2)) & 0x38) >> 2);
		const MESH_DATA* mesh =
			(const MESH_DATA*)meshes[objects[RAT].mesh_index + frame];

		if (!mesh || !mesh->aVtx || mesh->nVerts <= 0)
			continue;

		phd_PushMatrix();
		phd_TranslateAbs(fx->pos.x_pos, fx->pos.y_pos, fx->pos.z_pos);
		phd_RotYXZ(fx->pos.y_rot, fx->pos.x_rot, fx->pos.z_rot);

		const float* m = aMXPtr;
		const float keep = 1.0f - kMarkPull;
		auto put = [&](int v)
		{
			const ACMESHVERTEX& p = mesh->aVtx[v];

			tris.push_back((m[M00] * p.x + m[M01] * p.y + m[M02] * p.z +
				m[M03]) * keep);
			tris.push_back((m[M10] * p.x + m[M11] * p.y + m[M12] * p.z +
				m[M13]) * keep);
			tris.push_back((m[M20] * p.x + m[M21] * p.y + m[M22] * p.z +
				m[M23]) * keep);
		};

		// gt4: four corners and two words a quad; gt3: three and two.
		const short* quad = mesh->gt4;

		for (int q = 0; quad && q < mesh->ngt4; q++, quad += 6)
		{
			const int at[6] = { quad[0], quad[1], quad[2], quad[0], quad[2],
				quad[3] };

			for (int c = 0; c < 6; c++)
				if (at[c] >= 0 && at[c] < mesh->nVerts)
					put(at[c]);
				else
					put(0);
		}

		const short* tri = mesh->gt3;

		for (int t = 0; tri && t < mesh->ngt3; t++, tri += 5)
			for (int c = 0; c < 3; c++)
				put(tri[c] >= 0 && tri[c] < mesh->nVerts ? tri[c] : 0);

		phd_PopMatrix();
	}

	if (!tris.empty())
		HDRenderWaterMarkTriangles(&tris[0], (int)(tris.size() / 3));
}

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
	HDSpringCrackDraw();

	/* The frame behind the water sheets: every opaque thing of the frame is
	 * in the render target now - the HD rooms and objects, Lara and the
	 * classic buckets - and nothing of the blend phase yet. One copy, and
	 * only when a room of this frame has a sheet (none with tomb5.hd_water
	 * 0, which builds no sheet). A tank's film asks for it while it is
	 * wet, so its strength is set first. */
	if (HDRenderWaterSheetsBuilt())
		FeedWaterFilms();

	/* The tanks' wet floors' darkening, over the opaque frame; their mirror
	 * comes at the frame's end. The draw takes the frame's copy first, so
	 * the sheets drawn after it read that same copy and see the floor under
	 * them dry (hd_render.cpp, "the wet floor"). */
	if (HDRenderWaterSheetsBuilt())
	{
		float m[16];

		FeedWaterWet();
		WorldToFrame(m);
		HDRenderWaterWetDraw(m);
	}

	for (int i = 0; i < g_frame_count; i++)
		if (HDRenderGroupHasSheet(g_frame[i].group))
		{
			HDRenderWaterCopy();
			break;
		}
	HDSpringCopy();

	/* The game's own ripples for the rings on the sheets and Lara's place
	 * for their waterline, once a frame and only in a level that built
	 * sheets (none with tomb5.hd_water 0). */
	if (HDRenderWaterSheetsBuilt())
	{
		FeedWaterRipples();
		FeedWaterLara();
		MarkWaterCreatures();
	}

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

	DrawStandInHolders(HD_PASS_BLEND);
	DrawStaticPieces(HD_PASS_BLEND, false);

	for (int i = 0; i < g_frame_count; i++)
	{
		DrawRoomWindowed(g_frame[i], HD_PASS_ADDITIVE);
	}

	DrawStandInHolders(HD_PASS_ADDITIVE);
	DrawStaticPieces(HD_PASS_ADDITIVE, false);

	// The rooms are done - see the same line in HDFrameDraw.
	HDRenderScissorOff();

	/* The wet floors' mirror, after the rooms' glass and glows and before
	 * the cards and the objects' glows: the rooms' glows are in the copy it
	 * reads, and the dome, the spray and a glow drawn after it lie over it
	 * as over the floor (hd_render.cpp, "the wet floor"). */
	HDRenderMarbleDraw(HDObjectFrameDraw, &projection, frame.bulbCount);
	HDRenderWaterWetModels(HDObjectFrameDraw, &projection, frame.bulbCount);
	if (HDRenderWaterSheetsBuilt())
		HDRenderWaterWetMirror();

	DrawWaterCards();
	HDObjectFrameDraw(&projection, frame.bulbCount, HD_PASS_ADDITIVE);

	HDRenderFrameEnd();

	g_frame_room_calls += HDRenderDrawCalls();
	g_frame_book_calls += HDRenderBookCalls();
	HDObjectFrameEnd();

	if (!g_matrix_checked && gLogEnabled && g_frame_count)
	{
		g_matrix_checked = true;
		MatrixCheck(g_frame[0].room, g_frame[0].group, g_frame[0].world, projection);
		Log("HDLOG frame rooms=%d calls=%d dropped=%d bulbs=%d fog_start=%.0f "
			"wibble=%d cam_uw=%d shimmer=%d objects=%d object_calls=%d "
			"cutout_calls=%d cutout_pages=%d flipbook_calls=%d "
			"gloss_calls=%d water_calls=%d book_calls=%d clear_calls=%d "
			"sheen_calls=%d",
			g_frame_count, g_frame_room_calls, g_frame_dropped,
			frame.bulbCount, frame.distanceFogStart, (int)wibble,
			frame.cameraUnderwater, tomb5.shimmer ? 1 : 0,
			objects_here, HDObjectDrawCalls(),
			HDRenderCutoutCalls(), HDPageCacheCutoutPages(),
			HDRenderFlipbookCalls(), HDRenderGlossCalls(),
			HDRenderWaterCalls(), g_frame_book_calls, g_frame_clear_calls,
			g_frame_sheen_calls);
	}

	/* The followed book, page by page: the first book drawn in the level
	 * writes one line each time its page changes, for its first 31 pages -
	 * a whole turn of a 30-page book and the first page of the next. The
	 * frame line above is written once per level and cannot show a page
	 * turning. `clock` is the drawn state's time the page came from,
	 * `tick` the engine's GlobalCounter, `alpha` the blend factor the frame
	 * was drawn at and `present` the HD frames filled in this level: at 60
	 * frames a second the present column steps by 3 for a 0.05 s book and
	 * by 1, 2, 1, 2 for a 0.025 s one. Nothing is written with hd_motion 0,
	 * where no book turns. */
	if (gLogEnabled && g_bookstep_lines < 31)
	{
		int group = -1;
		int part = -1;
		int page = 0;

		if (HDRenderBookWatch(&group, &part, &page) && page != g_bookstep_page)
		{
			g_bookstep_page = page;
			g_bookstep_lines++;
			Log("HDLOG bookstep level=%s group=%d part=%d frame=%d clock=%.3f "
				"tick=%ld alpha=%.2f present=%ld", g_stem, group, part, page,
				g_drawn_clock, (long)GlobalCounter, g_drawn_alpha,
				g_book_presents);
		}
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

			/* An empty window leaves out the pieces in the box one sector
			 * smaller (DecidePairs); the line says so at its end. */
			for (int i = 0; i < g_frame_count; i++)
				Log("HDLOG   room=%d group=%d clip=%d,%d,%d,%d %s%s",
					g_frame[i].room, g_frame[i].group,
					g_frame[i].x0, g_frame[i].y0, g_frame[i].x1, g_frame[i].y1,
					!g_frame[i].windowed ? "(no window - no art inside another room)"
						: ClipEmpty(g_frame[i]) ? "EMPTY - the intruding art is not drawn"
						: g_frame[i].full ? "(the whole screen)" : "",
					g_frame[i].windowed && ClipEmpty(g_frame[i])
						? " where it stands in the box one sector smaller" : "");
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

			/* `pairs` `whole` `scissored` are the pieces of intruding art
			 * DecidePairs asked about, and its two answers - the second
			 * `scissored` counts pieces, the first rooms. The `reach_` fields
			 * at the end are the rooms with an empty window, which are not
			 * asked: `reach_rooms` the ones with intruding pieces,
			 * `reach_pieces` those pieces, `reach_whole` the ones drawn whole
			 * (outside the box one sector smaller), `reach_hidden` the ones
			 * inside that box, left out. The reachart lines below name them
			 * per room and holder.
			 *
			 * `stand=` is the frame's answer for the stand ranges
			 * (hd_cover.h, DecideStand): classic rooms with stand art on the
			 * list / ranges drawn with their own group / drawn in their
			 * holder's window / left out because their holder is listed and
			 * they lie on faces it draws classic or are another group's copy /
			 * in-holder draws past HD_MAX_STAND_DRAW. `stand_behind=` is how
			 * many of those drawn with their group are drawn pushed back
			 * (HD_STAND_BEHIND): art reaching onto faces a listed room draws
			 * classic.
			 *
			 * `unlinked_pieces` are the pieces of the listed rooms whose art
			 * stands in the air of a room their own room has no link with
			 * (DecidePairs), `unlinked_drawn` those drawn - inside their own
			 * room's window, or whole on a claimed face the classic path
			 * draws -, `unlinked_hidden` those left out because that window
			 * is empty. */
			Log("HDLOG clip rooms=%d open=%d full=%d scissored=%d empty=%d "
				"windowed_tris=%d items_spared=%d view=%s view_obj=%s "
				"pairs=%d whole=%d scissored=%d reach_rooms=%d "
				"reach_pieces=%d reach_whole=%d reach_hidden=%d "
				"stand=%d/%d/%d/%d/%d stand_behind=%d unlinked_pieces=%d "
				"unlinked_drawn=%d unlinked_hidden=%d",
				g_frame_count, g_clip_open, g_clip_full, g_clip_scissored,
				g_clip_empty, g_clip_windowed_tris, g_clip_items_spared,
				views, viewsObj, g_clip_pairs, g_clip_pairs_whole,
				g_clip_pairs_scissored, g_clip_reach_rooms,
				g_clip_reach_pieces, g_clip_reach_whole, g_clip_reach_hidden,
				g_stand_frame_count, g_clip_stand_with, g_clip_stand_holder,
				g_clip_stand_out, g_clip_stand_dropped, g_clip_stand_behind,
				g_clip_unlinked_pieces, g_clip_unlinked_drawn,
				g_clip_unlinked_hidden);
			LogReachArt();
			g_reachart_due = false;
		}
		else if (g_reachart_due)
		{
			// The frame a window event's ROOMLIST dump was written on.
			LogReachArt();
			g_reachart_due = false;
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

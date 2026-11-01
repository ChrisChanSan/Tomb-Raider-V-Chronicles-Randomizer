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
#pragma once
#include "function_stubs.h"	// gLogEnabled - the one bool this whole file hangs on

/* The frame profile: where a frame's milliseconds go.
 *
 * FPSLOG (specific/hd/hd_level.cpp) says how long a frame took. It cannot
 * say why, and two measured symptoms - the title screen falling to 23
 * frames a second, and about three seconds of warm-up at the start of
 * every level - are exactly the questions FPSLOG cannot answer. This file
 * answers them: every drawn frame is broken into the phases below, and a
 * frame that missed its slot writes down what each of its phases cost.
 *
 * The phases, in the order the frame walks them:
 *
 *   LOGIC       ControlPhase - the game's own tick or ticks. Measured at
 *               the call site (game/gameflow.cpp) and not inside
 *               ControlPhase, which has early returns. It is the
 *               ControlPhase that ran before this frame's draw: the loop
 *               is draw, present, logic, so the logic of iteration n falls
 *               between present n and present n+1 and belongs to frame
 *               n+1's interval. That is a definition, not an
 *               approximation - ProfileFrameBegin must not clear this
 *               accumulator, because by the time it runs the milliseconds
 *               it is about to report are already in it.
 *
 *               It is cleared by ProfileFrameEnd instead, after the line
 *               has been written and the second's totals have taken it.
 *               Clearing it in neither place lets it accumulate for the
 *               whole level instead: `logic` would grow by a few tenths of
 *               a millisecond every frame and pass `work` after two
 *               seconds. A PROFLOG reader can tell a correct build from a
 *               broken one at a glance - `logic` has to stand still from
 *               second to second, and it has to be small.
 *
 *   GATHER      DrawPhaseGame from its first line to just before
 *               S_OutputPolyList: Lara's matrices, the room walk, every
 *               item, the sprites, the game info. Almost nothing is drawn
 *               here - the classic path fills buckets and the sort list,
 *               the HD path writes down rooms, items and Lara for later.
 *               The one exception is the sky, which draws itself; hence
 *               SKY.
 *     ROOMS       DrawRooms inside it, which is nearly all of it.
 *     SKY         SkyDrawPhase inside ROOMS - the horizon, the moon, the
 *                 stars, the lightning. It is the one place in the frame
 *                 that draws buckets and a sort list outside
 *                 S_OutputPolyList, which is why it is called out.
 *     HD_ROOM     the HDFrameRoom calls of this frame, added up.
 *     HD_OBJ      HDObjectBegin + HDObjectNode + HDObjectEnd, added up.
 *     HD_LARA     HDLaraDrawFrame.
 *
 *   OUTPUT      S_OutputPolyList - the whole draw.
 *     HD_DRAW     HDFrameDraw: the HD rooms' and objects' depth-writing
 *                 pass.
 *       HD_LIGHT    FillRoomLighting inside it, per room.
 *       HD_OBJDRAW  HDObjectFrameDraw inside it (and inside HD_GLOW).
 *     HD_GLOW     HDFrameDrawGlow - the HD additive pass.
 *     outrest     not a phase: printed as OUTPUT minus HD_DRAW and
 *                 HD_GLOW, i.e. everything classic the output phase did -
 *                 the buckets, the sort list, the binoculars, the pickup
 *                 display and the fade.
 *
 *   BUCKETS     DrawBuckets, wherever it was called from, added up.
 *   SORT        SortPolyList of the frame, added up - the quicksort
 *               itself and nothing else, so a big number here is CPU work
 *               on the sort list and can be read against sortn (below).
 *   SORTDRAW    every DrawSortList of the frame, added up - walking the
 *               sort buffer into the bucket vertices and handing them to
 *               the device. SORT and SORTDRAW are kept apart because a
 *               slow first frame can be either: the qsort itself, or the
 *               submission that follows it, and the two point at
 *               different problems.
 *
 *               These three are whole-frame totals on purpose: all of
 *               them are called from three or more places
 *               (S_OutputPolyList, the sky phase, the binocular and
 *               pickup blocks), and measuring them at the callee is the
 *               only way to catch all of them. So they overlap SKY and
 *               outrest and must not be added to the others - they are
 *               there to say how the classic work splits, not to make a
 *               total.
 *
 *   SPIN        S_DumpScreen's `while (n < gPresentMinTicks)` busy wait -
 *               the time the frame was finished and waiting for its slot.
 *               A big number here means the machine is fast and the cap
 *               is holding it back; a zero means the frame was late.
 *   FLIP        _EndScene + DXShowFrame - Present itself, and whatever the
 *               driver does inside it.
 *
 * The three numbers that are not phases, on the first_frame and late
 * lines:
 *
 *   work        ProfileFrameBegin to ProfileFrameEnd - the drawing half
 *               of the frame and nothing else. LOGIC is not in it,
 *               because the logic ran outside that bracket by the
 *               definition above. The invariant to check a log against is
 *               `work + logic <= frame`, never `logic <= work`.
 *   frame       the interval from the previous ProfileFrameEnd to this
 *               one: work, plus the logic, plus anything else the main
 *               loop did.
 *   sortn       how many entries the sort list held this frame, the
 *               largest of the frame's SortPolyList calls. It is there so
 *               a big SORT can be told from a first-touch SORT without a
 *               second session.
 *
 * Late, and pause. A frame is late when its interval ran over the slot
 * its own present asked for - gPresentMinTicks ticks of 1/60 s plus half
 * a tick of slack, so 25.0 ms for a frame that waited one tick and 41.7
 * ms for one that waited two. The budget follows the present, not
 * tomb5.fps_mode: with fps_mode 1 the game frame waits one tick but every
 * modal loop still waits two, and judging the menus against 16.7 ms would
 * call a perfectly healthy inventory thirty late frames a second.
 *
 * An interval is not late at all when the main loop was somewhere else: a
 * modal loop presented frames of its own between this frame and the last
 * one (the inventory, the pause menu, the load/save screen, the title's
 * options, the death menu, the level-change fade), or the whole process
 * was stopped for more than a second - an alt-tab, a window drag, a
 * debugger. Those are counted and printed as `pauses`, they never touch
 * `late`, and they get one short line of their own instead of a full late
 * line with seventeen phases that would all be meaningless.
 *
 * Nothing happens without -log. Every seam in the engine is
 * `ProfBegin(...)` / `ProfEnd(...)`, and both are one test of
 * gLogEnabled around a call. With the log off not one
 * QueryPerformanceCounter is issued, no accumulator moves and no string
 * is built - the same guard HDFramePresented uses, which is what lets
 * these seams stay in permanently.
 */

enum ProfPhase
{
	PROF_LOGIC = 0,
	PROF_GATHER,
	PROF_ROOMS,
	PROF_SKY,
	PROF_HD_ROOM,
	PROF_HD_OBJ,
	PROF_HD_LARA,
	PROF_OUTPUT,
	PROF_HD_DRAW,
	PROF_HD_LIGHT,
	PROF_HD_OBJDRAW,
	PROF_BUCKETS,
	PROF_HD_GLOW,
	PROF_SORT,
	PROF_SORTDRAW,
	PROF_SPIN,
	PROF_FLIP,
	PROF_COUNT
};

// The real work, all of it behind the inline guards below. Nothing outside
// this file should call these four directly.
void ProfileBeginPhase(int phase);
void ProfileEndPhase(int phase);
void ProfileNotePresent();
void ProfileNoteSort(long count);

/* The fog bulbs' pass (hd_render.cpp, fog_draw), once a frame with -log:
 * the processor's time in its draw, part of HD_DRAW, and the card's time
 * of the last pass the card has answered for - a frame or two before, -1
 * for none. PROFLOG prints them at the end of its lines, `fog=` like every
 * phase and `fog_gpu=` beside it: per second the mean and the worst, the
 * card's mean over the frames that had an answer. */
void ProfileNoteFog(double cpuMs, double gpuMs);

/* Marks a new level. Called from S_LoadLevelFile once the load thread has
 * joined and the HD build is done, with the level's file name - the
 * profile prints the seconds since this call, so a warm-up period has a
 * number to be counted in. Also writes the previous level's summary. */
void ProfileLevel(const char* fileName);

// The frame bracket. Begin is the first line of DrawPhaseGame, End the last;
// End is where the second is flushed and where a late frame writes its line.
void ProfileFrameBegin();
void ProfileFrameEnd();

inline void ProfBegin(int phase)
{
	if (gLogEnabled)
		ProfileBeginPhase(phase);
}

inline void ProfEnd(int phase)
{
	if (gLogEnabled)
		ProfileEndPhase(phase);
}

/* Marks that one present happened. Called from both present functions in
 * specific/output.cpp, so the profile can tell its own present apart from
 * the ones a modal loop made behind its back - that, and not a threshold
 * on the clock, is what makes an inventory visit a `pause` and not a late
 * frame. An FMV presents through fmv.cpp and is not counted here; it does
 * not have to be, because it stops the main loop for far longer than the
 * one second the pause rule's other half asks for. */
inline void ProfPresent()
{
	if (gLogEnabled)
		ProfileNotePresent();
}

// How long the sort list was. One call from SortPolyList; the frame keeps
// the largest it was handed.
inline void ProfSort(long count)
{
	if (gLogEnabled)
		ProfileNoteSort(count);
}

/* For a function with early returns - HDObjectBegin, HDObjectEnd,
 * HDLaraDrawFrame, DrawSortList. One object on the stack, two bool tests
 * when the log is off. */
struct ProfScope
{
	int phase;

	ProfScope(int p) : phase(p) { ProfBegin(p); }
	~ProfScope() { ProfEnd(phase); }
};

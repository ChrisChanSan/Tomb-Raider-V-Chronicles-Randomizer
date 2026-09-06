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

/* THE FRAME PROFILE (#148, order 1) - WHERE A FRAME'S MILLISECONDS GO.
 *
 * FPSLOG (specific\hd\hd_level.cpp) says HOW LONG a frame took. It cannot say
 * WHY, and the two numbers the user brought back from a real session - the
 * title screen falling to 23 frames a second, and about three seconds of
 * warm-up at the start of every level - are exactly the questions FPSLOG
 * cannot answer. This file answers them: every drawn frame is broken into the
 * phases below, and a frame that missed its slot writes down what each of its
 * phases cost.
 *
 * THE PHASES, in the order the frame walks them:
 *
 *   LOGIC       ControlPhase - the game's own tick or ticks. Measured at the
 *               call site (game\gameflow.cpp) and NOT inside ControlPhase,
 *               which has early returns. It is the ControlPhase that ran
 *               BEFORE this frame's draw: the loop is draw, present, logic,
 *               so the logic of iteration n falls between present n and
 *               present n+1 and belongs to frame n+1's interval. That is a
 *               definition, not an approximation - the accumulator is not
 *               cleared by ProfileFrameBegin for exactly this reason.
 *
 *   GATHER      DrawPhaseGame from its first line to just before
 *               S_OutputPolyList: Lara's matrices, the room walk, every
 *               item, the sprites, the game info. Almost nothing is drawn
 *               here - the classic path fills buckets and the sort list, the
 *               HD path writes down rooms, items and Lara for later. The one
 *               exception is the sky, which draws itself; hence SKY.
 *     ROOMS       DrawRooms inside it, which is nearly all of it.
 *     SKY         SkyDrawPhase inside ROOMS - the horizon, the moon, the
 *                 stars, the lightning. It is the ONE place in the frame
 *                 that draws buckets and a sort list outside
 *                 S_OutputPolyList, which is why it is called out.
 *     HD_ROOM     the HDFrameRoom calls of this frame, added up.
 *     HD_OBJ      HDObjectBegin + HDObjectNode + HDObjectEnd, added up.
 *     HD_LARA     HDLaraDrawFrame.
 *
 *   OUTPUT      S_OutputPolyList - the whole draw.
 *     HD_DRAW     HDFrameDraw: the HD rooms' and objects' depth-writing pass.
 *       HD_LIGHT    FillRoomLighting inside it, per room.
 *       HD_OBJDRAW  HDObjectFrameDraw inside it (and inside HD_GLOW).
 *     HD_GLOW     HDFrameDrawGlow - the HD additive pass.
 *     outrest     NOT a phase: printed as OUTPUT minus HD_DRAW and HD_GLOW,
 *                 i.e. everything CLASSIC the output phase did - the buckets,
 *                 the sort list, the binoculars, the pickup display and the
 *                 fade.
 *
 *   BUCKETS     DrawBuckets, wherever it was called from, added up.
 *   SORT        SortPolyList + every DrawSortList of the frame, added up.
 *
 *               These last two are WHOLE-FRAME totals on purpose: both are
 *               called from three or more places (S_OutputPolyList, the sky
 *               phase, the binocular and pickup blocks), and measuring them
 *               at the callee is the only way to catch all of them. So they
 *               overlap SKY and OUTREST and must not be added to the others -
 *               they are there to say how the classic work splits, not to
 *               make a total.
 *
 *   SPIN        S_DumpScreen's `while (n < gPresentMinTicks)` busy wait -
 *               the time the frame was FINISHED and waiting for its slot. A
 *               big number here means the machine is fast and the cap is
 *               holding it back; a zero means the frame was late.
 *   FLIP        _EndScene + DXShowFrame - Present itself, and whatever the
 *               driver does inside it.
 *
 * NOTHING HAPPENS WITHOUT -log. Every seam in the engine is `ProfBegin(...)`
 * / `ProfEnd(...)`, and both are one test of gLogEnabled around a call. With
 * the log off not one QueryPerformanceCounter is issued, no accumulator
 * moves and no string is built. That is the same guard HDFramePresented
 * uses, and it is why these seams can stay in for good
 * (feedback_diagnosenaehte_bleiben).
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
	PROF_SPIN,
	PROF_FLIP,
	PROF_COUNT
};

// The real work, all of it behind the inline guards below. Nothing outside
// this file should call these four directly.
void ProfileBeginPhase(int phase);
void ProfileEndPhase(int phase);

/* A NEW LEVEL. Called from S_LoadLevelFile once the load thread has joined
 * and the HD build is done, with the level's file name - the profile prints
 * the seconds since this call, so "the three warm-up seconds" have a number
 * to be counted in. Also writes the previous level's summary. */
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

/* For a function with early returns - HDObjectBegin, HDObjectEnd,
 * HDLaraDrawFrame, DrawSortList. One object on the stack, two bool tests
 * when the log is off. */
struct ProfScope
{
	int phase;

	ProfScope(int p) : phase(p) { ProfBegin(p); }
	~ProfScope() { ProfEnd(phase); }
};

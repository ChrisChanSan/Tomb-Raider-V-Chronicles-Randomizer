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
#include "cutseq_interp.h"
#include "interpolate.h"		// InterpInvalidate, InterpTickSerial, InterpIsTweenFrame
#include "interpolate_math.h"		// InterpCutseqBlend, InterpCutseqCameraCut - InterpTest checks these
#include "deltapak.h"			// GLOBAL_cutme, GLOBAL_cutseq_frame, GLOBAL_playing_cutseq, cutseq_num
#include "../specific/function_stubs.h"	// Log, gLogEnabled
#include "../specific/output.h"		// ActiveFpsMode, for the log line

/* The design note for this file is in the header; what is written down here
 * is the snapshot, the arithmetic's call sites and the log. */

namespace
{
	const int ACTORS = INTERP_CUTSEQ_ACTORS;
	const int NODES = INTERP_CUTSEQ_NODES;

	/* The scene the tables belong to. `g_nodes` is actor_pnodes
	 * (game\deltapak.cpp), whose entries point into the game_malloc pool or
	 * the resident cutscene buffers; they are read only while the scene that
	 * set them plays (GLOBAL_cutme unchanged and GLOBAL_playing_cutseq set). */
	PACKNODE** g_nodes = 0;
	NEW_CUTSCENE* g_cut = 0;
	bool g_open = false;

	/* Nodes per actor as DecodeAnim walks them: sixteen for Lara (actor 0,
	 * none when her slot is NO_ITEM), `nodes + 1` for every other actor. A
	 * table larger than NODES is drawn live; none in DATA\CUTSEQ.BIN is. */
	int g_count[ACTORS];

	/* x, y, z per node: the tables before the last tick, and the tick's own
	 * values while a blend stands in the live tables. 2400 bytes each. */
	short g_prev[ACTORS][NODES * 3];
	short g_saved[ACTORS][NODES * 3];
	bool g_prevValid = false;
	bool g_touched[ACTORS];
	bool g_anyTouched = false;

	/* The tick whose refusals were counted and logged. A tick is drawn on
	 * two to six frames, all from the same pair of tables, so the counters
	 * and the log take each tick once. */
	unsigned long g_countedSerial = 0;
	unsigned long g_cutSerial = 0;

	/* CUTLOG, -log only. Drawn frames, ticks and the frames blended and
	 * drawn live; cut ticks of the camera, actor-ticks drawn live for a root
	 * jump, node-ticks kept for a half turn; the braid's steps, blended
	 * frames whose eye stood in another room than the logic camera's, and
	 * the furthest Lara's blended root stood from her previous tick on a
	 * frame between two ticks. */
	int g_frames = 0;
	int g_ticks = 0;
	int g_blended = 0;
	int g_live = 0;
	int g_cuts = 0;
	int g_actorCuts = 0;
	int g_halfTurns = 0;
	int g_hair = 0;
	int g_eyeRooms = 0;
	long g_hairLag = 0;

	/* The frame index the tick decoded last; do_new_cutscene_camera steps
	 * GLOBAL_cutseq_frame after the decode. CutseqProbe's frames are these. */
	long DecodedFrame()
	{
		return GLOBAL_cutseq_frame - 1;
	}

	void LogCut(const char* why, long eye, long target, int actor, int node, long step)
	{
		if (gLogEnabled)
			Log("CUTLOG cut cutseq=%d frame=%ld why=%s eye=%ld target=%ld actor=%d node=%d step=%ld",
				(int)cutseq_num, DecodedFrame(), why, eye, target, actor, node, step);
	}

	// Whether the scene playing now is the one whose tables this file holds.
	bool SceneLive()
	{
		return g_open && g_nodes && GLOBAL_playing_cutseq && GLOBAL_cutme == g_cut;
	}

	void ReadTable(int i, short* out)
	{
		const PACKNODE* n = g_nodes[i];

		for (int k = 0; k < g_count[i]; k++)
		{
			out[3 * k] = n[k].xrot_run;
			out[3 * k + 1] = n[k].yrot_run;
			out[3 * k + 2] = n[k].zrot_run;
		}
	}

	void WriteTable(int i, const short* in)
	{
		PACKNODE* n = g_nodes[i];

		for (int k = 0; k < g_count[i]; k++)
		{
			n[k].xrot_run = in[3 * k];
			n[k].yrot_run = in[3 * k + 1];
			n[k].zrot_run = in[3 * k + 2];
		}
	}

	void ResetCounters()
	{
		g_frames = 0;
		g_ticks = 0;
		g_blended = 0;
		g_live = 0;
		g_cuts = 0;
		g_actorCuts = 0;
		g_halfTurns = 0;
		g_hair = 0;
		g_eyeRooms = 0;
		g_hairLag = 0;
	}
}

void CutseqInterpOpen(PACKNODE** actors)
{
	int nodes = 0;

	/* The rest of the snapshot first: InterpInvalidate calls
	 * CutseqInterpInvalidate, which must find nothing of this scene. */
	InterpInvalidate("cutseq open");

	g_nodes = actors;
	g_cut = GLOBAL_cutme;
	g_open = true;
	g_prevValid = false;
	g_anyTouched = false;

	for (int i = 0; i < ACTORS; i++)
	{
		g_touched[i] = false;
		g_count[i] = 0;

		if (i >= GLOBAL_cutme->numactors)
			continue;

		if (!i)
			g_count[i] = GLOBAL_cutme->actor_data[0].objslot != NO_ITEM ? 16 : 0;
		else
			g_count[i] = GLOBAL_cutme->actor_data[i].nodes + 1;

		nodes += g_count[i];
	}

	ResetCounters();

	if (gLogEnabled)
		Log("CUTLOG open cutseq=%d frames=%d actors=%d nodes=%d fps_mode=%d",
			(int)cutseq_num, (int)GLOBAL_numcutseq_frames, (int)GLOBAL_cutme->numactors, nodes,
			(int)ActiveFpsMode());
}

void CutseqInterpClose()
{
	if (!g_open)
		return;

	if (gLogEnabled)
		Log("CUTLOG close cutseq=%d frames=%d ticks=%d blended=%d live=%d cuts=%d actor_cuts=%d "
			"halfturns=%d hair=%d eye_rooms=%d hair_lag=%ld",
			(int)cutseq_num, g_frames, g_ticks, g_blended, g_live, g_cuts, g_actorCuts,
			g_halfTurns, g_hair, g_eyeRooms, g_hairLag);

	LogCut("close", -1, -1, -1, -1, -1);
	InterpInvalidate("cutseq close");
	g_open = false;
	g_nodes = 0;
	g_cut = 0;
}

void CutseqInterpSkip()
{
	LogCut("skip", -1, -1, -1, -1, -1);
	InterpInvalidate("cutseq skip");
}

void CutseqInterpInvalidate()
{
	CutseqRestore();
	g_prevValid = false;
}

void CutseqCapturePrev()
{
	if (!SceneLive())
	{
		g_prevValid = false;
		return;
	}

	if (gLogEnabled)
		g_ticks++;

	/* Until the first tick of the scene has decoded frame 0, the tables hold
	 * what InitPackNodes left, not a frame. */
	if (GLOBAL_cutseq_frame <= 0)
	{
		g_prevValid = false;
		return;
	}

	for (int i = 0; i < ACTORS; i++)
		if (g_count[i] > 0 && g_count[i] <= NODES)
			ReadTable(i, g_prev[i]);

	g_prevValid = true;
}

int CutseqBlendable()
{
	if (!SceneLive())
		return CUTSEQ_LIVE_LOST;

	if (!g_prevValid)
	{
		const unsigned long serial = InterpTickSerial();

		if (serial != g_cutSerial)
		{
			g_cutSerial = serial;
			LogCut("open", -1, -1, -1, -1, -1);
		}

		return CUTSEQ_LIVE_OPEN;
	}

	return 0;
}

bool CutseqCameraCut(const long prevEye[3], const long eye[3], const long prevTarget[3],
	const long target[3])
{
	long eyeStep, targetStep;

	if (!InterpCutseqCameraCut(prevEye, eye, prevTarget, target, &eyeStep, &targetStep))
		return false;

	if (gLogEnabled)
	{
		const unsigned long serial = InterpTickSerial();

		if (serial != g_cutSerial)
		{
			g_cutSerial = serial;
			g_cuts++;
			LogCut("camera", eyeStep, targetStep, -1, -1, -1);
		}
	}

	return true;
}

int CutseqApply(float alpha)
{
	const unsigned long serial = InterpTickSerial();
	const bool firstOfTick = serial != g_countedSerial;
	int ceiling = 0;

	g_countedSerial = serial;

	if (!SceneLive() || !g_prevValid)
		return 0;

	for (int i = 0; i < ACTORS; i++)
	{
		const int n = g_count[i];
		short out[NODES * 3];
		long rootStep;
		int halfTurns, halfNode;

		if (n <= 0)
			continue;

		if (n > NODES)
		{
			ceiling++;
			continue;
		}

		ReadTable(i, g_saved[i]);

		if (!InterpCutseqBlend(g_prev[i], g_saved[i], n, alpha, out, &rootStep, &halfTurns,
			&halfNode))
		{
			if (gLogEnabled && firstOfTick)
			{
				g_actorCuts++;
				LogCut("actor", -1, -1, i, 0, rootStep);
			}

			continue;
		}

		if (halfTurns && gLogEnabled && firstOfTick)
		{
			g_halfTurns += halfTurns;
			LogCut("halfturn", -1, -1, i, halfNode, halfTurns);
		}

		/* How far the braid stands behind Lara on a frame between two ticks:
		 * it was last stepped from her pose at the previous tick. */
		if (!i && gLogEnabled && InterpIsTweenFrame())
		{
			const long lag = InterpCutseqRootStep(g_prev[0], out);

			if (lag > g_hairLag)
				g_hairLag = lag;
		}

		WriteTable(i, out);
		g_touched[i] = true;
		g_anyTouched = true;
	}

	return ceiling;
}

void CutseqRestore()
{
	if (!g_anyTouched)
		return;

	for (int i = 0; i < ACTORS; i++)
	{
		if (!g_touched[i])
			continue;

		/* The tables are written only while their scene plays, and no scene
		 * ends inside DrawPhaseGame; tested anyway before a pointer into the
		 * pool is written through. */
		if (SceneLive())
			WriteTable(i, g_saved[i]);

		g_touched[i] = false;
	}

	g_anyTouched = false;
}

void CutseqNoteFrame(bool blended)
{
	if (!gLogEnabled || !SceneLive())
		return;

	g_frames++;

	if (blended)
		g_blended++;
	else
		g_live++;
}

void CutseqNoteHair()
{
	if (gLogEnabled)
		g_hair++;
}

void CutseqNoteEyeRoom()
{
	if (gLogEnabled)
		g_eyeRooms++;
}

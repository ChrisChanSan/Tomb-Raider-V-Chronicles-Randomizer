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
#include "../global/types.h"

/* The cutscene actors on a frame between two logic ticks.
 *
 * Once per tick do_new_cutscene_camera (game\deltapak.cpp) decodes the next
 * frame of DATA\CUTSEQ.BIN into every actor's node table (the rot_run values
 * of its PACKNODEs) and sets the camera from the camera's two nodes. The draw
 * reads nothing else of a cutscene: updateAnimFrame turns a table into the
 * frame DrawCutSeqActors draws, and frigup_lara builds Lara, actor 0, from
 * hers. So a frame between two ticks is drawn from the two ticks' tables
 * blended, the way interpolate.cpp draws items[]: the tables of the previous
 * tick are copied at the start of the tick, the blend is written into the
 * live tables before DrawPhaseGame draws and the tick's values are put back
 * after it. The decoder keeps its own state per node and runs only in the
 * tick, so it sees exactly what a thirty hertz run leaves it. The camera needs
 * nothing here: InterpBeginFrame blends camera.pos and camera.target from the
 * previous tick as in the game, with the cutscene's own cut rule below. */

/* The scene's tables are ready (init_cutseq_actors): `actors` is
 * actor_pnodes. Drops the whole interpolation snapshot, since the scene
 * starts from a camera and a Lara of the game, and writes CUTLOG's open line. */
void CutseqInterpOpen(PACKNODE** actors);

/* The scene has ended (handle_cutseq_triggering, before the next scene of
 * the credits opens): CUTLOG's close line, and the snapshot dropped again. */
void CutseqInterpClose();

/* The skipper has ended the scene early (do_cutseq_skipper_stuff): the
 * snapshot is dropped, so the fade that follows starts from a live frame. */
void CutseqInterpSkip();

/* From InterpInvalidate: the node snapshot goes with the rest. A blend still
 * standing in the tables is put back first, because the decoder adds its
 * next deltas to whatever the tables hold. */
void CutseqInterpInvalidate();

// From InterpCapturePrev, once per tick: the tables the tick is about to advance.
void CutseqCapturePrev();

/* Whether this frame's actors can be drawn from blended tables: 0 when they
 * can; CUTSEQ_LIVE_OPEN on the first tick of a scene, whose previous tick
 * decoded nothing yet; CUTSEQ_LIVE_LOST when a scene plays that this file
 * never opened, which a healthy log never shows. Either way InterpBeginFrame
 * draws the whole frame live. */
enum
{
	CUTSEQ_LIVE_OPEN = 1,
	CUTSEQ_LIVE_LOST = 2
};

int CutseqBlendable();

/* The cutscene camera's cut rule (INTERP_CUTSEQ_SNAP, interpolate_math.h) on
 * the eye and the target of the two ticks; a cut draws the whole frame live.
 * CUTLOG writes one line per cut tick. */
bool CutseqCameraCut(const long prevEye[3], const long eye[3], const long prevTarget[3],
	const long target[3]);

/* Writes the blended tables at `alpha`, keeping the tick's values aside. An
 * actor whose root jumps is left at the tick's values; so is a node with an
 * axis half a turn from its previous value (InterpCutseqBlend). Returns how
 * many actors were left live because their table is larger than the snapshot
 * (INTERPLOG's cutscene=). */
int CutseqApply(float alpha);

// From InterpEndFrame: the tick's values back into the tables.
void CutseqRestore();

/* CUTLOG's counters, -log only: a frame drawn in a cutscene and whether it was
 * blended (InterpEndFrame), the braid stepped (frigup_lara), and a blended
 * frame whose drawn eye stands in another room than the logic camera's
 * (InterpEyeRoom). */
void CutseqNoteFrame(bool blended);
void CutseqNoteHair();
void CutseqNoteEyeRoom();

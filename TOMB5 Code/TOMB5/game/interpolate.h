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

/* TIME INTERPOLATION (#148, order 2) - THE ROOT AND THE CAMERA.
 *
 * Order 1 split the clock: S_DumpScreen waits one 1/60 s tick instead of two
 * for DrawPhaseGame's own present, and ControlPhase's accumulator - `for
 * (framecount += nframes; framecount > 0; framecount -= 2)` - spends two of
 * them per logic step, so the simulation keeps its thirty ticks a second
 * while sixty frames are presented. Every second frame was therefore a
 * DUPLICATE of the one before it. This file is what fills those frames in.
 *
 * WHAT IT DOES, in one sentence: it remembers where everything stood before
 * the last logic tick, and on the in-between frame it writes the HALFWAY
 * point into the live structures, lets the whole renderer draw from them
 * exactly as it always has, and then puts the original bytes back.
 *
 * THREE CALL SITES, AND THAT IS THE WHOLE SEAM:
 *
 *   InterpCapturePrev()   game\control.cpp, the first statement inside the
 *                         tick body, before GlobalCounter++. It is the state
 *                         BEFORE the tick that is about to run. When one
 *                         ControlPhase runs several ticks (a hitch, a long
 *                         load) each one overwrites the last, so what stands
 *                         at draw time is the state before the FINAL tick -
 *                         which is the only neighbour the drawn state has.
 *
 *   InterpBeginFrame(a)   game\draw.cpp, the first line of DrawPhaseGame.
 *                         Writes the interpolated values into items[],
 *                         effects[], hairs[][] and camera, and keeps a copy
 *                         of every value it overwrote.
 *
 *   InterpEndFrame()      game\draw.cpp, after S_AnimateTextures. Puts the
 *                         copies back, byte for byte.
 *
 * Nothing downstream knows. DrawRooms, InsertRoom, phd_PutPolygons, the
 * bucket sorter, the shadows, the lighting, HDFrameRoom, HDObjectNode and
 * HDLaraDraw all read the same globals they read today and get the
 * interpolated ones - which is why the HD renderer follows this order with
 * not one line changed in specific\hd\ (see the plan, 0.3: the HD seam takes
 * its matrices off the engine's own stack).
 *
 * WHAT IS INTERPOLATED HERE, AND WHAT IS NOT. This order moves the ROOT of a
 * thing - where it stands and which way it faces - and the CAMERA. The POSE
 * (which animation frame each limb wears) still steps at thirty; that is
 * order 4, and it has its own machinery waiting for it in draw.cpp's second
 * matrix stack. Sparks, rain, snow, debris, blood, drips, bubbles, gunshells,
 * rats, bats, spiders, ropes, shockwaves and lightning are not in the
 * snapshot at all - order 5, on the user's own eye.
 *
 * ALPHA COSTS NO CLOCK. After ControlPhase returns, framecount is -1 when a
 * tick ran and 0 when none did, so the two states of the sixty-hertz cadence
 * tell themselves apart with one comparison and cannot drift. See
 * InterpAlphaNow().
 *
 * fps_mode 0 TAKES EVERY BRANCH OUT. InterpAlphaNow returns 1, and at 1 both
 * InterpBeginFrame and InterpEndFrame return before they write anything at
 * all - no snapshot is applied, no phd_LookAt is issued, not one byte of the
 * game's state is touched. That is proof 6 of the plan (K11): the thirty
 * hertz picture stays byte-identical to the build before this order. */

// The state before the last logic tick. Called once per tick.
void InterpCapturePrev();

/* What the frame about to be drawn should be drawn at: 1 for the live state,
 * 0.5 for the frame that falls between two logic ticks. Read at the top of
 * DrawPhaseGame and handed straight to InterpBeginFrame - it stands as its
 * own function so that the rule is one line and can be read where the caller
 * is. */
float InterpAlphaNow();

void InterpBeginFrame(float alpha);
void InterpEndFrame();

/* THE PREVIOUS STATE IS NOT A NEIGHBOUR OF THIS ONE. Called wherever game
 * time jumped rather than flowed: a level start, a level being freed, a
 * savegame being read, and the return from every menu that stops the world
 * (the inventory, the load/save screen, the pause menu). The next frame after
 * one of these draws the live state, and the tick after it starts the
 * interpolation again.
 *
 * `why` is one short word and reaches the log; it is never null. */
void InterpInvalidate(const char* why);

/* What the frame BEING DRAWN right now is drawn at - 1 while nothing is
 * interpolated. For the log lines of other modules (HDLOG); it is not part of
 * the mechanism. */
float InterpFrameAlpha();

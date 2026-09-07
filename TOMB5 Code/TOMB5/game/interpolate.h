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
 * matrix stack.
 *
 * ORDER 5 THEN TOOK THE PARTICLES, in the two ways they divide into. The ones
 * that carry a VELOCITY - sparks, fire and smoke sparks, rain, snow, the
 * underwater dust, blood, drips, debris and the shockwave's radius - need no
 * snapshot at all, because the velocity IS the step the last tick took and
 * the previous position is a subtraction (InterpTrailLong,
 * game\interpolate_math.h; InterpParticleAlpha below). The ones that carry
 * only a POSITION - gunshells, rats, bats, spiders, bubbles and the rope's
 * mesh segments - joined this file's snapshot, where they cost 16.7 KB and not
 * one line of their own drawing code. Only the lightning is still drawn at
 * thirty, and it is drawn at thirty on purpose: its segments are re-rolled
 * from scratch on every tick, so there is nothing between two of them to
 * interpolate.
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
 * hertz picture stays byte-identical to the build before this order.
 *
 * ORDER 3 ADDED TWO THINGS TO THIS FILE, and both are about the frame that
 * carries no logic tick rather than about the blend:
 *
 *   InterpIsTweenFrame()   the one criterion every per-frame counter in the
 *                          build now asks before it steps. Its own note is
 *                          below.
 *
 *   THE LOGIC RNG IS ROLLED BACK over a tween frame. The plan's safety
 *   argument (0.4) said the draw and the logic have separate generators -
 *   GetRandomDraw on rand_2, GetRandomControl on rand_1 - so extra frames
 *   cannot desynchronise the game. That is not true of this engine: the
 *   DRAW path calls GetRandomControl in at least five places (the star
 *   twinkle, four thousand draws a frame; the floor lasers' water pulse;
 *   the enemy muzzle flash; the steel door's lens flare; the cutscene
 *   speech head). At sixty each of them would consume the LOGIC stream
 *   twice per tick and the simulation would drift apart between the two
 *   frame rates. So InterpBeginFrame remembers rand_1 on a tween frame and
 *   InterpEndFrame puts it back, which makes the consumption per logic tick
 *   exactly what it is at thirty - one frame's worth - whatever any drawing
 *   code does. Proof 5 of the plan, a byte-identical savegame from the same
 *   inputs at both rates, rests on this. */

// The state before the last logic tick. Called once per tick.
void InterpCapturePrev();

/* What the frame about to be drawn should be drawn at: 1 for the live state,
 * 0.5 for the frame that falls between two logic ticks. Read at the top of
 * DrawPhaseGame and handed straight to InterpBeginFrame - it stands as its
 * own function so that the rule is one line and can be read where the caller
 * is.
 *
 * AT fps_mode 2 IT IS NOT A HALF (#148, order 6). Bound to the display's
 * refresh the frames do not land on the ticks, so the factor comes from the
 * clock instead - InterpTickFraction below - and walks the whole interval
 * from 0 to 1 across every logic step. */
float InterpAlphaNow();

/* HOW FAR PAST THE LAST LOGIC TICK THE CLOCK STANDS, as a fraction of a logic
 * step (1/30 s), clamped to [0,1] (#148, order 6).
 *
 * The display-bound frame rate's blend factor, and the only reading in the
 * engine that is finer than a 1/60 s tick. It is built from ControlPhase's
 * accumulator and the remainder in the engine's own clock (TIME_SubTick,
 * specific\time.cpp) and needs no accumulator of its own; the derivation is
 * written out over InterpRefreshAlpha in game\interpolate_math.h, where the
 * arithmetic lives so that InterpTest can check it without a game.
 *
 * Nothing calls it at fps_mode 0 or 1. */
float InterpTickFraction();

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

/* THE ONE CRITERION OF ORDER 3 (#148).
 *
 * True when the frame being drawn carries NO logic tick - the in-between
 * frame of the sixty hertz cadence. Every counter in the build that steps
 * once per DRAWN frame and is meant to measure TIME asks this and steps only
 * when it is false, so that a fade, a raindrop, a flash and a corpse take
 * exactly as long in wall-clock seconds at sixty as they do at thirty.
 * "Everything must feel like vanilla, only more frames" is the whole rule of
 * this order, and this is the whole of its mechanism.
 *
 * WHAT IT IS NOT: it is not "alpha < 1". A cutscene frame is drawn live and
 * still carries no tick; a frame whose previous state was invalidated is
 * drawn live and still carries no tick. Both must stop the counters, or a
 * fade would run at double speed through every cutscene. The latch is
 * therefore set from the cadence itself and not from whether the snapshot
 * was applied.
 *
 * ITS LIFETIME IS THE WHOLE GAME-LOOP ITERATION, not just the draw. Two of
 * the counters (gfLegendTime in DoLevel, cutseq_busy_timeout in
 * handle_cutseq_triggering) sit in the main loop on either side of
 * DrawPhaseGame, so the latch is set at the top of DrawPhaseGame and STAYS
 * SET until the next frame sets it again. The alternation is what matters
 * and it holds on both sides of the call.
 *
 * AND IT IS FALSE IN EVERY MODAL LOOP. The inventory, the load/save screen,
 * the death menu and the load screen's fade loop present at two ticks and
 * step their own counters once per present, exactly as they did at thirty -
 * so S_DumpScreen calls InterpNoteClassicPresent() whenever the present it
 * just made was a thirty hertz one, which puts the latch down. Without that,
 * a modal loop entered right after a tween frame would inherit a `true` that
 * nothing inside it could ever clear, and RenderLoadPic's `while (DoFade !=
 * 2)` would never end.
 *
 * At fps_mode 0 it is false always and every guard in the build is inert. */
bool InterpIsTweenFrame();

/* A present that is not a sixty hertz game frame's own. Called from
 * S_DumpScreen and S_DumpScreenFrame - see the note above. */
void InterpNoteClassicPresent();

/* ORDER 4 - THE POSE (#148).
 *
 * Order 2 moves where a thing STANDS between two logic ticks. This is the
 * half that moves its LIMBS, and the whole of what this file contributes to
 * it is one question asked from the draw:
 *
 *   InterpPoseTime(item, &prevAnim, &prevFrame)
 *
 * "How far between the two ticks is this frame, and what animation frame did
 * this item wear before the last one?" It returns 0 - do not blend the pose -
 * whenever there is any doubt at all:
 *
 *   this frame is not a tween frame, or the snapshot was not applied (a
 *   cutscene, a level start, a return from the inventory, fps_mode 0);
 *   the item is not an items[] entry;
 *   the slot has no previous state, changed hands, or SNAPPED - the root is
 *   being drawn live, so the pose is drawn live with it.
 *
 * and otherwise the time alpha, a half. The animation NUMBER changing and the
 * frame number not stepping by exactly one are rule K6 and are tested where
 * the frames are - GetFramesAt (game\draw.cpp) and InterpPoseAt
 * (game\interpolate_math.h).
 *
 * WHY THE SNAP TEST IS TAKEN FROM THE SNAPSHOT AND NOT RE-RUN HERE: by the
 * time the draw asks, InterpBeginFrame has already written the blended
 * position into item->pos, so the distance between the previous state and
 * "now" reads half of what it really was and a teleport could pass the test.
 * The verdict is therefore recorded in InterpBeginFrame's own walk, before a
 * byte is overwritten. */
float InterpPoseTime(const ITEM_INFO* item, short* prevAnim, short* prevFrame);

/* The three counters INTERPLOG's pose fields are made of: one item drawn with
 * its pose blended in time, one drawn with the pose of the current tick
 * because K6 refused the blend, and one whose blended pose went through the
 * HD path's interpolated branch. Counted only with -log. */
void InterpNotePose(const ITEM_INFO* item);
void InterpNotePoseSkip();
void InterpNotePoseHD();
/* And the fourth (#123, fix round S, S2): a rate 1 pair whose two poses are
 * too far apart for an element-wise matrix blend to survive. */
void InterpNotePoseFar();

/* ORDER 5 - THE PARTICLES (#148).
 *
 * The one number a drawing routine needs in order to put a spark, a raindrop
 * or a drip where it stood BETWEEN the last two logic ticks: the same alpha
 * everything else on this frame is drawn at, or 1 when the frame is a live
 * one (fps_mode 0, the frame that carries the tick, a cutscene, a level
 * start, every return from a menu). At 1 every call site keeps vanilla's own
 * expression untouched, which is what makes proof 6 - a byte-identical 30 Hz
 * frame - still true after this order.
 *
 * It is `g_applied ? g_alpha : 1`, i.e. it is tied to what the SNAPSHOT did
 * rather than to the cadence, and that is deliberate: a frame that draws the
 * items live must draw the particles live too, or a cutscene would have the
 * sparks half a step behind the body throwing them.
 *
 * The arithmetic that goes with it is InterpTrailLong (game\interpolate_math.h),
 * where the reasoning about the time axis is written out: a particle is walked
 * BACKWARDS from where the tick left it by the step it just took, never
 * forwards, so that it shows the same instant as the bodies around it. */
float InterpParticleAlpha();

/* WHAT WAS INTERPOLATED THIS FRAME, BY KIND (INTERPLOG's `particles=` and
 * `particle_kinds=`). Each drawing routine counts what it really moved and
 * says so once, after its loop - so the line reads "how many particles the
 * player's eye saw move on the in-between frames", not "how many exist".
 *
 * The bitmask is what makes the line diagnostic rather than decorative: a
 * kind that is missing from it in a scene that plainly has that kind on
 * screen is a draw site whose guard never fires, and there is no other way
 * to see that from the picture - it simply looks like thirty hertz.
 *
 * Counted only with -log. */
enum InterpParticleKind
{
	INTERP_PART_SPARK = 0,		// spark[1024]        - S_DrawSparks
	INTERP_PART_FIRE,		// fire_spark[20]     - S_DrawFireSparks
	INTERP_PART_SMOKE,		// smoke_spark[32]    - S_DrawSmokeSparks
	INTERP_PART_BLOOD,		// blood[32]          - DrawBlood
	INTERP_PART_DRIP,		// Drips[32]          - DrawDrips
	INTERP_PART_DEBRIS,		// debris[256]        - DrawDebris
	INTERP_PART_RAIN,		// Rain[1024]         - DoRain
	INTERP_PART_SNOW,		// Snow[1024]         - DoSnow
	INTERP_PART_DUST,		// uwdust[256]        - DoUwEffect
	INTERP_PART_SHOCKWAVE,		// ShockWaves[16]     - DrawShockwaves
	INTERP_PART_SHELL,		// Gunshells[24]      - the snapshot
	INTERP_PART_SWARM,		// Rats/Bats/Spiders  - the snapshot
	INTERP_PART_BUBBLE,		// Bubbles[40]        - the snapshot
	INTERP_PART_ROPE,		// RopeList[8]        - the snapshot
	INTERP_PART_COUNT
};

void InterpNoteParticles(int kind, int n);

/* WHAT STEPPED THIS SECOND (TICKLOG). The counters of order 3 report
 * themselves so that "identical in wall-clock time" is a measured number
 * rather than a claim: the same TICKLOG line must come out of a thirty and a
 * sixty run. Counted only with -log. */
enum InterpStepKind
{
	INTERP_STEP_FADE = 0,		// Fade()          - the screen fade
	INTERP_STEP_SCREENFADE,		// DoScreenFade()  - death and level change
	INTERP_STEP_WEATHER,		// DoRain / DoSnow / DoUwEffect
	INTERP_STEP_LEGEND,		// gfLegendTime    - the level caption
	INTERP_STEP_AFTERDEATH,		// item->after_death - the corpse fade
	INTERP_STEP_COUNT
};

void InterpNoteStep(int kind);

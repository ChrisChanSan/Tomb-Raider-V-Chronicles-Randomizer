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

/* THE RING AT THE FRAME RATE (#154), AND ITS LOGIC STILL AT THIRTY.
 *
 * "Sehr merklich, wenn das Spiel 60 hat und das Inventar 30" - the user, on
 * the one thing in the build that still stepped once per present after order
 * 1b put every modal loop back on two ticks. His whole-game log reads
 * `FPSLOG ... frames=165 ...` while he plays and `frames=30 budget=33.3` for
 * every second the ring is up, and the jump between the two is felt.
 *
 * WHAT THIS FILE IS. game\newinv2.cpp's S_CallInventory2 is a modal loop with
 * a present of its own; it now presents at the frame rate (one tick at
 * fps_mode 1, display-bound at fps_mode 2) and runs its LOGIC BODY - the
 * input, the pulse colour, GameTimer, the repeats, the mode handlers and
 * every counter the ring owns - once per two ticks, through an accumulator of
 * exactly ControlPhase's shape. This file is that accumulator, the snapshot
 * of what moves continuously, and the blend the drawing reads.
 *
 * WHY THE BLEND IS HANDED TO THE DRAW INSTEAD OF WRITTEN INTO THE RING.
 * Order 2 writes its blended values straight into items[] and puts the
 * original bytes back after the draw (InterpBeginFrame / InterpEndFrame),
 * because in the game the whole logic tick is over before DrawPhaseGame is
 * entered. The ring is not built that way: draw_current_object_list STEPS AND
 * DRAWS IN ONE PASS - `yrot += 1022` and the brightness ramp sit inside the
 * very loop that draws each icon, and the slide is stepped at the bottom of
 * the same function, after the draw. A value written before that body is
 * stepped over by the body itself, and one written after it is too late for
 * the picture. So the blend is asked for at the READ, by the nine accessors
 * below, each of which answers the live value whenever there is nothing to
 * blend - at fps_mode 0 on the first test, on the frame that carries the
 * step's own picture, and after every cut.
 *
 * AND WHY IT IS THE STEP FRAME THAT IS BLENDED, WHICH READS BACKWARDS AT
 * FIRST. In the game the draw comes BEFORE the tick, so the drawn states are
 * T1, mid(T1,T2), T2, ... - the live frame first and the in-between frame
 * after it. In the ring the step comes before the draw, and the same
 * arrangement would draw C1, mid(C0,C1), C2, mid(C1,C2): a picture that walks
 * backwards on every second frame. The even, monotone half steps are the
 * other way round - mid(C0,C1), C1, mid(C1,C2), C2 - so the frame that
 * carries the step is drawn at a half and the frame between two steps is
 * drawn live. That is not a choice: it is what InterpRefreshAlpha (the same
 * derivation order 6 uses, game\interpolate_math.h) returns for the ring's
 * own accumulator, 0.5 on the frame whose step just ran and 1.0 on the next,
 * and InterpTest asserts both sequences side by side.
 *
 * TWO GENERATIONS FOR WHAT VANILLA STEPS AFTER ITS DRAW. The spin, the
 * brightness, the two ring fades and the examine zoom are stepped BEFORE the
 * icon is drawn, so the value this frame draws is the new one and the pair to
 * blend is (previous, live). The slide (objlistmovement) and the ammo
 * selector's fade are stepped AFTER their draw - at the bottom of
 * draw_current_object_list and in fade_ammo_selector, which the loop calls
 * after draw_ammo_selector - so the value this frame draws is the one the
 * LAST step produced, and their pair is the two previous generations. That
 * keeps each of them exactly one step behind the spin, which is the relation
 * they already have at thirty.
 *
 * WHAT DOES NOT BLEND, and it is the same rule order 2 keeps: discrete state.
 * curobjinlist, ringactive, menu_active, the selected option, the keypad
 * digits, the text and every count in it step once per logic step and are
 * drawn as they stand. A step in which curobjinlist changed is a JUMP - the
 * slide is reset to 0 in the same breath - and that field is drawn live for
 * that frame rather than blended across the reset.
 *
 * fps_mode 0 TAKES EVERY BRANCH OUT. InvInterpBeginFrame returns true on its
 * first test, the accumulator is not read, no snapshot is taken, the latch is
 * not touched and every accessor returns its argument - so the ring runs the
 * instruction sequence it ran before this round, at two ticks a present. */

struct InvInterpSource
{
	RINGME* ring[2];	// rings[RING_INVENTORY], rings[RING_AMMO]
	AMMOLIST* ammo;		// ammo_object_list[3]
	short* ammoFade;	// ammo_selector_fade_val
	short* combineFade;	// combine_ring_fade_val
	short* normalFade;	// normal_ring_fade_val
	short* examine;		// examine_mode - the zoom of the examined item
};

/* The ring is up: wire the fields, clear the accumulator and drop whatever
 * previous state a former visit left. `why` reaches INVLOG. */
void InvInterpOpen(const InvInterpSource* src, const char* why);
void InvInterpClose();

/* The first statement of the loop body. Spends the ticks the last present
 * returned, decides whether this frame carries the ring's logic step, works
 * out what the frame is drawn at, raises or lowers the tween latch for every
 * guarded counter in the build (InterpNoteMenuFrame) and, on a step frame,
 * rolls the snapshot forward before a byte of it moves.
 *
 * Returns true when this frame carries the step - which is every frame at
 * fps_mode 0. */
bool InvInterpBeginFrame();

/* How many 1/60 s ticks the ring's own present waits for: two at fps_mode 0,
 * one at fps_mode 1, none at fps_mode 2 where the panel paces the frame. The
 * caller puts gPresentMinTicks back to two straight after the present, the
 * way DrawPhaseGame does, so that FPSLOG and PROFLOG judge the frame against
 * the slot it really asked for. */
long InvFrameMinTicks();

// What S_DumpScreen returned, fed to the accumulator.
void InvInterpPresent(long ticks);

/* Time jumped rather than flowed: a mode opened or closed, the load/save
 * screen ran its own loop, the ring's list was rebuilt. The next step frame
 * draws live and the one after it starts blending again. `why` is one short
 * word, reaches INVLOG and is never null. */
void InvInterpCut(const char* why);

/* WHAT THIS FRAME DRAWS. Each one is handed the live value and returns it
 * unchanged whenever nothing is to be blended.
 *
 * `ring` is RING_INVENTORY or RING_AMMO and `n` the slot in that ring's
 * current_object_list; `invitem` is what stands in the slot, which is how a
 * rebuilt list is told apart from a moved one without a call site of its
 * own. */
ushort InvDrawSpin(long ring, long n, short invitem, ushort live);
long InvDrawBright(long ring, long n, short invitem, long live);
long InvDrawMove(long ring, long live);

/* AND THE SLOT THAT SLIDE BELONGS TO. curobjinlist is discrete and is
 * never blended - but it is HALF OF A PAIR with the slide, and the two
 * have to be drawn from the same generation or the row is put one slot
 * along while its offset still says otherwise. The reset makes that
 * literal: the step that sets objlistmovement back to 0 moves
 * curobjinlist in the same breath, so a frame drawing the new slot with
 * the old offset shows the whole row seven eighths of a slot to the side
 * for one frame.
 *
 * So the LAYOUT of the row - which icons stand where, and which name is
 * printed under them - asks this, and the ring's own state keeps the live
 * field. The option list under the row keeps the live one too, which is
 * not an oversight: at thirty draw_current_object_list has already run its
 * stepping block by the time handle_inventry_menu reads curobjinlist, so
 * the option text is one step ahead of the row THERE TOO, and this keeps
 * that relation instead of inventing a new one.
 *
 * `count` is numobjectsinlist as it stands NOW. A slot from before a list
 * was rebuilt - a combine, a separate - can be past the end of the new
 * one, and a row drawn from it would start on a stale entry for a frame.
 * Out of range hands the live slot back, which is the same self-policing
 * the per-slot `invitem` does for the spin. */
long InvDrawCur(long ring, long live, long count);
ushort InvDrawAmmoSpin(long i, short invitem, ushort live);
long InvDrawAmmoFade(long live);
long InvDrawCombineFade(long live);
long InvDrawNormalFade(long live);
long InvDrawExamine(long live);

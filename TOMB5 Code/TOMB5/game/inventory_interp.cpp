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
#include "inventory_interp.h"
#include "interpolate.h"		// InterpNoteMenuFrame - the one tween latch
#include "interpolate_math.h"		// InterpLerpAngle, InterpLerpLong, InterpRefreshAlpha
#include "gameflow.h"			// gfLevelId, for the log line
#include "../specific/function_stubs.h"	// Log, gLogEnabled
#include "../specific/output.h"		// ActiveFpsMode, PresentSlotMs
#include "../specific/time.h"		// TIME_SubTick - the display-bound blend factor

/* The design note for this whole file is in the header; what is written
 * down here is the arithmetic and the log. */

namespace
{
	const int RING_COUNT = 2;	// RING_INVENTORY and RING_AMMO
	const int SLOT_COUNT = 100;	// RINGME::current_object_list[100]
	const int AMMO_COUNT = 3;	// ammo_object_list[3] (game\newinv2.cpp)

	/* One slot of the snapshot. `invitem` is not interpolated - it is there
	 * so that a rebuilt list can be told from a moved one, the same job
	 * object_number does in the main interpolation snapshot: the combine
	 * ring and every separate/combine put different items in the same
	 * slots, and a spin blended across that would show a different object
	 * turning from where the old one stood. */
	struct SlotState
	{
		short invitem;
		ushort yrot;
		ushort bright;
	};

	struct RingState
	{
		SlotState slot[SLOT_COUNT];
		long move;		// objlistmovement
		long cur;		// curobjinlist - the jump marker, never blended
	};

	struct Snapshot
	{
		RingState ring[RING_COUNT];
		ushort ammoSpin[AMMO_COUNT];
		short ammoItem[AMMO_COUNT];
		long ammoFade;
		long combineFade;
		long normalFade;
		short examine;
	};

	InvInterpSource g_src = { { 0, 0 }, 0, 0, 0, 0, 0 };
	bool g_open = false;

	/* Two generations; see the header comment above for why. g_gen[0] is
	 * the state before the step this frame ran, g_gen[1] the state before
	 * the one before it. g_gens counts how many of them are real, so that
	 * the first step after a cut draws live and the second draws live for
	 * the fields that need the older pair. */
	Snapshot g_gen[2];
	int g_gens = 0;

	/* The accumulator, in 1/60s ticks, is ControlPhase's own shape.
	 *
	 * `for (framecount += nframes; framecount > 0; framecount -= 2)`
	 * (control.cpp) spends two ticks on a logic step and runs one as soon
	 * as the accumulator is positive. This is that loop with the body run
	 * at most once, because the ring's body draws as it steps and a
	 * second pass in one frame would draw into a poly list that is already
	 * being filled. A frame slower than one logic step therefore steps the
	 * ring once, which is exactly what thirty hertz does when a frame runs
	 * long, and the clamp below keeps a hitch from banking steps it would
	 * then spend on frames that are not late at all. */
	long g_acc = 0;
	long g_pending = 0;
	bool g_step = true;
	float g_alpha = 1.0F;

	// INVLOG. The second here is a logic second - thirty ring steps - because
	// the ring stops GlobalCounter and there is no other clock in it.
	int g_secFrames = 0;
	int g_secSteps = 0;
	int g_secTween = 0;
	float g_secAlphaMin = 1.0F;
	float g_secAlphaMax = 0.0F;
	unsigned long g_secKinds = 0;
	int g_secCuts = 0;
	int g_at = 0;
	int g_totalSteps = 0;
	int g_totalFrames = 0;

	/* What really moved on the in-between frames, by kind - INVLOG's
	 * `blend=` mask. The same reasoning as INTERPLOG's particle_kinds: a
	 * kind that is plainly on screen and missing from the mask is an
	 * accessor whose guard never fires, and there is no other way to see
	 * that from the picture, because a field that never blends simply
	 * looks like thirty. */
	enum
	{
		KIND_SPIN = 0,
		KIND_BRIGHT,
		KIND_MOVE,
		KIND_AMMO_SPIN,
		KIND_AMMO_FADE,
		KIND_COMBINE_FADE,
		KIND_NORMAL_FADE,
		KIND_EXAMINE,
		KIND_COUNT
	};

	const char* const g_kindName[KIND_COUNT] =
	{
		"spin", "bright", "slide", "ammo_spin", "ammo_fade",
		"combine_fade", "normal_fade", "examine"
	};

	void NoteKind(int kind)
	{
		if (gLogEnabled)
			g_secKinds |= 1UL << kind;
	}

	void LogSecond();

	/* One frame, counted - and it is counted at fps_mode 0 too, for
	 * comparison: a thirty hertz second of the ring must read frames=30
	 * steps=30 tween=0 against a sixty hertz second's frames=60 steps=30
	 * tween=30, and a line that only exists in one of the two modes cannot
	 * say so. It is the same rule INTERPLOG's counters follow. Behind
	 * gLogEnabled, so without -log nothing here moves. */
	void NoteFrame()
	{
		if (!gLogEnabled)
			return;

		g_secFrames++;
		g_totalFrames++;

		if (g_step)
		{
			g_secSteps++;
			g_totalSteps++;
		}
		else
			g_secTween++;

		if (g_alpha < g_secAlphaMin)
			g_secAlphaMin = g_alpha;

		if (g_alpha > g_secAlphaMax)
			g_secAlphaMax = g_alpha;

		if (g_secSteps >= 30)
			LogSecond();
	}

	/* The slot the ring's present asks for, in milliseconds, for the log
	 * lines alone. PresentSlotMs is the engine's single definition of "how
	 * long a frame of this kind is allowed to take", and it reads
	 * gPresentMinTicks, which is at its default two everywhere except
	 * inside the ring's own present - so the number is asked for with the
	 * ring's value briefly in place rather than worked out a second time
	 * here, where a second derivation could drift from the engine's own
	 * definition.
	 *
	 * -log only, and nothing else ever runs between the two assignments. */
	double RingSlotMs()
	{
		const long saved = gPresentMinTicks;

		gPresentMinTicks = InvFrameMinTicks();

		const double ms = PresentSlotMs();

		gPresentMinTicks = saved;
		return ms;
	}

	/* Is there anything to blend at all. `gens` is how many generations the
	 * field needs - one for what vanilla steps before its draw, two for what
	 * it steps after it (see the header). At fps_mode 0 the count never
	 * leaves zero, because InvInterpBeginFrame returns before it captures,
	 * and the mode is checked as well so that a rate changed while the ring
	 * stands open cannot leave a stale generation driving the picture. */
	bool Blending(int gens)
	{
		return g_open && g_gens >= gens && ActiveFpsMode() != 0;
	}

	/* For a field that vanilla steps before its draw, a frame at alpha 1 is
	 * the live one and the blend has nothing to add: `live` is already the
	 * value this frame is meant to show. Saying so here rather than leaving
	 * it to the lerp keeps INVLOG's `blend=` honest - a kind is named only
	 * on a frame where the picture really was moved. A field vanilla steps
	 * after its draw has no such frame: at alpha 1 it wants the last
	 * step's value and not the live one, which is why it carries a second
	 * generation. */
	bool BlendingAhead()
	{
		return g_alpha < 1.0F;
	}

	ushort BlendSpin(ushort a, ushort b)
	{
		// The short way round, and it is the engine's own lerp: a ring icon
		// spinning at 1022 a step crosses the wrap every 64 steps, and the
		// long way round there is a full turn backwards in one frame.
		return (ushort)InterpLerpAngle((short)a, (short)b, g_alpha);
	}

	void LogSecond()
	{
		if (!gLogEnabled || g_secFrames <= 0)
			return;

		char kinds[128];

		kinds[0] = 0;

		for (int k = 0; k < KIND_COUNT; k++)
		{
			if (!(g_secKinds & (1UL << k)))
				continue;

			if (kinds[0])
				strcat(kinds, ",");

			strcat(kinds, g_kindName[k]);
		}

		Log("INVLOG level=%d at=%ds frames=%d steps=%d tween=%d alpha=%.2f/%.2f "
			"budget=%.1f blend=%s cuts=%d",
			(int)gfLevelId, g_at, g_secFrames, g_secSteps, g_secTween,
			(double)(g_secAlphaMin <= g_secAlphaMax ? g_secAlphaMin : 1.0F),
			(double)(g_secAlphaMax >= g_secAlphaMin ? g_secAlphaMax : 1.0F),
			RingSlotMs(), kinds[0] ? kinds : "-", g_secCuts);

		g_at++;
		g_secFrames = 0;
		g_secSteps = 0;
		g_secTween = 0;
		g_secAlphaMin = 1.0F;
		g_secAlphaMax = 0.0F;
		g_secKinds = 0;
		g_secCuts = 0;
	}

	// One generation of the snapshot, taken immediately before the step that
	// is about to move it.
	void Capture(Snapshot* s)
	{
		for (int r = 0; r < RING_COUNT; r++)
		{
			const RINGME* ring = g_src.ring[r];

			if (!ring)
				continue;

			for (int n = 0; n < SLOT_COUNT; n++)
			{
				s->ring[r].slot[n].invitem = ring->current_object_list[n].invitem;
				s->ring[r].slot[n].yrot = ring->current_object_list[n].yrot;
				s->ring[r].slot[n].bright = ring->current_object_list[n].bright;
			}

			s->ring[r].move = ring->objlistmovement;
			s->ring[r].cur = ring->curobjinlist;
		}

		for (int i = 0; i < AMMO_COUNT; i++)
		{
			s->ammoSpin[i] = g_src.ammo ? g_src.ammo[i].yrot : (ushort)0;
			s->ammoItem[i] = g_src.ammo ? g_src.ammo[i].invitem : (short)0;
		}

		s->ammoFade = g_src.ammoFade ? *g_src.ammoFade : 0;
		s->combineFade = g_src.combineFade ? *g_src.combineFade : 0;
		s->normalFade = g_src.normalFade ? *g_src.normalFade : 0;
		s->examine = g_src.examine ? *g_src.examine : (short)0;
	}
}

void InvInterpOpen(const InvInterpSource* src, const char* why)
{
	g_src = *src;
	g_open = true;
	g_gens = 0;
	g_acc = 0;
	g_pending = 0;
	g_step = true;
	g_alpha = 1.0F;
	g_at = 0;
	g_totalSteps = 0;
	g_totalFrames = 0;
	g_secFrames = 0;
	g_secSteps = 0;
	g_secTween = 0;
	g_secAlphaMin = 1.0F;
	g_secAlphaMax = 0.0F;
	g_secKinds = 0;
	g_secCuts = 0;

	if (gLogEnabled)
		Log("INVLOG open level=%d why=%s fps_mode=%d min_ticks=%d budget=%.1f",
			(int)gfLevelId, why ? why : "?", (int)ActiveFpsMode(),
			(int)InvFrameMinTicks(), RingSlotMs());
}

void InvInterpClose()
{
	if (gLogEnabled)
	{
		LogSecond();
		Log("INVLOG close level=%d frames=%d steps=%d",
			(int)gfLevelId, g_totalFrames, g_totalSteps);
	}

	g_open = false;
	g_gens = 0;
	g_step = true;
	g_alpha = 1.0F;

	/* The latch goes down on the way out. The ring may have been on a
	 * tween frame when it was closed, and what follows it is a return into
	 * ControlPhase - whose next drawn frame sets the latch for itself - or, on
	 * the MainThread.ended path, whatever the shutdown draws. Neither should
	 * inherit a `true` that nothing of theirs can clear; it is the same
	 * argument InterpNoteClassicPresent makes for the modal loops. */
	InterpNoteMenuFrame(false);
}

long InvFrameMinTicks()
{
	const long mode = ActiveFpsMode();

	if (mode == 0)
		return 2;

	/* The same three answers GameFrameMinTicks gives a played frame, and for
	 * the same reasons - zero is "do not wait for a tick, because the present
	 * waits" and is only ever reached with a device that really blocks in
	 * Present (specific\output.cpp, ActiveFpsMode). */
	return mode == 2 ? 0 : 1;
}

bool InvInterpBeginFrame()
{
	/* fps_mode 0 is the ring as it was. One test, and nothing below this
	 * line runs: no accumulator, no snapshot, no latch. The present waits its
	 * two ticks (InvFrameMinTicks above), S_DumpScreen puts the tween latch
	 * down through InterpNoteClassicPresent exactly as it always did, and
	 * every accessor in this file answers the value it was handed. */
	if (ActiveFpsMode() == 0)
	{
		g_step = true;
		g_alpha = 1.0F;
		NoteFrame();
		return true;
	}

	/* The accumulator and the factor are both in interpolate_math.h, along
	 * with the rest of this feature's arithmetic, so that InterpTest runs
	 * the engine's own functions and not a copy of them. What is left here
	 * is the state they are handed and what the answer is used for. */
	g_step = InterpRingStep(&g_acc, g_pending);
	g_pending = 0;

	/* What the frame is drawn at.
	 *
	 * At fps_mode 2 it comes from the clock, through the same derivation
	 * used for the game (InterpRefreshAlpha, interpolate_math.h): the
	 * accumulator as it stands after this frame's step has been taken out
	 * of it, plus the remainder in the engine's own clock. The phase is the
	 * game's own - the last Sync() was inside the previous present, which is
	 * where DrawPhaseGame's is too.
	 *
	 * At fps_mode 1 the frames land on the ticks and the same formula returns
	 * exactly 0.5 and 1.0 by turns, so the parity is used directly and no
	 * clock is read: (-1 + 1 + 1)/2 on the frame whose step ran, (0 + 1 + 1)/2
	 * on the frame between. InterpTest asserts that the two agree. */
	g_alpha = InterpRingAlpha(ActiveFpsMode(), g_acc, TIME_SubTick(), g_step);

	/* The tween latch. The ring's present no longer waits two ticks, so
	 * S_DumpScreen's InterpNoteClassicPresent does not fire for it any more
	 * and nothing else would set this. Raised here, at the top of the body
	 * and before S_OutputPolyList, because that is where the guarded
	 * counters the ring reaches really run - GnFrameCounter (which the
	 * keypad's flash and the blinking text read), the screen fade, the
	 * weapon flash and the F12 debounce. */
	InterpNoteMenuFrame(!g_step);

	if (g_step)
	{
		// The older generation first, or it would be overwritten by the one
		// that is about to be taken.
		g_gen[1] = g_gen[0];
		Capture(&g_gen[0]);

		if (g_gens < 2)
			g_gens++;
	}

	NoteFrame();
	return g_step;
}

void InvInterpPresent(long ticks)
{
	g_pending += ticks;
}

void InvInterpCut(const char* why)
{
	if (gLogEnabled && g_open)
	{
		g_secCuts++;
		Log("INVLOG cut level=%d at=%ds why=%s steps=%d",
			(int)gfLevelId, g_at, why ? why : "?", g_totalSteps);
	}

	g_gens = 0;
}

/* What this frame draws.
 *
 * Six of the nine take the pair (previous, live) - the icon's spin and its
 * brightness, the ammo spin, and the combine, normal and examine fades -
 * because vanilla steps them before the icon is drawn, so the live value
 * is the one this frame would have shown. The other three - the row
 * slide, the current slot and the ammo fade, behind Blending(2) - take
 * the two previous generations, because vanilla steps them after the draw
 * and the live value belongs to the next frame. They do not stand in that
 * order below; see the header comment for the full picture. */

ushort InvDrawSpin(long ring, long n, short invitem, ushort live)
{
	if (!Blending(1) || !BlendingAhead() || ring < 0 || ring >= RING_COUNT || n < 0 || n >= SLOT_COUNT)
		return live;

	const SlotState& prev = g_gen[0].ring[ring].slot[n];

	// A rebuilt list put a different object in this slot: there is nothing
	// between the two angles to draw.
	if (prev.invitem != invitem)
		return live;

	NoteKind(KIND_SPIN);
	return BlendSpin(prev.yrot, live);
}

long InvDrawBright(long ring, long n, short invitem, long live)
{
	if (!Blending(1) || !BlendingAhead() || ring < 0 || ring >= RING_COUNT || n < 0 || n >= SLOT_COUNT)
		return live;

	const SlotState& prev = g_gen[0].ring[ring].slot[n];

	if (prev.invitem != invitem)
		return live;

	NoteKind(KIND_BRIGHT);
	return InterpLerpLong(prev.bright, live, g_alpha);
}

long InvDrawMove(long ring, long live)
{
	if (!Blending(2) || ring < 0 || ring >= RING_COUNT)
		return live;

	const long a = g_gen[1].ring[ring].move;
	const long b = g_gen[0].ring[ring].move;

	/* The jump: objlistmovement walks to 65536 in eight steps and is then set
	 * straight back to 0 with curobjinlist moved on by one - a reset, not a
	 * motion, and blending across it would slide the whole row halfway back
	 * for one frame. The pair that straddles it draws the value the step
	 * frame itself shows, so the picture stands for the two frames the jump
	 * takes and moves again on the next pair. */
	const bool jumped = g_gen[1].ring[ring].cur != g_gen[0].ring[ring].cur;

	if (!jumped)
		NoteKind(KIND_MOVE);

	return InterpRingSlide(a, b, jumped, g_alpha);
}

long InvDrawCur(long ring, long live, long count)
{
	/* The same gate InvDrawMove uses, and it has to be the same one: when the
	 * slide is drawn from the snapshot the slot must be, and when it is drawn
	 * live the slot must be. Nothing is interpolated here - a slot is a slot -
	 * it is only a question of which of the two a frame shows. */
	if (!Blending(2) || ring < 0 || ring >= RING_COUNT)
		return live;

	return InterpRingSlot(g_gen[0].ring[ring].cur, live, count);
}

ushort InvDrawAmmoSpin(long i, short invitem, ushort live)
{
	if (!Blending(1) || !BlendingAhead() || i < 0 || i >= AMMO_COUNT)
		return live;

	if (g_gen[0].ammoItem[i] != invitem)
		return live;

	NoteKind(KIND_AMMO_SPIN);
	return BlendSpin(g_gen[0].ammoSpin[i], live);
}

long InvDrawAmmoFade(long live)
{
	if (!Blending(2))
		return live;

	NoteKind(KIND_AMMO_FADE);
	return InterpLerpLong(g_gen[1].ammoFade, g_gen[0].ammoFade, g_alpha);
}

long InvDrawCombineFade(long live)
{
	if (!Blending(1) || !BlendingAhead())
		return live;

	NoteKind(KIND_COMBINE_FADE);
	return InterpLerpLong(g_gen[0].combineFade, live, g_alpha);
}

long InvDrawNormalFade(long live)
{
	if (!Blending(1) || !BlendingAhead())
		return live;

	NoteKind(KIND_NORMAL_FADE);
	return InterpLerpLong(g_gen[0].normalFade, live, g_alpha);
}

long InvDrawExamine(long live)
{
	if (!Blending(1) || !BlendingAhead())
		return live;

	/* The examine screen opens at 0 and ramps to 128 in sixteen steps. A
	 * frame whose previous generation was the closed state is the opening
	 * and is a cut of its own (the ring calls InvInterpCut for it), so what
	 * is left here is the ramp and its ceiling, both of which blend. */
	NoteKind(KIND_EXAMINE);
	return InterpLerpLong(g_gen[0].examine, live, g_alpha);
}

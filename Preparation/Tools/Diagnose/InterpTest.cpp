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

/* InterpTest - the arithmetic of the 60 FPS interpolation, checked without a
 * game (#148, order 2). Build with bit.bat, run with no arguments.
 *
 * WHAT IT CHECKS, and why these and not others: order 2 has exactly one part
 * that can be got wrong silently. The snapshot either works or the picture
 * shakes; the three call sites either compile or they do not; but a short
 * angle blended THE LONG WAY ROUND is invisible almost all the time and looks
 * like a rendering glitch when it is not - one enemy, facing roughly south,
 * spinning backwards for half a tick every second frame. It has to be proven
 * at the boundary, and the boundary is a wrap that a play test will not put a
 * body on when you want it to.
 *
 * So this includes the ENGINE'S OWN HEADER - not a copy of the functions -
 * and asserts on it. There is no twin to keep in step.
 *
 * It is also the cadence proof: the alpha rule of DrawPhaseGame is four lines
 * of integer arithmetic on framecount and it can be run here for a whole
 * simulated minute, at both frame rates and through a hitch, which is the one
 * way to see that the drawn states really are even half steps and that
 * fps_mode 0 never produces anything but 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"

static int g_fail = 0;
static int g_checks = 0;

static void ok(bool cond, const char* what, long got, long want)
{
	g_checks++;

	if (!cond)
	{
		g_fail++;
		printf("  FAIL %-58s got %ld, want %ld\n", what, got, want);
	}
}

static void okf(bool cond, const char* what, double got, double want)
{
	g_checks++;

	if (!cond)
	{
		g_fail++;
		printf("  FAIL %-58s got %.4f, want %.4f\n", what, got, want);
	}
}

/* ------------------------------------------------------------- the angles -- */

/* The engine's own turn direction, worked out independently of the function
 * under test: which way round is shorter, as a sign. -1 anticlockwise, +1
 * clockwise, and 0 when the two angles are the same. Half a turn has no
 * shorter way and is reported as -1 to match the wrap's own choice. */
static int shorter_way(short a, short b)
{
	long d = (long)b - (long)a;		// -65535 .. 65535, no wrap

	while (d > 32768) d -= 65536;
	while (d < -32768) d += 65536;

	if (d == 0)
		return 0;

	return d > 0 ? 1 : -1;
}

static void angles()
{
	printf("angles - the short way round\n");

	// The plain cases first, so that a failure here says the arithmetic is
	// broken rather than the wrap handling.
	ok(InterpLerpAngle(0, 16384, 0.5F) == 8192, "0 -> 16384 at 0.5",
		InterpLerpAngle(0, 16384, 0.5F), 8192);
	ok(InterpLerpAngle(0, -16384, 0.5F) == -8192, "0 -> -16384 at 0.5",
		InterpLerpAngle(0, -16384, 0.5F), -8192);
	ok(InterpLerpAngle(1000, 2000, 0.5F) == 1500, "1000 -> 2000 at 0.5",
		InterpLerpAngle(1000, 2000, 0.5F), 1500);

	// The endpoints have to be exact, or every frame that lands on a logic
	// tick would be a hair off the state the logic holds.
	ok(InterpLerpAngle(12345, -20000, 0.0F) == 12345, "alpha 0 is the previous state",
		InterpLerpAngle(12345, -20000, 0.0F), 12345);
	ok(InterpLerpAngle(12345, -20000, 1.0F) == -20000, "alpha 1 is the live state",
		InterpLerpAngle(12345, -20000, 1.0F), -20000);

	/* THE WRAP. 32000 to -32000 is 1536 units clockwise over the top, not
	 * 64000 units anticlockwise through zero. Halfway is 32768, which is
	 * -32768 as a short. */
	ok(InterpLerpAngle(32000, -32000, 0.5F) == -32768, "32000 -> -32000 goes OVER 32767",
		InterpLerpAngle(32000, -32000, 0.5F), -32768);
	ok(InterpLerpAngle(-32000, 32000, 0.5F) == -32768, "-32000 -> 32000 goes UNDER -32768",
		InterpLerpAngle(-32000, 32000, 0.5F), -32768);

	// A quarter of the way over the same wrap: 32000 + 384.
	ok(InterpLerpAngle(32000, -32000, 0.25F) == 32384, "32000 -> -32000 at 0.25",
		InterpLerpAngle(32000, -32000, 0.25F), 32384);

	// And the same wrap read the other way: three-quarters of the way from
	// 32000 is 32000 + 1152 = 33152, i.e. -32384.
	ok(InterpLerpAngle(32000, -32000, 0.75F) == -32384, "32000 -> -32000 at 0.75",
		InterpLerpAngle(32000, -32000, 0.75F), -32384);

	// Zero crossed from below: -100 to +100 is 200 forwards, not 65336 back.
	ok(InterpLerpAngle(-100, 100, 0.5F) == 0, "-100 -> 100 crosses zero",
		InterpLerpAngle(-100, 100, 0.5F), 0);

	/* EXACTLY HALF A TURN. 32768 as a short is -32768, so the wrap chooses
	 * anticlockwise. Arbitrary, deterministic, and the only requirement. */
	ok(InterpLerpAngle(0, -32768, 0.5F) == -16384, "half a turn is anticlockwise",
		InterpLerpAngle(0, -32768, 0.5F), -16384);

	/* THE SWEEP. Every pair of angles on a 512-unit lattice - 16,384 pairs -
	 * blended at a half, asserting three things at once:
	 *
	 *   the direction is the shorter one,
	 *   the halfway point is no further from either end than the whole way,
	 *   and it is within one unit of half the true short distance.
	 *
	 * The last of those is what would catch a sign error that happens to
	 * pick the right direction, and the first is what would catch the
	 * -64000 bug that this whole function exists to prevent. */
	{
		int bad_dir = 0, bad_dist = 0, pairs = 0, halfturns = 0;

		for (long a = -32768; a < 32768; a += 512)
		{
			for (long b = -32768; b < 32768; b += 512)
			{
				const short m = InterpLerpAngle((short)a, (short)b, 0.5F);
				const int wantDir = shorter_way((short)a, (short)b);
				const int gotDir = shorter_way((short)a, m);
				long d = b - a;
				long step;

				while (d > 32768) d -= 65536;
				while (d < -32768) d += 65536;

				/* EXACTLY HALF A TURN HAS NO SHORTER WAY, so there is
				 * nothing here for a sweep to assert - both directions are
				 * equally right and the wrap picks one. Which one it picks
				 * is asserted on its own above; here the pair is counted
				 * and stepped over, so that the sweep says something true
				 * about the 16,256 pairs that do have an answer instead of
				 * something false about all of them. */
				if (labs(d) == 32768)
				{
					halfturns++;
					continue;
				}

				pairs++;

				if (a != b && wantDir != 0 && gotDir != 0 && gotDir != wantDir)
					bad_dir++;

				step = (long)m - a;

				while (step > 32768) step -= 65536;
				while (step < -32768) step += 65536;

				if (labs(step * 2 - d) > 2)
					bad_dist++;
			}
		}

		printf("  sweep: %d pairs, %d exact half turns stepped over\n", pairs, halfturns);
		ok(bad_dir == 0, "sweep: every pair turns the short way", bad_dir, 0);
		ok(bad_dist == 0, "sweep: every pair lands within a unit of halfway", bad_dist, 0);
	}
}

/* ---------------------------------------------------------- the positions -- */

static void positions()
{
	printf("positions\n");

	ok(InterpLerpLong(0, 1024, 0.5F) == 512, "0 -> 1024 at 0.5",
		InterpLerpLong(0, 1024, 0.5F), 512);
	ok(InterpLerpLong(-1024, 1024, 0.5F) == 0, "-1024 -> 1024 at 0.5",
		InterpLerpLong(-1024, 1024, 0.5F), 0);
	ok(InterpLerpLong(7, 7, 0.5F) == 7, "a value that did not move",
		InterpLerpLong(7, 7, 0.5F), 7);

	// The endpoints, again exactly - a frame at alpha 1 must draw the live
	// position to the unit or fps_mode 0 would not be byte-identical.
	ok(InterpLerpLong(123456, -98765, 1.0F) == -98765, "alpha 1 is the live position",
		InterpLerpLong(123456, -98765, 1.0F), -98765);
	ok(InterpLerpLong(123456, -98765, 0.0F) == 123456, "alpha 0 is the previous position",
		InterpLerpLong(123456, -98765, 0.0F), 123456);

	/* The truncation, both signs. A single unit split in half is zero
	 * forwards and zero backwards - it truncates TOWARD ZERO, so a body
	 * moving one unit per tick stands still on the tween frame and jumps
	 * one unit on the live one, rather than drifting. */
	ok(InterpLerpLong(0, 1, 0.5F) == 0, "one unit forwards truncates to nothing",
		InterpLerpLong(0, 1, 0.5F), 0);
	ok(InterpLerpLong(0, -1, 0.5F) == 0, "one unit backwards truncates to nothing",
		InterpLerpLong(0, -1, 0.5F), 0);

	/* THE WHOLE COORDINATE RANGE OF A TR5 LEVEL. A room grid is at most 100
	 * sectors a side and y runs deep; this is well past anything a level
	 * file holds, and it is here because the multiply is done in float and a
	 * float has 24 bits of mantissa. At 2 million units the step between
	 * representable floats is still well under a unit. */
	{
		int bad = 0;

		for (long a = -2000000; a <= 2000000; a += 100003)
		{
			for (long d = -4096; d <= 4096; d += 257)
			{
				const long m = InterpLerpLong(a, a + d, 0.5F);

				if (labs((m - a) * 2 - d) > 2)
					bad++;
			}
		}

		ok(bad == 0, "sweep: halfway is halfway over the whole level range", bad, 0);
	}
}

/* ------------------------------------------------------------ the snapping -- */

static void thresholds()
{
	printf("snap thresholds\n");

	ok(INTERP_ITEM_SNAP == 512, "an item snaps past half a sector",
		INTERP_ITEM_SNAP, 512);
	ok(INTERP_CAM_SNAP == 1024, "the camera snaps past a whole sector",
		INTERP_CAM_SNAP, 1024);

	ok(InterpManhattan(3, -4, 5) == 12, "manhattan takes the absolute values",
		InterpManhattan(3, -4, 5), 12);
	ok(InterpManhattan(0, 0, 0) == 0, "manhattan of nothing", InterpManhattan(0, 0, 0), 0);

	/* IT MAY SNAP SOONER THAN THE TRUE DISTANCE WOULD, NEVER LATER. That is
	 * the whole licence for using a Manhattan sum in place of a length, and
	 * it is worth an assertion rather than a claim. */
	{
		int bad = 0;

		for (long x = -700; x <= 700; x += 37)
			for (long y = -700; y <= 700; y += 41)
				for (long z = -700; z <= 700; z += 43)
				{
					const double real = sqrt((double)x * x + (double)y * y + (double)z * z);

					if ((double)InterpManhattan(x, y, z) < real - 1e-9)
						bad++;
				}

		ok(bad == 0, "manhattan is never smaller than the true distance", bad, 0);
	}

	/* THE CASES THE RULE IS FOR, at the engine's own numbers.
	 *
	 * Lara's terminal fall is 130-odd units a tick and a run is about 32 -
	 * both far under the threshold, so ordinary movement always
	 * interpolates. A TranslateItem across one sector is 1024 and a flipmap
	 * move is further still - both over it, so both snap. */
	ok(InterpManhattan(0, -140, 0) <= INTERP_ITEM_SNAP, "a terminal fall interpolates",
		InterpManhattan(0, -140, 0), INTERP_ITEM_SNAP);
	ok(InterpManhattan(32, 0, 0) <= INTERP_ITEM_SNAP, "a run interpolates",
		InterpManhattan(32, 0, 0), INTERP_ITEM_SNAP);
	ok(InterpManhattan(1024, 0, 0) > INTERP_ITEM_SNAP, "a sector-wide teleport snaps",
		InterpManhattan(1024, 0, 0), INTERP_ITEM_SNAP);
	ok(InterpManhattan(0, 1024, 0) > INTERP_ITEM_SNAP, "a floor-height teleport snaps",
		InterpManhattan(0, 1024, 0), INTERP_ITEM_SNAP);

	// And the edge itself, both sides of it.
	ok(InterpManhattan(512, 0, 0) <= INTERP_ITEM_SNAP, "exactly 512 still interpolates",
		InterpManhattan(512, 0, 0), INTERP_ITEM_SNAP);
	ok(InterpManhattan(513, 0, 0) > INTERP_ITEM_SNAP, "513 snaps",
		InterpManhattan(513, 0, 0), INTERP_ITEM_SNAP);
}

/* ------------------------------------------------------------- the cadence -- */

/* DrawPhaseGame's alpha rule and ControlPhase's accumulator, transcribed as
 * the four lines they are, and run for a simulated minute.
 *
 *   alpha       = framecount < 0 ? 0.5 : 1     (game\interpolate.cpp)
 *   framecount += nframes; while (> 0) tick, framecount -= 2   (control.cpp)
 *
 * What has to come out of it: at sixty, half the frames tween and the drawn
 * state advances in even half steps; at thirty, nothing tweens at all; and
 * over a whole minute the number of TICKS is the same in both, because the
 * simulation is what must not change. */
static void cadence()
{
	printf("cadence\n");

	for (int mode = 0; mode <= 1; mode++)
	{
		const int nframes = mode ? 1 : 2;	// what S_DumpScreen returns
		long framecount = 0;
		int frames = 0, tween = 0, ticks = 0;
		double drawn = 0;			// the state the frame drew, in ticks
		double lastDrawn = 0;
		double worstStep = 0, bestStep = 1e9;
		double state = 0;			// the logic's own state, in ticks

		for (int i = 0; i < 60 * (mode ? 60 : 30); i++)
		{
			const float alpha = mode && framecount < 0 ? 0.5F : 1.0F;

			// what this frame draws: the live state, or halfway back
			drawn = state - (1.0 - alpha);
			frames++;

			if (alpha != 1.0F)
				tween++;

			if (i)
			{
				const double step = drawn - lastDrawn;

				if (step > worstStep) worstStep = step;
				if (step < bestStep) bestStep = step;
			}

			lastDrawn = drawn;

			for (framecount += nframes; framecount > 0; framecount -= 2)
			{
				ticks++;
				state += 1.0;
			}
		}

		printf("  fps_mode %d: frames=%d tween=%d ticks=%d step %.2f..%.2f ticks\n",
			mode, frames, tween, ticks, bestStep, worstStep);

		ok(ticks == 1800, mode ? "60 Hz: 1800 logic ticks in a minute"
			: "30 Hz: 1800 logic ticks in a minute", ticks, 1800);

		if (mode)
		{
			ok(tween == frames / 2, "60 Hz: exactly half the frames are tween frames",
				tween, frames / 2);
			okf(fabs(worstStep - 0.5) < 1e-9 && fabs(bestStep - 0.5) < 1e-9,
				"60 Hz: every drawn step is exactly half a tick", worstStep, 0.5);
		}
		else
		{
			ok(tween == 0, "30 Hz: not one frame is interpolated", tween, 0);
			okf(fabs(worstStep - 1.0) < 1e-9 && fabs(bestStep - 1.0) < 1e-9,
				"30 Hz: every drawn step is a whole tick", worstStep, 1.0);
		}
	}

	/* A HITCH. S_DumpScreen returns how many 1/60 s ticks the frame really
	 * took, so a frame that took four of them feeds four. The accumulator
	 * must catch up rather than drift, and the alpha rule must not get stuck
	 * on one side: after a hitch the cadence has to come back on its own. */
	{
		long framecount = 0;
		int ticks = 0, frames = 0;
		int nframes = 1;

		for (int i = 0; i < 600; i++)
		{
			frames++;

			if (i == 100 || i == 101)	// two frames that each took four ticks
				nframes = 4;
			else
				nframes = 1;

			for (framecount += nframes; framecount > 0; framecount -= 2)
				ticks++;
		}

		// 600 frames of one tick would be 300 ticks; the hitch fed six
		// extra ticks, which is three extra logic steps.
		ok(ticks == 303, "a hitch is caught up, not dropped and not doubled", ticks, 303);
		ok(framecount == 0 || framecount == -1,
			"the accumulator comes back to the cadence", framecount, 0);
	}
}

/* -------------------------------------------------------- the counters -- */

/* ORDER 3, and the whole of its claim in one table.
 *
 * Every counter in the build that used to step once per DRAWN frame now asks
 * InterpIsTweenFrame() first, and the engine's rule for that is exactly
 *
 *     tween = (alpha < 1), and alpha = fps_mode && framecount < 0 ? 0.5 : 1
 *
 * latched once per frame at the top of DrawPhaseGame (game\interpolate.cpp).
 * What has to come out of it is that a whole simulated SECOND of wall-clock
 * time produces the SAME number of steps at thirty and at sixty - because a
 * fade that takes 24 frames at thirty has to take 24 steps and 48 frames at
 * sixty, or the death screen would come half a second early.
 *
 * The five counters below are the five TICKLOG reports, and each is modelled
 * the way its own code now reads:
 *
 *   fade        Fade()          - a grey level walking to a target by a
 *                                 speed, and the thing DoLevel waits on
 *   screenfade  DoScreenFade()  - nine steps, then DoFade = 2. RenderLoadPic
 *                                 cannot leave its loop until this happens,
 *                                 which is why a stuck guard would HANG the
 *                                 game and not merely slow a fade down
 *   weather     DoRain/DoSnow/DoUwEffect - one simulation step
 *   legend      gfLegendTime--  - the level caption's dwell
 *   afterdeath  the corpse fade - 128 steps to KillItem
 *
 * The interesting one is `screenfade`: what is asserted is not only that the
 * step count matches but that the number of PRESENTS a fade lasts DOUBLES at
 * sixty, which is the same statement said in wall-clock time. */
static void counters()
{
	printf("counters - what steps per logic tick (order 3)\n");

	struct Run
	{
		int frames;		// presents in the simulated second
		int ticks;		// logic ticks in it
		int steps;		// guarded counter steps
		int fadePresents;	// presents from SetFade to DoFade == 2
	};

	Run r[2];

	for (int mode = 0; mode <= 1; mode++)
	{
		const int nframes = mode ? 1 : 2;
		const int presents = mode ? 60 : 30;	// one wall-clock second
		long framecount = 0;

		// the guarded counter and the nine-step screen fade
		int steps = 0;
		int fadeCnt = 0, doFade = 1, fadePresents = 0;

		r[mode].frames = 0;
		r[mode].ticks = 0;
		r[mode].fadePresents = 0;

		for (int i = 0; i < presents; i++)
		{
			/* The engine's own two lines, transcribed: InterpAlphaNow and
			 * the latch InterpBeginFrame sets from it. */
			const float alpha = (mode && framecount < 0) ? 0.5F : 1.0F;
			const bool tween = alpha < 1.0F;

			r[mode].frames++;

			// any guarded counter - Fade, the weather, the legend, the
			// corpse fade: they all reduce to this one test
			if (!tween)
				steps++;

			// DoScreenFade, as it now stands, including the wait on it
			if (doFade == 1)
			{
				fadePresents++;

				if (!tween)
				{
					fadeCnt++;

					if (fadeCnt > 8)
						doFade = 2;
				}
			}

			for (framecount += nframes; framecount > 0; framecount -= 2)
				r[mode].ticks++;
		}

		r[mode].steps = steps;
		r[mode].fadePresents = fadePresents;

		printf("  fps_mode %d: presents=%d ticks=%d steps=%d  a 9-step fade took %d presents\n",
			mode, r[mode].frames, r[mode].ticks, r[mode].steps, r[mode].fadePresents);
	}

	ok(r[0].frames == 30 && r[1].frames == 60,
		"a second is 30 presents at thirty and 60 at sixty", r[1].frames, 60);
	ok(r[0].ticks == r[1].ticks && r[0].ticks == 30,
		"the same second is 30 logic ticks in both modes", r[1].ticks, 30);
	ok(r[0].steps == r[1].steps,
		"a guarded counter steps the same number of times in both modes",
		r[1].steps, r[0].steps);
	ok(r[1].steps == 30,
		"and that number is the tick count, not the frame count", r[1].steps, 30);
	/* 2n - 1 AND NOT 2n, and the missing one is real rather than a rounding
	 * of the model. The fade begins on a live frame and ends on one, so at
	 * sixty the nine steps are spread over nine live frames with eight
	 * in-between frames wedged between them - seventeen presents, not
	 * eighteen. It lasts nine LOGIC TICKS in both modes, which is what the
	 * user's rule asks for; in wall-clock time the sixty hertz fade ends one
	 * sixtieth of a second sooner because it does not sit through a trailing
	 * in-between frame it has nothing left to draw on. */
	ok(r[1].fadePresents == 2 * r[0].fadePresents - 1,
		"the screen fade spans the same nine ticks - 2n-1 presents at sixty",
		r[1].fadePresents, 2 * r[0].fadePresents - 1);

	/* AND THE SAME AGAIN THROUGH A HITCH. A frame that took four ticks feeds
	 * four into the accumulator, which then runs two logic steps in one
	 * ControlPhase and comes back to the cadence. A counter must not lose a
	 * step over that: the number of guarded steps in the run has to be the
	 * number of TICKS, whatever the frame times did. */
	{
		long framecount = 0;
		int steps = 0, ticks = 0;

		for (int i = 0; i < 600; i++)
		{
			const bool tween = framecount < 0;

			if (!tween)
				steps++;

			const int nframes = (i == 100 || i == 101) ? 4 : 1;

			for (framecount += nframes; framecount > 0; framecount -= 2)
				ticks++;
		}

		/* NOT EQUAL, and this is the one honest limit of the rule. Each of
		 * the two slow frames here fed four sixtieths into the accumulator
		 * and therefore ran TWO logic ticks, while a counter that steps once
		 * per drawn frame can only step once for it. So the counter ends two
		 * behind the tick count - one for each doubled frame - and never
		 * catches up.
		 *
		 * That is deliberate and it is what the thirty hertz engine does
		 * too when a frame runs long: a fade that stutters with the frame
		 * rate is right, a fade that sprints to catch up is not. It is also
		 * why the guard is a FRAME test and not a tick counter. */
		printf("  through a hitch: steps=%d ticks=%d (a doubled frame runs two ticks, steps once)\n",
			steps, ticks);
		ok(ticks == 303, "the hitch runs its extra logic ticks", ticks, 303);
		ok(steps == 301, "and the counter is behind by one per doubled frame, no more",
			steps, 301);
		ok(framecount == 0 || framecount == -1,
			"the cadence comes back after the hitch", framecount, 0);
	}

	/* fps_mode 0 TAKES THE WHOLE THING OUT. At thirty the accumulator never
	 * ends a call below zero, so the latch is never set and every guard in
	 * the build reads false on every frame - which is the byte-identical
	 * claim (plan, proof 6 / K11) said as arithmetic. */
	{
		long framecount = 0;
		int tween = 0;

		for (int i = 0; i < 1800; i++)
		{
			if (framecount < 0)
				tween++;

			for (framecount += 2; framecount > 0; framecount -= 2)
				;
		}

		ok(tween == 0, "fps_mode 0: not one frame is ever a tween frame", tween, 0);
	}
}

/* ---------------------------------------------------------------- the pose --
 *
 * ORDER 4 HAS TWO CLAIMS AND THIS IS WHERE BOTH ARE SETTLED.
 *
 * CLAIM ONE: replacing four hard-wired branches with one float factor does
 * not change a single drawn frame at thirty hertz. The branches were
 * `IM_rate == 2 || (IM_frac == 2 && IM_rate == 4)` -> a half, `IM_frac == 1`
 * -> a quarter, otherwise three quarters, written out twelve times each in
 * four functions; they are transcribed here as the reference and the engine's
 * own header is asked for the same numbers. Both matrix stacks are checked,
 * because they round differently on purpose: the float stack keeps vanilla's
 * three EXPRESSIONS (a rearrangement of them would differ in the last bit)
 * and the integer stack keeps vanilla's SHIFTS (which floor, where a cast
 * would truncate - the two differ on every negative odd number).
 *
 * CLAIM TWO: the pose drawn between two logic ticks is the MIDPOINT of the
 * two poses the game draws at thirty, and never anything else. What is
 * asserted for that is monotonicity - `key + blend` may never go backwards
 * over a whole animation walked at sixty - and identity at the tick frames,
 * where it must be vanilla's own factor to the bit.
 *
 * WHICH (frac, rate) PAIRS ARE REAL. Measured 2026-09-07 by walking the
 * animation tables of all sixteen level files of the retail game: rates 1, 2,
 * 3, 4, 5, 6, 9 and 10 all occur (3619, 3350, 35, 5399, 3187, 2, 2 and 120
 * animations), and GetFrames can shorten any of them at an animation's last
 * interval. The counts are in the table below. They are the reason vanilla's
 * classification is KEPT rather than replaced by frac/rate: at rate 5 the two
 * disagree wildly - vanilla draws frames 1..4 of a five frame interval at
 * .25, .75, .75, .75 - and a third of every animation in TR5 would move
 * differently from the day it shipped. */

/* Vanilla's own factor, transcribed from the branches of game\draw.cpp as
 * they stood before order 4. This is the reference, not the thing tested. */
static float vanilla_alpha(long frac, long rate)
{
	if (rate == 2 || (frac == 2 && rate == 4))
		return 0.5F;

	if (frac == 1)
		return 0.25F;

	return 0.75F;
}

// Vanilla's float blend, expression for expression (aInterpolateMatrix).
static float vanilla_blend_float(float m, float i, long frac, long rate)
{
	if (rate == 2 || (frac == 2 && rate == 4))
		return (i + m) * 0.5F;

	if (frac == 1)
		return m + (i - m) * 0.25F;

	return i - (i - m) * 0.25F;
}

// Vanilla's integer blend, shift for shift (InterpolateMatrix).
static long vanilla_blend_long(long m, long i, long frac, long rate)
{
	if (rate == 2 || (frac == 2 && rate == 4))
		return m + ((i - m) >> 1);

	if (frac == 1)
		return m + ((i - m) >> 2);

	return i - ((i - m) >> 2);
}

static void pose_factor()
{
	printf("pose - the one factor against the four branches (order 4)\n");

	/* The measured population of the sixteen level files, so that the table
	 * says how much of the real game each row stands for. A pair with no
	 * count still has to agree - the randomizer moves animations between
	 * levels - but the counts say where a disagreement would be felt. */
	struct Pair { long frac, rate; long long frames; };
	static const Pair real[] =
	{
		{ 1,  1,       85 }, { 1,  2,    42928 }, { 1,  3,      583 },
		{ 1,  4,    52058 }, { 1,  5,    35078 }, { 1,  6,        4 },
		{ 1,  9,        2 }, { 1, 10,      318 }, { 2,  2,       99 },
		{ 2,  3,      568 }, { 2,  4,    50781 }, { 2,  5,    34490 },
		{ 2,  6,        2 }, { 2,  9,        2 }, { 2, 10,      318 },
		{ 3,  3,       40 }, { 3,  4,    49674 }, { 3,  5,    33920 },
		{ 3,  6,        2 }, { 3,  9,        2 }, { 3, 10,      318 },
		{ 4,  5,    33433 }, { 4,  6,        2 }, { 4,  9,        2 },
		{ 4, 10,      318 }, { 5,  6,        2 }, { 5,  9,        2 },
		{ 5, 10,      318 }, { 6,  9,        2 }, { 6, 10,      304 },
		{ 7,  9,        2 }, { 7, 10,      290 }, { 8, 10,      262 },
		{ 9, 10,      262 }
	};

	/* Matrix elements to blend. A rotation element (+-16384), a translation
	 * (world units), the negative odd numbers that separate a shift from a
	 * cast, and the pairs that separate `(a + b) / 2` from `a + (b - a) / 2`. */
	static const long v[] =
	{
		0, 1, -1, 2, -2, 3, -3, 7, -7, 16384, -16384, 16383, -16383,
		1023, -1023, 100001, -100001, 524287, -524288, 12345, -12345
	};
	const int nv = (int)(sizeof(v) / sizeof(v[0]));

	printf("  frac rate   factor   frames in the 16 levels   float   long\n");

	long long covered = 0;

	for (int p = 0; p < (int)(sizeof(real) / sizeof(real[0])); p++)
	{
		const long frac = real[p].frac;
		const long rate = real[p].rate;
		const float want = vanilla_alpha(frac, rate);
		const float got = InterpKeyAlpha(frac, rate);
		long badF = 0, badL = 0;

		okf(got == want, "the factor is vanilla's own", got, want);

		const long a8 = InterpEighths(got);

		for (int i = 0; i < nv; i++)
		{
			for (int j = 0; j < nv; j++)
			{
				const float wf = vanilla_blend_float((float)v[i], (float)v[j], frac, rate);
				const float gf = InterpBlendFloat((float)v[i], (float)v[j], got);
				const long wl = vanilla_blend_long(v[i], v[j], frac, rate);
				const long gl = InterpBlendLong(v[i], v[j], a8);

				if (gf != wf)
					badF++;

				if (gl != wl)
					badL++;
			}
		}

		ok(badF == 0, "float stack: not one element differs", badF, 0);
		ok(badL == 0, "integer stack: not one element differs", badL, 0);

		printf("   %2ld   %2ld    %.2f     %11lld            %s   %s\n",
			frac, rate, (double)got, real[p].frames,
			badF ? "DIFFER" : "same  ", badL ? "DIFFER" : "same");

		covered += real[p].frames;
	}

	printf("  -> %lld animation frames of the retail game, %d matrix element pairs each,\n"
		"     max difference in units: 0\n", covered, nv * nv);

	/* And the same over a domain far wider than any level file, because the
	 * randomizer moves animations between levels and an imported TR3 enemy
	 * brings its own rates with it. */
	long wider = 0;

	for (long rate = 1; rate <= 32; rate++)
	{
		for (long frac = 1; frac < rate; frac++)
		{
			const float got = InterpKeyAlpha(frac, rate);
			const long a8 = InterpEighths(got);

			for (int i = 0; i < nv; i++)
				for (int j = 0; j < nv; j++)
					if (InterpBlendFloat((float)v[i], (float)v[j], got) !=
						vanilla_blend_float((float)v[i], (float)v[j], frac, rate) ||
						InterpBlendLong(v[i], v[j], a8) !=
						vanilla_blend_long(v[i], v[j], frac, rate))
						wider++;
		}
	}

	ok(wider == 0, "every (frac, rate) up to 32: identical to the branches", wider, 0);

	/* THE ARM MATRIX WROTE ITS HALF DIFFERENTLY. InterpolateArmMatrix's
	 * half branch is `(a + b) >> 1` where InterpolateMatrix's is
	 * `a + ((b - a) >> 1)`, and the one loop now serves both, so the two have
	 * to be the same expression for every pair of values - which they are,
	 * because `a` is an integer and a shift is a floor:
	 *
	 *     a + floor((b - a) / 2) = floor((2a + b - a) / 2) = floor((a + b) / 2)
	 *
	 * It is worth an assertion rather than an argument: the arm matrices are
	 * the hands, and a unit of drift there is a gun that does not sit in the
	 * fingers. */
	{
		long armDiff = 0;

		for (int i = 0; i < nv; i++)
			for (int j = 0; j < nv; j++)
				if (((v[i] + v[j]) >> 1) != InterpBlendLong(v[i], v[j], 4))
					armDiff++;

		ok(armDiff == 0, "the arm matrix's own half is the same half", armDiff, 0);
	}

	// The three factors are quarters, so they are exact eighths.
	ok(InterpEighths(0.25F) == 2, "a quarter is two eighths", InterpEighths(0.25F), 2);
	ok(InterpEighths(0.5F) == 4, "a half is four eighths", InterpEighths(0.5F), 4);
	ok(InterpEighths(0.75F) == 6, "three quarters is six eighths", InterpEighths(0.75F), 6);

	// frac 0 is "no blend at all" and must not be classified as a quarter.
	okf(InterpKeyAlpha(0, 4) == 0.0F, "frac 0 is no blend", InterpKeyAlpha(0, 4), 0.0);
}

/* ------------------------------------------------------- the pose in time -- */

static void pose_time()
{
	printf("\npose - the midpoint of the two poses a tick apart (order 4)\n");

	/* An animation of nine frames, walked at sixty hertz: tick, tween, tick,
	 * tween. `key + blend` is where the pose stands in KEY FRAME units and it
	 * is the one number that has to behave - never backwards, never past the
	 * pair it names, and exactly vanilla's own value on every tick frame. */
	const long rates[] = { 1, 2, 4, 5 };

	for (int r = 0; r < 4; r++)
	{
		const long rate = rates[r];
		const long base = 0;
		const long end = 1000;			// no last-interval clamp
		double last = -1.0;
		long backwards = 0;

		printf("  rate %ld:", rate);

		for (long f = 0; f <= 8; f++)
		{
			// the TICK frame: vanilla's own pair and factor
			const long frac = f % rate;
			const double tickPos = (double)(f / rate) +
				(frac ? (double)vanilla_alpha(frac, InterpKeyRate(rate, f / rate, end)) : 0.0);

			if (tickPos < last)
				backwards++;

			last = tickPos;
			printf(" %.3f", tickPos);

			// the TWEEN frame that follows it, between f and f + 1
			long key = 0;
			float blend = 0.0F;

			if (InterpPoseAt(f, f + 1, base, rate, end, 0.5F, &key, &blend))
			{
				const double pos = (double)key + (double)blend;

				if (pos < last)
					backwards++;

				last = pos;
				printf(" [%.3f]", pos);
			}
			else
				printf(" [-]");
		}

		printf("\n");
		ok(backwards == 0, "the pose never walks backwards over the animation", backwards, 0);
	}

	printf("  (in brackets: the frame between two ticks. Rate 4 comes out as even\n"
		"   eighths, rate 2 as even quarters, rate 1 - which never interpolated at\n"
		"   all in vanilla - as exact halves.)\n");

	/* Rate 4, the case the order names: the pair, the factor and the midpoint
	 * for every frame of one key frame interval. */
	{
		struct Want { long f; long key; double blend; };
		static const Want w[] =
		{
			{ 1, 0, 0.125 }, { 2, 0, 0.375 }, { 3, 0, 0.625 }, { 4, 0, 0.875 },
			{ 5, 1, 0.125 }, { 6, 1, 0.375 }, { 7, 1, 0.625 }, { 8, 1, 0.875 }
		};

		for (int i = 0; i < 8; i++)
		{
			long key = 0;
			float blend = 0.0F;
			const bool got = InterpPoseAt(w[i].f - 1, w[i].f, 0, 4, 1000, 0.5F, &key, &blend);

			ok(got, "rate 4: a midpoint exists", got ? 1 : 0, 1);
			ok(key == w[i].key, "rate 4: the key frame pair", key, w[i].key);
			okf(fabs(blend - w[i].blend) < 1e-6, "rate 4: the midpoint", blend, w[i].blend);
		}
	}

	/* RULE K6 - every way the blend is refused. Each of these is a pose that
	 * has no neighbour, and each is drawn live for one frame, which is what
	 * thirty hertz shows anyway. */
	{
		long key = 0;
		float blend = 0.0F;

		ok(!InterpPoseAt(8, 0, 0, 4, 1000, 0.5F, &key, &blend),
			"K6: a loop wrapping to its own start is not blended", 0, 0);
		ok(!InterpPoseAt(4, 4, 0, 4, 1000, 0.5F, &key, &blend),
			"K6: a frame that did not step is not blended", 0, 0);
		ok(!InterpPoseAt(4, 9, 0, 4, 1000, 0.5F, &key, &blend),
			"K6: a jump of five frames is not blended", 0, 0);
		ok(!InterpPoseAt(-1, 0, 0, 4, 1000, 0.5F, &key, &blend),
			"K6: the animation's first frame is not blended", 0, 0);
		ok(!InterpPoseAt(99, 100, 100, 4, 1000, 0.5F, &key, &blend),
			"K6: a frame before frame_base is not blended", 0, 0);
		ok(!InterpPoseAt(3, 4, 0, 0, 1000, 0.5F, &key, &blend),
			"K6: an animation with no rate is not blended", 0, 0);
	}

	/* THE LAST INTERVAL OF AN ANIMATION IS SHORTER, and GetFrames says so by
	 * shortening the rate. The midpoint has to follow it rather than the
	 * animation's nominal rate, or the pose would slow down at the end of
	 * every animation in the game. */
	{
		ok(InterpKeyRate(4, 0, 6) == 4, "a full interval keeps its rate", InterpKeyRate(4, 0, 6), 4);
		ok(InterpKeyRate(4, 1, 6) == 2, "the last interval is shortened", InterpKeyRate(4, 1, 6), 2);

		long key = 0;
		float blend = 0.0F;

		// f = 5, p = 4: both in key frame 1, whose rate is 2 rather than 4.
		ok(InterpPoseAt(4, 5, 0, 4, 6, 0.5F, &key, &blend),
			"the shortened interval still blends", 1, 1);
		ok(key == 1, "the shortened interval's own pair", key, 1);
		okf(fabs(blend - 0.25) < 1e-6, "the shortened interval's midpoint", blend, 0.25);
	}

	/* THE BLEND IS ALWAYS INSIDE THE PAIR. A factor outside 0..1 would draw a
	 * pose the animation never contains - an arm past its own extreme. Swept
	 * over every rate and every frame of a long animation. */
	{
		long outside = 0;

		for (long rate = 1; rate <= 16; rate++)
		{
			for (long f = 1; f <= 200; f++)
			{
				long key = 0;
				float blend = 0.0F;

				if (InterpPoseAt(f - 1, f, 0, rate, 150, 0.5F, &key, &blend))
					if (blend < 0.0F || blend > 1.0F)
						outside++;
			}
		}

		ok(outside == 0, "the midpoint never leaves the key frame pair", outside, 0);
	}

	/* AND AT fps_mode 0 THERE IS NO MIDPOINT AT ALL. The engine's gate is
	 * InterpPoseTime, which returns 0 unless the snapshot was applied - and
	 * it is applied on no frame at all at thirty hertz (order 2's own proof).
	 * With t = 0 the arithmetic itself also collapses onto the previous
	 * tick's own pose, which is the belt to that braces. */
	{
		long key = 0;
		float blend = 1.0F;

		InterpPoseAt(3, 4, 0, 4, 1000, 0.0F, &key, &blend);
		okf(blend == vanilla_alpha(3, 4), "t = 0 is the previous tick's own pose",
			blend, vanilla_alpha(3, 4));
	}
}

int main()
{
	printf("InterpTest - the arithmetic of the 60 FPS interpolation (#148, orders 2, 3 and 4)\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "interpolate_math.h\n\n");

	angles();
	positions();
	thresholds();
	cadence();
	counters();
	pose_factor();
	pose_time();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

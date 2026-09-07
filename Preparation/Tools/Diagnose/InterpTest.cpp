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

int main()
{
	printf("InterpTest - the arithmetic of the 60 FPS interpolation (#148, order 2)\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "interpolate_math.h\n\n");

	angles();
	positions();
	thresholds();
	cadence();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

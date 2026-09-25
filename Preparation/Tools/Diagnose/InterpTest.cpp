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
 * game. Build with bit.bat, run with no arguments.
 *
 * What it checks, and why: a short angle blended the long way round is
 * invisible almost all the time and looks like a rendering glitch when it is
 * not - one enemy, facing roughly south, spinning backwards for half a tick
 * every second frame. It has to be proven at the boundary, and the boundary
 * is a wrap that ordinary play will not reliably put a body on.
 *
 * This includes the engine's own header - not a copy of the functions - and
 * asserts on it, so there is no twin to keep in step.
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
#include <limits.h>		// LONG_MIN / LONG_MAX - the extremes of root_narrow

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

	/* The wrap: 32000 to -32000 is 1536 units clockwise over the top, not
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

	/* Exactly half a turn: 32768 as a short is -32768, so the wrap chooses
	 * anticlockwise. Arbitrary, deterministic, and the only requirement. */
	ok(InterpLerpAngle(0, -32768, 0.5F) == -16384, "half a turn is anticlockwise",
		InterpLerpAngle(0, -32768, 0.5F), -16384);

	/* A half turn is a snap for a node, not a blend - InterpHalfTurn is the
	 * question ApplyNode (game\interpolate.cpp) asks before it blends a
	 * rotation, and turn180_effect (game\effects.cpp) is the one thing that
	 * makes the answer yes: y_rot -= 0x8000. Exactly half a turn either way
	 * is one; a unit short of it is a turn like any other. */
	ok(InterpHalfTurn(0, -32768), "0 -> -32768 is a half turn",
		InterpHalfTurn(0, -32768), 1);
	ok(InterpHalfTurn(-32768, 0), "-32768 -> 0 is a half turn",
		InterpHalfTurn(-32768, 0), 1);
	ok(InterpHalfTurn(1000, -31768), "1000 -> -31768 is a half turn",
		InterpHalfTurn(1000, -31768), 1);
	ok(!InterpHalfTurn(0, 32767), "0 -> 32767 is a turn, not a half turn",
		InterpHalfTurn(0, 32767), 0);
	ok(!InterpHalfTurn(0, -32767), "0 -> -32767 is a turn, not a half turn",
		InterpHalfTurn(0, -32767), 0);
	ok(!InterpHalfTurn(12345, 12345), "no turn is no half turn",
		InterpHalfTurn(12345, 12345), 0);

	{
		// turn180_effect itself, from every angle on the 512-unit lattice.
		int missed = 0;

		for (int a = -32768; a < 32768; a += 512)
		{
			const short from = (short)a;
			const short to = (short)(from - 0x8000);

			if (!InterpHalfTurn(from, to))
				missed++;
		}

		ok(missed == 0, "turn180_effect from every lattice angle is a half turn",
			missed, 0);
	}

	/* The sweep: every pair of angles on a 512-unit lattice - 16,384 pairs -
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

				/* Exactly half a turn has no shorter way, so there is
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

/* The ninth step of a screen fade. SetFade's arithmetic as
 * specific\specificfx.cpp does it - eight steps of (end - start) >> 3, the
 * ninth call draws `end` - played through in both directions, once as the
 * thirty hertz ticks see it (the value read before the step, `end` on the
 * ninth) and once as an in-between frame between the eighth and the ninth
 * tick sees it (the value after the eighth step). */
static void fade()
{
	printf("fade - the ninth step of a screen fade\n");

	const long dirs[2][2] = { { 255, 0 }, { 0, 255 } };
	const long classic[2][9] = {
		{ 255, 223, 191, 159, 127, 95, 63, 31, 0 },
		{ 0, 31, 62, 93, 124, 155, 186, 217, 255 } };

	for (int d = 0; d < 2; d++)
	{
		const long start = dirs[d][0], end = dirs[d][1];
		const long step = (end - start) >> 3;
		long val = start, cnt = 0, rawAfter8 = -1000, tweenAfter8 = -1000;
		int wrong = 0;

		for (int tick = 1; tick <= 9; tick++)
		{
			long drawn = val;		// read before the step, as DoScreenFade does
			val += step;
			cnt++;
			const long raw = val;
			val = InterpFadeClamp(val, step, end);
			if (cnt > 8)
				drawn = end;
			if (drawn != classic[d][tick - 1])
				wrong++;
			if (tick == 8)
			{
				rawAfter8 = raw;
				tweenAfter8 = val;
			}
		}

		if (d == 0)
		{
			ok(wrong == 0, "255 -> 0: the nine ticks draw what they always drew", wrong, 0);
			ok(rawAfter8 == -1, "255 -> 0: the eighth step lands on -1 unclamped", rawAfter8, -1);
			ok(tweenAfter8 == 0, "255 -> 0: the in-between frame after the eighth tick draws 0, not alpha 255", tweenAfter8, 0);
		}
		else
		{
			ok(wrong == 0, "0 -> 255: the nine ticks draw what they always drew", wrong, 0);
			ok(rawAfter8 == 248, "0 -> 255: the eighth step lands on 248", rawAfter8, 248);
			ok(tweenAfter8 == 248, "0 -> 255: the in-between frame after the eighth tick draws 248, unchanged", tweenAfter8, 248);
		}
	}

	// The colour the engine builds from the unclamped value: alpha 255, black.
	const long black = (long)((unsigned long)-1L << 24);
	ok(black == (long)0xFF000000, "-1 shifted into the colour is alpha 255 - the black tick", black, (long)0xFF000000);
}

/* Which camera type changes are cuts. The numbers are
 * camera_type's own (chase 0, fixed 1, look 2, combat 3, cinematic 4,
 * heavy 5); the engine static_asserts the three the rule names. */
static void camera()
{
	printf("camera - the hand-over that is not a cut\n");

	ok(!InterpCamTypeCut(0, 3, 0, 0), "chase -> combat (drawing a weapon) is a hand-over", InterpCamTypeCut(0, 3, 0, 0), 0);
	ok(!InterpCamTypeCut(3, 0, 3, 3), "combat -> chase (holstering) is a hand-over", InterpCamTypeCut(3, 0, 3, 3), 0);
	ok(!InterpCamTypeCut(0, 2, 0, 0), "chase -> look is a hand-over", InterpCamTypeCut(0, 2, 0, 0), 0);
	ok(!InterpCamTypeCut(2, 3, 2, 3), "look -> combat, both halves, is a hand-over", InterpCamTypeCut(2, 3, 2, 3), 0);
	ok(!InterpCamTypeCut(0, 0, 3, 0), "old_type combat -> chase alone is a hand-over", InterpCamTypeCut(0, 0, 3, 0), 0);
	ok(!InterpCamTypeCut(3, 3, 3, 3), "no change is no cut", InterpCamTypeCut(3, 3, 3, 3), 0);
	ok(InterpCamTypeCut(0, 1, 0, 0), "chase -> fixed is a cut", InterpCamTypeCut(0, 1, 0, 0), 1);
	ok(InterpCamTypeCut(1, 0, 1, 0), "fixed -> chase is a cut", InterpCamTypeCut(1, 0, 1, 0), 1);
	ok(InterpCamTypeCut(0, 0, 0, 1), "old_type chase -> fixed alone is a cut", InterpCamTypeCut(0, 0, 0, 1), 1);
	ok(InterpCamTypeCut(5, 0, 5, 0), "heavy -> chase is a cut", InterpCamTypeCut(5, 0, 5, 0), 1);
	ok(InterpCamTypeCut(0, 4, 0, 4), "chase -> cinematic is a cut", InterpCamTypeCut(0, 4, 0, 4), 1);
	ok(InterpCamTypeCut(3, 1, 3, 1), "combat -> fixed is a cut", InterpCamTypeCut(3, 1, 3, 1), 1);
}

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

	/* The whole coordinate range of a TR5 level. A room grid is at most 100
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

	/* It may snap sooner than the true distance would, never later. That is
	 * the whole licence for using a Manhattan sum in place of a length. */
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

	/* The cases the rule is for, at the engine's own numbers.
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

	/* A hitch: S_DumpScreen returns how many 1/60 s ticks the frame really
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


/* ------------------------------------------------------- the camera aim -- */

/* The camera's target, and where it is a direction.
 *
 * The rule under test is InterpCamAimCut (game\interpolate_math.h) and the
 * fault it repairs is measurable without a game: FirstPersonCamera builds
 * camera.target at camera.pos + direction * 0x5100, so a head turn of a few
 * degrees between two logic ticks moves that point further than
 * INTERP_CAM_SNAP, and the plain distance test then reads it as a camera cut
 * - camcut=10..17 a second while Lara stands still.
 *
 * What is asserted, in this order:
 *
 *   1. the old rule really does fire on an idle head turn - the fault
 *      itself, so that this test would have caught it
 *   2. the new rule does not, over the whole range of turns a body, a head
 *      or a look control can make in one thirtieth of a second
 *   3. the new rule still catches a real cut - a new view, sixty degrees and
 *      more, and the half turn a fixed camera makes
 *   4. the CHASE camera is not touched at all: its target sits on Lara,
 *      inside INTERP_CAM_FAR_TARGET, where the test is the absolute one it
 *      always was. */

static const double kPi = 3.14159265358979323846;

/* A first person camera looking `yawDeg` degrees round and `pitchDeg` degrees
 * up, with the target at the engine's own 0x5100. */
static void fp_camera(double yawDeg, double pitchDeg, long* p, long* t)
{
	const double yaw = yawDeg * kPi / 180.0;
	const double pitch = pitchDeg * kPi / 180.0;
	const double r = 20736.0;		// 0x5100, game\camera.cpp

	p[0] = 84000; p[1] = -4300; p[2] = 72000;

	t[0] = p[0] + (long)(r * cos(pitch) * sin(yaw));
	t[1] = p[1] - (long)(r * sin(pitch));
	t[2] = p[2] + (long)(r * cos(pitch) * cos(yaw));
}

// The plain rule: the target compared as an absolute place.
static bool old_target_cut(const long* pt, const long* t)
{
	return InterpManhattan(t[0] - pt[0], t[1] - pt[1], t[2] - pt[2]) > INTERP_CAM_SNAP;
}

static void camera_aim()
{
	printf("the camera's aim\n");

	ok(INTERP_CAM_FAR_TARGET == 4096, "beyond four sectors a target is a direction",
		INTERP_CAM_FAR_TARGET, 4096);
	ok(INTERP_CAM_DIR_RADIUS == 1024, "and it is measured on a sector's radius",
		INTERP_CAM_DIR_RADIUS, 1024);

	/* 1. The fault: an idle animation turns Lara's head by a degree or two
	 * between two ticks. On a target 20736 units away that is 362 units per
	 * degree, so three degrees clears INTERP_CAM_SNAP and the old rule calls
	 * it a cut. */
	{
		long p0[3], t0[3], p1[3], t1[3];

		fp_camera(0.0, 0.0, p0, t0);
		fp_camera(3.0, 0.0, p1, t1);

		ok(old_target_cut(t0, t1), "the OLD rule cuts on a three degree head turn",
			old_target_cut(t0, t1) ? 1 : 0, 1);
		ok(!InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
			p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP),
			"the new rule does not", 0, 0);
	}

	/* 2. Every turn a body can make in one tick: Lara's fastest turn on the
	 * spot is under five degrees a tick and a roll is about fifteen; twenty
	 * is taken as the ceiling and swept in both axes and in both directions,
	 * with the eye moving as well. Not one of them may read as a cut. */
	{
		int cuts = 0, tried = 0;

		for (double yaw = -20.0; yaw <= 20.0; yaw += 0.5)
			for (double pitch = -20.0; pitch <= 20.0; pitch += 2.5)
			{
				long p0[3], t0[3], p1[3], t1[3];

				fp_camera(0.0, 0.0, p0, t0);
				fp_camera(yaw, pitch, p1, t1);

				// and the eye walks a running step while she turns
				p1[0] += 30; p1[1] -= 12; p1[2] += 18;
				t1[0] += 30; t1[1] -= 12; t1[2] += 18;

				tried++;

				if (InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
					p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP))
					cuts++;
			}

		ok(cuts == 0, "no turn up to twenty degrees a tick reads as a cut", cuts, 0);
		ok(tried > 500, "and the sweep really ran", tried, 500);
	}

	/* 3. A real cut is still a cut: a new view is not a turn - sixty degrees,
	 * a right angle and a half turn all have to be caught. */
	{
		const double at[3] = { 60.0, 90.0, 180.0 };

		for (int i = 0; i < 3; i++)
		{
			long p0[3], t0[3], p1[3], t1[3];

			fp_camera(0.0, 0.0, p0, t0);
			fp_camera(at[i], 0.0, p1, t1);

			ok(InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
				p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP),
				"a real cut is still caught", (long)at[i], (long)at[i]);
		}
	}

	/* 4. The chase camera is untouched: its target sits on Lara, 1536 from
	 * the eye - under INTERP_CAM_FAR_TARGET - so InterpCamAim hands back the
	 * offset itself and the comparison is the plain absolute one. */
	{
		long p0[3] = { 84000, -4300, 72000 };
		long t0[3] = { 84000, -5300, 73536 };		// 1536 ahead and 1000 up
		long p1[3] = { 84010, -4300, 72020 };
		long t1[3] = { 84010, -5300, 73556 };		// the same shot, moved
		long t2[3] = { 86000, -5300, 73536 };		// a fixed camera taking over

		ok(!InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
			p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP),
			"a chase camera following Lara does not cut", 0, 0);

		ok(InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
			p1[0], p1[1], p1[2], t2[0], t2[1], t2[2], INTERP_CAM_SNAP),
			"a target jumping two sectors still cuts", 1, 1);
	}

	/* And the reduction is the identity under the threshold: whatever else
	 * changes, a near target must come back as its own offset to the unit, or
	 * the classic case would not be byte-identical. */
	{
		int bad = 0;

		for (long d = 0; d <= 4096; d += 337)
		{
			long a[3];

			InterpCamAim(1000, 2000, 3000, 1000 + d, 2000, 3000, &a[0], &a[1], &a[2]);

			if (a[0] != d || a[1] != 0 || a[2] != 0)
				bad++;
		}

		ok(bad == 0, "a target inside the radius is passed through untouched", bad, 0);
	}
}


/* ------------------------------------------------------- the pose blend -- */

/* The pose, and the pair a matrix blend cannot survive.
 *
 * A dive roll's known failure mode: one in-between frame with a leg fully
 * extended the wrong way. The cause is a packed ten bit angle interpolated
 * the long way round - 1000 to 20 giving 510 where it should give 1022.
 *
 * The first thing asserted here is that this cannot happen, and it is
 * asserted against the arithmetic the engine really runs: the two poses are
 * blended as matrices (game\draw.cpp, InterpolateMatrix), and an element-wise
 * lerp of two rotations always lands on the short-way bisector. So the 1000
 * to 20 case is run through it, on all three axes and in both directions, and
 * the angle that comes out is read back.
 *
 * The second is what really goes wrong: the same lerp shrinks by cos(D/2),
 * and at half a turn the frame is singular. InterpPoseFar is the rule that
 * refuses such a pair, and it is asserted to fire on a half turn, to leave a
 * quiet pair alone, and to answer per node rather than per frame. */

static const double kPiS = 3.14159265358979323846;

/* The angle a blended 3x3 comes back as, about the axis it was built on.
 * atan2 of the two elements that carry it, in the engine's 16 bit space and
 * then in the packed ten bit one, which is the space a dive roll's angles
 * are stored in. */
static double blended_angle_10bit(short a10, short b10, int axis, float alpha)
{
	short ra[3] = { 0, 0, 0 }, rb[3] = { 0, 0, 0 };
	float ma[9], mb[9], mm[9];

	ra[axis] = (short)(a10 << 6);
	rb[axis] = (short)(b10 << 6);

	InterpPoseRot(ra, ma);
	InterpPoseRot(rb, mb);

	for (int i = 0; i < 9; i++)
		mm[i] = ma[i] + (mb[i] - ma[i]) * alpha;

	/* The rotation's own sine and cosine, read out of the two elements of the
	 * plane the axis turns in. Which pair those are depends on the axis; the
	 * magnitude is the shrink and the ratio is the angle. */
	double s = 0.0, c = 0.0;

	if (axis == 0)			// x: the y,z plane
	{
		c = mm[8];  s = -mm[7];
	}
	else if (axis == 1)		// y: the z,x plane
	{
		c = mm[8];  s = mm[6];
	}
	else				// z: the x,y plane
	{
		c = mm[0];  s = mm[1];
	}

	double deg = atan2(s, c) * 180.0 / kPiS;

	if (deg < 0.0)
		deg += 360.0;

	return deg * 1024.0 / 360.0;	// back into the packed ten bit space
}

static void pose_wrap()
{
	printf("the pose the short way round\n");

	/* 1. The known case: 1000 and 20 are 44 units apart across the wrap; the
	 * midpoint is 1022 and never 510. Run on all three axes and in both
	 * directions - six cases - through the very blend the engine uses. */
	{
		int bad = 0, ran = 0;

		for (int axis = 0; axis < 3; axis++)
			for (int way = 0; way < 2; way++)
			{
				const short a = (short)(way ? 20 : 1000);
				const short b = (short)(way ? 1000 : 20);
				const double got = blended_angle_10bit(a, b, axis, 0.5F);
				double d = got - 1022.0;

				while (d > 512.0) d -= 1024.0;
				while (d < -512.0) d += 1024.0;

				ran++;

				if (d < -1.0 || d > 1.0)
					bad++;
			}

		ok(ran == 6, "all three axes and both directions", ran, 6);
		ok(bad == 0, "1000 and 20 blend to 1022 and never to 510", bad, 0);
	}

	/* 2. And the shrink is real: the same blend at a half turn keeps nothing
	 * of the frame at all, which is the fault itself. */
	{
		/* 512 in the packed ten bit space IS half a turn; the rotation
		 * builder wants the engine's own sixteen bit angle, and half a turn
		 * there is -32768 (512 << 6 is 32768, which a short cannot hold - it
		 * IS -32768, and writing it out says so instead of truncating). */
		short ra[3] = { 0, 0, 0 }, rb[3] = { 0, (short)-32768, 0 };
		float ma[9], mb[9];

		InterpPoseRot(ra, ma);
		InterpPoseRot(rb, mb);

		const double c = InterpPoseTurnCos(ma, mb);

		ok(c < -0.999, "half a turn reads as half a turn", (long)(c * 1000.0), -1000);

		/* the blended z column's length, which is what carries the bone */
		float mm[9];

		for (int i = 0; i < 9; i++)
			mm[i] = (ma[i] + mb[i]) * 0.5F;

		const double len = sqrt((double)mm[6] * mm[6] + (double)mm[7] * mm[7] +
			(double)mm[8] * mm[8]);

		ok(len < 0.01, "and its midpoint keeps none of the bone's frame",
			(long)(len * 1000.0), 0);
	}

	/* 3. The rule: a frame is nine shorts of box and offset and then one
	 * packed rotation per node. Two frames are built by hand - node 0 quiet,
	 * node 1 turned - and InterpPoseFar has to answer for the pair. */
	{
		short quiet[9 + 4 * 2], turned[9 + 4 * 2], small_[9 + 4 * 2];

		for (int i = 0; i < 9; i++)
			quiet[i] = turned[i] = small_[i] = 0;

		/* Four nodes, each one two-word packed triple of zeroes. */
		for (int n = 0; n < 4; n++)
		{
			quiet[9 + n * 2] = 0;      quiet[9 + n * 2 + 1] = 0;
			turned[9 + n * 2] = 0;     turned[9 + n * 2 + 1] = 0;
			small_[9 + n * 2] = 0;     small_[9 + n * 2 + 1] = 0;
		}

		/* node 1 of `turned` gets y = 512 (half a turn), of `small_` y = 40
		 * (about fourteen degrees). The packed layout is x in bits 20..29,
		 * y in 10..19 and z in 0..9 of the two words together. */
		{
			const unsigned long half = ((unsigned long)512 << 10);
			const unsigned long bit = ((unsigned long)40 << 10);

			turned[9 + 1 * 2] = (short)(unsigned short)(half >> 16);
			turned[9 + 1 * 2 + 1] = (short)(unsigned short)(half & 0xFFFF);
			small_[9 + 1 * 2] = (short)(unsigned short)(bit >> 16);
			small_[9 + 1 * 2 + 1] = (short)(unsigned short)(bit & 0xFFFF);
		}

		ok(InterpPoseFar(quiet, turned, 4, INTERP_POSE_TURN_COS),
			"a half turn at one node refuses the pair", 1, 1);
		ok(!InterpPoseFar(quiet, small_, 4, INTERP_POSE_TURN_COS),
			"a fourteen degree turn does not", 0, 0);
		ok(!InterpPoseFar(quiet, quiet, 4, INTERP_POSE_TURN_COS),
			"and a pose against itself never does", 0, 0);
	}

	/* 4. The limit is where it says it is: ninety degrees is the last turn
	 * that passes; a hair over it is refused. The packed space is 1024 to the
	 * circle, so ninety degrees is 256. */
	{
		short quiet[9 + 2], at90[9 + 2], past90[9 + 2];

		for (int i = 0; i < 9; i++)
			quiet[i] = at90[i] = past90[i] = 0;

		quiet[9] = 0; quiet[10] = 0;

		const unsigned long a = ((unsigned long)255 << 10);
		const unsigned long b = ((unsigned long)258 << 10);

		at90[9] = (short)(unsigned short)(a >> 16);
		at90[10] = (short)(unsigned short)(a & 0xFFFF);
		past90[9] = (short)(unsigned short)(b >> 16);
		past90[10] = (short)(unsigned short)(b & 0xFFFF);

		ok(!InterpPoseFar(quiet, at90, 1, INTERP_POSE_TURN_COS),
			"just under ninety degrees passes", 0, 0);
		ok(InterpPoseFar(quiet, past90, 1, INTERP_POSE_TURN_COS),
			"just over it is refused", 1, 1);
	}

	/* 5. The key pair rule asks the hips alone: half a turn and 91 degrees at
	 * node 0 fire, 89 degrees and a pose against itself do not, and a half
	 * turn at node 1 with the hips quiet does not either. 91 degrees is 259 in
	 * the packed space (91.05), 89 degrees is 253 (88.95). */
	{
		short quiet[9 + 2 * 2], half[9 + 2 * 2], d91[9 + 2 * 2], d89[9 + 2 * 2],
			limb[9 + 2 * 2];

		for (int i = 0; i < 9 + 2 * 2; i++)
			quiet[i] = half[i] = d91[i] = d89[i] = limb[i] = 0;

		const unsigned long h = ((unsigned long)512 << 10);
		const unsigned long a = ((unsigned long)259 << 10);
		const unsigned long b = ((unsigned long)253 << 10);

		half[9] = (short)(unsigned short)(h >> 16);
		half[10] = (short)(unsigned short)(h & 0xFFFF);
		d91[9] = (short)(unsigned short)(a >> 16);
		d91[10] = (short)(unsigned short)(a & 0xFFFF);
		d89[9] = (short)(unsigned short)(b >> 16);
		d89[10] = (short)(unsigned short)(b & 0xFFFF);
		limb[11] = (short)(unsigned short)(h >> 16);
		limb[12] = (short)(unsigned short)(h & 0xFFFF);

		ok(InterpKeyFar(quiet, half), "a half turn of the hips is a far key pair", 1, 1);
		ok(InterpKeyFar(quiet, d91), "91 degrees of the hips is far", 1, 1);
		ok(!InterpKeyFar(quiet, d89), "89 degrees of the hips is not", 0, 0);
		ok(!InterpKeyFar(half, half), "a key frame against itself never is", 0, 0);
		ok(!InterpKeyFar(quiet, limb), "a half turn below the hips does not count", 0, 0);
	}
}

/* The camera's shell.
 *
 * Choosing the far branch per camera rather than per axis matters here: a
 * target hovering on the shell at INTERP_CAM_FAR_TARGET would otherwise have
 * one of the pair reduced to 1024 and the other left at 4096, a difference of
 * three thousand units of nothing. The branch is chosen once for the pair,
 * and both sides take the same scale. */
static void camera_shell()
{
	printf("the camera's shell\n");

	/* 1. A target walking through the shell: the eye stands still and the
	 * target moves out along its own ray from 3900 to 4400 in single units:
	 * not one step of it may read as a cut. */
	{
		int cuts = 0, tried = 0;
		long p[3] = { 10000, -2000, 20000 };

		for (long d = 3900; d < 4400; d++)
		{
			long t0[3] = { p[0], p[1], p[2] + d };
			long t1[3] = { p[0], p[1], p[2] + d + 1 };

			tried++;

			if (InterpCamAimCut(p[0], p[1], p[2], t0[0], t0[1], t0[2],
				p[0], p[1], p[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP))
				cuts++;
		}

		ok(tried == 500, "the target really walked through the shell", tried, 500);
		ok(cuts == 0, "and no step of it reads as a cut", cuts, 0);
	}

	/* 2. A target sitting on the shell with one unit of jitter, thirty ticks
	 * of it - the case that makes the seam a fault and not a curiosity. */
	{
		int cuts = 0;
		long p[3] = { 10000, -2000, 20000 };

		for (int i = 0; i < 30; i++)
		{
			const long a = INTERP_CAM_FAR_TARGET + (i & 1);
			const long b = INTERP_CAM_FAR_TARGET + ((i + 1) & 1);
			long t0[3] = { p[0], p[1], p[2] + a };
			long t1[3] = { p[0], p[1], p[2] + b };

			if (InterpCamAimCut(p[0], p[1], p[2], t0[0], t0[1], t0[2],
				p[0], p[1], p[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP))
				cuts++;
		}

		ok(cuts == 0, "a target jittering on the shell never cuts", cuts, 0);
	}

	/* 3. And camera_aim's own answers are unchanged: the two cameras the
	 * engine really has are nowhere near the shell, so everything it asserts
	 * has to stand - that section runs before this one and would have failed
	 * already; what is added here is the one case it could not see. */
	{
		long p0[3], t0[3], p1[3], t1[3];

		fp_camera(0.0, 0.0, p0, t0);
		fp_camera(3.0, 0.0, p1, t1);

		ok(!InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
			p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP),
			"a three degree head turn still does not cut", 0, 0);

		fp_camera(180.0, 0.0, p1, t1);

		ok(InterpCamAimCut(p0[0], p0[1], p0[2], t0[0], t0[1], t0[2],
			p1[0], p1[1], p1[2], t1[0], t1[1], t1[2], INTERP_CAM_SNAP),
			"and a half turn still does", 1, 1);
	}
}

/* The title menu's own cadence.
 *
 * The title and the inventory keep two ticks per present at every setting
 * (GameFrameMinTicks). What that does to ControlPhase's accumulator is
 * arithmetic and can be run here:
 *
 *     framecount += 2; while (framecount > 0) { tick; framecount -= 2; }
 *
 * From 0 it returns to 0 - and from -1 it returns to -1, forever. A plain
 * `framecount < 0` alpha rule would therefore read true on every single frame
 * of a title entered on the odd half of the sixty hertz cadence, and answer
 * 0.5 to all of them: every frame an in-between frame, every guarded counter
 * standing still, DoScreenFade never reaching its ninth step, DoFade never
 * becoming 2, and a full screen black quad standing over the menu while
 * TitleOptions zeroes the input. The parity it depends on is why the first
 * title of a session would be unaffected.
 *
 * Both halves of the fix are asserted: the rule (a classic-cadence frame is
 * never a tween frame, whatever the accumulator holds) and the clearing
 * (DoTitle sets framecount to 0 as DoLevel always has). */
static void title_cadence()
{
	printf("the title's cadence\n");

	for (int start = -1; start <= 0; start++)
	{
		long framecount = start;
		int tweenOld = 0, tweenNew = 0, ticks = 0, frames = 0;

		for (int i = 0; i < 300; i++)
		{
			/* the plain framecount-parity rule, and the rule with a test added
			 * for a frame that is not a sixty hertz one */
			const float alphaOld = framecount < 0 ? 0.5F : 1.0F;
			const float alphaNew = 1.0F;		// GameFrameMinTicks() >= 2

			frames++;

			if (alphaOld < 1.0F) tweenOld++;
			if (alphaNew < 1.0F) tweenNew++;

			for (framecount += 2; framecount > 0; framecount -= 2)
				ticks++;
		}

		if (start < 0)
			ok(tweenOld == frames,
				"the OLD rule made every title frame a tween frame", tweenOld, frames);
		else
			ok(tweenOld == 0, "on the even half it never did", tweenOld, 0);

		ok(tweenNew == 0, "the new rule never does, on either half", tweenNew, 0);
		ok(ticks == frames, "the title still runs one logic tick per frame",
			ticks, frames);
		ok(framecount == start, "and the accumulator keeps the parity it began with",
			framecount, start);
	}

	/* And what DoTitle does about it: clearing the accumulator on the way in
	 * makes the parity even whatever the level left behind, which is the
	 * belt to the rule's braces. */
	{
		long framecount = 0;			// game\gameflow.cpp, DoTitle
		int tween = 0;

		for (int i = 0; i < 60; i++)
		{
			if (framecount < 0)
				tween++;

			for (framecount += 2; framecount > 0; framecount -= 2)
				;
		}

		ok(tween == 0, "a cleared accumulator alone would have fixed it too", tween, 0);
	}
}

/* -------------------------------------------------------- the counters -- */

/* Every counter in the build that would step once per drawn frame asks
 * InterpIsTweenFrame() first, and the engine's rule for that is exactly
 *
 *     tween = (alpha < 1), and alpha = fps_mode && framecount < 0 ? 0.5 : 1
 *
 * latched once per frame at the top of DrawPhaseGame (game\interpolate.cpp).
 * What has to come out of it is that a whole simulated second of wall-clock
 * time produces the same number of steps at thirty and at sixty - because a
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
	printf("counters - what steps per logic tick\n");

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
	 * rule asks for; in wall-clock time the sixty hertz fade ends one
	 * sixtieth of a second sooner because it does not sit through a trailing
	 * in-between frame it has nothing left to draw on. */
	ok(r[1].fadePresents == 2 * r[0].fadePresents - 1,
		"the screen fade spans the same nine ticks - 2n-1 presents at sixty",
		r[1].fadePresents, 2 * r[0].fadePresents - 1);

	/* And the same again through a hitch: a frame that took four ticks feeds
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

		/* Not equal, and this is the one honest limit of the rule. Each of
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

	/* fps_mode 0 takes the whole thing out. At thirty the accumulator never
	 * ends a call below zero, so the latch is never set and every guard in
	 * the build reads false on every frame - which is the byte-identical
	 * claim said as arithmetic. */
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
 * The single-factor pose blend has two claims, and this is where both are
 * settled.
 *
 * Claim one: replacing four hard-wired branches with one float factor does
 * not change a single drawn frame at thirty hertz. The branches were
 * `IM_rate == 2 || (IM_frac == 2 && IM_rate == 4)` -> a half, `IM_frac == 1`
 * -> a quarter, otherwise three quarters, written out twelve times each in
 * four functions; they are transcribed here as the reference and the engine's
 * own header is asked for the same numbers. Both matrix stacks are checked,
 * because they round differently on purpose: the float stack keeps vanilla's
 * three expressions (a rearrangement of them would differ in the last bit)
 * and the integer stack keeps vanilla's shifts (which floor, where a cast
 * would truncate - the two differ on every negative odd number).
 *
 * Claim two: the pose drawn between two logic ticks is the midpoint of the
 * two poses the game draws at thirty, and never anything else. What is
 * asserted for that is monotonicity - `key + blend` may never go backwards
 * over a whole animation walked at sixty - and identity at the tick frames,
 * where it must be vanilla's own factor to the bit.
 *
 * Which (frac, rate) pairs are real: measured by walking the animation
 * tables of all sixteen level files of the retail game. Rates 1, 2, 3, 4, 5,
 * 6, 9 and 10 all occur (3619, 3350, 35, 5399, 3187, 2, 2 and 120
 * animations), and GetFrames can shorten any of them at an animation's last
 * interval. The counts are in the table below. They are the reason vanilla's
 * classification is kept rather than replaced by frac/rate: at rate 5 the two
 * disagree wildly - vanilla draws frames 1..4 of a five frame interval at
 * .25, .75, .75, .75 - and a third of every animation in TR5 would move
 * differently from the day it shipped. */

/* Vanilla's own factor, transcribed from the branches of game\draw.cpp as
 * they stood before the single-factor form. This is the reference, not the
 * thing tested. */
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
	printf("pose - the one factor against the four branches\n");

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

	/* The arm matrix wrote its half differently: InterpolateArmMatrix's
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
	printf("\npose - the midpoint of the two poses a tick apart\n");

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

	/* The neighbour rule - every way the blend is refused. Each of these is a pose that
	 * has no neighbour, and each is drawn live for one frame, which is what
	 * thirty hertz shows anyway. */
	{
		long key = 0;
		float blend = 0.0F;

		ok(!InterpPoseAt(8, 0, 0, 4, 1000, 0.5F, &key, &blend),
			"neighbour rule: a loop wrapping to its own start is not blended", 0, 0);
		ok(!InterpPoseAt(4, 4, 0, 4, 1000, 0.5F, &key, &blend),
			"neighbour rule: a frame that did not step is not blended", 0, 0);
		ok(!InterpPoseAt(4, 9, 0, 4, 1000, 0.5F, &key, &blend),
			"neighbour rule: a jump of five frames is not blended", 0, 0);
		ok(!InterpPoseAt(-1, 0, 0, 4, 1000, 0.5F, &key, &blend),
			"neighbour rule: the animation's first frame is not blended", 0, 0);
		ok(!InterpPoseAt(99, 100, 100, 4, 1000, 0.5F, &key, &blend),
			"neighbour rule: a frame before frame_base is not blended", 0, 0);
		ok(!InterpPoseAt(3, 4, 0, 0, 1000, 0.5F, &key, &blend),
			"neighbour rule: an animation with no rate is not blended", 0, 0);
	}

	/* The last interval of an animation is shorter, and GetFrames says so by
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

	/* The blend is always inside the pair: a factor outside 0..1 would draw a
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

	/* And at fps_mode 0 there is no midpoint at all: the engine's gate is
	 * InterpPoseTime, which returns 0 unless the snapshot was applied, and
	 * it is applied on no frame at all at thirty hertz. With t = 0 the
	 * arithmetic itself also collapses onto the previous tick's own pose,
	 * which is the belt to that braces. */
	{
		long key = 0;
		float blend = 1.0F;

		InterpPoseAt(3, 4, 0, 4, 1000, 0.0F, &key, &blend);
		okf(blend == vanilla_alpha(3, 4), "t = 0 is the previous tick's own pose",
			blend, vanilla_alpha(3, 4));
	}
}

/* ---------------------------------------------- the pose walking backwards -- */

/* Where vanilla draws `frame` of an animation, in key frame units: the key
 * index plus the test's own copy of the factor, over the rate GetFrames
 * shortens the last interval to. */
static double key_pos(long frame, long base, long rate, long end)
{
	const long f = frame - base;
	const long k = f / rate;
	const long frac = f % rate;

	return (double)k + (frac ? (double)vanilla_alpha(frac, InterpKeyRate(rate, k, end)) : 0.0);
}

static void pose_back()
{
	printf("\npose - the backwards rule, the pull-up played down\n");

	/* Every rate TR5 uses much and every step up to three, walked down from
	 * frame 12 to 0 with the last step cut short, as the lowering walks. A
	 * pair is accepted exactly when its frames lie at most one key interval
	 * apart (worked out here, not by the rule); an accepted one starts on the
	 * previous tick's pose, ends on the current tick's, is their midpoint at
	 * a half, never walks upwards and names a pair between the two ticks'. */
	const long rates[] = { 1, 2, 4, 5 };

	for (int r = 0; r < 4; r++)
	{
		for (long step = 1; step <= 3; step++)
		{
			const long rate = rates[r];
			const long base = 0;
			const long end = 1000;			// no last-interval clamp
			long wrongVerdict = 0, badStart = 0, badEnd = 0, badMid = 0;
			long upwards = 0, badPair = 0, accepted = 0;
			double last = key_pos(12, base, rate, end);

			for (long p = 12; p > 0; )
			{
				const long f = p - (p < step ? p : step);
				const long kp = p / rate;
				const long kc = f / rate;
				const bool want = kp <= kc + 1;
				const double pp = key_pos(p, base, rate, end);
				const double pc = key_pos(f, base, rate, end);
				long k0 = 0, k1 = 0, kh = 0;
				float b0 = 0.0F, b1 = 0.0F, bh = 0.0F;
				const bool got0 = InterpPoseBack(p, f, base, rate, end, 0.0F, &k0, &b0);
				const bool got1 = InterpPoseBack(p, f, base, rate, end, 1.0F, &k1, &b1);
				const bool goth = InterpPoseBack(p, f, base, rate, end, 0.5F, &kh, &bh);

				if (got0 != want || got1 != want || goth != want)
					wrongVerdict++;

				if (want && got0 && got1 && goth)
				{
					const double m = (double)kh + (double)bh;

					accepted++;

					if (fabs((double)k0 + (double)b0 - pp) > 1e-6)
						badStart++;

					if (fabs((double)k1 + (double)b1 - pc) > 1e-6)
						badEnd++;

					if (m != (pp + pc) * 0.5)
						badMid++;

					if (m > last || pc > m)
						upwards++;

					if (kh < kc || kh > kp || k0 < kc || k0 > kp || k1 < kc || k1 > kp ||
						bh < 0.0F || bh >= 1.0F || b0 < 0.0F || b0 >= 1.0F || b1 < 0.0F || b1 >= 1.0F)
						badPair++;
				}
				else if (pc > last)
					upwards++;

				last = pc;
				p = f;
			}

			printf("  rate %ld, step %ld: %ld accepted\n", rate, step, accepted);
			ok(wrongVerdict == 0, "backwards: accepted exactly within one key interval", wrongVerdict, 0);
			ok(badStart == 0, "backwards: t = 0 is the previous tick's pose", badStart, 0);
			ok(badEnd == 0, "backwards: t = 1 is the current tick's pose", badEnd, 0);
			ok(badMid == 0, "backwards: t = 0.5 is exactly their midpoint", badMid, 0);
			ok(upwards == 0, "backwards: tick, tween, tick never walks upwards", upwards, 0);
			ok(badPair == 0, "backwards: the pair lies in [kc, kp], the factor in [0, 1)", badPair, 0);
		}
	}

	/* The lowering itself: animation 97, 74 frames at rate 4, stepped from
	 * its last frame down by three with the last step cut short (73, 70, ...,
	 * 4, 1, 0) - once at frame_base 0, where InterpKeyRate's clamp fires on
	 * the last key, and once at 3000, where it never does, as in a level. */
	const long bases[] = { 0, 3000 };

	for (int b = 0; b < 2; b++)
	{
		const long base = bases[b];
		const long end = base + 73;
		long steps = 0, refused = 0, badStart = 0, badEnd = 0, badMid = 0;
		long upwards = 0, badPair = 0, badSweep = 0;
		double last = key_pos(end, base, 4, end);

		// The first four tweens at t = 0.5, worked out by hand.
		struct Want { long key; double blend; };
		static const Want w[] = { { 17, 0.875 }, { 17, 0.125 }, { 16, 0.375 }, { 15, 0.625 } };

		for (long p = end; p > base; )
		{
			const long f = p - (p - base < 3 ? p - base : 3);
			const long kp = (p - base) / 4;
			const long kc = (f - base) / 4;
			const double pp = key_pos(p, base, 4, end);
			const double pc = key_pos(f, base, 4, end);
			long key = 0;
			float blend = 0.0F;

			if (!InterpPoseBack(p, f, base, 4, end, 0.5F, &key, &blend))
				refused++;
			else
			{
				const double m = (double)key + (double)blend;

				if (steps < 4)
				{
					ok(key == w[steps].key, "lowering: the pair of an early tween", key, w[steps].key);
					okf(fabs(blend - w[steps].blend) < 1e-6, "lowering: the factor of an early tween",
						blend, w[steps].blend);
				}

				if (m != (pp + pc) * 0.5)
					badMid++;

				if (m > last || pc > m)
					upwards++;

				if (key < kc || key > kp || blend < 0.0F || blend >= 1.0F)
					badPair++;

				/* The whole tween in eighths of a tick: continuous from one
				 * tick pose to the other and never upwards. */
				double prevPos = pp;

				for (int e = 0; e <= 8; e++)
				{
					const float t = (float)e / 8.0F;
					long k = 0;
					float bl = 0.0F;

					InterpPoseBack(p, f, base, 4, end, t, &k, &bl);

					const double pos = (double)k + (double)bl;

					if (pos > prevPos || fabs(pos - (pp + (pc - pp) * t)) > 1e-6 ||
						(e == 0 && fabs(pos - pp) > 1e-6) || (e == 8 && fabs(pos - pc) > 1e-6))
						badSweep++;

					prevPos = pos;
				}

				long k0 = 0, k1 = 0;
				float b0 = 0.0F, b1 = 0.0F;

				if (!InterpPoseBack(p, f, base, 4, end, 0.0F, &k0, &b0) ||
					fabs((double)k0 + (double)b0 - pp) > 1e-6)
					badStart++;

				if (!InterpPoseBack(p, f, base, 4, end, 1.0F, &k1, &b1) ||
					fabs((double)k1 + (double)b1 - pc) > 1e-6)
					badEnd++;
			}

			last = pc;
			steps++;
			p = f;
		}

		printf("  lowering at frame_base %ld: %ld steps\n", base, steps);
		ok(steps == 25, "lowering: 25 ticks from the last frame to the first", steps, 25);
		ok(refused == 0, "lowering: every step is accepted", refused, 0);
		ok(badStart == 0, "lowering: t = 0 is the previous tick's pose", badStart, 0);
		ok(badEnd == 0, "lowering: t = 1 is the current tick's pose", badEnd, 0);
		ok(badMid == 0, "lowering: t = 0.5 is exactly their midpoint", badMid, 0);
		ok(upwards == 0, "lowering: tick, tween, tick never walks upwards", upwards, 0);
		ok(badPair == 0, "lowering: the pair lies in [kc, kp], the factor in [0, 1)", badPair, 0);
		ok(badSweep == 0, "lowering: every t walks the path down, no cut at either end", badSweep, 0);

		// The last step, 1 down to 0, cut short to one frame.
		long key = -1;
		float blend = -1.0F;

		ok(InterpPoseBack(base + 1, base, base, 4, end, 0.5F, &key, &blend),
			"lowering: the cut-short last step is accepted", 1, 1);
		ok(key == 0, "lowering: the last step's pair", key, 0);
		okf(fabs(blend - 0.125) < 1e-6, "lowering: the last step's factor", blend, 0.125);
	}

	/* The upper pair, and why the forward rule's clamp form would not do:
	 * 70 down to 67 walks from 17.5 to 16.75, the midpoint 17.125 lies above
	 * key 17. The clamp form - pair kc, previous pose taken as 1.0 on it -
	 * would draw 16.875, a quarter of an interval too far along. */
	{
		long key = 0;
		float blend = 0.0F;

		InterpPoseBack(3070, 3067, 3000, 4, 3073, 0.5F, &key, &blend);
		ok(key == 17, "the midpoint above a passed key takes the upper pair", key, 17);

		const double ac = vanilla_alpha(3, 4);
		const double clamp = 16.0 + ac + (1.0 - ac) * 0.5;

		okf(fabs((key + blend) - clamp - 0.25) < 1e-6,
			"the clamp form would draw it a quarter interval too far along",
			(double)key + (double)blend - clamp, 0.25);
	}

	/* The refusals. Each draws the current tick's pose, as InterpPoseAt's
	 * refusals do. */
	{
		long key = 0;
		float blend = 0.0F;

		ok(!InterpPoseBack(4, 5, 0, 4, 1000, 0.5F, &key, &blend),
			"backwards rule: a forward step is not its ground", 0, 0);
		ok(!InterpPoseBack(4, 4, 0, 4, 1000, 0.5F, &key, &blend),
			"backwards rule: a standing frame is not blended", 0, 0);
		ok(!InterpPoseBack(7, 4, 0, 0, 1000, 0.5F, &key, &blend),
			"backwards rule: an animation with no rate is not blended", 0, 0);
		ok(!InterpPoseBack(101, 99, 100, 4, 1000, 0.5F, &key, &blend),
			"backwards rule: a frame below frame_base is not blended", 0, 0);
		ok(!InterpPoseBack(3074, 3071, 3000, 4, 3073, 0.5F, &key, &blend),
			"backwards rule: a previous frame beyond frame_end is not blended", 0, 0);
		ok(!InterpPoseBack(5, 3, 0, 1, 1000, 0.5F, &key, &blend),
			"backwards rule: a step of 2 at rate 1 spans two keys", 0, 0);

		long nine = 0;

		for (long f = 0; f <= 60; f++)
			if (InterpPoseBack(f + 9, f, 0, 4, 1000, 0.5F, &key, &blend))
				nine++;

		ok(nine == 0, "backwards rule: a step of 9 at rate 4 is never blended", nine, 0);
	}

	/* The shortened last interval: at frame_end 6 key 1 has rate 2, and both
	 * rules must place every frame on vanilla's own factor there. Frame 5 is
	 * frac 1 over rate 2, a half - the nominal rate 4 would give a quarter. */
	{
		long key = 0;
		float blend = 0.0F;

		InterpPoseBack(5, 3, 0, 4, 6, 0.0F, &key, &blend);
		ok(key == 1, "clamped interval: t = 0 on the previous tick's pair", key, 1);
		okf(blend == 0.5F, "clamped interval: vanilla's factor at the shortened rate", blend, 0.5);

		long disagree = 0, compared = 0;

		for (long p = 1; p <= 6; p++)
		{
			for (long f = 0; f < p; f++)
			{
				long kb = 0, kf = 0;
				float bb = 0.0F, bf = 0.0F;

				if (!InterpPoseBack(p, f, 0, 4, 6, 0.0F, &kb, &bb))
					continue;

				// The forward rule's tick poses: its end at t = 1, its start at t = 0.
				InterpPoseAt(p - 1, p, 0, 4, 6, 1.0F, &kf, &bf);

				if (fabs((double)kb + bb - ((double)kf + bf)) > 1e-6)
					disagree++;

				InterpPoseBack(p, f, 0, 4, 6, 1.0F, &kb, &bb);
				InterpPoseAt(f, f + 1, 0, 4, 6, 0.0F, &kf, &bf);

				if (fabs((double)kb + bb - ((double)kf + bf)) > 1e-6)
					disagree++;

				compared++;
			}
		}

		printf("  clamped interval: %ld backwards pairs compared with the forward rule\n", compared);
		ok(compared > 0, "clamped interval: pairs to compare", compared, 1);
		ok(disagree == 0, "clamped interval: both rules place the ticks alike", disagree, 0);
	}
}

/* ------------------------------------------- the display-bound cadence -- */

/* The blend factor bound to the display's own rate, and the one thing about
 * it that cannot be seen in the code.
 *
 * At fps_mode 1 the frames land on the ticks and the blend factor is a parity
 * bit: half when the last ControlPhase ran a tick, one when it did not. Bind
 * the rate to the display instead and nothing lands anywhere in particular -
 * at 144 Hz the frames come every 0.4167 ticks - so the factor comes out of
 * the clock, as `(framecount + subTick + 1) / 2`.
 *
 * What has to be true of that, and what cannot be judged by eye:
 *
 *   1. the logic is still thirty. Ten simulated seconds must run 300 logic
 *      steps at 60, at 120 and at 144 Hz alike, and the frames must number
 *      600, 1200 and 1440.
 *   2. the factor never leaves [0,1) - not "is clamped into it", never
 *      leaves it. A clamp means the frame asked for a state the logic has
 *      not computed, and the picture stands still for that frame; one per
 *      logic step is a stutter at 30 Hz on top of a 144 Hz picture.
 *   3. what is drawn advances with the wall clock, exactly. The content of
 *      frame i is `2(k-1) + 2 * alpha` in tick units, and the difference
 *      between that and the real time the frame was drawn at has to be a
 *      constant - the same constant on every frame of the run. Anything else
 *      is drift, and drift is what a fixed-step renderer bound to a free
 *      clock gets wrong.
 *   4. at 60 Hz it is the sixty hertz cadence again, alternating half a step
 *      per frame, which is what says the mode is not a different animation
 *      speed but the same one sampled more often.
 *
 * The model is the engine's loop, not an idealisation of it: the frame is
 * drawn, the flip waits for the panel, the clock is read after the flip
 * (specific\output.cpp, S_DumpScreen), and ControlPhase is handed the whole
 * ticks that came back. The vblanks are put on their own grid at their own
 * phase so that the two clocks are genuinely independent, the way a 144 Hz
 * panel and a 1/60 s counter are. */
static void refresh()
{
	printf("display-bound cadence\n");

	const int hz[3] = { 60, 120, 144 };

	for (int h = 0; h < 3; h++)
	{
		const double period = 60.0 / hz[h];	// the panel's period in 1/60 s ticks
		const double phase = 0.137;		// the vblank grid's own offset
		const int frames = hz[h] * 10;		// ten seconds of them

		long framecount = 0;
		long S = 0;				// whole ticks Sync() has given away
		int clamped = 0;
		unsigned long tickSerial = 0, steppedSerial = 0;
		int carried = 0;			// frames that stepped the counters
		int stalls = 0;				// frames whose content did not advance
		double alphaMin = 2.0, alphaMax = -1.0;
		double offset = 0, offsetMin = 1e9, offsetMax = -1e9;
		double lastContent = -1e9;

		// The level start: framecount = 0 and one priming ControlPhase(2),
		// exactly as DoLevel does it (game\gameflow.cpp).
		int k = 0;

		for (framecount += 2; framecount > 0; framecount -= 2)
			k++;

		for (int i = 0; i < frames; i++)
		{
			// The top of the frame is where the last flip left us.
			const double now = i * period + phase;
			const double subTick = now - (double)S;
			const float alpha = InterpRefreshAlpha(framecount, subTick);

			/* The guarded-counter criterion at a rate that does not divide:
			 * `alpha < 1` says "no tick on this frame" while the cadence is
			 * two frames to a tick; here it would be true on nearly every
			 * frame and every counter in the build would stand still. So the
			 * ticks are counted and the frame that finds a new one claims
			 * it - game\interpolate.cpp, g_tickSerial. */
			if (tickSerial != steppedSerial)
			{
				steppedSerial = tickSerial;
				carried++;
			}

			// What this frame draws, in ticks of simulated time: the pair
			// (k-1, k) blended by alpha.
			const double content = 2.0 * (k - 1) + 2.0 * alpha;

			if (alpha < alphaMin) alphaMin = alpha;
			if (alpha > alphaMax) alphaMax = alpha;

			if (alpha >= 1.0F || alpha < 0.0F)
				clamped++;

			if (i > 0 && content <= lastContent)
				stalls++;

			lastContent = content;

			offset = content - now;

			if (i > 0)
			{
				if (offset < offsetMin) offsetMin = offset;
				if (offset > offsetMax) offsetMax = offset;
			}

			// The flip waits for the next vblank; the clock is read after it.
			{
				const double flip = (i + 1) * period + phase;
				const long whole = (long)flip;
				long n = whole - S;

				S = whole;

				if (n > 10)
					n = 10;

				for (framecount += n; framecount > 0; framecount -= 2)
				{
					k++;
					tickSerial++;
				}
			}
		}

		printf("  %3d Hz: frames=%d ticks=%d alpha %.3f..%.3f clamped=%d stalls=%d "
			"lag spread %.2e ticks carried=%d\n",
			hz[h], frames, k - 1, alphaMin, alphaMax, clamped, stalls,
			offsetMax - offsetMin, carried);

		ok(frames == hz[h] * 10, "the panel's own frame count", frames, hz[h] * 10);
		ok(k - 1 == 300, "300 logic steps in ten seconds, whatever the panel does",
			k - 1, 300);
		ok(clamped == 0, "the blend factor never has to be clamped", clamped, 0);
		ok(carried == k - 1,
			"exactly one frame per logic tick steps the per-frame counters",
			carried, k - 1);
		ok(stalls == 0, "no frame draws what the one before it drew", stalls, 0);
		/* The tolerance is a float's, not a double's: the engine's blend
		 * factor is a float and everything derived from it carries that
		 * precision. A millionth of a tick is 17 nanoseconds of animation. */
		okf(offsetMax - offsetMin < 1e-5,
			"what is drawn follows the wall clock without drifting",
			offsetMax - offsetMin, 0.0);
	}

	/* And at sixty it is the sixty hertz cadence again: one tick of content per
	 * frame - the same step fps_mode 1 draws - so the mode is the same motion
	 * sampled at the panel's rate and not a different speed. */
	{
		const double period = 1.0;
		long framecount = 0, S = 0;
		int k = 0;
		double lastContent = -1e9;
		double worst = 0;

		for (framecount += 2; framecount > 0; framecount -= 2)
			k++;

		for (int i = 0; i < 600; i++)
		{
			const double now = i * period + 0.5;
			const float alpha = InterpRefreshAlpha(framecount, now - (double)S);
			const double content = 2.0 * (k - 1) + 2.0 * alpha;

			if (i > 1)
			{
				const double step = content - lastContent;
				const double off = step - 1.0 < 0 ? 1.0 - step : step - 1.0;

				if (off > worst)
					worst = off;
			}

			lastContent = content;

			{
				const double flip = (i + 1) * period + 0.5;
				const long whole = (long)flip;
				const long n = whole - S;

				S = whole;

				for (framecount += n; framecount > 0; framecount -= 2)
					k++;
			}
		}

		okf(worst < 1e-5, "60 Hz: every drawn step is exactly one 1/60 s tick",
			worst, 0.0);
	}

	/* A hitch, and the clamp is what it is for: one frame takes 200 ms - the
	 * panel misses twenty-eight of its slots at 144 Hz - so the factor asks
	 * for a state six logic steps ahead of the newest one there is. It is
	 * given the newest instead, which is what thirty hertz shows on a slow
	 * frame, and the ticks are caught up rather than lost: the simulation's
	 * own time may not depend on how long a frame took to draw. */
	{
		const double period = 60.0 / 144.0;
		long framecount = 0, S = 0, credited = 0, dropped = 0;
		int k = 0, clamped = 0;
		double now = 0;

		for (framecount += 2; framecount > 0; framecount -= 2)
			k++;

		for (int i = 0; i < 1440; i++)
		{
			const float alpha = InterpRefreshAlpha(framecount, now - (double)S);

			if (alpha >= 1.0F)
				clamped++;

			// 200 ms - twelve 1/60 s ticks - on frame 700.
			now += (i == 700) ? 12.0 : period;

			{
				const long whole = (long)now;
				long n = whole - S;

				S = whole;

				if (n > 10)
				{
					dropped += n - 10;
					n = 10;
				}

				credited += n;

				for (framecount += n; framecount > 0; framecount -= 2)
					k++;
			}
		}

		/* The invariant, not a hand-counted total: every tick that was fed
		 * to the accumulator was either spent on a logic step or is still
		 * standing in it. Two steps of the priming ControlPhase(2) go in on
		 * the left, and what the loop left behind comes off on the right -
		 * so no game time is lost because a frame took long to draw, and
		 * none is invented either, whatever numbers the model produces. */
		ok(2 * k == credited + 2 - framecount,
			"a 200 ms hitch is caught up: no logic tick lost, none invented",
			2 * k, (int)(credited + 2 - framecount));

		/* And the one place a tick really does go missing is vanilla's own
		 * `if (nframes > 10) nframes = 10` (game\control.cpp), which is what
		 * stops a five-second stall from running the game forward for five
		 * seconds when it comes back. The hitch fed twelve. */
		ok(dropped == 2, "vanilla's own cap dropped the two ticks past ten",
			(int)dropped, 2);
		ok(clamped > 0, "and the clamp is what carried the frame that asked too much",
			clamped > 0 ? 1 : 0, 1);
	}
}

/* ------------------------------------------------------------ particles -- */

/* The particle trail's one claim: a particle drawn from its own velocity
 * stands on exactly the same time axis as a body drawn from the snapshot.
 *
 * A body is drawn at `prev + (cur - prev) * alpha`, i.e. the world at
 * (tick - 1) + alpha. A particle has no `prev` - there are a thousand sparks
 * and two thousand raindrops and nobody is going to store two states for
 * each of them - but it has the thing the store would have held: every
 * particle simulation in this engine settles its velocity first and steps
 * its position by it second, so
 *
 *     prev = cur - step
 *
 * is exact rather than approximate, and the engine draws
 * `InterpTrailLong(cur, step, alpha)`.
 *
 * What could go wrong silently, and is therefore what is checked here:
 *
 *   1. the sign. `cur + vel * alpha` - the obvious reading of "move it along
 *      a bit" - would run the particle a whole logic step ahead of everything
 *      around it. Sparks would lead the gun that fired them. In a picture it
 *      looks like nothing at all; it looks like the game.
 *   2. the rounding. `cur - step * (1 - alpha)` is the same algebra and not
 *      the same integer: a C cast truncates towards zero, so on odd steps the
 *      two forms differ by one unit and the particles would sit beside where
 *      the item machinery would have put them. Checked over every step a TR5
 *      particle can carry, and the count of what the wrong form would have
 *      got wrong is printed beside it.
 *   3. the cadence. At fps_mode 2 the factor comes from a clock, reaches 0 on
 *      the frame after a tick and 1 where the next one is due, and the drawn
 *      particle must walk from prev to cur without ever leaving that interval
 *      or stepping backwards.
 *
 * Dead particles staying dead needs no arithmetic and gets none: every draw
 * site's `if (!On) continue;` is vanilla's and was not touched, and the
 * simulation that clears `On` runs on the tick. What is modelled below is the
 * other half of that - a particle that was born on the last tick is drawn
 * between its birthplace and where the tick left it, never before it. */
static void particles()
{
	printf("particles - the trail, and the axis it shares with the bodies\n");

	/* 1. THE IDENTITY, over every step a TR5 particle can carry. A spark's
	 * is `Xvel >> 5` with Xvel a short, so a few hundred units at most; a
	 * raindrop's fall is 8 to 88; the widest in the build is a shockwave's
	 * radius. Four thousand each way covers all of them with room to spare,
	 * and the negative half is not decoration - sparks rise, bubbles rise,
	 * and a shift of a negative number is where an integer form goes wrong.
	 */
	{
		const float alphas[5] = { 0.0F, 0.25F, 0.5F, 0.75F, 1.0F };
		long cases = 0;
		long wrong = 0;
		long naiveWrong = 0;

		for (long step = -4096; step <= 4096; step++)
		{
			for (int a = 0; a < 5; a++)
			{
				const long cur = 100000;
				const long got = InterpTrailLong(cur, step, alphas[a]);
				const long want = InterpLerpLong(cur - step, cur, alphas[a]);
				const long naive = cur - (long)((float)step * (1.0F - alphas[a]));

				cases++;

				if (got != want)
					wrong++;

				if (naive != want)
					naiveWrong++;
			}
		}

		printf("  %ld step/factor pairs: %ld differ from the snapshot's own lerp, "
			"%ld would have with `cur - step * (1 - alpha)`\n", cases, wrong, naiveWrong);

		ok(wrong == 0, "the trail IS the snapshot form, to the unit", wrong, 0);
		ok(naiveWrong > 0,
			"and the obvious rearrangement is not - which is why it is not used",
			naiveWrong > 0 ? 1 : 0, 1);
	}

	/* 2. SIXTY HERTZ. A falling particle under gravity - the velocity changes
	 * every tick, which is the case a fixed-velocity model would not catch -
	 * drawn over sixty frames beside an ITEM whose previous state really is
	 * remembered. Every frame, both of them, and the difference must be zero
	 * at every single one. */
	{
		long framecount = 0;
		long pos = 0, prev = 0, vel = 0, step = 0;
		long lastDrawn = 0;
		int frames = 0, tween = 0, mismatch = 0, backwards = 0, ahead = 0, behind = 0;
		bool first = true;

		// DoLevel's own priming tick, so that there is a previous state
		// before the first frame is drawn (game\gameflow.cpp).
		vel += 96;
		step = vel >> 5;
		prev = pos;
		pos += step;

		for (int i = 0; i < 60; i++)
		{
			const float alpha = framecount < 0 ? 0.5F : 1.0F;
			const long item = InterpLerpLong(prev, pos, alpha);	// the snapshot form
			const long part = InterpTrailLong(pos, step, alpha);	// the trail form

			frames++;

			if (alpha < 1.0F)
				tween++;

			if (item != part)
				mismatch++;

			if (part > pos)			// ahead of the newest state
				ahead++;

			if (part < prev)		// behind the one before it
				behind++;

			if (!first && part < lastDrawn)
				backwards++;

			lastDrawn = part;
			first = false;

			for (framecount += 1; framecount > 0; framecount -= 2)
			{
				vel += 96;
				step = vel >> 5;
				prev = pos;
				pos += step;
			}
		}

		printf("  fps_mode 1: frames=%d tween=%d fell %ld units, "
			"mismatch=%d ahead=%d behind=%d backwards=%d\n",
			frames, tween, pos, mismatch, ahead, behind, backwards);

		ok(mismatch == 0,
			"60 Hz: the particle draws where the snapshot would have put it",
			mismatch, 0);
		ok(tween == 30, "60 Hz: half the frames are in-between ones", tween, 30);
		ok(ahead == 0, "60 Hz: never drawn past the state the game holds", ahead, 0);
		ok(behind == 0, "60 Hz: never drawn before the state it came from", behind, 0);
		ok(backwards == 0, "60 Hz: the fall never steps back up", backwards, 0);
	}

	/* 3. The panel's own rate: the same particle at 144 Hz, with the
	 * factor coming from the clock through InterpRefreshAlpha and the flip
	 * paced by the display - the model of refresh() above, with a particle in
	 * it. What is new here and not in the sixty hertz run is that alpha
	 * really does reach 0: the frame right after a tick draws the PREVIOUS
	 * state, and the trail has to land on it exactly, or the picture would
	 * jump by a whole step once per logic tick. */
	{
		const double period = 60.0 / 144.0;
		const double phase = 0.137;
		const int frames = 144 * 5;

		long framecount = 0;
		long S = 0;
		long pos = 0, prev = 0, vel = 0, step = 0;
		long lastDrawn = 0;
		int mismatch = 0, backwards = 0, ahead = 0, behind = 0, atPrev = 0;
		bool first = true;

		vel += 96;
		step = vel >> 5;
		prev = pos;
		pos += step;

		for (int i = 0; i < frames; i++)
		{
			const double now = i * period + phase;
			const float alpha = InterpRefreshAlpha(framecount, now - (double)S);
			const long item = InterpLerpLong(prev, pos, alpha);
			const long part = InterpTrailLong(pos, step, alpha);

			if (item != part)
				mismatch++;

			if (part > pos)
				ahead++;

			if (part < prev)
				behind++;

			if (alpha == 0.0F && part != prev)
				atPrev++;

			if (!first && part < lastDrawn)
				backwards++;

			lastDrawn = part;
			first = false;

			{
				const double flip = (i + 1) * period + phase;
				const long whole = (long)flip;
				long n = whole - S;

				S = whole;

				if (n > 10)
					n = 10;

				for (framecount += n; framecount > 0; framecount -= 2)
				{
					vel += 96;
					step = vel >> 5;
					prev = pos;
					pos += step;
				}
			}
		}

		printf("  fps_mode 2 at 144 Hz: frames=%d fell %ld units, "
			"mismatch=%d ahead=%d behind=%d backwards=%d\n",
			frames, pos, mismatch, ahead, behind, backwards);

		ok(mismatch == 0,
			"144 Hz: the particle draws where the snapshot would have put it",
			mismatch, 0);
		ok(ahead == 0, "144 Hz: never drawn past the state the game holds", ahead, 0);
		ok(behind == 0, "144 Hz: never drawn before the state it came from", behind, 0);
		ok(backwards == 0, "144 Hz: the fall never steps back up", backwards, 0);
		ok(atPrev == 0, "144 Hz: a factor of nought draws the previous state exactly",
			atPrev, 0);
	}

	/* 4. Thirty hertz takes no branch at all. InterpParticleAlpha returns 1
	 * there and every draw site keeps vanilla's own expression - but if one
	 * of them ever computed the trail anyway, a factor of 1 has to be the
	 * identity or proof 6 (a byte-identical 30 Hz frame) would fail on a
	 * rounding. Checked over the same range as 1. */
	{
		long wrong = 0;

		for (long step = -4096; step <= 4096; step++)
			if (InterpTrailLong(12345, step, 1.0F) != 12345)
				wrong++;

		ok(wrong == 0, "at a factor of one the trail is the live position, unchanged",
			wrong, 0);
	}

	/* 5. A particle that was born on the last tick - the "no pre-run" rule.
	 *
	 * Everything that spawns a particle in this engine runs inside the tick
	 * loop and BEFORE the simulation walk (game\control.cpp), so a spark
	 * created on tick N has already taken one step by the time the frame is
	 * drawn: its stored position is birthplace + step. The trail therefore
	 * walks it back TOWARDS its birthplace and can never put it before that,
	 * which is what "a new drop appears at the tick, not ahead of it" means
	 * in arithmetic. */
	{
		const long birth = 5000;
		const long step = 37;			// what the spawning tick then moved it by
		const long pos = birth + step;
		int outside = 0;

		for (int a = 0; a <= 100; a++)
		{
			const float alpha = (float)a / 100.0F;
			const long drawn = InterpTrailLong(pos, step, alpha);

			if (drawn < birth || drawn > pos)
				outside++;
		}

		ok(outside == 0,
			"a newborn particle is drawn between its birthplace and the tick's end",
			outside, 0);
	}
}

/* ------------------------------------------- the portal walk and the eye -- */

/* The room the interpolated eye stands in.
 *
 * What is being proven: on an in-between frame the eye is blended and the
 * view matrix is rebuilt from it, while the portal walk of game\draw.cpp is
 * seeded with the logic camera's room. Where the eye has crossed a portal
 * plane between two ticks, two pieces of arithmetic in GetRoomBounds /
 * SetRoomBounds throw the room behind that portal away for that one frame.
 *
 * The walk itself cannot be linked here - it reads room[], the matrix stack
 * and a dozen file-static globals of draw.cpp - so what is checked is the
 * arithmetic of the two tests, copied line for line from the engine and run
 * on ANDREA1's own portal records, read out of the shipped level with
 * `TrgProbe -roomspace -room 2` and `-room 57`:
 *
 *   room 2  origin 45056 / 0 / 33792, portal -> room 57, normal -1/0/0,
 *           first corner (3072, -1792, 4096) room local = world x 48128
 *   room 57 origin 47104 / 0 / 36864, portal -> room  2, normal  1/0/0,
 *           first corner (1023,   256, 1024) room local = world x 48127
 *
 * That wall at world x 48128 is the corner of the Piazza dell'Opera where a
 * camera walking east across it between two logic ticks leaves the eye, at
 * the halfway point, 72 units past it. */
static bool portal_followed(long cornerWorld, long eyeWorld, long normal)
{
	/* game\draw.cpp, GetRoomBounds:
	 *   x = r->x + p->v1[0] - w2v_matrix[M03];
	 *   if (x && (x ^ p->normal[0]) < 0) SetRoomBounds(...); else continue;
	 * The XOR is a sign test: it is negative when exactly one of the two is. */
	const long x = cornerWorld - eyeWorld;

	return x != 0 && ((x ^ normal) < 0);
}

/* game\draw.cpp, SetRoomBounds: how many of the four corners come out at
 * z <= 0 in view space. Only the third row of the view matrix matters for
 * that, and for a camera looking along a horizontal direction that row IS the
 * direction: v->z = forward . (corner - eye). tooNear == 4 makes the function
 * return before it writes anything, so the room behind the portal is never
 * put on the draw list. */
static int too_near(const long corner[4][3], const long eye[3], const double fwd[3])
{
	int tooNear = 0;

	for (int i = 0; i < 4; i++)
	{
		const double z =
			fwd[0] * (double)(corner[i][0] - eye[0]) +
			fwd[1] * (double)(corner[i][1] - eye[1]) +
			fwd[2] * (double)(corner[i][2] - eye[2]);

		if (z <= 0.0)
			tooNear++;
	}

	return tooNear;
}


/* -------------------------------------------------------- the reach ------ */

/* The second rectangle of the portal walk.
 *
 * What is being proven: SetRoomBounds clips a child portal against the
 * window of the room it is seen through, and drops it when nothing is left.
 * Where an HD remaster has torn a wall open, the room behind that wall is
 * seen far wider than the classic doorway allows, and the child portal falls
 * outside the parent's window - so the room behind it is not drawn and the
 * player sees the clear colour through the hole.
 *
 * The numbers are measured off an eye in JOBY2's own geometry. RoomProbe
 * -walk, seeded in room 114 at 40024/-3408/24107, yaw 335, pitch -10,
 * 1280x720, dumps this from the room list:
 *
 *   room 124's window, through the classic doorway of room 123   0,0..535,719
 *   the portal 124 -> 99, its own projection on the screen     542,116..754,389
 *   the classic child rectangle, clamped against 124's window  535,116..535,389
 *
 * (The walk prints 124's window as 0,0..536,719 - the right edge is 535.5 as
 * a float and 535 as the short it is kept in, and the child is clamped
 * against the short.)
 *
 * That last one has no width, which is the `empty` of the walk and the black
 * hole in the picture: room 99's doorway begins six pixels to the right of
 * where room 124's own window ends.
 *
 * The rectangle arithmetic of both rules is copied from game\draw.cpp here
 * (SetRoomBounds and SetRoomBoundsReach), which is all that differs between
 * them - the projection of the four corners is the same in both. */
struct T4Rect
{
	float l, t, r, b;
};

static bool t4_empty(const T4Rect& q)
{
	return q.l >= q.r || q.t >= q.b;
}

/* SetRoomBounds: the projection SEEDED with the parent's own window and then
 * clamped back against it, which is the engine's own two steps and not one
 * intersection - the seeding is why an empty rectangle comes out sitting ON
 * the parent's edge rather than beyond it. */
static T4Rect t4_classic(const T4Rect& raw, const T4Rect& parent)
{
	T4Rect q;

	q.l = raw.l < parent.r ? raw.l : parent.r;
	q.r = raw.r > parent.l ? raw.r : parent.l;
	q.t = raw.t < parent.b ? raw.t : parent.b;
	q.b = raw.b > parent.t ? raw.b : parent.t;

	if (q.l < parent.l) q.l = parent.l;
	if (q.r > parent.r) q.r = parent.r;
	if (q.t < parent.t) q.t = parent.t;
	if (q.b > parent.b) q.b = parent.b;

	return q;
}

// SetRoomBoundsReach: the same projection clamped against the SCREEN.
static T4Rect t4_reach(const T4Rect& raw, float w, float h)
{
	T4Rect q = raw;

	if (q.l < 0) q.l = 0;
	if (q.r > w) q.r = w;
	if (q.t < 0) q.t = 0;
	if (q.b > h) q.b = h;

	return q;
}

/* And why the reach needs a seen-mark: a ring of rooms whose portals all face
 * the eye can be walked round for ever once the parent stops shrinking the
 * child's rectangle: each lap widens some room's classic window by a pixel
 * and earns another queue entry. This is that ring, four rooms round, with
 * the two brakes: the mark (one reach visit per room) and none at all. */
static long t4_ring_walk(int rooms, int laps, bool mark)
{
	// Every room is visited once per lap; with the mark, a room that has had
	// its reach visit is only queued again when the classic window grows,
	// which in this model it does not.
	long entries = 0;
	int seen = 0;

	for (int lap = 0; lap < laps; lap++)
		for (int i = 0; i < rooms; i++)
		{
			if (mark && lap > 0)
				continue;

			entries++;
			seen++;
		}

	(void)seen;
	return entries;
}

static void reach_rect()
{
	printf("the reach - the second rectangle of the portal walk\n");

	const float w = 1279.0f, h = 719.0f;
	const T4Rect parent = { 0.0f, 0.0f, 535.0f, 719.0f };	// room 124
	const T4Rect raw = { 542.0f, 116.0f, 754.0f, 389.0f };	// portal 124 -> 99

	const T4Rect classic = t4_classic(raw, parent);
	const T4Rect reach = t4_reach(raw, w, h);

	ok(t4_empty(classic) == true,
		"JOBY2: the classic child rectangle of 124 -> 99 is EMPTY", 1, 1);
	ok((long)classic.l == 535 && (long)classic.r == 535,
		"and it is empty because it has no width, at the parent's right edge",
		(long)classic.l, 535);
	ok(t4_empty(reach) == false,
		"the reach's rectangle for the same portal is NOT empty", 0, 0);
	ok((long)reach.l == 542 && (long)reach.r == 754,
		"and it is the portal's own projection, untouched by the parent",
		(long)reach.l, 542);
	ok((long)(reach.r - reach.l) == 212 && (long)(reach.b - reach.t) == 273,
		"212 by 273 pixels of room 99 that the classic walk does not draw",
		(long)(reach.r - reach.l), 212);

	/* The invariant the whole approach rests on: the classic rectangle is
	 * inside the reach's, always - it is the same projection with one more
	 * clamp on it. So a portal the reach refuses is a portal the classic
	 * rule refuses too, and switching the reach on can only add rooms. */
	{
		int inside = 0, tried = 0;
		unsigned seed = 12345;

		for (int i = 0; i < 2000; i++)
		{
			T4Rect q, par;

			seed = seed * 1103515245u + 12345u;
			q.l = (float)((seed >> 8) % 1400) - 60.0f;
			seed = seed * 1103515245u + 12345u;
			q.r = q.l + (float)((seed >> 8) % 400);
			seed = seed * 1103515245u + 12345u;
			q.t = (float)((seed >> 8) % 800) - 40.0f;
			seed = seed * 1103515245u + 12345u;
			q.b = q.t + (float)((seed >> 8) % 300);
			seed = seed * 1103515245u + 12345u;
			par.l = (float)((seed >> 8) % 900);
			seed = seed * 1103515245u + 12345u;
			par.r = par.l + (float)((seed >> 8) % 500);
			seed = seed * 1103515245u + 12345u;
			par.t = (float)((seed >> 8) % 500);
			seed = seed * 1103515245u + 12345u;
			par.b = par.t + (float)((seed >> 8) % 400);

			if (par.r > w) par.r = w;
			if (par.b > h) par.b = h;

			const T4Rect c = t4_classic(q, par);
			const T4Rect a = t4_reach(q, w, h);

			tried++;

			if (t4_empty(c) || (c.l >= a.l && c.r <= a.r && c.t >= a.t &&
				c.b <= a.b))
				inside++;
		}

		ok(inside == tried, "the classic rectangle is inside the reach's, in "
			"2,000 random pairs", inside, tried);
	}

	/* The ring, and why the seen-mark is the brake: with a second rectangle
	 * to widen instead of a mark, ANDREA3's worst frame takes over 20,000
	 * dequeues against the classic walk's 33, and ANDY2 and ANDREA3 run
	 * RoomProbe out of memory. With the mark the same survey's worst frame
	 * over the whole game is 319 (JOBY4). */
	{
		const long marked = t4_ring_walk(4, 500, true);
		const long loose = t4_ring_walk(4, 500, false);

		ok(marked == 4, "a ring of four rooms costs four queue entries with "
			"the seen-mark", marked, 4);
		ok(loose == 2000, "and five hundred laps without it", loose, 2000);
	}
}

/* ----------------------------------------- the two capacities ----------- */

/* The arrays of the portal walk, and why no level can fill them.
 *
 * These two mirror game\draw.cpp and have to be kept in step with it by hand.
 * What it proves is not that the numbers are large; it is the arithmetic
 * that makes them enough:
 *
 *   1. the ring's size is a power of two, because the engine wraps with a
 *      mask (`room_list_end & (kRoomListRing - 1)`) on both the write side
 *      and the dequeue side. A size that is not a power of two turns that
 *      mask into a different index from the modulo it replaced, and a queue
 *      that writes at one index and reads at another loses rooms silently.
 *   2. the queue's DEPTH cannot pass the room count, whatever the level's
 *      portals look like, because a room is queued only while its
 *      `bound_active & 2` is clear and queueing sets that bit - so at most
 *      one entry per room is pending at any moment. The model below is the
 *      engine's own loop with that interlock and a level of 256 rooms all
 *      joined to all, which is the worst graph there is.
 *   3. draw_rooms takes one entry per room (`bound_active & 1`), so its
 *      capacity has only to be the room maximum. */
static const long kT5DrawRoomsMax = 256;	// game\draw.cpp, draw_rooms
static const long kT5RoomListRing = 512;	// game\draw.cpp, draw_room_list
static const long kT5RoomMax = 256;		// game\setup.cpp, BuildOutsideTable's own bound

/* The engine's queue, run on a level of `rooms` rooms in which every room has
 * a portal to every other. Returns the deepest the queue ever got, or -1 if
 * the ring guard refused an entry - which is the failure this is watching
 * for. `entries` comes back with what draw_rooms would have had to hold.
 *
 * The one thing the model has to borrow from geometry is what STOPS the walk.
 * In the engine a room is re-queued whenever a portal hands it a rectangle
 * that is not empty, and what ends that is the windows shrinking along the
 * chain until one of them has no width; a graph with no geometry in it has
 * nothing that shrinks, so each room here may be queued kVisits times and no
 * more. That bound has no effect on the thing being measured: the DEPTH is
 * held down by the pending interlock alone, whatever the walk's length. */
static long t5_queue_depth(long rooms, long ring, long* entries)
{
	static const long kVisits = 3;
	static char pending[kT5RoomMax];
	static char listed[kT5RoomMax];
	static long visits[kT5RoomMax];
	static long queue[kT5RoomListRing];
	long start = 0, end = 0, deepest = 0, drawn = 0;

	if (rooms > kT5RoomMax || ring > kT5RoomListRing)
		return -2;

	for (long i = 0; i < rooms; i++)
	{
		pending[i] = listed[i] = 0;
		visits[i] = 0;
	}

	queue[0] = 0;
	pending[0] = 1;
	end = 1;

	while (start != end)
	{
		const long rn = queue[start & (ring - 1)];

		start++;
		pending[rn] = 0;

		if (!listed[rn])
		{
			listed[rn] = 1;
			drawn++;
		}

		for (long to = 0; to < rooms; to++)
		{
			if (to == rn || pending[to] || visits[to] >= kVisits)
				continue;

			// The engine's own guard, with the engine's own answer to it.
			if (end - start >= ring)
				return -1;

			queue[end & (ring - 1)] = to;
			end++;
			pending[to] = 1;
			visits[to]++;

			if (end - start > deepest)
				deepest = end - start;
		}
	}

	*entries = drawn;
	return deepest;
}

static void walk_capacity()
{
	printf("the walk's two arrays - capacity to the engine's own maximum\n");

	ok((kT5RoomListRing & (kT5RoomListRing - 1)) == 0,
		"the queue ring's size is a power of two, so the mask is the modulo",
		kT5RoomListRing & (kT5RoomListRing - 1), 0);

	// And the mask really is the modulo over a run longer than one lap.
	{
		int same = 0;

		for (long i = 0; i < 5000; i++)
			if ((i & (kT5RoomListRing - 1)) == i % kT5RoomListRing)
				same++;

		ok(same == 5000, "and it agrees with % over 5,000 consecutive writes",
			same, 5000);
	}

	ok(kT5DrawRoomsMax >= kT5RoomMax,
		"draw_rooms holds every room a level may have", kT5DrawRoomsMax,
		kT5RoomMax);

	/* The worst graph there is, walked. 256 rooms all joined to all is not a
	 * level anybody could build, and that is the point: if the queue cannot
	 * pass the room count here it cannot pass it anywhere. */
	{
		long entries = 0;
		const long deepest = t5_queue_depth(kT5RoomMax, kT5RoomListRing,
			&entries);

		ok(deepest > 0, "256 rooms joined to all: the ring guard never fires",
			deepest, 1);
		ok(deepest <= kT5RoomMax, "and the queue is never deeper than the "
			"room count", deepest, kT5RoomMax);
		ok(deepest < kT5RoomListRing, "which leaves the ring with room to "
			"spare", deepest, kT5RoomListRing);
		ok(entries <= kT5DrawRoomsMax, "and draw_rooms takes one entry per "
			"room, no more", entries, kT5DrawRoomsMax);
	}

	/* And the counter-case, so that the check above is not a tautology: a
	 * ring of 128 - what the engine had before this capacity was raised - is
	 * overrun by the same level, and the guard is what catches it. */
	{
		long entries = 0;

		ok(t5_queue_depth(kT5RoomMax, 128, &entries) == -1,
			"a ring of 128 against the same level: the guard refuses an "
			"entry", 1, 1);
	}
}

static void portal_walk()
{
	printf("portal walk - the room the blended eye stands in\n");

	// The two portal planes of the pair, in world units.
	const long wall = 48128;		// room 2's portal to room 57
	const long back = 48127;		// room 57's portal to room 2

	/* The camera's own two ticks, straight off the alley: one on either side
	 * of the wall, 400 units apart - a quarter of a sector, which is what a
	 * chase camera swinging around that corner covers in a thirtieth of a
	 * second. */
	const long tickA = 48000;
	const long tickB = 48400;
	const long eye = (tickA + tickB) / 2;	// the in-between frame: 48200

	ok(eye == 48200, "the blended eye is the midpoint of the two ticks",
		eye, 48200);

	// The live frames either side of it are drawn from their own tick and
	// both agree with the room they are in.
	ok(portal_followed(wall, tickA, -1) == true,
		"tick A (x 48000, room 2): the portal to 57 IS followed", 1, 1);
	ok(portal_followed(wall, tickB, -1) == false,
		"tick B (x 48400, room 57): room 2's portal is behind the eye", 0, 0);

	/* And this is the fault: the tween frame draws from room 2 - the logic
	 * camera has not stepped yet - with the eye already 72 units east of the
	 * wall, and the sign test refuses the portal. Room 57, and everything the
	 * walk would have reached through it, is missing for that one frame. */
	ok(portal_followed(wall, eye, -1) == false,
		"the tween frame seeded with room 2 DROPS the portal to 57", 0, 0);

	/* With the eye's own room as the seed the same frame keeps both rooms:
	 * from room 57 the portal back to room 2 faces the camera, so the walk
	 * reaches 2 as well and the picture is whole. */
	ok(portal_followed(back, eye, 1) == true,
		"seeded with room 57 the portal back to room 2 IS followed", 1, 1);

	/* The second way the same frame loses the room: even where the sign test
	 * passes, SetRoomBounds drops a portal whose four corners all fall behind
	 * the eye. The portal quad is the level file's own, in room 2's frame:
	 * (3072,-1792,4096) (3072,256,4096) (3072,256,5120) (3072,-1792,5120)
	 * plus the room origin 45056/0/33792. */
	const long quad[4][3] =
	{
		{ 45056 + 3072,     -1792, 33792 + 4096 },
		{ 45056 + 3072,       256, 33792 + 4096 },
		{ 45056 + 3072,       256, 33792 + 5120 },
		{ 45056 + 3072,     -1792, 33792 + 5120 }
	};
	const long eyeIn[3] = { 48000, -1024, 38400 };		// tick A
	const long eyeOut[3] = { eye,  -1024, 38400 };		// the tween frame
	const double east[3] = { 1.0, 0.0, 0.0 };		// looking along +x
	const double west[3] = { -1.0, 0.0, 0.0 };

	ok(too_near(quad, eyeIn, east) == 0,
		"from tick A, looking east, no corner of the portal is behind the eye",
		too_near(quad, eyeIn, east), 0);
	ok(too_near(quad, eyeOut, east) == 4,
		"from the blended eye, looking east, tooNear == 4 - the portal is dropped",
		too_near(quad, eyeOut, east), 4);
	ok(too_near(quad, eyeOut, west) == 0,
		"and looking west from the same point it is not - the sign test is the "
		"one that fires there",
		too_near(quad, eyeOut, west), 0);

	/* The whole interval, and where in it the answer changes: the eye walks
	 * from tick A to tick B; the portal is followed while it is west of the
	 * wall and refused after it. At fps_mode 0 there is no point in between
	 * to ask about, which is the player's first control - the flash is gone
	 * at thirty because no frame is ever drawn from a blended eye. */
	{
		int refused = 0;
		int first = -1;

		for (int step = 0; step <= 100; step++)
		{
			const long at = tickA + (tickB - tickA) * step / 100;

			if (!portal_followed(wall, at, -1))
			{
				refused++;

				if (first < 0)
					first = step;
			}
		}

		ok(first == 32, "the portal is refused from 32 % of the way across",
			first, 32);
		ok(refused == 69, "and for the rest of the interval", refused, 69);
		printf("  the eye crosses the wall at x %ld; %d of 101 sampled "
			"positions between the two ticks draw room 2 without room 57\n",
			wall, refused);
	}

	/* The same pair the other way: walking west, the logic room is 57 while
	 * the eye is already back in room 2, and it is room 57's own portal that
	 * is refused - the flash is symmetric and neither room is special. */
	{
		const long westA = 48200;
		const long westB = 47800;
		const long westEye = (westA + westB) / 2;

		ok(portal_followed(back, westA, 1) == true,
			"tick A (x 48200, room 57): the portal to room 2 IS followed", 1, 1);
		ok(portal_followed(back, westEye, 1) == false,
			"the tween frame seeded with room 57 DROPS the portal to room 2",
			0, 0);
		ok(portal_followed(wall, westEye, -1) == true,
			"seeded with room 2 the portal back to 57 IS followed", 1, 1);
	}
}

/* ------------------------------------------------------------ the ring -- */

/* The inventory ring at the frame rate, and its logic at thirty.
 *
 * The ring is a modal loop that never reaches ControlPhase, so it keeps an
 * accumulator of its own - InterpRingStep - and works its blend factor out of
 * it with InterpRingAlpha. Both live in the engine's own header, so what runs
 * below is the engine's arithmetic and not a transcription of it.
 *
 * Why this section exists: the order of the ring's loop is the other way
 * round from the game's - the game draws and then ticks, the ring steps and
 * then draws. Carrying the blend on the in-between frame the way the game
 * does, run in that order, walks the picture backwards on every second frame
 * - not something that can be judged by eye, because 8.3 ms of backwards
 * spin looks exactly like a rendering glitch. So both arrangements are run
 * here on the same simulated ring, and the one that is even and monotone is
 * the one the engine implements. */
static void ring_cadence()
{
	printf("the ring's cadence\n");

	/* ---- the accumulator at fps_mode 1: one tick per present ---- */
	{
		long acc = 0;
		int steps = 0, frames = 0, tween = 0, runOfTween = 0, worstRun = 0;
		int wrongStep = 0, wrongTween = 0, clockDisagreed = 0;

		for (int i = 0; i < 600; i++)
		{
			const bool step = InterpRingStep(&acc, 1);
			const float alpha = InterpRingAlpha(1, acc, 1.0, step);

			frames++;

			if (step)
			{
				steps++;
				runOfTween = 0;

				if (alpha != 0.5F)
					wrongStep++;
			}
			else
			{
				tween++;
				runOfTween++;

				if (runOfTween > worstRun)
					worstRun = runOfTween;

				if (alpha != 1.0F)
					wrongTween++;
			}

			// the clock says the same thing, which is the check that the
			// parity shortcut is the same rule and not a second one
			if (InterpRefreshAlpha(acc, 1.0) != alpha)
				clockDisagreed++;
		}

		ok(steps == 300, "600 ring frames at sixty run 300 logic steps", steps, 300);
		ok(tween == 300, "and 300 of them carry no step", tween, 300);
		ok(worstRun == 1, "never two frames in a row without a step", worstRun, 1);
		ok(wrongStep == 0, "fps_mode 1: every step frame is drawn at a half", wrongStep, 0);
		ok(wrongTween == 0, "fps_mode 1: every frame between is drawn live", wrongTween, 0);
		ok(clockDisagreed == 0, "fps_mode 1: the clock and the parity agree on all 600",
			clockDisagreed, 0);

		printf("  fps_mode 1: frames=%d steps=%d tween=%d, the clock agrees on every one\n",
			frames, steps, tween);
	}

	/* ---- the accumulator at 165 Hz, an off-integer refresh rate ----
	 *
	 * The frames are paced by the display and the whole ticks come back from
	 * Sync() on their own grid, so the returned counts are 0 and 1 in the
	 * pattern a 60 Hz counter makes under 165 presents a second. It is
	 * modelled the way the engine really gets them: the clock is read after
	 * the present, and what has crossed a tick boundary since the last read is
	 * what S_DumpScreen returns. */
	{
		const double hz = 165.0;
		const double frameTicks = 60.0 / hz;	// 1/60 s ticks per frame
		long acc = 0;
		double clock = 0.0;			// in ticks, since the ring opened
		long handed = 0;			// whole ticks Sync() has given away
		int steps = 0, frames = 0, clamped = 0;
		float last = 0.0F;
		int backwards = 0;
		bool first = true;

		for (int i = 0; i < 1650; i++)	// ten seconds
		{
			clock += frameTicks;

			const long whole = (long)clock;
			const long ticks = whole - handed;

			handed = whole;

			const bool step = InterpRingStep(&acc, ticks);
			const double subTick = clock - (double)handed;
			const float alpha = InterpRingAlpha(2, acc, subTick, step);

			frames++;

			if (step)
				steps++;

			if (alpha >= 1.0F)
				clamped++;

			/* The drawn instant is (step count) + alpha, in half-steps of a
			 * logic step. It must never go backwards - a backwards frame is
			 * exactly the fault this section exists to catch. */
			const float now = (float)steps + alpha;

			if (!first && now < last)
				backwards++;

			last = now;
			first = false;
		}

		ok(steps == 300, "ten seconds at 165 Hz run 300 ring steps", steps, 300);
		ok(frames == 1650, "against 1650 frames", frames, 1650);
		ok(backwards == 0, "and the drawn instant never walks backwards", backwards, 0);
		ok(clamped == 0, "the factor is never clamped to the newest state", clamped, 0);

		printf("  fps_mode 2 at 165 Hz: frames=%d steps=%d, never clamped, never backwards\n",
			frames, steps);
	}

	/* ---- a hitch does not make the ring run at the frame rate ---- */
	{
		long acc = 0;
		int steps = 0;

		// twenty ticks arrive in one frame (a 333 ms hitch), then sixty hertz
		InterpRingStep(&acc, 20);

		for (int i = 0; i < 60; i++)
		{
			if (InterpRingStep(&acc, 1))
				steps++;
		}

		ok(steps == 30, "after a hitch the ring is back to one step per two ticks",
			steps, 30);
		ok(acc >= -1 && acc <= 2, "and the accumulator is bounded", acc, 0);
	}

	/* ---- the two arrangements, on the same simulated spin ----
	 *
	 * The ring's own motion: the selected icon turns 1022 a logic step. What
	 * is compared is the sequence of DRAWN angles under the engine's rule (the
	 * step frame at a half, the frame between live) and under the one the
	 * design note asked for (the step frame live, the frame between at a
	 * half). Both are run off the same steps.
	 */
	{
		long acc = 0;
		long yrot = 0;			// the live field
		long prev = 0;			// the generation the step frame captured
		long lastMine = 0, lastTheirs = 0;
		int backMine = 0, backTheirs = 0;
		int unevenMine = 0;
		bool first = true;

		for (int i = 0; i < 400; i++)
		{
			const bool step = InterpRingStep(&acc, 1);
			const float alpha = InterpRingAlpha(1, acc, 1.0, step);

			if (step)
			{
				prev = yrot;		// captured before the body moves it
				yrot += 1022;		// and the body moves it
			}

			// what the engine draws: blend(prev, live, alpha)
			const long mine = (long)InterpLerpAngle((short)prev, (short)yrot, alpha);

			// what the note asked for: live on the step frame, the midpoint
			// on the frame between
			const long theirs = step ? yrot : (prev + yrot) / 2;

			/* Taken as a SHORT, so that the wrap at 65536 - which 1022 a
			 * step reaches after 64 steps - is the shorter way round and not
			 * a jump of a whole turn. That is the very property the blend is
			 * asserted on. */
			if (!first)
			{
				const short dMine = (short)((unsigned short)mine - (unsigned short)lastMine);
				const short dTheirs = (short)((unsigned short)theirs - (unsigned short)lastTheirs);

				if (dMine < 0)
					backMine++;

				if (dTheirs < 0)
					backTheirs++;

				if (dMine != 511)
					unevenMine++;
			}

			lastMine = mine;
			lastTheirs = theirs;
			first = false;
		}

		ok(backMine == 0, "the engine's arrangement never draws the spin backwards",
			backMine, 0);
		ok(unevenMine == 0, "and every drawn step is exactly half of 1022", unevenMine, 0);
		ok(backTheirs > 0, "the other arrangement walks backwards - which is why it is not the one",
			backTheirs > 0 ? 1 : 0, 1);

		printf("  the spin over 400 frames: the engine's arrangement %d backward frames, "
			"the note's %d\n", backMine, backTheirs);
	}

	/* ---- the spin over the wrap ----
	 *
	 * 1022 a step crosses 65536 every 64 steps. Blended the long way round the
	 * icon would spin a whole turn backwards for one frame, and that is the
	 * fault this whole file was written for. Walked far enough to cross the
	 * wrap four times.
	 */
	{
		long acc = 0;
		unsigned short yrot = 0;
		unsigned short prev = 0;
		int backwards = 0;
		int crossings = 0;
		long lastDrawn = 0;
		bool first = true;

		for (int i = 0; i < 600; i++)
		{
			const bool step = InterpRingStep(&acc, 1);
			const float alpha = InterpRingAlpha(1, acc, 1.0, step);

			if (step)
			{
				prev = yrot;
				yrot = (unsigned short)(yrot + 1022);

				if (yrot < prev)
					crossings++;
			}

			const unsigned short drawn =
				(unsigned short)InterpLerpAngle((short)prev, (short)yrot, alpha);

			/* the step between two drawn angles, taken as a short, is the
			 * shorter way round by construction - what is asserted is that it
			 * is always FORWARD and never more than a step */
			if (!first)
			{
				const short d = (short)(drawn - (unsigned short)lastDrawn);

				if (d < 0 || d > 1022)
					backwards++;
			}

			lastDrawn = drawn;
			first = false;
		}

		// 300 steps of 1022 is 306600, which is four whole turns and a bit
		ok(crossings == 4, "the spin crossed the wrap four times in 600 frames",
			crossings, 4);
		ok(backwards == 0, "and not one drawn frame turned the long way round",
			backwards, 0);
	}

	/* ---- the slide, and the jump in it ----
	 *
	 * objlistmovement walks 0 -> 65536 in eight steps of 8192 and is then set
	 * back to 0 with curobjinlist moved on. The pair that straddles the reset
	 * must not be blended: 57344 and 0 blend to 28672, which is the whole row
	 * sliding halfway back for one frame.
	 */
	{
		long acc = 0;
		long move = 0;			// the live field
		long cur = 0;			// curobjinlist
		long gen[2] = { 0, 0 };		// the two previous generations
		long genCur[2] = { 0, 0 };
		int gens = 0;
		int resets = 0, halfway = 0, backwards = 0, uneven = 0, unpaired = 0;
		long lastDrawn = 0;
		bool first = true;

		// three whole turns of the ring, one press at a time
		for (int turn = 0; turn < 3; turn++)
		{
			for (int i = 0; i < 20; i++)
			{
				const bool step = InterpRingStep(&acc, 1);
				const float alpha = InterpRingAlpha(1, acc, 1.0, step);

				if (step)
				{
					gen[1] = gen[0];
					genCur[1] = genCur[0];
					gen[0] = move;
					genCur[0] = cur;

					if (gens < 2)
						gens++;
				}

				const bool jumped = genCur[1] != genCur[0];
				const long drawn = gens >= 2
					? InterpRingSlide(gen[1], gen[0], jumped, alpha)
					: move;

				/* The pair: the slot the row is laid out from has to belong to
				 * the same generation the offset came from - the reset moves
				 * both in one breath, and a frame that took the new slot with
				 * the old offset put the whole row seven eighths of a slot to
				 * the side. */
				const long drawnCur = gens >= 2
					? InterpRingSlot(genCur[0], cur, 100)
					: cur;

				if (gens >= 2 && drawnCur != genCur[0])
					unpaired++;

				if (jumped && drawn == 28672)
					halfway++;

				if (!first && gens >= 2)
				{
					const long d = drawn - lastDrawn;

					// forward, and never more than one whole step; the two
					// frames of the jump stand still and are the only zeros
					if (d < 0 && !jumped)
						backwards++;

					if (d != 0 && d != 4096 && !jumped)
						uneven++;
				}

				// Not before the second generation stands: until then the
				// accessor hands back the live field and the two are a
				// different sequence.
				if (gens >= 2)
				{
					lastDrawn = drawn;
					first = false;
				}

				// the step the bottom of draw_current_object_list takes
				if (step)
				{
					if (move > 0)
						move += 8192;

					if (!move && i == 0)
						move += 8192;	// the press

					if (move >= 65536)
					{
						move = 0;
						cur++;
						resets++;
					}
				}
			}
		}

		ok(resets == 3, "three presses turned the ring three slots", resets, 3);
		ok(halfway == 0, "and the reset was never blended halfway back", halfway, 0);
		ok(backwards == 0, "the drawn slide never walks backwards", backwards, 0);
		ok(uneven == 0, "and moves in exact half steps of 4096", uneven, 0);
		ok(unpaired == 0, "the slot and the offset always come from one generation",
			unpaired, 0);
		ok(InterpRingSlot(7, 2, 5) == 2, "a slot past the end of a rebuilt list falls back to the live one",
			InterpRingSlot(7, 2, 5), 2);
		ok(InterpRingSlot(3, 2, 5) == 3, "and one inside it does not", InterpRingSlot(3, 2, 5), 3);

		printf("  the slide: %d turns of the ring, %d frames blended halfway across the reset\n",
			resets, halfway);
	}

	/* ---- fps_mode 0 takes every branch out ---- */
	{
		long acc = 0;
		int steps = 0, frames = 0, blended = 0;

		for (int i = 0; i < 300; i++)
		{
			// the ring's present waits its two ticks, so two come back
			const bool step = InterpRingStep(&acc, 2);
			const float alpha = InterpRingAlpha(0, acc, 1.0, step);

			frames++;

			if (step)
				steps++;

			if (alpha != 1.0F)
				blended++;
		}

		ok(steps == frames, "fps_mode 0: every ring frame carries its step",
			steps, frames);
		ok(blended == 0, "fps_mode 0: and not one of them is ever blended", blended, 0);
	}
}

/* ----------------------------------------------------- the title shadow -- */

/* The title's input shadow: one tap moves one row.
 *
 * TitleOptions is called once per present and reads dbinput, which
 * S_UpdateInput (specific\input.cpp) writes once per tick, in two statements
 * with the frame's own input read between them:
 *
 *     if (debounce) dbinput = inputBusy;              // the previous frame's
 *     inputBusy = linput;                              // this one's
 *     if (debounce) dbinput = inputBusy & (dbinput ^ inputBusy);
 *
 * so what comes out is the rising edge against the previous frame's input,
 * and dbinput's own incoming value is overwritten by the first line before
 * the second ever reads it. A held key is one edge and then nothing. At two
 * ticks a present that edge is read exactly once. At one, every second
 * ControlPhase runs no tick, dbinput keeps standing, and the next call reads
 * the same edge a second time: one tap of Down moves two rows.
 *
 * dbinput's previous value is not part of the next tick's answer - the first
 * statement above discards it - so zeroing it between ticks and restoring it
 * afterwards changes nothing about a held key's own edge. The restore still
 * matters for a different reason: SetDebounce is cleared inside the first
 * tick of a ControlPhase call (game\control.cpp, ControlPhase), so a call
 * that runs several ticks rewrites dbinput on the first of them only, and a
 * zero left behind would then stand through the rest of the hitch. Both
 * halves are asserted below.
 */
static void title_shadow()
{
	printf("the title's input shadow\n");

	const long DOWN = 0x0002;		// IN_BACK, as a bit

	for (int mode = 0; mode <= 1; mode++)
	{
		// mode 0: two ticks a present. mode 1: one tick a present.
		const long perPresent = mode ? 1 : 2;
		long framecount = 0;
		long dbinput = 0;
		long inputBusyPrev = 0;		// what the debounce formula in specific/input.cpp hands the edge test
		unsigned long serial = 0;	// g_tickSerial
		unsigned long seen = 0;		// the menu's own latch
		int actedRaw = 0, actedShadow = 0, restored = 0, mismatched = 0;

		for (int frame = 0; frame < 40; frame++)
		{
			// the key is held from frame 4 to frame 24
			const long inputBusy = (frame >= 4 && frame < 24) ? DOWN : 0;

			/* TitleOptions, as DoTitle calls it: the shadow first, then the
			 * menu's read, then the restore. */
			const bool repeat = serial == seen;

			seen = serial;

			const bool shadow = repeat && mode != 0;
			const long before = dbinput;

			// what an unshadowed build would have acted on
			if (dbinput & DOWN)
				actedRaw++;

			// the shadow goes ON, exactly as DoTitle writes it
			if (shadow)
				dbinput = 0;

			// TitleOptions' own read, inside the window
			if (dbinput & DOWN)
				actedShadow++;

			// and OFF again
			if (shadow)
				dbinput = before;

			if (dbinput == before)
				restored++;
			else
				mismatched++;

			/* DrawPhaseGame, then ControlPhase - and S_UpdateInput's two
			 * writes in their own order, with SetDebounce true only for the
			 * first tick of the call (game\control.cpp). */
			bool debounce = true;

			for (framecount += perPresent; framecount > 0; framecount -= 2)
			{
				serial++;

				if (debounce)
				{
					dbinput = inputBusyPrev;
					dbinput = inputBusy & (dbinput ^ inputBusy);
				}

				inputBusyPrev = inputBusy;
				debounce = false;
			}
		}

		if (mode == 0)
		{
			ok(actedRaw == 1, "at two ticks a present the menu reads one tap once",
				actedRaw, 1);
			ok(actedShadow == 1, "and the shadow changes nothing there",
				actedShadow, 1);
		}
		else
		{
			ok(actedRaw == 2, "at one tick a present the unshadowed menu reads it twice",
				actedRaw, 2);
			ok(actedShadow == 1, "and the shadow brings it back to one",
				actedShadow, 1);
		}

		ok(mismatched == 0, "dbinput is restored bit for bit on every frame",
			mismatched, 0);
		ok(restored == 40, "on all forty of them", restored, 40);
	}

	/* And the held key stays held - the whole reason the shadow is a shadow: a
	 * dbinput zeroed for good would put the edge back on every single tick,
	 * because the formula reads its own previous value. */
	{
		const long DOWN2 = 0x0002;
		long dbinput = 0;
		long prev = 0;
		int edges = 0;

		for (int tick = 0; tick < 20; tick++)
		{
			const long inputBusy = DOWN2;	// held for the whole run

			dbinput = prev;			// :821
			dbinput = inputBusy & (dbinput ^ inputBusy);	// :881
			prev = inputBusy;

			if (dbinput & DOWN2)
				edges++;
		}

		ok(edges == 1, "a held key is one edge and then nothing", edges, 1);
	}

	/* And what the restore really buys: a ControlPhase that runs several ticks
	 * refreshes dbinput on the first of them only - SetDebounce is cleared
	 * inside the tick body - so a zero left behind by an unrestored shadow
	 * stands through the rest of that hitch and swallows the edge the first
	 * tick found. Run here as the two builds side by side. */
	{
		const long SELECT = 0x0001;
		long dbKept = 0, dbLost = 0;
		long prev = 0;
		int seenKept = 0, seenLost = 0;

		// one present that runs three ticks; the key goes down on the first
		for (int tick = 0; tick < 3; tick++)
		{
			const long inputBusy = SELECT;
			const bool debounce = tick == 0;

			if (debounce)
			{
				dbKept = prev;
				dbKept = inputBusy & (dbKept ^ inputBusy);
				dbLost = prev;
				dbLost = inputBusy & (dbLost ^ inputBusy);
			}

			prev = inputBusy;

			// the menu reads between the ticks, and the unrestored build
			// leaves its zero behind
			if (dbKept & SELECT)
				seenKept++;

			if (dbLost & SELECT)
				seenLost++;

			dbLost = 0;
		}

		ok(seenKept == 3, "the restored shadow keeps the edge through a three-tick call",
			seenKept, 3);
		ok(seenLost == 1, "an unrestored one loses it after the first tick",
			seenLost, 1);
	}
}

/* The root offset across a half turn.
 *
 * The defect, in numbers: on the tick the underwater roll changes animation,
 * turn180_effect fires on the very frame the change lands on. ApplyNode
 * (game\interpolate.cpp) then keeps the live rotation for the whole drawn
 * frame - a half turn is a snap, not a blend - while InterpRootNow blends the
 * previous tick's root offset, which belongs to the frame half a turn back,
 * straight into this one. The blend walks through her own pivot.
 *
 * The numbers below are measured, not invented:
 *   - RootProbe over ANDREA1.TRC: animation 203's last frame 5330 carries
 *     (42,122,68); animation 205's frame 5423, the one the change lands on and
 *     the one FlipEffect 0 sits on, carries (-45,122,-67); and 205's own key 0
 *     (frame 5422, which the change skips) carries (42,122,68) again - the
 *     authors wrote the pre-flip offset and the post-flip offset as the same
 *     world point.
 *   - one roll's own drawn offsets on four consecutive in-between frames:
 *     (32,122,53), (17,122,29), (1,122,4), (-15,122,-20).
 *   - the ground roll's refused tick, from the same tool: offset_prev=
 *     (-14,-421,-7), offset_cur=(-8,-439,-7), and no flip on that tick - the
 *     effect fired one tick earlier, inside animation 48, where the frame
 *     steps by one and the pose is accepted.
 *
 * The measure: both offsets are turned by the same rotation when they are
 * drawn - the live one - so the world error is the distance in the item's own
 * frame between the drawn offset and the segment the two ticks put it on, and
 * no rotation has to be built to check it. */
static double seg_dist(const long p[3], const long a[3], const long b[3])
{
	const double ax = (double)a[0], ay = (double)a[1], az = (double)a[2];
	const double bx = (double)b[0], by = (double)b[1], bz = (double)b[2];
	const double dx = bx - ax, dy = by - ay, dz = bz - az;
	const double len = dx * dx + dy * dy + dz * dz;
	double t = 0.0;

	if (len > 0.0)
	{
		t = ((p[0] - ax) * dx + (p[1] - ay) * dy + (p[2] - az) * dz) / len;

		if (t < 0.0)
			t = 0.0;

		if (t > 1.0)
			t = 1.0;
	}

	const double ex = p[0] - (ax + dx * t);
	const double ey = p[1] - (ay + dy * t);
	const double ez = p[2] - (az + dz * t);

	return sqrt(ex * ex + ey * ey + ez * ez);
}

static void root_flip()
{
	printf("root offset across a half turn\n");

	/* The two signs: half a turn about y negates x and z and leaves y, and it
	 * is its own inverse - which is the one property the rule needs, because
	 * the same function has to answer a flip either way round. */
	{
		long worst = 0;
		long badY = 0;
		long badBack = 0;

		for (long x = -400; x <= 400; x += 7)
		{
			for (long y = -400; y <= 400; y += 13)
			{
				for (long z = -400; z <= 400; z += 11)
				{
					const long in[3] = { x, y, z };
					long out[3], back[3];

					InterpRootFlip(in, out);
					InterpRootFlip(out, back);

					if (out[1] != y)
						badY++;

					if (back[0] != x || back[1] != y || back[2] != z)
						badBack++;

					if (out[0] != -x || out[2] != -z)
						worst++;
				}
			}
		}

		ok(worst == 0, "the flip negates x and z over the whole sweep", worst, 0);
		ok(badY == 0, "and never touches y - the height above the pivot is hers",
			badY, 0);
		ok(badBack == 0, "and is its own inverse", badBack, 0);
	}

	/* The underwater roll, anim 203 frame 5330 -> anim 205 frame 5423. */
	{
		const long prev[3] = { 42, 122, 68 };
		const long cur[3] = { -45, 122, -67 };
		long turned[3];

		InterpRootFlip(prev, turned);

		ok(turned[0] == -42 && turned[1] == 122 && turned[2] == -68,
			"205's pre-flip offset turned into the post-flip frame", turned[0], -42);

		/* The level file's own answer: 205 key 0 is the pre-flip offset and
		 * key 1 the post-flip one, so the turned one has to land on key 1. */
		const double gap = seg_dist(turned, cur, cur);

		okf(gap < 4.0, "and lands on the frame the animation really carries",
			gap, 0.0);

		double worstToday = 0.0;
		double worstFixed = 0.0;

		for (int s = 0; s <= 256; s++)
		{
			const float t = (float)s / 256.0F;
			long today[3], fixed[3];

			InterpRootLerp(prev, cur, t, today);
			InterpRootLerp(turned, cur, t, fixed);

			const double a = seg_dist(today, turned, cur);
			const double b = seg_dist(fixed, turned, cur);

			if (a > worstToday)
				worstToday = a;

			if (b > worstFixed)
				worstFixed = b;
		}

		okf(worstToday > 140.0, "today the drawn root leaves the two ticks by over 140 units",
			worstToday, 140.0);
		okf(worstFixed <= 2.0, "turned first, it never leaves them by more than the truncation",
			worstFixed, 2.0);

		/* One roll's own four measured frames, against the same segment. */
		const long filmed[4][3] =
		{
			{ 32, 122, 53 }, { 17, 122, 29 }, { 1, 122, 4 }, { -15, 122, -20 }
		};
		int filmedOut = 0;

		printf("  the roll's four filmed frames, units beside the two ticks:");

		for (int f = 0; f < 4; f++)
		{
			const double d = seg_dist(filmed[f], turned, cur);

			printf(" %.0f", d);

			if (d > 50.0)
				filmedOut++;
		}

		printf("\n  worst over the whole alpha sweep: today %.0f, turned first %.1f\n",
			worstToday, worstFixed);

		ok(filmedOut == 4, "all four frames the play test filmed are over 50 units out",
			filmedOut, 4);
	}

	/* And a tick without a flip is the arithmetic it always was: the ground
	 * roll's refused tick is the worked example, and the sweep behind it is
	 * every offset pair this file can think of: where the walk says no flip,
	 * the engine hands InterpRootLerp the previous offset unchanged, so the
	 * two lines below are the whole of the plain rule. */
	{
		const long prev[3] = { -14, -421, -7 };
		const long cur[3] = { -8, -439, -7 };
		int moved = 0;

		for (int s = 0; s <= 256; s++)
		{
			const float t = (float)s / 256.0F;
			long a[3], b[3];

			InterpRootLerp(prev, cur, t, a);
			InterpRootLerp(prev, cur, t, b);

			if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2])
				moved++;
		}

		ok(moved == 0, "the ground roll's refused tick blends exactly as it did",
			moved, 0);

		/* It is refused, and it is not a flip: the previous tick's offset and
		 * this one's are on the same side of her axis, which a flip never is. */
		ok(prev[0] * cur[0] > 0 && prev[2] * cur[2] > 0,
			"and its two offsets are on the same side of her, so no flip is asked for",
			(long)(prev[0] * cur[0] > 0), 1);
	}

	/* The predicate is ApplyNode's own, so the engine cannot fire one rule
	 * without the other. Every half turn is a half turn either way round, and
	 * nothing under a half turn is one. */
	{
		int missed = 0;
		int spurious = 0;

		for (long a = -32768; a < 32768; a += 37)
		{
			const short from = (short)a;

			if (!InterpHalfTurn(from, (short)(from + 0x8000)))
				missed++;

			if (!InterpHalfTurn(from, (short)(from - 0x8000)))
				missed++;

			for (long d = -32000; d <= 32000; d += 4001)
			{
				if (d && InterpHalfTurn(from, (short)(from + d)))
					spurious++;
			}
		}

		ok(missed == 0, "every half turn is seen, both ways round", missed, 0);
		ok(spurious == 0, "and nothing short of one is taken for it", spurious, 0);
	}
}

/* What a position step pays for, and nothing else. The engine's own order is
 * InterpRootFlip, then InterpRootKey, then InterpRootNarrow, then
 * InterpRootLerp from the narrowed start offset - so every case below hands
 * InterpRootNarrow the offset the draw is in, and the last group proves that
 * the order is not free. */
static void root_narrow()
{
	printf("the root blends only what a position step pays for\n");

	/* The ladder, and it is the regression proof for the whole rule: ANDREA1
	 * and JOBY5 both carry the wrap of anim 161 at these numbers, and the
	 * TranslateItem that runs at it is (0,-512,0). 512 pays for 504, so the y
	 * axis keeps its whole step and the start offset IS prev - which makes
	 * InterpRootLerp's operands today's, at every alpha there is. */
	{
		const long prev[3] = { -9, -879, -103 };
		const long cur[3] = { -13, -375, -100 };
		const long step[3] = { 0, -512, 0 };
		long start[3];

		InterpRootNarrow(prev, cur, step, start);

		ok(start[1] == prev[1], "the ladder wrap keeps its whole y step: the start offset is prev",
			start[1], prev[1]);
		ok(labs(cur[1] - start[1]) == 504, "which is 504 units, and 512 paid for it",
			labs(cur[1] - start[1]), 504);

		/* x and z have a step of -4 and +3 and nothing pays for either, so the
		 * refused frame is drawn at the offset its own pose was built for. */
		ok(start[0] == cur[0] && start[2] == cur[2],
			"and x and z, which nothing paid for, start at the frame's own offset",
			start[0], cur[0]);

		int movedY = 0, movedXZ = 0;

		for (int s = 0; s <= 256; s++)
		{
			const float t = (float)s / 256.0F;
			long today[3], narrow[3];

			InterpRootLerp(prev, cur, t, today);
			InterpRootLerp(start, cur, t, narrow);

			if (narrow[1] != today[1])
				movedY++;

			if (narrow[0] != cur[0] || narrow[2] != cur[2])
				movedXZ++;
		}

		ok(movedY == 0, "the drawn y is today's at every alpha in 1/256 steps", movedY, 0);
		ok(movedXZ == 0, "and x and z never leave the frame's own offset", movedXZ, 0);
	}

	/* The crawl, which is the case this rule is for: ANDREA1 anim 274 frame
	 * 6655 to anim 222 frame 5653, a y step of 61 units and item->pos does
	 * not move.
	 * Nothing is paid, the blend comes out at cur, and InterpRootNow's own
	 * "blended == cur" exit then hands the draw nothing at all. */
	{
		const long prev[3] = { 0, -204, -37 };
		const long cur[3] = { 0, -143, -37 };
		const long step[3] = { 0, 0, 0 };
		long start[3];
		int moved = 0;

		InterpRootNarrow(prev, cur, step, start);

		ok(start[0] == cur[0] && start[1] == cur[1] && start[2] == cur[2],
			"crawl to crouch keeps nothing: the start offset is cur", start[1], cur[1]);

		for (int s = 0; s <= 256; s++)
		{
			const float t = (float)s / 256.0F;
			long narrow[3];

			InterpRootLerp(start, cur, t, narrow);

			if (narrow[0] != cur[0] || narrow[1] != cur[1] || narrow[2] != cur[2])
				moved++;
		}

		ok(moved == 0, "so the blend is cur at every alpha, and the rule stands down",
			moved, 0);

		/* And the picture it replaces: the old blend draws the crouch pose
		 * 30 units above the offset it was built for. */
		long today[3];

		InterpRootLerp(prev, cur, 0.5F, today);
		ok(today[1] == -174 && cur[1] - today[1] == 31,
			"where today it is drawn 31 units above its own root at a half",
			cur[1] - today[1], 31);
	}

	/* A partial case, measured and not invented: ANDREA1's wrap of anim 174
	 * into anim 102, whose TranslateItem is (-50,-512,0) against an offset step
	 * of (+41,+514,+35). x is paid in full, y is two units short of its step,
	 * and z is not paid at all - three verdicts on one transition. */
	{
		const long prev[3] = { -49, -953, 157 };
		const long cur[3] = { -8, -439, 192 };
		const long step[3] = { -50, -512, 0 };
		long start[3], today[3], narrow[3];

		InterpRootNarrow(prev, cur, step, start);

		ok(start[0] == prev[0], "174's x is paid in full and starts at prev", start[0], prev[0]);
		ok(start[1] == cur[1] - 512, "its y keeps 512 of 514 - what the command paid",
			start[1], cur[1] - 512);
		ok(start[2] == cur[2], "and its z, which nothing paid for, starts at cur",
			start[2], cur[2]);

		InterpRootLerp(prev, cur, 0.5F, today);
		InterpRootLerp(start, cur, 0.5F, narrow);

		ok(today[1] == -696 && narrow[1] == -695,
			"so a half draws y at -695 where today draws -696", narrow[1], -695);
		ok(today[2] == 174 && narrow[2] == 192,
			"and z at its own 192 where today drags it to 174", narrow[2], 192);
	}

	/* The invariant, over a grid and not over an example: the start offset is
	 * always between prev and cur inclusive, which is the whole of "never more
	 * than today and never in another direction"; a step that points the same
	 * way as the offset step pays for nothing; and a step on one axis narrows
	 * that axis alone. */
	{
		int outside = 0, sameSign = 0, zeroStep = 0, bled = 0;

		for (long p = -600; p <= 600; p += 23)
		{
			for (long c = -600; c <= 600; c += 29)
			{
				const long d = c - p;

				for (long v = -900; v <= 900; v += 37)
				{
					const long prev[3] = { p, p, p };
					const long cur[3] = { c, c, c };
					const long onY[3] = { 0, v, 0 };
					long start[3];

					InterpRootNarrow(prev, cur, onY, start);

					/* Between the two, inclusive, on the axis that was paid. */
					if (d > 0 ? (start[1] < p || start[1] > c) : (start[1] > p || start[1] < c))
						outside++;

					if (labs(c - start[1]) > labs(d))
						outside++;

					if (d && v && ((d > 0) == (v > 0)) && start[1] != c)
						sameSign++;

					if (!v && start[1] != c)
						zeroStep++;

					/* x and z had no step of their own, so they cannot move. */
					if (start[0] != c || start[2] != c)
						bled++;
				}
			}
		}

		ok(outside == 0, "the start offset never leaves the segment prev..cur", outside, 0);
		ok(sameSign == 0, "a step that points the same way as the offset step pays for nothing",
			sameSign, 0);
		ok(zeroStep == 0, "and a zero step pays for nothing at all", zeroStep, 0);
		ok(bled == 0, "a step on one axis narrows that axis and no other", bled, 0);
	}

	/* The flip comes first, and it is not a free choice. The underwater
	 * roll's own offsets: 203's last frame carries (42,122,68) and the frame
	 * the change lands on carries (-45,122,-67), so the turned previous offset
	 * is (-42,122,-68) and the step the narrowing weighs is -3 on x, not -87. */
	{
		const long prev[3] = { 42, 122, 68 };
		const long cur[3] = { -45, 122, -67 };
		const long step[3] = { 4, 0, 0 };
		long turned[3], right[3], wrong[3];

		InterpRootFlip(prev, turned);
		InterpRootNarrow(turned, cur, step, right);
		InterpRootNarrow(prev, cur, step, wrong);

		ok(right[0] == turned[0],
			"flipped first, the 3 units of x a step of 4 could pay for are all of it",
			right[0], turned[0]);
		ok(wrong[0] == cur[0] + 4,
			"taken in the old frame the same step pays for 4 of 87 and starts elsewhere",
			wrong[0], cur[0] + 4);
		/* The two must not agree; printing the pair is what tells a failing
		 * run which value both collapsed onto. */
		ok(right[0] != wrong[0], "so the order of the two is not free",
			right[0], wrong[0]);

		/* And with nothing to pay, a flip tick stands down like any other. */
		const long none[3] = { 0, 0, 0 };
		long idle[3];

		InterpRootNarrow(turned, cur, none, idle);
		ok(idle[0] == cur[0] && idle[1] == cur[1] && idle[2] == cur[2],
			"and a flip tick that nothing pays for is handed back to the draw site",
			idle[0], cur[0]);
	}

	/* Extremes: the offsets are shorts widened to long, so their difference is
	 * at most 65535; `step` is a difference of two tick positions and could in
	 * principle be anything a long holds. Nothing in InterpRootNarrow negates
	 * it, so the whole range is safe - including the value that has no
	 * positive twin. */
	{
		const long lo = -32768, hi = 32767;
		const long prev[3] = { lo, hi, lo };
		const long cur[3] = { hi, lo, hi };
		const long huge[3] = { LONG_MIN, LONG_MAX, 0 };
		long start[3];

		InterpRootNarrow(prev, cur, huge, start);

		ok(start[0] == lo, "a step of LONG_MIN against a rise of 65535 pays for all of it",
			start[0], lo);
		ok(start[1] == hi, "and LONG_MAX against a fall of 65535 likewise", start[1], hi);
		ok(start[2] == cur[2], "while a zero step still pays for nothing", start[2], cur[2]);

		/* The same magnitudes pointing the SAME way pay for nothing, which is
		 * the branch an overflow would have hidden. */
		const long same[3] = { LONG_MAX, LONG_MIN, LONG_MAX };
		long idle[3];

		InterpRootNarrow(prev, cur, same, idle);
		ok(idle[0] == cur[0] && idle[1] == cur[1] && idle[2] == cur[2],
			"and the same magnitudes the same way round pay for nothing", idle[0], cur[0]);
	}
}

/* --------------------------------------------------- key frames built -- */

/* The angle between two nodes' rotations given as three angles, in degrees,
 * through the engine's own matrix (InterpPoseRot) and trace (InterpPoseTurnCos). */
static double rot_angle(const short* a, const short* b)
{
	float ma[9], mb[9];

	InterpPoseRot(a, ma);
	InterpPoseRot(b, mb);

	double c = (double)InterpPoseTurnCos(ma, mb);

	if (c > 1.0)
		c = 1.0;
	else if (c < -1.0)
		c = -1.0;

	return acos(c) * 180.0 / 3.14159265358979323846;
}

/* The worst distance from 1 of the three column lengths of a rotation given
 * as three angles. */
static double rot_column_error(const short* xyz)
{
	float m[9];
	double worst = 0.0;

	InterpPoseRot(xyz, m);

	for (int c = 0; c < 3; c++)
	{
		const double l = sqrt((double)m[c] * m[c] + (double)m[3 + c] * m[3 + c] +
			(double)m[6 + c] * m[6 + c]);

		if (fabs(l - 1.0) > worst)
			worst = fabs(l - 1.0);
	}

	return worst;
}

/* The shortest column of vanilla's element-wise blend of two rotations at
 * `alpha` (InterpBlendFloat, the arithmetic of InterpolateMatrix), in the
 * engine's matrix stack. InterpPoseRot composes its rows the other way round
 * (Rz * Rx * Ry for the stack's Ry * Rx * Rz), so the stack's columns are its
 * rows. */
static double blend_shortest_column(const short* a, const short* b, float alpha)
{
	float ma[9], mb[9];
	double shortest = 9.0;

	InterpPoseRot(a, ma);
	InterpPoseRot(b, mb);

	for (int c = 0; c < 3; c++)
	{
		double s = 0.0;

		for (int r = 0; r < 3; r++)
		{
			const double v = InterpBlendFloat(ma[c * 3 + r], mb[c * 3 + r], alpha);

			s += v * v;
		}

		if (sqrt(s) < shortest)
			shortest = sqrt(s);
	}

	return shortest;
}

/* A far key pair drawn from a key frame built at the weight, and the frame
 * turned by half a turn (interpolate_math.h, InterpKeyFrameLerp and
 * InterpKeyFrameTurn), and where turn180_effect fires (InterpTurnKind). */
static void key_build()
{
	printf("key frames built rather than blended\n");

	/* 1. The packed forms read and written again: every one-axis word on all
	 * three axes, and a sweep of three-axis pairs, come back word for word. */
	{
		long bad = 0;

		for (int axis = 0; axis < 3; axis++)
		{
			for (int v = 0; v < 4096; v++)
			{
				const short w = (short)(unsigned short)(((axis + 1) << 14) | v);
				const short* p = &w;
				short xyz[3], out[2];

				InterpUnpackRot(p, xyz);

				if (InterpRotAxis(&w) != axis || InterpPackRot(xyz, axis, out) != 1 || out[0] != w)
					bad++;
			}
		}

		ok(bad == 0, "one-axis rotations unpack and pack to themselves", bad, 0);

		bad = 0;

		for (unsigned long x = 0; x < 1024; x += 31)
		{
			for (unsigned long y = 0; y < 1024; y += 29)
			{
				for (unsigned long z = 0; z < 1024; z += 37)
				{
					const unsigned long packed = (x << 20) | (y << 10) | z;
					const short w[2] = { (short)(unsigned short)(packed >> 16),
						(short)(unsigned short)(packed & 0xFFFF) };
					const short* p = w;
					short xyz[3], out[2];

					InterpUnpackRot(p, xyz);

					if (InterpRotAxis(w) != -1 || InterpPackRot(xyz, -1, out) != 2 ||
						out[0] != w[0] || out[1] != w[1])
						bad++;
				}
			}
		}

		ok(bad == 0, "three-axis rotations unpack and pack to themselves", bad, 0);
	}

	/* 2. Two rotations 98.8 degrees apart about y (17984, a step of the
	 * one-axis form) blended at a half: 49.4 degrees from each, a rotation;
	 * and on three axes (y and x together), the same half on each side. */
	{
		const short a[3] = { 0, 0, 0 };
		const short b[3] = { 0, 17984, 0 };
		short r[3];

		InterpSlerpRot(a, b, 0.5F, r);

		okf(fabs(rot_angle(a, r) - 49.4) < 0.1, "a 98.8 degree pair at a half is 49.4 from the first",
			rot_angle(a, r), 49.4);
		okf(fabs(rot_angle(r, b) - 49.4) < 0.1, "and 49.4 from the second", rot_angle(r, b), 49.4);
		okf(rot_column_error(r) < 1e-5, "and a rotation", rot_column_error(r), 0.0);
		okf(blend_shortest_column(a, b, 0.5F) < 0.66, "where vanilla's blend keeps 0.65 of the frame",
			blend_shortest_column(a, b, 0.5F), 0.65);

		const short c[3] = { 6400, 12800, -3200 };
		const short d[3] = { -5760, -9600, 7040 };
		const double whole = rot_angle(c, d);

		InterpSlerpRot(c, d, 0.25F, r);

		okf(fabs(rot_angle(c, r) - whole * 0.25) < 0.2 && fabs(rot_angle(r, d) - whole * 0.75) < 0.2,
			"three axes at a quarter: a quarter and three quarters of the turn",
			rot_angle(c, r), whole * 0.25);
	}

	/* 3. The short way across the wrap: 30000 and -30000 in y are 5536
	 * apart through the half turn, not 60000 through zero. */
	{
		const short a[3] = { 0, 30000, 0 };
		const short b[3] = { 0, -30000, 0 };
		short r[3];

		InterpSlerpRot(a, b, 0.5F, r);

		okf(fabs(rot_angle(a, r) - 15.2) < 0.1 && fabs(rot_angle(r, b) - 15.2) < 0.1,
			"the half across the wrap is 15.2 degrees from each end", rot_angle(a, r), 15.2);
		ok(r[1] > 32000 || r[1] < -32000, "and stands at the half turn, not at zero", r[1], -32768);
	}

	/* 4. A built frame: the box from the nearer key, the offset as
	 * InterpRootKey blends it, a one-axis form against a three-axis form
	 * built in the three-axis form, a pair of one-axis forms on the same axis
	 * kept in it, and a node equal in both keys copied. */
	{
		short f0[9 + 5], f1[9 + 5], out[INTERP_KEYFRAME_MAX];
		const short box0[6] = { -100, 90, -700, 0, -80, 60 };
		const short box1[6] = { -120, 110, -650, 10, -70, 90 };

		for (int i = 0; i < 6; i++)
		{
			f0[i] = box0[i];
			f1[i] = box1[i];
		}

		f0[6] = 10; f0[7] = -500; f0[8] = 7;
		f1[6] = -30; f1[7] = -420; f1[8] = -7;

		/* node 0: one axis y 4096 against three axes (640, 12800, 0) */
		const short y0[3] = { 0, 4096, 0 };
		const short y1[3] = { 640, 12800, 0 };

		InterpPackRot(y0, 1, f0 + 9);
		InterpPackRot(y1, -1, f1 + 9);

		/* node 1: one axis x on both, 1600 and -2400 */
		f0[10] = (short)(unsigned short)((1 << 14) | (1600 >> 4));
		f1[11] = (short)(unsigned short)((1 << 14) | ((unsigned short)(short)-2400 >> 4 & 0xFFF));

		/* node 2: the same three-axis rotation in both */
		const short same[3] = { 1280, -2560, 640 };

		InterpPackRot(same, -1, f0 + 11);
		InterpPackRot(same, -1, f1 + 12);

		const int n = InterpKeyFrameLerp(f0, f1, 3, 0.25F, out);
		long root[3];

		InterpRootKey(f0, f1, 1, 0.25F, root);

		ok(n == 9 + 2 + 1 + 2, "the built frame's length: two words, one word, two words", n, 14);
		ok(out[0] == box0[0] && out[5] == box0[5], "at a quarter the box is the first key's", out[0], box0[0]);
		ok(out[6] == root[0] && out[7] == root[1] && out[8] == root[2],
			"and the offset InterpRootKey's", out[7], root[1]);
		ok(InterpRotAxis(out + 9) == -1, "a one-axis node against a three-axis one is built three-axis",
			InterpRotAxis(out + 9), -1);

		const short* p = out + 9;
		short r0[3], r1[3], r2[3];

		InterpUnpackRot(p, r0);
		InterpUnpackRot(p, r1);
		InterpUnpackRot(p, r2);

		const double whole = rot_angle(y0, y1);

		okf(fabs(rot_angle(y0, r0) - whole * 0.25) < 0.4,
			"and turned a quarter of the way", rot_angle(y0, r0), whole * 0.25);
		ok(InterpRotAxis(out + 11) == 0 && labs((long)r1[0] - InterpLerpAngle(1600, -2400, 0.25F)) <= 8,
			"one axis against the same axis stays one-axis, blended the short way", r1[0],
			InterpLerpAngle(1600, -2400, 0.25F));
		ok(out[12] == f0[11] && out[13] == f0[12], "a node equal in both keys is copied", out[12], f0[11]);

		InterpKeyFrameLerp(f0, f1, 3, 0.75F, out);

		ok(out[0] == box1[0] && out[5] == box1[5], "at three quarters the box is the second key's",
			out[0], box1[0]);
	}

	/* 5. The back jump's twist end, animation 213 key 0 to 1 in ANDREA1, its
	 * two packed hips rotations copied from the level file (03f2 6ed1 and
	 * 3bc0 3fb1, 98.8 degrees apart); and the forward twist's 209 key 0 to 1
	 * (3280 7942 and 022f 5be6, 152.6 degrees). The middle frame vanilla draws
	 * shrinks the hips to 0.70 and 0.36 of their length; built, it is a
	 * rotation half way along. */
	{
		const short k213[2][2] = { { 0x03f2, 0x6ed1 }, { 0x3bc0, 0x3fb1 } };
		const short k209[2][2] = { { 0x3280, 0x7942 }, { 0x022f, 0x5be6 } };
		const short (*pairs[2])[2] = { k213, k209 };
		const double turn[2] = { 98.8, 152.6 };
		const double shrink[2] = { 0.70, 0.36 };
		const char* what[2][4] =
		{
			{ "213: the hips turn 98.8 degrees between the keys",
				"213 built at a half: 49.4 degrees from key 0",
				"and 49.4 from key 1, a rotation",
				"where vanilla's middle frame keeps a hips column of 0.70" },
			{ "209: the hips turn 152.6 degrees between the keys",
				"209 built at a half: 76.3 degrees from key 0",
				"and 76.3 from key 1, a rotation",
				"where vanilla's middle frame keeps a hips column of 0.36" }
		};

		for (int k = 0; k < 2; k++)
		{
			short f0[9 + 2], f1[9 + 2], out[INTERP_KEYFRAME_MAX];

			for (int i = 0; i < 9; i++)
				f0[i] = f1[i] = 0;

			f0[9] = (short)(unsigned short)pairs[k][0][0];
			f0[10] = (short)(unsigned short)pairs[k][0][1];
			f1[9] = (short)(unsigned short)pairs[k][1][0];
			f1[10] = (short)(unsigned short)pairs[k][1][1];

			const short* p0 = f0 + 9;
			const short* p1 = f1 + 9;
			short a[3], b[3], m[3];

			InterpUnpackRot(p0, a);
			InterpUnpackRot(p1, b);
			InterpKeyFrameLerp(f0, f1, 1, 0.5F, out);

			const short* pm = out + 9;

			InterpUnpackRot(pm, m);

			okf(fabs(rot_angle(a, b) - turn[k]) < 0.05, what[k][0], rot_angle(a, b), turn[k]);
			okf(fabs(rot_angle(a, m) - turn[k] * 0.5) < 0.5, what[k][1], rot_angle(a, m), turn[k] * 0.5);
			okf(fabs(rot_angle(m, b) - turn[k] * 0.5) < 0.5 && rot_column_error(m) < 1e-5,
				what[k][2], rot_angle(m, b), turn[k] * 0.5);
			okf(fabs(blend_shortest_column(a, b, 0.5F) - shrink[k]) < 0.01, what[k][3],
				blend_shortest_column(a, b, 0.5F), shrink[k]);
		}
	}

	/* 6. A frame turned by half a turn: the offset and the box turn about the
	 * vertical, node 0's matrix loses the sign of its x and z columns (a half
	 * turn about y applied before it), the other nodes are copied, and turned
	 * twice a three-axis frame is itself again. 212's last frame lies between
	 * key 2 (44,-548,-7) and key 3 (44,-586,-7). */
	{
		short f[9 + 3], out[INTERP_KEYFRAME_MAX], back[INTERP_KEYFRAME_MAX];
		const short box[6] = { -120, 110, -650, 10, -70, 90 };
		const short hips[3] = { 6400, -15360, 1920 };

		for (int i = 0; i < 6; i++)
			f[i] = box[i];

		f[6] = 44; f[7] = -567; f[8] = -7;
		InterpPackRot(hips, -1, f + 9);
		f[11] = (short)(unsigned short)((3 << 14) | 0x123);

		const int n = InterpKeyFrameTurn(f, 2, out);

		ok(n == 12 && out[6] == -44 && out[7] == -567 && out[8] == 7,
			"the offset turns: (44,-567,-7) becomes (-44,-567,7)", out[6], -44);
		ok(out[0] == -110 && out[1] == 120 && out[2] == -650 && out[3] == 10 &&
			out[4] == -90 && out[5] == 70, "the box's x and z extents turn with it", out[0], -110);
		ok(out[11] == f[11], "node 1 is copied", out[11], f[11]);

		const short* p = out + 9;
		short turned[3];
		float m0[9], m1[9];

		InterpUnpackRot(p, turned);
		InterpPoseRot(hips, m0);
		InterpPoseRot(turned, m1);

		double worst = 0.0;

		for (int r = 0; r < 3; r++)
		{
			worst = fmax(worst, fabs((double)m1[r * 3 + 0] + m0[r * 3 + 0]));
			worst = fmax(worst, fabs((double)m1[r * 3 + 1] - m0[r * 3 + 1]));
			worst = fmax(worst, fabs((double)m1[r * 3 + 2] + m0[r * 3 + 2]));
		}

		okf(worst < 1e-5, "node 0 is the same frame with its x and z columns negated", worst, 0.0);

		InterpKeyFrameTurn(out, 2, back);

		bool same = true;

		for (int i = 0; i < 12; i++)
			same = same && back[i] == f[i];

		ok(same, "turned twice, a three-axis frame is itself", same ? 1 : 0, 1);

		/* one-axis hips: y stays one-axis, x becomes three-axis */
		short g[9 + 1], h[INTERP_KEYFRAME_MAX];

		for (int i = 0; i < 9; i++)
			g[i] = 0;

		g[9] = (short)(unsigned short)((2 << 14) | 0x100);
		ok(InterpKeyFrameTurn(g, 1, h) == 10 && InterpRotAxis(h + 9) == 1 &&
			(unsigned short)h[9] == ((2 << 14) | 0x900), "a one-axis y hips node stays one-axis, half a turn on",
			(unsigned short)h[9], (2 << 14) | 0x900);

		g[9] = (short)(unsigned short)((1 << 14) | 0x100);
		ok(InterpKeyFrameTurn(g, 1, h) == 11 && InterpRotAxis(h + 9) == -1,
			"a one-axis x hips node becomes three-axis", InterpRotAxis(h + 9), -1);
	}

	/* 7. Where the effect fires: a three-key animation of rate 4 from frame
	 * 100 whose hips turn 12 degrees from key 0 to 1 and 170 from key 1 to 2.
	 * On key 2's frame (108) the half turn is inside the far pair (1, 2) and
	 * that pair is held; on frame 106, inside the same pair, it is not held
	 * (the flip is not on the second key); on frame 104 (key 1) the pair
	 * (0, 1) turns 12 degrees: early; on the first frame, none. */
	{
		const long size = 11;
		short keys[3 * 11 + 11];

		for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
			keys[i] = 0;

		const short k1[3] = { 0, 2176, 0 };
		const short k2[3] = { 0, (short)(2176 + 30976), 0 };

		InterpPackRot(k1, -1, keys + size + 9);
		InterpPackRot(k2, -1, keys + 2 * size + 9);

		long pair = 0;

		ok(InterpTurnKind(keys, size, 100, 4, 108, &pair) == INTERP_TURN_INSIDE && pair == 1,
			"the flip on key 2 after a 170 degree pair: inside, pair 1", pair, 1);
		ok(InterpKeyHold(INTERP_TURN_INSIDE, 1, 108, 100, 4, 1), "and that pair is held", 1, 1);
		ok(!InterpKeyHold(INTERP_TURN_INSIDE, 1, 108, 100, 4, 0), "the pair before it is not", 0, 0);
		ok(InterpTurnKind(keys, size, 100, 4, 106, &pair) == INTERP_TURN_INSIDE &&
			!InterpKeyHold(INTERP_TURN_INSIDE, pair, 106, 100, 4, pair),
			"a flip strictly inside the far pair is not held", pair, 1);
		ok(InterpTurnKind(keys, size, 100, 4, 104, &pair) == INTERP_TURN_EARLY && pair == 0,
			"the flip on key 1 after a 12 degree pair: early, pair 0", pair, 0);
		ok(InterpTurnKind(keys, size, 100, 4, 100, &pair) == INTERP_TURN_NONE && pair == -1,
			"the flip on the first frame: none", pair, -1);
	}
}

/* ------------------------------------ the bridge after an early half turn -- */

/* The worst angle, in degrees, between the nodes of two frames in the level's
 * own format, node by node (InterpUnpackRot reads either packed form). */
static double frame_worst_angle(const short* f, const short* g, int nodes)
{
	const short* a = f + 9;
	const short* b = g + 9;
	double worst = 0.0;

	for (int node = 0; node < nodes; node++)
	{
		short ra[3], rb[3];

		InterpUnpackRot(a, ra);
		InterpUnpackRot(b, rb);
		worst = fmax(worst, rot_angle(ra, rb));
	}

	return worst;
}

/* A frame holding nothing but a three-axis hips rotation, which is all
 * InterpHipsTurn, InterpBridgeLong and InterpKeyFar read. */
static void hips_frame(short* f, unsigned short w0, unsigned short w1)
{
	for (int i = 0; i < 9; i++)
		f[i] = 0;

	f[9] = (short)w0;
	f[10] = (short)w1;
}

/* The dot product of the two turns InterpHipsTurn finds: positive where they
 * turn the same way round, negative where they turn against each other. */
static double hips_turn_dot(const short* a0, const short* a1, const short* b0, const short* b1)
{
	float u[3], v[3];

	InterpHipsTurn(a0, a1, u);
	InterpHipsTurn(b0, b1, v);

	return (double)u[0] * v[0] + (double)u[1] * v[1] + (double)u[2] * v[2];
}

/* The frames between two ticks across the animation change after an early
 * half turn (interpolate_math.h, InterpBridgeFires, InterpBridgeLong and
 * InterpKeyFrameBridge): bridged from the pose the flip tick drew to the
 * pose of the current tick. The frames are ANDREA1's, copied from the level
 * file: the standing-jump twist's 210 key 8 (frame 5544, the flip frame) and
 * 211 key 0 (frame 5545) whole, fifteen nodes; the hips of 210 key 7, 211
 * key 1, the back twist's 212 keys 2 and 3 and 213 keys 0 and 1, and the
 * running-jump twist's 207 keys 3 and 4 and 209 keys 0 and 1. */
static void key_bridge()
{
	printf("the bridge across an early half turn's animation change\n");

	static const short k210_8[36] =
	{
		-164, 137, -712, -460, -194, 197, 69, -626, -7, 14940, -11049, 3949, -3203,
		15287, -513, 14704, 3078, 4093, -14473, 15208, 511, 14832, 1021, 12512,
		-18479, 3276, -10441, 319, -853, -12403, 3235, -14105, 528, 1902, -16199, 20181
	};
	static const short k211_0[36] =
	{
		-153, 148, -793, -541, -194, 195, -84, -626, -7, 1459, 13018, 3949, -3203,
		15287, -513, 14640, 3078, 4093, -14473, 15208, 511, 14784, 1021, 12512,
		-18479, 3276, -10441, 319, -853, -12395, 3235, -14105, 528, 1902, -16207, 20181
	};
	const int nodes = 15;
	short from[INTERP_KEYFRAME_MAX], out[INTERP_KEYFRAME_MAX];

	/* The flip tick drew 210's last frame turned by half a turn; with a
	 * remainder of zero that is the turned key frame itself. */
	InterpKeyFrameTurn(k210_8, nodes, from);

	/* 1. The ends: at t 0 the bridge is the first pose and at t 1 the
	 * second, node by node to the step of the packed form, either way round
	 * at the hips. */
	for (int way = 0; way < 2; way++)
	{
		InterpKeyFrameBridge(from, k211_0, nodes, 0.0F, way == 1, out);

		const double at0 = frame_worst_angle(out, from, nodes);

		InterpKeyFrameBridge(from, k211_0, nodes, 1.0F, way == 1, out);

		const double at1 = frame_worst_angle(out, k211_0, nodes);

		okf(at0 < 0.5, way ? "the long way: at t 0 every node is the flip tick's pose"
			: "the short way: at t 0 every node is the flip tick's pose", at0, 0.0);
		okf(at1 < 0.5, way ? "and at t 1 every node the current tick's"
			: "and at t 1 every node the current tick's", at1, 0.0);
	}

	/* 2. The arc. The hips turn 179.2 degrees from 210's turned last frame to
	 * 211's first, and the shorter way round turns against her own turn over
	 * 210's last pair (keys 7 to 8, turned) and over 211's first (keys 0 to
	 * 1), so the longer way is taken, 180.8 degrees: at t 0.5 the hips are a
	 * rotation 90.4 degrees from each end, on the far side of the shorter
	 * way. */
	{
		short k7[11], k8[11], before0[INTERP_KEYFRAME_MAX], before1[INTERP_KEYFRAME_MAX];
		short k211_1[11];

		hips_frame(k7, 0x3c3c, 0xfcd4);
		hips_frame(k8, 0x3a5c, 0xd4d7);
		hips_frame(k211_1, 0x06d2, 0x9efa);
		InterpKeyFrameTurn(k7, 1, before0);
		InterpKeyFrameTurn(k8, 1, before1);

		const short* pf = from + 9;
		const short* pt = k211_0 + 9;
		short hf[3], ht[3], hm[3];

		InterpUnpackRot(pf, hf);
		InterpUnpackRot(pt, ht);

		okf(fabs(rot_angle(hf, ht) - 179.2) < 0.1, "210 to 211: the hips 179.2 degrees apart",
			rot_angle(hf, ht), 179.2);

		const bool longHips = InterpBridgeLong(from, k211_0, before0, before1, k211_0, k211_1);

		ok(hips_turn_dot(from, k211_0, before0, before1) < 0.0 &&
			hips_turn_dot(from, k211_0, k211_0, k211_1) < 0.0,
			"the shorter way turns against 210's last pair and 211's first", 0, 0);
		ok(longHips, "so the longer way is taken", longHips ? 1 : 0, 1);

		InterpKeyFrameBridge(from, k211_0, nodes, 0.5F, longHips, out);

		const short* pm = out + 9;

		InterpUnpackRot(pm, hm);

		okf(fabs(rot_angle(hf, hm) - 90.4) < 0.5, "at t 0.5 the hips are 90.4 degrees from the first",
			rot_angle(hf, hm), 90.4);
		okf(fabs(rot_angle(hm, ht) - 90.4) < 0.5 && rot_column_error(hm) < 1e-5,
			"and 90.4 from the second, a rotation", rot_angle(hm, ht), 90.4);
		ok(hips_turn_dot(from, out, from, k211_0) < 0.0,
			"on the far side of the shorter way", 0, 0);

		/* every node of the middle frame a rotation */
		const short* p = out + 9;
		double worst = 0.0;

		for (int node = 0; node < nodes; node++)
		{
			short r[3];

			InterpUnpackRot(p, r);
			worst = fmax(worst, rot_column_error(r));
		}

		okf(worst < 1e-5, "and every node of it a rotation", worst, 0.0);

		/* the shorter way, asked for, lies on the near side */
		InterpKeyFrameBridge(from, k211_0, nodes, 0.5F, false, out);
		ok(hips_turn_dot(from, out, from, k211_0) > 0.0,
			"the shorter way's middle lies on its own side", 0, 0);

		/* against on one side only, or with no pair after: the shorter way */
		ok(!InterpBridgeLong(from, k211_0, before1, before0, k211_0, k211_1),
			"a turn against one side only keeps the shorter way", 1, 0);
		ok(!InterpBridgeLong(from, k211_0, before0, before1, 0, 0),
			"and so does a turn with no pair after it", 1, 0);
	}

	/* The back twist 212 to 213 (its flip frame between keys 2 and 3 at a
	 * half) turns 19.5 degrees and the running-jump twist 207 to 209 (keys 3
	 * and 4 at a quarter) 13.5: near pairs, the shorter way. */
	{
		short a[11], b[11], ta[INTERP_KEYFRAME_MAX], tb[INTERP_KEYFRAME_MAX];
		short f212[INTERP_KEYFRAME_MAX], f207[INTERP_KEYFRAME_MAX];
		short t0[11], t1[11];

		hips_frame(a, 0x066c, 0x4305);
		hips_frame(b, 0x041a, 0x72d5);
		InterpKeyFrameTurn(a, 1, ta);
		InterpKeyFrameTurn(b, 1, tb);
		InterpKeyFrameLerp(ta, tb, 1, 0.5F, f212);
		hips_frame(t0, 0x03f2, 0x6ed1);
		hips_frame(t1, 0x3bc0, 0x3fb1);

		const short* pf = f212 + 9;
		const short* pt = t0 + 9;
		short hf[3], ht[3];

		InterpUnpackRot(pf, hf);
		InterpUnpackRot(pt, ht);

		okf(fabs(rot_angle(hf, ht) - 19.5) < 0.1 && !InterpBridgeLong(f212, t0, ta, tb, t0, t1),
			"212 to 213: 19.5 degrees, the shorter way", rot_angle(hf, ht), 19.5);

		hips_frame(a, 0x30db, 0x9058);
		hips_frame(b, 0x32f8, 0x6947);
		InterpKeyFrameTurn(a, 1, ta);
		InterpKeyFrameTurn(b, 1, tb);
		InterpKeyFrameLerp(ta, tb, 1, 0.25F, f207);
		hips_frame(t0, 0x3280, 0x7942);
		hips_frame(t1, 0x022f, 0x5be6);

		pf = f207 + 9;
		pt = t0 + 9;
		InterpUnpackRot(pf, hf);
		InterpUnpackRot(pt, ht);

		okf(fabs(rot_angle(hf, ht) - 13.5) < 0.1 && !InterpBridgeLong(f207, t0, ta, tb, t0, t1),
			"207 to 209: 13.5 degrees, the shorter way", rot_angle(hf, ht), 13.5);
	}

	/* 3. The box and the root offset are the current tick's, at every
	 * weight: what InterpRootNow reads off the drawn pair (both slots the
	 * bridge) is the offset it reads off that tick's own frame. */
	{
		const float ts[3] = { 0.25F, 0.5F, 0.75F };
		bool box = true, offset = true;

		for (int i = 0; i < 3; i++)
		{
			InterpKeyFrameBridge(from, k211_0, nodes, ts[i], true, out);

			for (int k = 0; k < 6; k++)
				box = box && out[k] == k211_0[k];

			long drawn[3], own[3];

			InterpRootKey(out, out, 1, ts[i], drawn);
			InterpRootKey(k211_0, 0, 0, 0.0F, own);

			for (int k = 0; k < 3; k++)
				offset = offset && out[6 + k] == k211_0[6 + k] && drawn[k] == own[k];
		}

		ok(box, "the bridge's box is the current tick's", out[0], k211_0[0]);
		ok(offset, "and its root offset, read as the root rule reads it", out[6], k211_0[6]);
	}

	/* 4. When it fires: a frame between two ticks, the tick before on the
	 * frame an early half turn fires on, the animation changed since. Not
	 * for a half turn inside a far pair (the underwater roll 205, flip 5423;
	 * the tightrope turn 451, flip 8916), nor one on an animation's first
	 * frame (the roll 147, flip 3591), nor a tick before that stood on
	 * another frame, nor the same animation, nor a frame that carries a tick
	 * (t 0 or 1). */
	{
		ok(InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 211, 0.5F),
			"fires: early, on the flip frame, 210 to 211, t 0.5", 0, 1);
		ok(InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 211, 1.0F / 5.5F) &&
			InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 211, 5.0F / 5.5F),
			"and at the first and the last of five frames a tick", 0, 1);
		ok(!InterpBridgeFires(INTERP_TURN_INSIDE, 5423, 205, 5423, 206, 0.5F),
			"not for the half turn inside the underwater roll 205", 1, 0);
		ok(!InterpBridgeFires(INTERP_TURN_INSIDE, 8916, 451, 8916, 452, 0.5F),
			"nor inside the tightrope turn 451", 1, 0);
		ok(!InterpBridgeFires(INTERP_TURN_NONE, 3591, 147, 3591, 146, 0.5F),
			"nor for the roll 147, which fires on its first frame", 1, 0);
		ok(!InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5543, 211, 0.5F),
			"nor where the tick before stood on another frame", 1, 0);
		ok(!InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 210, 0.5F),
			"nor where the animation did not change", 1, 0);
		ok(!InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 211, 1.0F) &&
			!InterpBridgeFires(INTERP_TURN_EARLY, 5544, 210, 5544, 211, 0.0F),
			"nor on a frame that carries a tick", 1, 0);
	}
}

/* ------------------------------------------ far in the drawn frame, any node -- */

/* The turn in degrees between two nodes' drawn rotations (InterpDrawnRots),
 * through the engine's own trace (InterpPoseTurnCos). */
static double drawn_angle(const float* a, const float* b)
{
	double c = (double)InterpPoseTurnCos(a, b);

	if (c > 1.0)
		c = 1.0;
	else if (c < -1.0)
		c = -1.0;

	return acos(c) * 180.0 / 3.14159265358979323846;
}

/* The determinant of a 3x3, row major. */
static double det3(const double* m)
{
	return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
		m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/* The key pairs whose drawn rotation turns more than 90 degrees at any node
 * (interpolate_math.h, InterpDrawnRots, InterpKeyFarDrawn, InterpKeyFarFill
 * and InterpKeyFarBit), and the frame built across them (InterpKeyFrameFar).
 * The frames and Lara's bone table are ANDREA1's, copied from the level
 * file: the standing jump forward's take-off (76 keys 0 and 1), 10 keys 0
 * and 1 (state AS_RUN), hitting a wall (53, keys 0 and 1), the run's first
 * pair (1 keys 0 and 1), the tightrope turn's held pair (451 keys 25 and 26)
 * and the standing jump up's take-off (91 keys 0 and 1). */
static void key_far_all()
{
	printf("key pairs far in the drawn frame, at any node\n");

	static const long bones[14 * 4] =
	{
		2, -42, 20, 2, 0, 9, 185, 3, 0, 1, 192, -8, 3, 43, 20, 2, 0, -10, 185, 2,
		0, 0, 193, -2, 1, -1, -49, 11, 2, 59, -142, -12, 0, 10, 95, -2, 0, 1, 101, -1,
		3, -57, -143, -12, 0, -9, 98, -2, 0, -1, 99, -1, 1, 2, -198, -23
	};
	static const short k76[2][36] =
	{
		{ -176, 138, -451, 4, -153, 202, -8, -217, -68, 15359, -1021, 3001, -29061, 14040,
			511, 1808, 29731, 2999, 11676, 14055, -512, 1839, -26666, 14464, 3066, 14480,
			-14402, 303, -6, -12475, 13967, 31816, 528, 1031, -16126, 16924 },
		{ -175, 188, -712, 0, -324, 330, -8, -412, -8, 14767, -2044, 16191, -7, -16384,
			15136, 4101, 16207, -6145, -16384, 15184, 1021, 16303, -5, 3276, -10523, 319,
			-6, -12464, 3235, -14039, 528, 1032, -16138, 16858, 35, 0 }
	};
	static const short k10[2][36] =
	{
		{ -102, 114, -781, -54, -322, 235, 1, -443, -5, 15919, -9215, 20320, 19956, 20297,
			17078, 19899, 16543, 16288, -15361, 13008, 958, 3151, -12302, -12370, 1488,
			16444, 3351, -28189, -16272, 479, 24575, 35, 0, 33, 0, 34 },
		{ -174, 180, -782, -44, -293, 275, 4, -438, -5, 20449, 16351, -6124, 12599, -512,
			20275, 2176, 995, 20138, 20445, 80, 14354, 14305, 12160, 3455, -19477, -12383,
			15519, 17483, 2848, 15377, -16253, 415, -11278, 35, 0, 33 }
	};
	static const short k53[2][34] =
	{
		{ -112, 102, -764, -4, -182, 140, -9, -441, -72, 16208, 1027, 1183, -31743, 20379,
			1327, -6188, 1872, 10232, 19607, 399, -2009, 751, -2053, 1084, -23758, 383, -6,
			-13233, 659, 22742, 688, 2056, -15430, 20136 },
		{ -172, 148, -761, 2, -129, 78, -8, -439, -17, 16208, 1027, 623, 28702, 20273, 304,
			10206, 1040, 28639, 20129, 287, -3038, 783, -2053, 16191, 18256, 1112, 2565,
			-13259, 15952, 13518, 1175, -3593, -15406, 20122 }
	};
	static const short k1[2][33] =
	{
		{ -112, 105, -787, -4, -113, 210, 1, -447, -1, 16240, 9212, 1280, 2, 20295, 16447,
			15952, 8, 20478, 16521, 95, -12286, 447, 7148, 591, -1030, -12459, 15679, 2069,
			256, 7, -16150, 16368, 1023 },
		{ -115, 105, -786, 0, -142, 239, 1, -446, 6, 16240, 14333, 1248, 1, 20374, 16497,
			15760, 5, 20479, 16489, 95, -20482, 542, -29730, 607, -1030, -12467, 15647,
			12312, 240, 7, -16138, 16368, 4098 }
	};
	static const short k451[2][34] =
	{
		{ -306, 291, -765, 7, -194, 115, -3, -433, 12, 16264, -10253, 1039, -24569, 20410,
			15823, -15355, 16, 12302, 20284, 895, 25587, 238, -18419, 15999, -10514, 16465,
			-16348, 15775, -18190, 16447, -16287, 15503, -1030, 35 },
		{ -302, 308, -765, 9, -116, 196, 2, -433, -7, 16240, -1038, 1039, -32760, 20410,
			15887, -24562, 16352, 12302, 20295, 895, 18418, 255, 6156, 16015, -7436, 16461,
			-16348, 15759, -18183, 16449, -16287, 15471, -3073, 35 }
	};
	const int nodes = 15;

	/* 1. The walk over a hand-built tree: node 1 hangs off node 0, node 2
	 * off node 1 (node 1 pushes), node 3 off node 0 again (it pops). Node 1
	 * and node 2 each turn 60.03 degrees about y (one-axis, 683 steps of
	 * 4096): node 2's own turn is 60, its drawn turn 120, and node 3, which
	 * does not turn and hangs off the root, does not turn in the drawn frame
	 * either. Turned against its parent instead, node 2 does not turn at all
	 * in the drawn frame. */
	{
		const long tree[3 * 4] = { 2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
		const short y0 = (short)(unsigned short)(2 << 14);
		const short y60 = (short)(unsigned short)((2 << 14) | 683);
		const short yBack = (short)(unsigned short)((2 << 14) | (4096 - 683));
		const short f0[9 + 4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, y0, y0, y0, y0 };
		const short f1[9 + 4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, y0, y60, y60, y0 };
		const short f2[9 + 4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, y0, y60, yBack, y0 };
		float a[4 * 9], b[4 * 9], c[4 * 9];

		InterpDrawnRots(f0, 4, tree, a);
		InterpDrawnRots(f1, 4, tree, b);
		InterpDrawnRots(f2, 4, tree, c);

		const short own0[3] = { 0, 0, 0 };
		const short own1[3] = { 0, 683 * 16, 0 };
		int node = -1;
		float turnCos = 1.0F;

		okf(fabs(rot_angle(own0, own1) - 60.03) < 0.01, "a child turned 60 degrees on its own",
			rot_angle(own0, own1), 60.03);
		okf(fabs(drawn_angle(a + 2 * 9, b + 2 * 9) - 120.06) < 0.01,
			"under a parent turned 60 the same way turns 120 in the drawn frame",
			drawn_angle(a + 2 * 9, b + 2 * 9), 120.06);
		okf(drawn_angle(a + 3 * 9, b + 3 * 9) < 0.01, "a node popped back to the root does not turn",
			drawn_angle(a + 3 * 9, b + 3 * 9), 0.0);
		ok(InterpKeyFarDrawn(f0, f1, 4, tree, &node, &turnCos) && node == 2 &&
			!InterpPoseFar(f0, f1, 4, INTERP_POSE_TURN_COS),
			"so the pair is far in the drawn frame at node 2, and near by every node's own turn", node, 2);
		okf(drawn_angle(a + 2 * 9, c + 2 * 9) < 0.01 && !InterpKeyFarDrawn(f0, f2, 4, tree, 0, 0),
			"turned back against its parent, the child does not turn in the drawn frame",
			drawn_angle(a + 2 * 9, c + 2 * 9), 0.0);

		/* The order of the product: a parent turned 90 about y and a child
		 * 90 about x compose as one node of (90, 90, 0), phd_RotYXZ's y then
		 * x, which is what the draw's stack builds. */
		const long chain[1 * 4] = { 0, 0, 0, 0 };
		const short g[9 + 2] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
			(short)(unsigned short)((2 << 14) | 1024), (short)(unsigned short)((1 << 14) | 1024) };
		const short both[3] = { 16384, 16384, 0 };
		float d[2 * 9], m[9];
		double worst = 0.0;

		InterpDrawnRots(g, 2, chain, d);
		InterpPoseRot(both, m);

		for (int i = 0; i < 9; i++)
			worst = fmax(worst, fabs((double)d[9 + i] - m[i]));

		okf(worst < 1e-5, "a parent's y and a child's x compose in the draw's order", worst, 0.0);
	}

	/* 2. The standing jump forward's take-off, 76 key 0 to 1: the hips turn
	 * 13.0 degrees, the upper arm (node 11) 143.7 on its own and the forearm
	 * (node 12) 170.1 in the drawn frame. The pair is far, and the table
	 * marks it. Built at 0.5 every node is a rotation - in the drawn frame too
	 * - with the upper arm about 72 degrees from each key and the forearm
	 * about 85, where vanilla's middle frame leaves the forearm's drawn frame
	 * a determinant of cos(85.05)^2 = 0.007. */
	{
		int node = -1;
		float turnCos = 1.0F;
		const bool far0 = InterpKeyFarDrawn(k76[0], k76[1], nodes, bones, &node, &turnCos);
		const double turn = acos((double)turnCos) * 180.0 / 3.14159265358979323846;

		ok(far0 && node == 12, "76 key 0 to 1 is far, the forearm (node 12) furthest", node, 12);
		okf(fabs(turn - 170.1) < 0.5, "turning 170.1 degrees in the drawn frame", turn, 170.1);
		ok(!InterpKeyFar(k76[0], k76[1]), "where the hips alone do not decide it (13.0 degrees)", 0, 0);

		short block[2 * 36];
		unsigned long bits[INTERP_KEY_FAR_WORDS];

		for (int i = 0; i < 36; i++)
		{
			block[i] = k76[0][i];
			block[36 + i] = k76[1][i];
		}

		InterpKeyFarFill(block, 36, 1, nodes, bones, bits);
		ok(InterpKeyFarBit(bits, 0) && !InterpKeyFarBit(bits, 1),
			"the table marks its one pair and nothing past it", (long)bits[0], 1);

		short out[INTERP_KEYFRAME_MAX];
		float a[15 * 9], b[15 * 9], m[15 * 9];

		InterpKeyFrameLerp(k76[0], k76[1], nodes, 0.5F, out);
		InterpDrawnRots(k76[0], nodes, bones, a);
		InterpDrawnRots(k76[1], nodes, bones, b);
		InterpDrawnRots(out, nodes, bones, m);

		double colErr = 0.0;

		for (int n = 0; n < nodes; n++)
		{
			for (int c = 0; c < 3; c++)
			{
				const float* r = m + n * 9;
				const double l = sqrt((double)r[c] * r[c] + (double)r[3 + c] * r[3 + c] +
					(double)r[6 + c] * r[6 + c]);

				colErr = fmax(colErr, fabs(l - 1.0));
			}
		}

		okf(colErr < 1e-5, "built at 0.5, every node's drawn frame has columns of length 1",
			colErr, 0.0);

		const short* p0 = k76[0] + 9;
		const short* p1 = k76[1] + 9;
		const short* pm = out + 9;
		short r0[3], r1[3], rm[3];

		for (int n = 0; n <= 11; n++)
		{
			InterpUnpackRot(p0, r0);
			InterpUnpackRot(p1, r1);
			InterpUnpackRot(pm, rm);
		}

		okf(fabs(rot_angle(r0, rm) - 72.0) < 1.0 && fabs(rot_angle(rm, r1) - 72.0) < 1.0,
			"the upper arm about 72 degrees from each key", rot_angle(r0, rm), 72.0);
		okf(fabs(drawn_angle(a + 12 * 9, m + 12 * 9) - 85.0) < 1.0 &&
			fabs(drawn_angle(m + 12 * 9, b + 12 * 9) - 85.0) < 1.0,
			"the forearm about 85 degrees from each in the drawn frame",
			drawn_angle(a + 12 * 9, m + 12 * 9), 85.0);

		double v[9];

		for (int i = 0; i < 9; i++)
			v[i] = InterpBlendFloat(a[12 * 9 + i], b[12 * 9 + i], 0.5F);

		okf(fabs(det3(v) - 0.0074) < 0.001, "where vanilla's middle frame leaves it a determinant of 0.007",
			det3(v), 0.0074);
	}

	/* 3. The criterion is the drawn turn and not the own one. 10 key 0 to 1:
	 * no node turns more than 56.7 degrees on its own, node 13 94.2 in the
	 * drawn frame - far. 53 key 0 to 1: node 9 turns 147.6 on its own, its
	 * parent's turn takes part of it back, and no node turns more than 83.6
	 * in the drawn frame - near. The run's first pair, 1 key 0 to 1 (12.9 on
	 * its own, 14.5 drawn): near. */
	{
		int node = -1;
		float turnCos = 1.0F;
		const bool far10 = InterpKeyFarDrawn(k10[0], k10[1], nodes, bones, &node, &turnCos);
		double turn = acos((double)turnCos) * 180.0 / 3.14159265358979323846;

		ok(far10 && node == 13, "10 key 0 to 1 is far, node 13 furthest", node, 13);
		okf(fabs(turn - 94.2) < 0.5 && !InterpPoseFar(k10[0], k10[1], nodes, INTERP_POSE_TURN_COS),
			"94.2 degrees drawn, where no node's own turn passes 90", turn, 94.2);

		const bool far53 = InterpKeyFarDrawn(k53[0], k53[1], nodes, bones, &node, &turnCos);

		turn = acos((double)turnCos) * 180.0 / 3.14159265358979323846;
		okf(!far53 && fabs(turn - 83.6) < 0.5 && InterpPoseFar(k53[0], k53[1], nodes, INTERP_POSE_TURN_COS),
			"53 key 0 to 1 is near, 83.6 drawn, though node 9 turns 147.6 on its own", turn, 83.6);

		const bool far1 = InterpKeyFarDrawn(k1[0], k1[1], nodes, bones, &node, &turnCos);

		turn = acos((double)turnCos) * 180.0 / 3.14159265358979323846;
		okf(!far1 && turn < 45.0 && fabs(turn - 14.5) < 0.5, "the run's 1 key 0 to 1 is near, 14.5 drawn",
			turn, 14.5);
	}

	/* 4. The hold stays where it is. 451 key 25 to 26 (rate 2 from frame
	 * 8864, turn180_effect on 8916): the half turn is inside the pair, which
	 * is far and held. 76 carries no turn180_effect: far, and built. rich3's
	 * 210 key 1 to 2, far only at the head, with the flip on its second key
	 * (3992, rate 4 from 3984): the hips make it an early turn, not held. */
	{
		const long size = 34;
		static short keys[27 * 34];

		for (int i = 0; i < 27 * 34; i++)
			keys[i] = 0;

		for (int i = 0; i < 34; i++)
		{
			keys[25 * size + i] = k451[0][i];
			keys[26 * size + i] = k451[1][i];
		}

		long pair = -1;
		const int kind = InterpTurnKind(keys, size, 8864, 2, 8916, &pair);

		ok(InterpKeyFarDrawn(k451[0], k451[1], nodes, bones, 0, 0) && kind == INTERP_TURN_INSIDE &&
			pair == 25 && InterpKeyHold(kind, pair, 8916, 8864, 2, 25),
			"451 key 25 to 26: far, the half turn inside it, held", pair, 25);
		ok(InterpKeyFarDrawn(k76[0], k76[1], nodes, bones, 0, 0) &&
			!InterpKeyHold(INTERP_TURN_NONE, -1, -1, 1233, 2, 0),
			"76 key 0 to 1: far and not held", 0, 0);
		ok(!InterpKeyHold(INTERP_TURN_EARLY, 1, 3992, 3984, 4, 1),
			"an early turn's far pair with the flip on its second key is not held", 1, 0);
	}

	/* 5. The table's bound. It holds INTERP_KEY_FAR_PAIRS pairs, more than
	 * the 199 of Lara's longest playable animation. One-node frames of one
	 * axis in y, the last key turned 170 degrees: an animation of exactly
	 * that many pairs has its last one marked; one pair longer, the far pair
	 * lies past the table and is left to vanilla, and the answer past the
	 * table, or before it, is 'not far'. */
	{
		const long size = 10;
		const long most = INTERP_KEY_FAR_PAIRS;
		static short frames[(INTERP_KEY_FAR_PAIRS + 2) * 10];
		const long one[4] = { 0, 0, 0, 0 };
		unsigned long bits[INTERP_KEY_FAR_WORDS];

		ok(INTERP_KEY_FAR_PAIRS >= 199, "the table holds the longest animation's 199 pairs",
			INTERP_KEY_FAR_PAIRS, 199);

		for (long k = 0; k < most + 2; k++)
		{
			for (int i = 0; i < 9; i++)
				frames[k * size + i] = 0;

			frames[k * size + 9] = (short)(unsigned short)(2 << 14);
		}

		frames[most * size + 9] = (short)(unsigned short)((2 << 14) | 1934);
		InterpKeyFarFill(frames, size, most, 1, one, bits);

		long marked = 0;

		for (long k = 0; k < most; k++)
			marked += InterpKeyFarBit(bits, k) ? 1 : 0;

		ok(InterpKeyFarBit(bits, most - 1) && marked == 1,
			"the last pair of the longest animation the table holds is marked, alone", marked, 1);
		ok(!InterpKeyFarBit(bits, most) && !InterpKeyFarBit(bits, -1),
			"a pair past the table or before it is not far", 0, 0);

		frames[most * size + 9] = (short)(unsigned short)(2 << 14);
		frames[(most + 1) * size + 9] = (short)(unsigned short)((2 << 14) | 1934);
		InterpKeyFarFill(frames, size, most + 1, 1, one, bits);

		marked = 0;

		for (long k = 0; k <= most; k++)
			marked += InterpKeyFarBit(bits, k) ? 1 : 0;

		ok(marked == 0, "one pair longer, the far pair past the table is left unmarked", marked, 0);
	}

	/* 6. The way round a node takes where its own turn lies within a degree
	 * of half a turn (InterpKeyFrameFar). The standing jump up's take-off, 91
	 * key 0 to 1: the upper arm (node 11) turns 179.85 degrees on its own and
	 * 133.8 in the drawn frame. The shorter way of its own turn puts the arm
	 * about 180 degrees off the middle of that drawn turn on the in-between
	 * frame; the way the drawn frame asks for, about 21. The same way at a
	 * quarter and three quarters. A far pair with no such node (76) is
	 * InterpKeyFrameLerp's frame word for word. */
	{
		static const short k91[2][36] =
		{
			{ -176, 138, -452, 4, -154, 202, -8, -217, -68, 15375, -1021, 3001, -29061, 14040,
				511, 1808, 29731, 2999, 11676, 14056, 511, 1839, -26666, 14464, 3066, 14480,
				-14402, 303, -6, -12475, 13967, 31816, 528, 1031, -16126, 16924 },
			{ -113, 95, -873, 62, -156, 50, -8, -437, -12, 16224, 1027, 159, -29707, 20371,
				14511, -9196, 16272, 26625, 20310, 15200, 6131, 367, -1029, 634, -467, 527,
				-1030, -12475, 627, -19009, 624, 1032, -16126, 16640, 35, 0 }
		};
		float a[15 * 9], b[15 * 9], m[15 * 9], g[9];
		short built[INTERP_KEYFRAME_MAX], plain[INTERP_KEYFRAME_MAX];
		int node = -1;
		float turnCos = 1.0F;

		InterpKeyFarDrawn(k91[0], k91[1], nodes, bones, &node, &turnCos);
		okf(node == 11 && fabs(acos((double)turnCos) * 180.0 / 3.14159265358979323846 - 133.8) < 0.5,
			"91 key 0 to 1: the upper arm turns 133.8 degrees in the drawn frame",
			acos((double)turnCos) * 180.0 / 3.14159265358979323846, 133.8);

		InterpDrawnRots(k91[0], nodes, bones, a);
		InterpDrawnRots(k91[1], nodes, bones, b);
		InterpSlerpMat(a + 11 * 9, b + 11 * 9, 0.5F, g);

		InterpKeyFrameLerp(k91[0], k91[1], nodes, 0.5F, plain);
		InterpDrawnRots(plain, nodes, bones, m);

		const double offPlain = drawn_angle(m + 11 * 9, g);

		InterpKeyFrameFar(k91[0], k91[1], nodes, bones, 0.5F, built);
		InterpDrawnRots(built, nodes, bones, m);

		const double offFar = drawn_angle(m + 11 * 9, g);

		okf(offPlain > 170.0, "the shorter way of its own turn lies about 180 off the drawn turn's middle",
			offPlain, 179.9);
		okf(offFar < 25.0, "the way the drawn frame asks for, about 21", offFar, 20.7);

		bool steady = true;
		const float ts[2] = { 0.25F, 0.75F };

		for (int i = 0; i < 2; i++)
		{
			InterpSlerpMat(a + 11 * 9, b + 11 * 9, ts[i], g);
			InterpKeyFrameFar(k91[0], k91[1], nodes, bones, ts[i], built);
			InterpDrawnRots(built, nodes, bones, m);
			steady = steady && drawn_angle(m + 11 * 9, g) < 25.0;
		}

		ok(steady, "and that way at a quarter and three quarters too", steady ? 1 : 0, 1);

		const int n0 = InterpKeyFrameLerp(k76[0], k76[1], nodes, 0.5F, plain);
		const int n1 = InterpKeyFrameFar(k76[0], k76[1], nodes, bones, 0.5F, built);
		bool same = n0 == n1;

		for (int i = 0; i < n0 && same; i++)
			same = plain[i] == built[i];

		ok(same, "76, with no node near half a turn on its own, is InterpKeyFrameLerp's frame", n1, n0);
	}
}

int main()
{
	printf("InterpTest - the arithmetic of the 60 FPS interpolation\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "interpolate_math.h\n\n");

	angles();
	fade();
	camera();
	positions();
	thresholds();
	camera_aim();
	pose_wrap();
	camera_shell();
	cadence();
	title_cadence();
	refresh();
	counters();
	pose_factor();
	pose_time();
	pose_back();
	root_flip();
	root_narrow();
	key_build();
	key_bridge();
	key_far_all();
	particles();
	portal_walk();
	reach_rect();
	walk_capacity();
	ring_cadence();
	title_shadow();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

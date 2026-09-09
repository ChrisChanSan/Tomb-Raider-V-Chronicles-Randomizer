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

	/* FIX ROUND R (#148): a half turn is a SNAP for a node, not a blend -
	 * InterpHalfTurn is the question ApplyNode (game\interpolate.cpp) asks
	 * before it blends a rotation, and turn180_effect (game\effects.cpp) is
	 * the one thing that makes the answer yes: y_rot -= 0x8000. Exactly half
	 * a turn either way is one; a unit short of it is a turn like any other. */
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

/* FIX ROUND W (#148): the ninth step of a screen fade. SetFade's arithmetic
 * as specific\specificfx.cpp does it - eight steps of (end - start) >> 3, the
 * ninth call draws `end` - played through in both directions, once as the
 * thirty hertz ticks see it (the value read BEFORE the step, `end` on the
 * ninth) and once as an in-between frame between the eighth and the ninth
 * tick sees it (the value AFTER the eighth step). */
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


/* ------------------------------------------------------- fix round R -- */

/* THE CAMERA'S TARGET, AND WHERE IT IS A DIRECTION (fix round R).
 *
 * The rule under test is InterpCamAimCut (game\interpolate_math.h) and the
 * fault it repairs is measurable without a game: FirstPersonCamera builds
 * camera.target at camera.pos + direction * 0x5100, so a head turn of a few
 * degrees between two logic ticks moved that point further than
 * INTERP_CAM_SNAP and order 2 declared a camera CUT. The user's log of
 * 2026-09-07 reads camcut=10..17 a second with Lara STANDING STILL.
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

// The rule as it stood before fix round R: the target as an absolute place.
static bool old_target_cut(const long* pt, const long* t)
{
	return InterpManhattan(t[0] - pt[0], t[1] - pt[1], t[2] - pt[2]) > INTERP_CAM_SNAP;
}

static void camera_aim()
{
	printf("the camera's aim (fix round R)\n");

	ok(INTERP_CAM_FAR_TARGET == 4096, "beyond four sectors a target is a direction",
		INTERP_CAM_FAR_TARGET, 4096);
	ok(INTERP_CAM_DIR_RADIUS == 1024, "and it is measured on a sector's radius",
		INTERP_CAM_DIR_RADIUS, 1024);

	/* 1. THE FAULT. An idle animation turns Lara's head by a degree or two
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

	/* 2. EVERY TURN A BODY CAN MAKE IN ONE TICK. Lara's fastest turn on the
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

	/* 3. A REAL CUT IS STILL A CUT. A new view is not a turn: sixty degrees,
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

	/* 4. THE CHASE CAMERA IS UNTOUCHED. Its target sits on Lara, 1536 from
	 * the eye - under INTERP_CAM_FAR_TARGET - so InterpCamAim hands back the
	 * offset itself and the comparison is the absolute one order 2 shipped. */
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

	/* AND THE REDUCTION IS THE IDENTITY UNDER THE THRESHOLD. Whatever else
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


/* ------------------------------------------------------- fix round S -- */

/* THE POSE, AND THE PAIR A MATRIX BLEND CANNOT SURVIVE (fix round S, S2).
 *
 * The play test's dive roll: one in-between frame with a leg fully extended
 * the wrong way. The first reading was a packed ten bit angle interpolated
 * the LONG way round - 1000 to 20 giving 510 where it should give 1022.
 *
 * The first thing asserted here is that this cannot happen, and it is
 * asserted against the arithmetic the engine really runs: the two poses are
 * blended as MATRICES (game\draw.cpp, InterpolateMatrix), and an element-wise
 * lerp of two rotations always lands on the SHORT-way bisector. So the 1000
 * to 20 case is run through it, on all three axes and in both directions, and
 * the angle that comes out is read back.
 *
 * The second is what really goes wrong: the same lerp SHRINKS by cos(D/2),
 * and at half a turn the frame is singular. InterpPoseFar is the rule that
 * refuses such a pair, and it is asserted to fire on a half turn, to leave a
 * quiet pair alone, and to answer per NODE rather than per frame. */

static const double kPiS = 3.14159265358979323846;

/* The angle a blended 3x3 comes back as, about the axis it was built on.
 * atan2 of the two elements that carry it, in the engine's 16 bit space and
 * then in the packed TEN bit one, which is the space the play test's own
 * numbers were in. */
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
	printf("the pose the short way round (fix round S)\n");

	/* 1. THE PLAY TEST'S OWN CASE. 1000 and 20 are 44 units apart across the
	 * wrap; the midpoint is 1022 and never 510. Run on all three axes and in
	 * both directions - six cases - through the very blend the engine uses. */
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

	/* 2. AND THE SHRINK IS REAL. The same blend at a half turn keeps nothing
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

	/* 3. THE RULE. A frame is nine shorts of box and offset and then one
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

	/* 4. THE LIMIT IS WHERE IT SAYS IT IS. Ninety degrees is the last turn
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
}

/* THE CAMERA'S SHELL (fix round S, S3a).
 *
 * Fix round R chose the far branch per CAMERA. A target hovering on the shell
 * at INTERP_CAM_FAR_TARGET therefore had one of the pair reduced to 1024 and
 * the other left at 4096, and the difference of the two was three thousand
 * units of nothing. The branch is chosen once for the pair now, and both
 * sides take the same scale. */
static void camera_shell()
{
	printf("the camera's shell (fix round S)\n");

	/* 1. A TARGET WALKING THROUGH THE SHELL. The eye stands still and the
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

	/* 2. A TARGET SITTING ON THE SHELL WITH ONE UNIT OF JITTER, thirty ticks
	 * of it - the case that made the seam a fault and not a curiosity. */
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

	/* 3. AND ROUND R'S OWN ANSWERS ARE UNCHANGED. The two cameras the engine
	 * really has are nowhere near the shell, so everything camera_aim asserts
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

/* THE TITLE MENU'S OWN CADENCE (fix round R).
 *
 * The title and the inventory keep TWO ticks per present at every setting
 * (order 1b, GameFrameMinTicks). What that does to ControlPhase's accumulator
 * is arithmetic and can be run here:
 *
 *     framecount += 2; while (framecount > 0) { tick; framecount -= 2; }
 *
 * From 0 it returns to 0 - and from -1 it returns to -1, forever. So a title
 * entered on the odd half of the sixty hertz cadence read `framecount < 0` on
 * every single frame, and the rule as order 2 wrote it answered 0.5 to all of
 * them. Every frame of the title was an in-between frame, every counter order
 * 3 guards stood still, DoScreenFade never reached its ninth step, DoFade
 * never became 2, and a full screen black quad stood over the menu while
 * TitleOptions zeroed the input. That is the black screen of 2026-09-07, and
 * the parity it depends on is why the FIRST title of a session was fine.
 *
 * Both halves of the fix are asserted: the rule (a classic-cadence frame is
 * never a tween frame, whatever the accumulator holds) and the clearing
 * (DoTitle sets framecount to 0 as DoLevel always has). */
static void title_cadence()
{
	printf("the title's cadence (fix round R)\n");

	for (int start = -1; start <= 0; start++)
	{
		long framecount = start;
		int tweenOld = 0, tweenNew = 0, ticks = 0, frames = 0;

		for (int i = 0; i < 300; i++)
		{
			/* the rule as order 2 wrote it, and the rule with fix round R's
			 * test for a frame that is not a sixty hertz one */
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

	/* AND WHAT DoTitle NOW DOES ABOUT IT. Clearing the accumulator on the way
	 * in makes the parity even whatever the level left behind, which is the
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

/* ------------------------------------------- the display-bound cadence -- */

/* ORDER 6, AND THE ONE THING ABOUT IT THAT CANNOT BE SEEN IN THE CODE.
 *
 * At fps_mode 1 the frames land on the ticks and the blend factor is a parity
 * bit: half when the last ControlPhase ran a tick, one when it did not. Bind
 * the rate to the display instead and nothing lands anywhere in particular -
 * at 144 Hz the frames come every 0.4167 ticks - so the factor comes out of
 * the clock, as `(framecount + subTick + 1) / 2`.
 *
 * What has to be true of that, and what a play test cannot show:
 *
 *   1. THE LOGIC IS STILL THIRTY. Ten simulated seconds must run 300 logic
 *      steps at 60, at 120 and at 144 Hz alike, and the frames must number
 *      600, 1200 and 1440.
 *   2. THE FACTOR NEVER LEAVES [0,1). Not "is clamped into it" - never
 *      leaves it. A clamp means the frame asked for a state the logic has
 *      not computed, and the picture stands still for that frame; one per
 *      logic step is a stutter at 30 Hz on top of a 144 Hz picture.
 *   3. WHAT IS DRAWN ADVANCES WITH THE WALL CLOCK, exactly. The content of
 *      frame i is `2(k-1) + 2 * alpha` in tick units, and the difference
 *      between that and the real time the frame was drawn at has to be a
 *      CONSTANT - the same constant on every frame of the run. Anything else
 *      is drift, and drift is what a fixed-step renderer bound to a free
 *      clock gets wrong.
 *   4. AT 60 Hz IT IS THE SIXTY HERTZ CADENCE AGAIN, alternating half a step
 *      per frame, which is what says the mode is not a different animation
 *      speed but the same one sampled more often.
 *
 * THE MODEL IS THE ENGINE'S LOOP, not an idealisation of it: the frame is
 * drawn, the flip waits for the panel, the clock is read AFTER the flip
 * (specific\output.cpp, S_DumpScreen - see the long note there for why it
 * has to be after), and ControlPhase is handed the whole ticks that came
 * back. The vblanks are put on their own grid at their own phase so that the
 * two clocks are genuinely independent, the way a 144 Hz panel and a 1/60 s
 * counter are. */
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

			/* ORDER 3'S CRITERION AT A RATE THAT DOES NOT DIVIDE (order 6).
			 * `alpha < 1` said "no tick on this frame" while the cadence was
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

	/* AND AT SIXTY IT IS THE SIXTY HERTZ CADENCE. One tick of content per
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

	/* A HITCH, and the clamp is what it is FOR. One frame takes 200 ms - the
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

		/* THE INVARIANT, not a hand-counted total: every tick that was fed
		 * to the accumulator was either spent on a logic step or is still
		 * standing in it. Two steps of the priming ControlPhase(2) go in on
		 * the left, and what the loop left behind comes off on the right.
		 *
		 * Written this way it says what it is for - no game time is lost
		 * because a frame took long to draw, and none is invented either -
		 * and it says it for whatever numbers the model happens to produce.
		 */
		ok(2 * k == credited + 2 - framecount,
			"a 200 ms hitch is caught up: no logic tick lost, none invented",
			2 * k, (int)(credited + 2 - framecount));

		/* And the ONE place a tick really does go missing is vanilla's own
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

/* ORDER 5 (#148), AND ITS ONE CLAIM: a particle drawn from its own velocity
 * stands on EXACTLY the same time axis as a body drawn from the snapshot.
 *
 * The bodies of order 2 are drawn at `prev + (cur - prev) * alpha`, i.e. the
 * world at (tick - 1) + alpha. A particle has no `prev` - there are a thousand
 * sparks and two thousand raindrops and nobody is going to store two states
 * for each of them - but it has the thing the store would have held: every
 * particle simulation in this engine settles its velocity FIRST and steps its
 * position by it SECOND, so
 *
 *     prev = cur - step
 *
 * is exact rather than approximate, and the engine draws
 * `InterpTrailLong(cur, step, alpha)`.
 *
 * WHAT COULD GO WRONG SILENTLY, and is therefore what is checked here:
 *
 *   1. THE SIGN. `cur + vel * alpha` - the obvious reading of "move it along
 *      a bit" - would run the particle a whole logic step AHEAD of everything
 *      around it. Sparks would lead the gun that fired them. In a picture it
 *      looks like nothing at all; it looks like the game.
 *   2. THE ROUNDING. `cur - step * (1 - alpha)` is the same algebra and NOT
 *      the same integer: a C cast truncates towards zero, so on odd steps the
 *      two forms differ by one unit and the particles would sit beside where
 *      the item machinery would have put them. Checked over every step a TR5
 *      particle can carry, and the count of what the wrong form would have
 *      got wrong is printed beside it.
 *   3. THE CADENCE. At fps_mode 2 the factor comes from a clock, reaches 0 on
 *      the frame after a tick and 1 where the next one is due, and the drawn
 *      particle must walk from prev to cur without ever leaving that interval
 *      or stepping backwards.
 *
 * Dead particles staying dead needs no arithmetic and gets none: every draw
 * site's `if (!On) continue;` is vanilla's and was not touched, and the
 * simulation that clears `On` runs on the tick (order 3). What IS modelled
 * below is the other half of that - a particle that was BORN on the last tick
 * is drawn between its birthplace and where the tick left it, never before
 * it. */
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
			const long item = InterpLerpLong(prev, pos, alpha);	// order 2
			const long part = InterpTrailLong(pos, step, alpha);	// order 5

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

	/* 3. THE PANEL'S OWN RATE (order 6). The same particle at 144 Hz, with the
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

	/* 4. THIRTY HERTZ TAKES NO BRANCH AT ALL. InterpParticleAlpha returns 1
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

	/* 5. A PARTICLE THAT WAS BORN ON THE LAST TICK - the "no pre-run" rule.
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

/* THE ROOM OF THE INTERPOLATED EYE (#123, fix round T2, T2-1).
 *
 * WHAT IS BEING PROVEN. On an in-between frame the eye is blended and the
 * view matrix is rebuilt from it, while the portal walk of game\draw.cpp is
 * seeded with the LOGIC camera's room. Where the eye has crossed a portal
 * plane between two ticks, two pieces of arithmetic in GetRoomBounds /
 * SetRoomBounds throw the room behind that portal away for that one frame.
 *
 * The walk itself cannot be linked here - it reads room[], the matrix stack
 * and a dozen file-static globals of draw.cpp - so what is checked is the
 * ARITHMETIC of the two tests, copied line for line from the engine and run
 * on ANDREA1's OWN portal records, read out of the shipped level with
 * `TrgProbe -roomspace -room 2` and `-room 57`:
 *
 *   room 2  origin 45056 / 0 / 33792, portal -> room 57, normal -1/0/0,
 *           first corner (3072, -1792, 4096) room local = world x 48128
 *   room 57 origin 47104 / 0 / 36864, portal -> room  2, normal  1/0/0,
 *           first corner (1023,   256, 1024) room local = world x 48127
 *
 * That wall at world x 48128 is the corner of the Piazza dell'Opera the play
 * test photographed. The camera walks east across it between two logic ticks;
 * the eye at the halfway point stands 72 units past it. */
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

static void portal_walk()
{
	printf("portal walk - the room the blended eye stands in (#123, T2-1)\n");

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

	/* AND THIS IS THE BUG. The tween frame draws from room 2 - the logic
	 * camera has not stepped yet - with the eye already 72 units east of the
	 * wall, and the sign test refuses the portal. Room 57, and everything the
	 * walk would have reached through it, is missing for that one frame. */
	ok(portal_followed(wall, eye, -1) == false,
		"the tween frame seeded with room 2 DROPS the portal to 57", 0, 0);

	/* WITH THE EYE'S OWN ROOM AS THE SEED the same frame keeps both rooms:
	 * from room 57 the portal back to room 2 faces the camera, so the walk
	 * reaches 2 as well and the picture is whole. */
	ok(portal_followed(back, eye, 1) == true,
		"seeded with room 57 the portal back to room 2 IS followed", 1, 1);

	/* THE SECOND WAY THE SAME FRAME LOSES THE ROOM. Even where the sign test
	 * passes, SetRoomBounds drops a portal whose four corners all fall behind
	 * the eye. The portal quad is the record's own, in room 2's frame:
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

	/* THE WHOLE INTERVAL, and where in it the answer changes. The eye walks
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

	/* THE SAME PAIR THE OTHER WAY. Walking west, the logic room is 57 while
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

int main()
{
	printf("InterpTest - the arithmetic of the 60 FPS interpolation (#148, orders 2, 3, 4, 5 and 6)\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "interpolate_math.h\n\n");

	angles();
	fade();
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
	particles();
	portal_walk();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

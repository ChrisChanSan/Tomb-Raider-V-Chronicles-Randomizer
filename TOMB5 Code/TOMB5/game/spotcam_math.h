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

/* Where a camera flyby makes its heavy-trigger test, and nothing else.
 *
 * This header includes no engine header on purpose:
 * `Preparation\Tools\Diagnose\ModernTest.cpp` and TrgProbe include this file
 * itself, not a copy. game\spotcam.cpp holds the flyby, the test and its log.
 *
 * The cause these rules answer - the skip ends a SNAPBACK sequence while
 * the frame still stands at the camera before the last - and the census
 * rule are re-derived from TROne (GPLv3) by Todesengel1931 -
 * https://github.com/todesengel1931; no code of it is used here. */

/* The spotcam flags these rules read, the values of game\spotcam.h
 * (spotcam.cpp asserts that they agree). */
enum
{
	SPOT_LOOPCAMERA = 1 << 2,
	SPOT_TRACKCAMERA = 1 << 3,
	SPOT_SNAPBACK = 1 << 6,
	SPOT_TESTTRIGGER = 1 << 14
};

/* The place of the test the end of a sequence makes, as a SpotCam index, or
 * -1 for the place the frame's camera stands at.
 *
 * The frame's camera stands where the spline was when the frame began. A
 * skip jumps the spline to the end of its segment after that, so on a frame
 * whose segment end the skip forced the camera is still at the camera
 * before the one just reached. In the middle of a sequence that costs
 * nothing: the next frame starts exactly at the reached camera and tests
 * there. At the end of a SNAPBACK sequence the test runs in the same frame
 * and the flyby ends, so it takes the last camera's own place and room -
 * where an unskipped run stands, within one speed step, at its end.
 *
 * Without SNAPBACK the sequence ends on the spline to the chase camera,
 * whose first frame already stands at the last camera; that end and every
 * frame the skip did not force keep the camera's place. The title never
 * skips (spotcam.cpp), so `title` keeps it too. */
inline int SpotEndTestCamera(int first_flags, int skipped, int title,
	int last_camera)
{
	if (!skipped || title || !(first_flags & SPOT_SNAPBACK) ||
		last_camera < 0)
		return -1;

	return last_camera;
}

/* The sequences whose skip, without the rule above, tests the camera before
 * the last instead of the last: SNAPBACK on the first camera without
 * LOOPCAMERA or TRACKCAMERA (which never reach the end branch), and a heavy
 * test on the last camera. TrgProbe -flyby counts them per level. */
inline bool SpotSeqEndsShort(int first_flags, int last_flags)
{
	return !(first_flags & (SPOT_LOOPCAMERA | SPOT_TRACKCAMERA)) &&
		(first_flags & SPOT_SNAPBACK) && (last_flags & SPOT_TESTTRIGGER);
}

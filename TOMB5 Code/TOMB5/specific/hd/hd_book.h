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

/* The arithmetic of a .TRG material 15 book, and nothing else.
 *
 * A material 15 part is a book of consecutive texture pages: its own page is
 * page 0 and page0 + k is page k. param[0] is the phase (a whole number below
 * the count), param[1] the page count, param[2] the seconds each page stands
 * and param[3] zero (hd_render.cpp, MaterialBookTime). The two functions
 * below turn a clock into the page to draw.
 *
 * This header includes nothing on purpose:
 * `Preparation\Tools\Diagnose\InterpTest.cpp` includes this file itself, not
 * a copy, and checks the page cadence without a game, a level or a device.
 * hd_render.cpp draws with it and hd_level.cpp makes the clock. */

/* The time of the state the frame draws, in seconds.
 *
 * `tickClock` is the HD clock: GlobalCounter's thirtieths of a second summed
 * since the level began, so it steps once per logic tick and stands still
 * between two ticks. `alpha` is the blend factor the frame's state was drawn
 * at (game\interpolate.cpp): 1 for the live state, 0.5 for the frame drawn
 * halfway back between two ticks at 60 frames a second, a fraction at a
 * display-bound rate. So the drawn state stands (1 - alpha) of a tick behind
 * the tick clock, and a page stepped on this time moves with the picture
 * around it: at 60 frames a second the time advances by a sixtieth on every
 * present, where the tick clock advances by a thirtieth on every second one.
 *
 * At fps_mode 0 alpha is always 1 and this is the tick clock itself. Held at
 * zero at the start of a level, where a tween frame can stand before the
 * first tick. */
inline double HDBookClock(double tickClock, float alpha)
{
	const double t = tickClock - (1.0 - (double)alpha) / 30.0;

	return t > 0.0 ? t : 0.0;
}

/* The page of a book at a time: the whole number of steps since zero, plus
 * the phase, modulo the count - ((int)(t / step) + phase) % count.
 *
 * In double, and with a thousandth of a step added before the truncation.
 * The frames land exactly on the page boundaries - at 60 frames a second a
 * 0.05 s book changes page on every third present, at a time that is a whole
 * number of sixtieths - and a time summed out of thirtieths comes out a hair
 * under that boundary as often as over it. Without the nudge such a present
 * would show the old page one time and the new one the next, and the even
 * three-present rhythm would turn into two and four. The index is taken
 * modulo the count as an integer.
 *
 * The step is read to the microsecond first. The file stores it as a float,
 * and 0.05 as a float is 0.0500000007: over an hour of play that error adds
 * up to more than the nudge, and the page boundaries would drift off the
 * sixtieths they fall on. The game's steps are whole milliseconds.
 *
 * 0 for a book that is not one - no step, no pages. */
inline int HDBookPage(double clock, float step, int count, int phase)
{
	if (!(step > 0.0f) || count < 1)
		return 0;

	const double seconds = (double)(long long)((double)step * 1.0e6 + 0.5) / 1.0e6;

	if (!(seconds > 0.0))
		return 0;

	const double at = clock / seconds + 1.0e-3;
	const long long index = at > 0.0 ? (long long)at : 0;
	const int p = phase >= 0 ? phase % count : 0;

	return (int)((index % count + p) % count);
}

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

/* Where the game window stands in the windowed state, as arithmetic.
 *
 * The window is an ordinary overlapped window: the player moves it, sizes
 * it, maximizes and snaps it. What the engine itself decides is small and
 * lives here, free of Windows calls, so that ModernTest checks it with
 * made-up monitors (the group window_place):
 *
 * - the client of a chosen size, shrunk so that the whole window fits a
 *   monitor's work area and never below the smallest client;
 * - that client centred on a work area, or kept round the centre of the
 *   window it replaces (the Alt+Plus and Alt+Minus steps);
 * - whether a remembered client rectangle still lies on a work area of a
 *   connected monitor;
 * - the next larger or smaller entry of the mode list for those keys.
 *
 * Rectangles are in screen coordinates, right and bottom exclusive, as
 * Windows has them; a monitor left of or above the primary one has negative
 * coordinates, and every function here takes them as they come.
 *
 * `frame` is the window rectangle AdjustWindowRect makes of an empty client
 * at 0,0 for the window's style: left and top are minus the left border and
 * minus the caption with the top border, right and bottom are the right and
 * bottom borders. devicewindow.h fills it; the window round a client is the
 * client grown by it. */

// The smallest client the window may have; WM_GETMINMAXINFO holds it.
#define WINPLACE_MIN_W 640
#define WINPLACE_MIN_H 480

struct WPRect
{
	long left, top, right, bottom;
};

// One entry of the mode list: the size and the colour depth.
struct WPMode
{
	long w, h, bpp;
};

inline long WPWidth(const WPRect& r)
{
	return r.right - r.left;
}

inline long WPHeight(const WPRect& r)
{
	return r.bottom - r.top;
}

inline WPRect WPMake(long left, long top, long w, long h)
{
	WPRect r;

	r.left = left;
	r.top = top;
	r.right = left + w;
	r.bottom = top + h;
	return r;
}

// The window rectangle round `client`.
inline WPRect WinPlaceWindowOf(const WPRect& client, const WPRect& frame)
{
	WPRect r;

	r.left = client.left + frame.left;
	r.top = client.top + frame.top;
	r.right = client.right + frame.right;
	r.bottom = client.bottom + frame.bottom;
	return r;
}

/* The smallest window: the smallest client grown by the frame, which is
 * what WM_GETMINMAXINFO's ptMinTrackSize takes. */
inline void WinPlaceMinWindow(const WPRect& frame, long* w, long* h)
{
	*w = WINPLACE_MIN_W + WPWidth(frame);
	*h = WINPLACE_MIN_H + WPHeight(frame);
}

/* A client of w x h, made small enough that the window round it fits the
 * work area, then at least the smallest client. A work area too small for
 * even that leaves the smallest client: the window never shrinks below
 * what the minimum tracking size allows anyway. */
inline void WinPlaceFit(long w, long h, const WPRect& frame,
	const WPRect& work, long* outW, long* outH)
{
	const long maxW = WPWidth(work) - WPWidth(frame);
	const long maxH = WPHeight(work) - WPHeight(frame);

	if (w > maxW)
		w = maxW;

	if (h > maxH)
		h = maxH;

	if (w < WINPLACE_MIN_W)
		w = WINPLACE_MIN_W;

	if (h < WINPLACE_MIN_H)
		h = WINPLACE_MIN_H;

	*outW = w;
	*outH = h;
}

/* The window's top left corner moved so that the window lies inside the
 * work area; where it is larger than the area the left or top edge wins,
 * which keeps the caption on the screen. */
inline void WinPlaceInside(long* left, long* top, long ww, long wh,
	const WPRect& work)
{
	if (*left + ww > work.right)
		*left = work.right - ww;

	if (*top + wh > work.bottom)
		*top = work.bottom - wh;

	if (*left < work.left)
		*left = work.left;

	if (*top < work.top)
		*top = work.top;
}

/* The client rectangle of a w x h client whose window stands in the middle
 * of the work area. The size is taken as it comes; WinPlaceFit first when
 * it has to fit. */
inline WPRect WinPlaceCentre(long w, long h, const WPRect& frame,
	const WPRect& work)
{
	const long ww = w + WPWidth(frame);
	const long wh = h + WPHeight(frame);
	long left = work.left + (WPWidth(work) - ww) / 2;
	long top = work.top + (WPHeight(work) - wh) / 2;

	WinPlaceInside(&left, &top, ww, wh, work);
	return WPMake(left - frame.left, top - frame.top, w, h);
}

/* The client rectangle of a w x h client whose window has the centre the
 * window round `old` had, then moved inside the work area: a step of the
 * size stays where the player put the window as far as the monitor
 * allows. */
inline WPRect WinPlaceAround(const WPRect& old, long w, long h,
	const WPRect& frame, const WPRect& work)
{
	const WPRect was = WinPlaceWindowOf(old, frame);
	const long ww = w + WPWidth(frame);
	const long wh = h + WPHeight(frame);
	long left = was.left + WPWidth(was) / 2 - ww / 2;
	long top = was.top + WPHeight(was) / 2 - wh / 2;

	WinPlaceInside(&left, &top, ww, wh, work);
	return WPMake(left - frame.left, top - frame.top, w, h);
}

/* Whether a remembered client rectangle can be used again: at least the
 * smallest client, and its window, less the sizing border on every side,
 * inside one of the work areas. The border is allowed out because a window
 * snapped to an edge stands with its invisible sizing border beyond the
 * work area; anything further out is a monitor that is no longer there, or
 * one that moved, and the window is centred instead. */
inline bool WinPlaceUsable(const WPRect& client, const WPRect& frame,
	const WPRect* works, long count)
{
	const WPRect win = WinPlaceWindowOf(client, frame);
	const long border = frame.right;

	if (WPWidth(client) < WINPLACE_MIN_W || WPHeight(client) < WINPLACE_MIN_H)
		return false;

	for (long i = 0; i < count; i++)
	{
		const WPRect& work = works[i];

		if (win.left + border >= work.left &&
			win.top + border >= work.top &&
			win.right - border <= work.right &&
			win.bottom - border <= work.bottom)
			return true;
	}

	return false;
}

/* The entry of the mode list that Alt+Plus (dir > 0) or Alt+Minus
 * (dir < 0) takes from a client of w x h: of the entries with the colour
 * depth of `modes[current]`, going up the smallest whose area is larger
 * than the client's and that fits maxW x maxH (the largest client the work
 * area holds), going down the largest whose area is smaller and that is at
 * least the smallest client. Equal areas go to the earlier entry. The
 * client's own size decides, not the list's order, so a client the player
 * sized by hand steps to its neighbours too. -1 when there is none. */
inline long WinPlaceStep(const WPMode* modes, long count, long current,
	long w, long h, long maxW, long maxH, int dir)
{
	const long area = w * h;
	long best = -1;
	long bestArea = 0;

	if (current < 0 || current >= count || !dir)
		return -1;

	for (long i = 0; i < count; i++)
	{
		const WPMode& m = modes[i];
		const long a = m.w * m.h;

		if (m.bpp != modes[current].bpp)
			continue;

		if (dir > 0)
		{
			if (a <= area || m.w > maxW || m.h > maxH)
				continue;

			if (best < 0 || a < bestArea)
			{
				best = i;
				bestArea = a;
			}
		}
		else
		{
			if (a >= area || m.w < WINPLACE_MIN_W || m.h < WINPLACE_MIN_H)
				continue;

			if (best < 0 || a > bestArea)
			{
				best = i;
				bestArea = a;
			}
		}
	}

	return best;
}

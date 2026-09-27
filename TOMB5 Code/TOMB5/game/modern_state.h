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

/* The part of the modern control scheme that knows the engine.
 *
 * game/modern_move.cpp is the arithmetic and has no engine dependency at
 * all. This is the other half: it owns the state the scheme remembers,
 * measures the three things the arithmetic cannot work out for itself -
 * the camera's heading, where the orbit is pointing, and whether Lara is
 * getting anywhere - and is the only place in the tree that writes
 * Lara's own angles from the input function (her heading everywhere,
 * and in the water her attitude and bank as well).
 *
 * The tick below is called from one place, the seam in
 * specific/input.cpp between the block that puts the stick on the four
 * direction bits and the block that reads the key roles, and only
 * inside that seam's guard; the reset below it is called from two more
 * places, both under the same option (the Controls page when the scheme
 * is switched, and level start). With tomb5.control_modern off, nothing
 * here runs, so the engine stays what it was at the default (Tank). */

/* One logic tick of the modern control. `bits` is the input word as the
 * stick and the keyboard have left it; the return value is what the
 * seam puts back. `stick_x` is right-positive and `stick_y` is
 * forward-negative, exactly as specific/input.cpp reads joy_x/joy_y for
 * the classic bits. `action` and `jump` are the two key roles the
 * scheme needs to know about before they are read into the word - the
 * caller reads them, because the caller is the file that knows which
 * key and which pad button they are. The stick is the pad's left stick,
 * or with that at rest the four movement keys as a stick
 * (ModernKeyStick, game/modern_move.h); `feed` says which of the two,
 * MODERN_FEED_*, and is only printed, as MODERNLOG's `feed=`.
 *
 * Returns `bits` unchanged whenever the scheme is not responsible: a
 * state that stays classic, the stick at rest, and every call that is
 * not the game's own logic tick (the menus and the FMV loop call
 * S_UpdateInput too - see the tick guard in the .cpp). */
long ModernUpdateInput(long bits, long stick_x, long stick_y, long action, long jump, long feed);

/* Her heading as the function above left it this tick, for
 * game/camera.cpp and nothing else. Returns 0 when the modern input did
 * not run this tick at all, and then writes nothing: the camera has no
 * baseline on such a tick and must not seat a swing. The .cpp says
 * which four ticks those are and why the stamp is an equality. */
long ModernHeadingAfterInput(short* yaw);

/* Forget everything remembered: the held course, the spin, the blocked
 * counter and the position the counter measures against.
 *
 * Three callers, and the split between them is the point. This file calls it
 * whenever the tick chain visibly breaks - first person, a cutscene, the look
 * key, the title - which it can tell from the game's own tick counter.
 * game\gameflow.cpp calls it at a level start, because a level change stops
 * that counter and so cannot be told from an ordinary tick (the guard in the
 * .cpp says so at length); that call takes every savegame restore with it.
 * And specific\LoadSave.cpp calls it when the scheme is switched on the
 * Controls page, because a course seated under Tank is no course at all.
 *
 * It touches nothing but this file's own statics, so it is safe to call
 * before a level exists and with no Lara in the world. */
void ModernResetControls();

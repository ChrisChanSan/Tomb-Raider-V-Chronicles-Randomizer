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

/* THE MODERN CONTROL - THE PART THAT KNOWS THE ENGINE (#161, round 161a).
 *
 * game\modern_move.cpp is the arithmetic and has no engine in it at all. This
 * is the other half: it owns the one state the scheme remembers, it measures
 * the two things the arithmetic cannot work out for itself - the camera's
 * heading and whether Lara is getting anywhere - and it is the only place in
 * the tree that writes Lara's heading from the input function.
 *
 * It is called from ONE place, the seam in specific\input.cpp between the
 * block that puts the stick on the four direction bits and the block that
 * reads the key roles, and only inside that seam's guard. With
 * tomb5.control_modern off nothing here runs at all, which is how the engine
 * stays what it was at the default (Tank). */

/* One logic tick of the modern control. `bits` is the input word as the stick
 * and the keyboard have left it; the return value is what the seam puts back.
 * `stick_x` is right-positive and `stick_y` is FORWARD-NEGATIVE, exactly as
 * specific\input.cpp reads joy_x/joy_y for the classic bits. `action` and
 * `jump` are the two key roles the scheme needs to know about before they are
 * read into the word - the caller reads them, because the caller is the file
 * that knows which key and which pad button they are.
 *
 * Returns `bits` unchanged whenever the scheme is not responsible: a state
 * that stays classic, the stick at rest, and every call that is not the
 * game's own logic tick (the menus and the FMV loop call S_UpdateInput too -
 * see the tick guard in the .cpp). */
long ModernUpdateInput(long bits, long stick_x, long stick_y, long action, long jump);

/* Forget everything remembered: the held course, the spin, the blocked
 * counter and the position the counter measures against. Called from here
 * whenever the tick chain breaks (a level start, a savegame, the inventory,
 * first person, the look key), and meant to be called from the options page
 * as well when the scheme is switched - round 161a step 6/7. */
void ModernResetControls();

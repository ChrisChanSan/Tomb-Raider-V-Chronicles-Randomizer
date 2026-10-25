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

/* A lift door's delay kept while its pad is held.
 *
 * _TestTriggers (game\control.cpp) writes a trigger's timer into every
 * item the trigger names, on every frame the trigger is hit - a plain floor
 * trigger on every frame Lara stands on it. A negative timer (a signed
 * char) is a delay: TriggerActive counts the item's timer up from -n * 30
 * to 0 and keeps the item off meanwhile. So a lift door on such a pad
 * started its delay again on every frame and never opened while Lara stood
 * there, and a door already open shut again when she stepped back on, the
 * timer below -1 reading as off.
 *
 * The rule: the timer is not written when it is negative, the trigger is
 * not a heavy one, its type is no switch, heavy switch or antitrigger of
 * any kind, the item is a lift door and it already holds all five code
 * bits. Every other hit writes the timer as before. The condition is taken
 * from TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931
 * (THIRD-PARTY.md); the function and its checks are this engine's own. No
 * engine header here: ModernTest asserts on it. */

// The trigger types the rule names (global\types.h, trigger_types) and the
// code bits (IFL_CODEBITS); control.cpp checks that they agree.
#define LIFT_DOOR_SWITCH		2
#define LIFT_DOOR_ANTIPAD		6
#define LIFT_DOOR_ANTITRIGGER		9
#define LIFT_DOOR_HEAVYSWITCH		10
#define LIFT_DOOR_HEAVYANTITRIGGER	11
#define LIFT_DOOR_CODEBITS		0x3E00

/* Whether the trigger's hit leaves the item's timer as it is: `timer` the
 * trigger's, `heavy` the hit's, `type` the trigger's type, `liftDoor`
 * whether the item is a lift door, `flags` the item's. */
inline bool LiftDoorKeepsTimer(signed char timer, long heavy, int type,
	bool liftDoor, int flags)
{
	return timer < 0 && !heavy && type != LIFT_DOOR_SWITCH &&
		type != LIFT_DOOR_HEAVYSWITCH && type != LIFT_DOOR_ANTIPAD &&
		type != LIFT_DOOR_ANTITRIGGER && type != LIFT_DOOR_HEAVYANTITRIGGER &&
		liftDoor && (flags & LIFT_DOOR_CODEBITS) == LIFT_DOOR_CODEBITS;
}

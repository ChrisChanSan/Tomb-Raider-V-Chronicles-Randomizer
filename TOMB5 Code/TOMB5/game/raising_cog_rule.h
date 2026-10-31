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

/* The raising cog's lift runs to its end, and a pull meanwhile is kept.
 *
 * ControlRaisingCog (game\andrea.cpp) raises the cog COG_STEP units a tick
 * while TriggerActive holds - while the cog holds all five code bits.
 * COG_LIFT_UNITS make a lift; after COG_LIFTS lifts the cog sits in the gear
 * above it, the pulleys it names change over, and from then on it turns
 * with the gears. Its rope fires a SWITCH trigger, and a SWITCH trigger
 * toggles the code bits of its targets (_TestTriggers, game\control.cpp):
 * the pull that set a lift going turned them on, the cog turns them off
 * itself when the lift ends - and a pull while the lift still ran turned
 * them off before that. The lift stopped half way and went on only with one
 * more pull. Core hid the case behind the flyby every lift starts: it holds
 * Lara still for longer than a lift takes. With the flyby skipped (the
 * Cutscene skipper) the next pull can land while the cog still rises, and
 * three pulls gave fewer than three lifts.
 *
 * The rule: a lift that has begun runs to its end, whatever the code bits
 * do. The cog sees each pull meanwhile as its code bits gone off since the
 * tick before - Lara pulls one rope at a time, far less often than once a
 * tick - keeps it for a lift to come and turns the bits on again, so that
 * the next pull turns them off once more: two pulls are two. When the
 * running lift ends with a pull kept, the next lift begins in the same
 * tick, flyby and all, and the cog stays awake. A pull with no lift left
 * for it is dropped: never more than COG_LIFTS lifts. Lifted out, the cog
 * turns while its trigger is on, as before.
 *
 * The kept pulls stand in item_flags[3], a slot the cog never used. The
 * savegame writes it for every item it saves when it is not 0 and reads 0
 * where nothing was written (game\savegame.cpp), so a savegame from before
 * reads as no pull kept. A cog such a savegame left stopped half way - its
 * bits off in mid-lift - rises on at once, and the pull that stopped it is
 * kept like any other.
 *
 * No engine header here: ModernTest asserts on it. */

// The lifts that engage the cog, the height of one, the units of a tick.
#define COG_LIFTS			3
#define COG_LIFT_UNITS		256
#define COG_STEP			2

// What one tick of the control does (RaisingCogTick), a mask.
#define COG_KEPT			0x001	// a pull in the lift, kept for a later one
#define COG_DROPPED			0x002	// a pull in the lift, no lift left for it
#define COG_REARM			0x004	// that pull turned the bits off: on again
#define COG_DONE			0x008	// the lift has risen its height: counted
#define COG_ENGAGE			0x010	// it was the last: the pulleys change over
#define COG_SLEEP			0x020	// no lift follows: inactive, bits off
#define COG_BEGIN			0x040	// a lift begins: its flyby starts
#define COG_FROM_PENDING	0x080	// the lift that begins is a kept pull's
#define COG_RISE			0x100	// the cog rises COG_STEP units
#define COG_TURN			0x200	// lifted out, trigger on: it turns

// The cog's state, as the item holds it.
struct COG_STATE
{
	short lifts;	// item_flags[0]: lifts ended, COG_LIFTS once engaged
	short at;		// item_flags[2]: units the running lift has risen
	short pending;	// item_flags[3]: pulls kept for lifts to come
};

/* One tick of the control, `active` TriggerActive's answer in it: moves the
 * state on and says what the control does, in this order - the pull is
 * counted, then the lift's end, then the step. */
inline int RaisingCogTick(COG_STATE* c, bool active)
{
	int what = 0;

	// Lifted out: it turns while its trigger is on, as before.
	if (c->lifts >= COG_LIFTS)
		return active ? COG_TURN : 0;

	// No lift runs: one begins only with the trigger on, as before.
	if (c->at <= 0 && !active)
		return 0;

	// A lift runs and its bits are off: a pull since the tick before.
	if (c->at > 0 && !active)
	{
		what |= COG_REARM;

		if (c->pending < COG_LIFTS - 1 - c->lifts)
		{
			c->pending++;
			what |= COG_KEPT;
		}
		else
			what |= COG_DROPPED;
	}

	if (c->at >= COG_LIFT_UNITS)
	{
		c->at = 0;
		c->lifts++;
		what |= COG_DONE;

		if (c->lifts >= COG_LIFTS)
		{
			c->pending = 0;
			return what | COG_ENGAGE | COG_SLEEP;
		}

		if (c->pending <= 0)
			return what | COG_SLEEP;

		c->pending--;
		what |= COG_FROM_PENDING;
	}

	if (!c->at)
		what |= COG_BEGIN;

	c->at = (short)(c->at + COG_STEP);
	return what | COG_RISE;
}

/* The volume of the raise sound, 0 to 31: up over the first units of a
 * lift and down over its last, as Core had it. `at` is the units before
 * the step. */
inline int RaisingCogVolume(int at)
{
	if (at < 31)
		return at;

	if (at > 224)
		return 255 - at;

	return 31;
}

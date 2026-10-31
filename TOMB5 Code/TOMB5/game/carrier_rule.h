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

/* Who takes a pickup that lies in a carrier's reach at level start.
 *
 * GetCarriedItems (game\setup.cpp) hands every pickup but a wall grab that
 * lies in a carrier's room, less than 512 units from it in x and z and 256
 * in height, to that carrier and takes it off the floor. Core walked the
 * items once, in index order, so whoever stood first in the table took it.
 * But the two kinds of carrier do not give it back alike: a search object
 * hands its chain over where the item was placed, when Lara searches it
 * (CollectCarriedItems, game\pickup.cpp); an enemy only where he dies
 * (DropBaddyPickups, game\box.cpp), and an enemy that leaves the map takes
 * the chain with him. Core placed no pickup in reach of both kinds. The
 * randomizer puts items into search objects on purpose and its enemies
 * wherever the roll lands, so an enemy with the lower index standing in
 * front of a cupboard took what the cupboard was to hand over.
 *
 * The rule: two passes over the items, the search objects first, the
 * enemies then - an enemy gets only what no search object holds. Within a
 * pass nothing changed: index order, the same reach, the wall grabs left on
 * their walls, a pickup in reach of two carriers of one kind the lower
 * index's.
 *
 * The search objects are SEARCH_OBJECT1 to SEARCH_OBJECT3, the two MIP slots
 * between them included, as the range always read. SEARCH_OBJECT4 is none:
 * it hands over the one item lying at its own place through item_flags[1]
 * (InitialiseSearchObject, game\init.cpp), never a chain.
 *
 * The pool cyborg carries nothing. Slot 67 stays an enemy here - Core placed
 * both him and the pickups near him by hand. Slot 469 is placed by the roll
 * instead, so the pickup he would swallow is one the randomizer put
 * somewhere on purpose and the enemy standing on it is a coincidence; he
 * would give it back only where he dies, not where the item was meant to
 * be, and while the slot was still undead it never came back at all - a
 * softlock. Cheaper to keep the slot out of both passes than to depend on
 * its death.
 *
 * The drone carries nothing for the same reason, and one more: it dies in
 * the air, wherever the flyer zone took it, over a pit or a pool as well -
 * DropBaddyPickups would hand the chain over there.
 *
 * No engine header here: ModernTest asserts on it. */

// The slots the rule names (game\objects.h); setup.cpp checks that they agree.
#define CARRIER_SEARCH_OBJECT1	164
#define CARRIER_SEARCH_OBJECT3	168
#define CARRIER_CYBORG			469
#define CARRIER_DRONE			472

// The passes, in the order GetCarriedItems runs them.
#define CARRIER_PASS_SEARCH		0
#define CARRIER_PASS_ENEMY		1
#define CARRIER_PASSES			2

/* The pass in which an item of slot `object` takes the pickups in its reach:
 * CARRIER_PASS_SEARCH for a search object, CARRIER_PASS_ENEMY for a slot
 * the engine marks `intelligent` (only a loaded slot is), -1 for the cyborg,
 * the drone and every other slot. */
inline int CarrierPass(int object, bool intelligent)
{
	if (object == CARRIER_CYBORG || object == CARRIER_DRONE)
		return -1;

	if (object >= CARRIER_SEARCH_OBJECT1 && object <= CARRIER_SEARCH_OBJECT3)
		return CARRIER_PASS_SEARCH;

	return intelligent ? CARRIER_PASS_ENEMY : -1;
}

// The pass's word in the CARRYLOG line.
inline const char* CarrierPassName(int pass)
{
	if (pass == CARRIER_PASS_SEARCH)
		return "search";

	return pass == CARRIER_PASS_ENEMY ? "enemy" : "none";
}
